/*
 * soui_board.cpp -- ARGB32 -> RGB565 present path.
 *
 * swinx's SConnection::commitCanvas() blits a dirty rectangle into the
 * software framebuffer and then calls swinxFbPresent(&dirty)
 * (SConnection.cpp:1425-1430).  The registered callback below converts that
 * region to RGB565 and hands it to soui_board_lcd_flush(), which is provided
 * by soui_board_esp32.cpp (ILI9341V over SPI + FT6X36 over I2C).
 *
 * NOTE: the three board hooks used to be __attribute__((weak)) stubs here so
 * the skeleton could link without an LCD driver.  That only worked when the
 * strong definitions lived in the same translation unit or another archive
 * that was already being pulled in: libmain.a is scanned left to right, the
 * weak definitions in THIS object satisfied the references from app_main.cpp,
 * and the linker therefore never extracted soui_board_esp32.cpp.obj -- the
 * firmware booted with "no LCD driver linked: presents are counted".
 * Keeping exactly one (strong) definition of each hook removes the ordering
 * dependency.  main/CMakeLists.txt always compiles soui_board_esp32.cpp.
 *
 * Framebuffer and conversion buffer both live in PSRAM (see
 * components/soui/framebuffer_idf.cpp); nothing here is safe to call from an
 * ISR -- PSRAM and the flash share the same SPI bus.
 */
#include "soui_board.h"
#include "framebuffer.h"
#include "input.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdlib.h>

namespace
{
const char *kTag = "soui_board";
uint16_t *s_rgb565 = NULL;
uint32_t s_presentCount = 0;      // dirty-rect blits handed to the LCD
uint32_t s_presentPixels = 0;     // pixels pushed, for a rough fps estimate

// ---- touch sampling --------------------------------------------------------
// The FT6X36 is read by its OWN task, not by the UI thread.
//
// Why: the UI thread spends tens to hundreds of milliseconds inside one
// commitCanvas() (cairo software rendering + polled SPI).  Sampling from the
// pump means "no touch is read at all while a frame is being pushed", so a tap
// shorter than a frame is *lost*, not merely delayed -- that is exactly the
// "much less responsive than the LVGL firmware" symptom.  LVGL reads the panel
// from its own task every 30ms (CONFIG_LV_INDEV_DEF_READ_PERIOD) and so never
// loses a contact.
//
// Here the driver task samples every kTouchSampleMs and pushes a sample into a
// FreeRTOS queue only when something changed (button or coordinates).  The UI
// thread replays the queue into SwinxInjectMouse() at the top of each pump
// round, so a tap that happened mid-frame is delivered intact, in order
// (down .. moves .. up), as soon as the pump regains control.  Injection stays
// on the UI thread because the events must land in that thread's
// SConnection queue in order with its paint/timer messages.
struct TouchSample
{
    int16_t x;
    int16_t y;
    uint8_t down;
};

const int kTouchQueueLen = 32;      // ~256ms of changes at 8ms, far more than a tap
const int kTouchSampleMs = 8;       // vendor LVGL reads at 30ms; 8ms is cheap
const int kMaxSamplesPerPump = 16;  // bound the time spent replaying one round
const UBaseType_t kTouchTaskPriority = 6;   // above the UI task (5)
const int kTouchTaskStackBytes = 3072;

QueueHandle_t s_touchQ = NULL;

void touch_task(void *)
{
    int lastX = -1, lastY = -1, lastDown = -1;
    for (;;)
    {
        int x = 0, y = 0, down = 0;
        if (soui_board_touch_read(&x, &y, &down) &&
            (x != lastX || y != lastY || down != lastDown))
        {
            TouchSample s;
            s.x = (int16_t)x;
            s.y = (int16_t)y;
            s.down = (uint8_t)(down ? 1 : 0);
            // Never block: if the pump is more than 256ms behind, dropping the
            // oldest state change is better than stalling the I2C sampler.
            xQueueSend(s_touchQ, &s, 0);
            lastX = x;
            lastY = y;
            lastDown = down;
        }
        vTaskDelay(pdMS_TO_TICKS(kTouchSampleMs));
    }
}

void touch_start()
{
    if (s_touchQ)
        return;
    s_touchQ = xQueueCreate(kTouchQueueLen, sizeof(TouchSample));
    if (!s_touchQ)
    {
        ESP_LOGW(kTag, "touch queue alloc failed, falling back to in-pump polling");
        return;
    }
    // Same core as the UI task: a preempting sampler on the other core would
    // only add cross-core contention on the I2C driver's locks.
    if (xTaskCreatePinnedToCore(touch_task, "soui_touch", kTouchTaskStackBytes,
                                NULL, kTouchTaskPriority, NULL, 1) != pdPASS)
    {
        ESP_LOGW(kTag, "touch task create failed");
        vQueueDelete(s_touchQ);
        s_touchQ = NULL;
        return;
    }
    ESP_LOGI(kTag, "touch sampler task started (%dms)", kTouchSampleMs);
}

uint16_t *ensure_convert_buf()
{
    if (!s_rgb565)
    {
        const size_t bytes = (size_t)SwinxFbWidth() * SwinxFbHeight() * sizeof(uint16_t);
        s_rgb565 = static_cast<uint16_t *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_rgb565)
            s_rgb565 = static_cast<uint16_t *>(
                heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        if (!s_rgb565)
            ESP_LOGE(kTag, "cannot allocate %u-byte RGB565 buffer", (unsigned)bytes);
        else
            ESP_LOGI(kTag, "RGB565 conversion buffer %u bytes @ %p",
                     (unsigned)bytes, (void *)s_rgb565);
    }
    return s_rgb565;
}

inline uint16_t to_rgb565(uint32_t argb)
{
    // swinx fb is 0xAARRGGBB (premultiplied ARGB32, little-endian).
    const uint32_t r = (argb >> 16) & 0xFFu;
    const uint32_t g = (argb >> 8) & 0xFFu;
    const uint32_t b = argb & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

void on_present(const RECT *dirty)
{
    int x1 = 0, y1 = 0;
    int x2 = SwinxFbWidth(), y2 = SwinxFbHeight();
    if (dirty)
    {
        x1 = dirty->left;
        y1 = dirty->top;
        x2 = dirty->right;   // exclusive, same as SConnection.cpp:1403
        y2 = dirty->bottom;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > SwinxFbWidth())  x2 = SwinxFbWidth();
        if (y2 > SwinxFbHeight()) y2 = SwinxFbHeight();
    }
    if (x1 >= x2 || y1 >= y2)
        return;

    const uint32_t *fb = SwinxFbBits();
    uint16_t *out = ensure_convert_buf();
    if (!fb || !out)
        return;

    const int stride = SwinxFbStride();
    uint16_t *dst = out;
    for (int y = y1; y < y2; ++y)
    {
        const uint32_t *src = fb + (size_t)y * stride + x1;
        for (int x = x1; x < x2; ++x)
            *dst++ = to_rgb565(src[x - x1]);
    }
    soui_board_lcd_flush(x1, y1, x2, y2, out);
    ++s_presentCount;
    s_presentPixels += (uint32_t)(x2 - x1) * (uint32_t)(y2 - y1);
}
} // namespace

