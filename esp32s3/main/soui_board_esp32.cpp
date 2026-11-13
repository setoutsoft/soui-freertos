/*
 * soui_board_esp32.cpp -- strong implementations of the three board hooks for
 * the 2.8" IPS ESP32-S3 board (ILI9341V over SPI2 + FT6X36 over I2C0).
 *
 * Pin map and init sequence are transcribed from the vendor LVGL demo:
 *   1-示例程序_Demo/ESP-IDF/2.8inch_ESP32-S3_LVGL/sdkconfig
 *   components/lvgl_esp32_drivers/lvgl_tft/ili9341.c
 *   components/lvgl_esp32_drivers/lvgl_touch/ft6x36.c
 *
 * Why not link the vendor driver component: it depends on LVGL
 * (disp_driver.c / ili9341.h include lvgl.h).  SOUI renders through cairo, so
 * a ~250-line direct esp_driver_spi / esp_driver_i2c implementation is both
 * smaller and free of the LVGL component tree.
 *
 * All transfers are synchronous (spi_device_polling_transmit) and run on the
 * UI task.  PSRAM and flash share one SPI bus, so this must never be called
 * from an ISR.
 */
#include "soui_board.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
// IDF 5.4 moved the legacy blocking I2C API (driver/i2c.h) into the deprecated
// `driver` component; the split `esp_driver_i2c` component only ships the new
// i2c_master.h.  The legacy header is not on main's include path, so the touch
// controller is driven through the new master-bus API instead.
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

namespace
{
const char *kTag = "soui_lcd";

// ---- vendor sdkconfig (CONFIG_LV_*) ---------------------------------------
const int kPinMosi = 11;
const int kPinMiso = 13;
const int kPinClk  = 12;
const int kPinCs   = 10;
const int kPinDc   = 46;
const int kPinRst  = 18;
const int kPinBckl = 45;   // CONFIG_LV_BACKLIGHT_ACTIVE_LVL=y -> high = on
const int kPinSda  = 16;
const int kPinScl  = 15;
const int kI2cFreq = 400000;
const uint8_t kFt6x36Addr = 0x38;

const int kLcdW = 320;
const int kLcdH = 240;
const int kSpiHz = 40 * 1000 * 1000;   // 80MHz APB / divider 2

spi_device_handle_t s_spi = NULL;
i2c_master_bus_handle_t s_i2cBus = NULL;
i2c_master_dev_handle_t s_touchDev = NULL;
bool s_lcdReady = false;
bool s_touchReady = false;

// Staging buffer for the bulk pixel push.  It MUST live in internal RAM: SPI
// DMA cannot use a PSRAM buffer as source on this part, while the RGB565 rect
// handed to soui_board_lcd_flush() does live in PSRAM.
const size_t kFlushChunkBytes = 8192;   // 4096 pixels
uint8_t *s_chunk = NULL;

// Diagnostics: total time spent inside the SPI push, so app_main can report
// "ms per frame" and tell a rendering bottleneck from a bus bottleneck.
volatile uint64_t s_flushUs = 0;
volatile uint32_t s_flushRects = 0;

uint8_t *ensure_chunk_buf()
{
    if (!s_chunk)
    {
        s_chunk = (uint8_t *)heap_caps_malloc(kFlushChunkBytes,
                                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_chunk)
            ESP_LOGE(kTag, "cannot allocate %u-byte SPI staging buffer",
                     (unsigned)kFlushChunkBytes);
    }
    return s_chunk;
}

void spi_push(const uint8_t *data, size_t len)
{
    if (!s_spi || len == 0)
        return;
    gpio_set_level((gpio_num_t)kPinDc, 1);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi, &t);
}

// One register-read helper for both the probe and the polling path.
esp_err_t i2c_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_touchDev)
        return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_touchDev, &reg, 1, buf, len,
                                       pdMS_TO_TICKS(20));
}

// ILI9341 init table, verbatim from vendor ili9341.c
struct InitCmd
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;      // bit7 = 100ms delay, 0xFF = end
};

const InitCmd kInitCmds[] = {
    {0xCF, {0x00, 0xC1, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x00, 0x78}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x13}, 1},
    {0xC1, {0x13}, 1},
    {0xC5, {0x22, 0x35}, 2},
    {0xC7, {0xBD}, 1},
    {0x21, {0}, 0},                 // inversion on (CONFIG_LV_INVERT_COLORS)
    {0x36, {0x08}, 1},             // MADCTL, overwritten below
    {0xB6, {0x0A, 0x82}, 2},
    {0x3A, {0x55}, 1},             // RGB565
    {0xF6, {0x01, 0x30}, 2},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x00}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x0F, 0x35, 0x31, 0x0B, 0x0F, 0x06, 0x49, 0xA7,
            0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15},
    {0xE1, {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58,
            0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15},
    {0x11, {0}, 0x80},             // sleep out + delay
    {0x29, {0}, 0x80},             // display on  + delay
    {0x00, {0}, 0xFF},
};

