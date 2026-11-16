/*
 * framebuffer_idf.cpp -- ESP-IDF replacement for
 * swinx/src/platform/freertos/framebuffer.cpp.
 *
 * The upstream version declares
 *   static uint32_t s_fbBits[SWINX_FB_WIDTH * SWINX_FB_HEIGHT];
 * i.e. 320*240*4 == 300KB of static .bss.  On ESP32-S3 that would consume
 * most of the internal DRAM (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is
 * not enabled and only applies to EXT_RAM_BSS_ATTR-tagged objects anyway).
 *
 * This implementation exposes the identical C API from framebuffer.h but
 * allocates from the 8MB Octal PSRAM.  SConnection::commitCanvas()
 * (SConnection.cpp:1425) blits dirty regions into SwinxFbBits(), so the
 * pointer must be valid from the first call onward.
 *
 * The panel size is runtime-configurable: the board layer calls
 * SwinxFbInit(width, height, NULL) with the LCD dimensions before the UI
 * starts; without it the SWINX_FB_WIDTH/HEIGHT defaults apply.
 *
 * Route (1): when SWINX_FB_RGB565 is set (see framebuffer.h) the compositor
 * uses 16bpp, halving the framebuffer and letting the present callback feed
 * the LCD driver directly (no 150KB ARGB32->RGB565 staging buffer).
 *
 * Excluded from the build: framebuffer.cpp (see components/soui/CMakeLists.txt).
 */
#include "framebuffer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

namespace
{
const char *kTag = "soui_fb";
FBPIXEL *s_fbBits = NULL;
SwinxFbPresentCb s_presentCb = NULL;
/* Runtime-configurable size; SwinxFbInit() overrides the build-time
 * SWINX_FB_WIDTH/HEIGHT defaults so a different panel needs no rebuild
 * of swinx itself. */
int s_w = SWINX_FB_WIDTH;
int s_h = SWINX_FB_HEIGHT;

FBPIXEL to_fbpixel(uint32_t argb)
{
#if SWINX_FB_RGB565
    return (FBPIXEL)(((argb >> 8) & 0xF800u) |
                     ((argb >> 5) & 0x07E0u) |
                     ((argb >> 3) & 0x001Fu));
#else
    return (FBPIXEL)argb;
#endif
}

FBPIXEL *ensure_fb()
{
    if (!s_fbBits)
    {
        const size_t bytes = (size_t)s_w * s_h * sizeof(FBPIXEL);
        s_fbBits = static_cast<FBPIXEL *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_fbBits)
        {
            // Fallback: internal DRAM. Larger buffers usually do not fit;
            // surfaced in the log so the failure has an obvious root cause.
            ESP_LOGW(kTag, "PSRAM allocation failed, trying internal RAM");
            s_fbBits = static_cast<FBPIXEL *>(
                heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        }
        if (!s_fbBits)
        {
            ESP_LOGE(kTag, "cannot allocate %u-byte framebuffer", (unsigned)bytes);
            abort();
        }
        ESP_LOGI(kTag, "framebuffer %dx%d @ %p (%u bytes, %u bpp)",
                 s_w, s_h, (void *)s_fbBits,
                 (unsigned)bytes, (unsigned)(sizeof(FBPIXEL) * 8));
        const FBPIXEL black = to_fbpixel(0xFF000000u);   // opaque black
        for (int i = 0; i < s_w * s_h; ++i)
            s_fbBits[i] = black;
    }
    return s_fbBits;
}
} // namespace

extern "C"
{

FBPIXEL *SwinxFbBits(void)
{
    return ensure_fb();
}

int SwinxFbInit(int width, int height, FBPIXEL *bits)
{
    if (width <= 0 || height <= 0)
        return -1;
    // Allocate eagerly so a bad size fails here, not at the first blit; an
    // external buffer (bits != NULL) is accepted as-is.
    if (bits)
    {
        if (s_fbBits)
            heap_caps_free(s_fbBits);
        s_fbBits = bits;
    }
    else
    {
        const size_t bytes = (size_t)width * height * sizeof(FBPIXEL);
        FBPIXEL *p = static_cast<FBPIXEL *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!p)
            p = static_cast<FBPIXEL *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        if (!p)
        {
            ESP_LOGE(kTag, "cannot allocate %u-byte framebuffer", (unsigned)bytes);
            return -1;
        }
        if (s_fbBits)
            heap_caps_free(s_fbBits);
        s_fbBits = p;
        const FBPIXEL black = to_fbpixel(0xFF000000u);
        for (int i = 0; i < width * height; ++i)
            s_fbBits[i] = black;
    }
    s_w = width;
    s_h = height;
    return 0;
}

int SwinxFbWidth(void)
{
    return s_w;
}

int SwinxFbHeight(void)
{
    return s_h;
}

int SwinxFbStride(void)
{
    return s_w;   // pixels
}

void SwinxSetFbPresentCb(SwinxFbPresentCb cb)
{
    s_presentCb = cb;
}

void SwinxFbClear(uint32_t argb)
{
    FBPIXEL *fb = ensure_fb();
    const FBPIXEL v = to_fbpixel(argb);
    const int n = s_w * s_h;
    for (int i = 0; i < n; ++i)
        fb[i] = v;
}

void swinxFbPresent(const RECT *dirty)
{
    if (s_presentCb)
        s_presentCb(dirty);
}

} // extern "C"
