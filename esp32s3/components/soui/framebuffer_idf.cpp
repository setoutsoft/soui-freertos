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
 * allocates lazily from the 8MB Octal PSRAM.  SConnection::commitCanvas()
 * (SConnection.cpp:1425) memcpy()s dirty regions into SwinxFbBits(), so the
 * pointer must be valid from the first call onward.
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
uint32_t *s_fbBits = NULL;
SwinxFbPresentCb s_presentCb = NULL;

uint32_t *ensure_fb()
{
    if (!s_fbBits)
    {
        const size_t bytes = (size_t)SWINX_FB_WIDTH * SWINX_FB_HEIGHT * sizeof(uint32_t);
        s_fbBits = static_cast<uint32_t *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_fbBits)
        {
            // Fallback: internal DRAM. 300KB usually does not fit; surfaced in
            // the log so the failure has an obvious root cause.
            ESP_LOGW(kTag, "PSRAM allocation failed, trying internal RAM");
            s_fbBits = static_cast<uint32_t *>(
                heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        }
        if (!s_fbBits)
        {
            ESP_LOGE(kTag, "cannot allocate %u-byte framebuffer", (unsigned)bytes);
            abort();
        }
        ESP_LOGI(kTag, "framebuffer %dx%d @ %p (%u bytes)",
                 SWINX_FB_WIDTH, SWINX_FB_HEIGHT, (void *)s_fbBits, (unsigned)bytes);
        for (int i = 0; i < SWINX_FB_WIDTH * SWINX_FB_HEIGHT; ++i)
            s_fbBits[i] = 0xFF000000u;   // opaque black
    }
    return s_fbBits;
}
} // namespace

extern "C"
{

uint32_t *SwinxFbBits(void)
{
    return ensure_fb();
}

int SwinxFbWidth(void)
{
    return SWINX_FB_WIDTH;
}

int SwinxFbHeight(void)
{
    return SWINX_FB_HEIGHT;
}

int SwinxFbStride(void)
{
    return SWINX_FB_WIDTH;   // pixels
}

void SwinxSetFbPresentCb(SwinxFbPresentCb cb)
{
    s_presentCb = cb;
}

void SwinxFbClear(uint32_t argb)
{
    uint32_t *fb = ensure_fb();
    const int n = SWINX_FB_WIDTH * SWINX_FB_HEIGHT;
    for (int i = 0; i < n; ++i)
        fb[i] = argb;
}

void swinxFbPresent(const RECT *dirty)
{
    if (s_presentCb)
        s_presentCb(dirty);
}

} // extern "C"