void spi_write(const uint8_t *data, size_t len, bool isData)
{
    if (!s_spi || len == 0)
        return;
    gpio_set_level((gpio_num_t)kPinDc, isData ? 1 : 0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi, &t);
}

void lcd_cmd(uint8_t cmd)
{
    spi_write(&cmd, 1, false);
}

void lcd_data(const uint8_t *data, size_t len)
{
    spi_write(data, len, true);
}

void lcd_init_panel()
{
    // Non-SPI GPIOs
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << kPinDc) | (1ULL << kPinRst) | (1ULL << kPinBckl);
    out.mode = GPIO_MODE_OUTPUT;
    out.pull_up_en = GPIO_PULLUP_DISABLE;
    out.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&out));

    gpio_set_level((gpio_num_t)kPinRst, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level((gpio_num_t)kPinRst, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = {};
    bus.mosi_io_num = kPinMosi;
    bus.miso_io_num = kPinMiso;
    bus.sclk_io_num = kPinClk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kLcdW * 2 * 16;
    esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(e));
        return;
    }

    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = kSpiHz;
    dev.spics_io_num = kPinCs;
    dev.queue_size = 4;
    dev.flags = 0;
    e = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (e != ESP_OK)
    {
        ESP_LOGE(kTag, "spi_bus_add_device: %s", esp_err_to_name(e));
        return;
    }

    for (const InitCmd *p = kInitCmds; p->len != 0xFF; ++p)
    {
        lcd_cmd(p->cmd);
        if (p->len & 0x1F)
            lcd_data(p->data, p->len & 0x1F);
        if (p->len & 0x80)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    // CONFIG_LV_DISPLAY_ORIENTATION_LANDSCAPE (index 2) of
    // ili9341.c's {0x48, 0x88, 0x28, 0xE8} table.
    const uint8_t madctl = 0x28;
    lcd_cmd(0x36);
    lcd_data(&madctl, 1);

    // CONFIG_LV_INVERT_COLORS=y
    lcd_cmd(0x21);

    gpio_set_level((gpio_num_t)kPinBckl, 1);
    s_lcdReady = true;
    ESP_LOGI(kTag, "ILI9341V ready: %dx%d landscape, 40MHz SPI, backlight on",
             kLcdW, kLcdH);
}

void touch_init()
{
    i2c_master_bus_config_t busCfg = {};
    busCfg.i2c_port = I2C_NUM_0;
    busCfg.sda_io_num = (gpio_num_t)kPinSda;
    busCfg.scl_io_num = (gpio_num_t)kPinScl;
    busCfg.clk_source = I2C_CLK_SRC_DEFAULT;
    busCfg.glitch_ignore_cnt = 7;
    // The new master-bus API has no per-line sda_pullup_en/scl_pullup_en
    // fields (those belonged to the legacy I2C_MODE_MASTER config); the
    // equivalent is this single flag, as in the vendor's own pull-up setup.
    busCfg.flags.enable_internal_pullup = 1;
    esp_err_t e = i2c_new_master_bus(&busCfg, &s_i2cBus);
    if (e != ESP_OK)
    {
        ESP_LOGW(kTag, "i2c_new_master_bus: %s (touch disabled)", esp_err_to_name(e));
        return;
    }

    i2c_device_config_t devCfg = {};
    devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devCfg.device_address = kFt6x36Addr;
    devCfg.scl_speed_hz = kI2cFreq;
    // The panel may NACK before its first conversion; do not fail the probe.
    devCfg.flags.disable_ack_check = false;
    e = i2c_master_bus_add_device(s_i2cBus, &devCfg, &s_touchDev);
    if (e != ESP_OK)
    {
        ESP_LOGW(kTag, "i2c_master_bus_add_device: %s", esp_err_to_name(e));
        s_touchDev = NULL;
        return;
    }

    uint8_t id = 0;
    e = i2c_read_reg(0xA8, &id, 1);   // FT6X36_PANEL_ID_REG
    if (e == ESP_OK)
    {
        s_touchReady = true;
        ESP_LOGI(kTag, "FT6X36 ready @0x%02x, panel id 0x%02x", kFt6x36Addr, id);
    }
    else
    {
        ESP_LOGW(kTag, "FT6X36 probe failed: %s", esp_err_to_name(e));
    }
}
} // namespace