extern "C"
{

// The board hooks themselves are NOT defined here: soui_board_esp32.cpp owns
// them (see the header comment above).

void soui_board_init(void)
{
    SwinxSetFbPresentCb(on_present);
    soui_board_lcd_init();
    touch_start();
    ESP_LOGI(kTag, "swinx fb %dx%d -> soui_board_lcd_flush",
             SwinxFbWidth(), SwinxFbHeight());
}

namespace
{
// One sample -> one injected Win32 message.  A repeat of the previous
// (x, y, down) is dropped: pumping WM_MOUSEMOVE at 8ms for a finger that is
// not moving would flood the queue with no-op messages and push real input
// further back.
void inject_sample(HWND hWnd, const TouchSample &s)
{
    int x = s.x;
    int y = s.y;
    const int down = s.down ? 1 : 0;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= SwinxFbWidth())  x = SwinxFbWidth() - 1;
    if (y >= SwinxFbHeight()) y = SwinxFbHeight() - 1;

    static int s_injX = -1, s_injY = -1, s_injDown = -1;
    if (x == s_injX && y == s_injY && down == s_injDown)
        return;

    if (down && !s_injDown)
        SwinxInjectMouse(hWnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON);
    else if (!down && s_injDown)
        SwinxInjectMouse(hWnd, WM_LBUTTONUP, x, y, 0);
    else
        SwinxInjectMouse(hWnd, WM_MOUSEMOVE, x, y, down ? MK_LBUTTON : 0);

    s_injX = x;
    s_injY = y;
    s_injDown = down;
}
} // namespace

void soui_board_poll_touch(HWND hWnd)
{
    // Buffered path: replay what the sampler task captured while this thread
    // was busy painting.  Order is preserved, so a tap never degenerates into
    // a "move" or gets swallowed by a coincident release.
    if (s_touchQ)
    {
        TouchSample s;
        int n = 0;
        while (n < kMaxSamplesPerPump && xQueueReceive(s_touchQ, &s, 0) == pdTRUE)
        {
            inject_sample(hWnd, s);
            ++n;
        }
        return;
    }

    // Fallback (queue/task creation failed): sample inline, as before.
    int x = 0, y = 0, down = 0;
    if (!soui_board_touch_read(&x, &y, &down))
        return;
    TouchSample s;
    s.x = (int16_t)x;
    s.y = (int16_t)y;
    s.down = (uint8_t)(down ? 1 : 0);
    inject_sample(hWnd, s);
}

uint32_t soui_board_present_count(void)
{
    return s_presentCount;
}

uint32_t soui_board_present_pixels(void)
{
    return s_presentPixels;
}

// Frames actually rendered, i.e. WM_PAINTs the pump finished.  Kept here (and
// not in app_main) so the demo window can read it without reaching into the
// pump; see the header for why it differs from present_count.
static volatile uint32_t s_frameCount = 0;

void soui_board_frame_tick(void)
{
    ++s_frameCount;
}

uint32_t soui_board_frame_count(void)
{
    return s_frameCount;
}

} // extern "C"