extern "C"
{

void soui_board_lcd_init(void)
{
    lcd_init_panel();
    touch_init();
}

void soui_board_lcd_flush(int x1, int y1, int x2, int y2, const uint16_t *rgb565)
{
    if (!s_lcdReady || !rgb565 || x1 >= x2 || y1 >= y2)
        return;
    if (x1 < 0 || y1 < 0 || x2 > kLcdW || y2 > kLcdH)
        return;

    // First-flush trace + a slow heartbeat: proves the swinx present path
    // really reached the panel instead of silently dropping frames, without
    // flooding the 115200 console (one line per 200 rects).
    static uint32_t s_flushCount = 0;
    ++s_flushCount;
    if (s_flushCount == 1 || (s_flushCount % 200) == 0)
        ESP_LOGI(kTag, "flush #%u rect (%d,%d)-(%d,%d)",
                 (unsigned)s_flushCount, x1, y1, x2, y2);

    const int w = x2 - x1;
    const int h = y2 - y1;
    uint8_t d[4];

    d[0] = (x1 >> 8) & 0xFF;
    d[1] = x1 & 0xFF;
    d[2] = ((x2 - 1) >> 8) & 0xFF;
    d[3] = (x2 - 1) & 0xFF;
    lcd_cmd(0x2A);
    lcd_data(d, 4);

    d[0] = (y1 >> 8) & 0xFF;
    d[1] = y1 & 0xFF;
    d[2] = ((y2 - 1) >> 8) & 0xFF;
    d[3] = (y2 - 1) & 0xFF;
    lcd_cmd(0x2B);
    lcd_data(d, 4);

    lcd_cmd(0x2C);

    // Bulk push.  The panel accepts an unbounded pixel stream after 0x2C, so
    // the whole rect goes out in a handful of large transactions.  The first
    // version sent one transaction per row, which pays the driver's per-call
    // overhead (bus lock, DMA descriptor setup, DC toggle) h up to 240 times
    // for no benefit -- at 40MHz the wire time for 640 bytes is only ~128us.
    //
    // Byte order: the SPI peripheral streams bytes verbatim, so a little-endian
    // uint16_t would arrive as lo,hi.  Swap while copying into the staging
    // buffer instead of doing a second pass over the rect.
    uint8_t *buf = ensure_chunk_buf();
    if (!buf)
        return;

    const int64_t tStart = esp_timer_get_time();
    uint8_t *p = buf;
    size_t used = 0;
    for (int row = 0; row < h; ++row)
    {
        const uint16_t *src = rgb565 + (size_t)row * w;
        for (int i = 0; i < w; ++i)
        {
            const uint16_t v = src[i];
            *p++ = (uint8_t)(v >> 8);
            *p++ = (uint8_t)(v & 0xFF);
            used += 2;
            if (used >= kFlushChunkBytes)
            {
                spi_push(buf, used);
                used = 0;
                p = buf;
            }
        }
    }
    if (used)
        spi_push(buf, used);

    s_flushUs += (uint64_t)(esp_timer_get_time() - tStart);
    ++s_flushRects;
}

uint64_t soui_board_flush_us(void)
{
    return s_flushUs;
}

uint32_t soui_board_flush_rects(void)
{
    return s_flushRects;
}

int soui_board_touch_read(int *x, int *y, int *pressed)
{
    if (!s_touchReady)
        return 0;

    static int s_lastX = 0, s_lastY = 0, s_lastDown = 0;
    uint8_t buf[5] = {0};
    esp_err_t e = i2c_read_reg(0x02, buf, 5);   // FT6X36_TD_STAT_REG
    const int points = (e == ESP_OK) ? (buf[0] & 0x0F) : 0;

    if (e != ESP_OK || points == 0)
    {
        if (!s_lastDown)
            return 0;
        s_lastDown = 0;
        *x = s_lastX;
        *y = s_lastY;
        *pressed = 0;
        return 1;   // one release event
    }

    const int rawX = ((buf[1] & 0x0F) << 8) | buf[2];
    const int rawY = ((buf[3] & 0x0F) << 8) | buf[4];
    // CONFIG_LV_FT6X36_SWAPXY + CONFIG_LV_FT6X36_INVERT_Y (LV_VER_RES=240)
    int sx = rawY;
    int sy = kLcdH - rawX;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= kLcdW) sx = kLcdW - 1;
    if (sy >= kLcdH) sy = kLcdH - 1;

    s_lastX = sx;
    s_lastY = sy;
    s_lastDown = 1;
    *x = sx;
    *y = sy;
    *pressed = 1;
    return 1;
}

} // extern "C"
