/*
 * soui_fr/soui_app.cpp
 *
 * Real SOUI application smoke test on FreeRTOS:
 *   SApplication + SAppCfg (render-gdi/cairo + imgdecoder-stb + PE-COFF
 *   resource blob + SHostWnd), i.e. the same bring-up sequence a desktop SOUI
 *   app runs in _tWinMain().  The build side is identical to soui_sim's
 *   (cmake/soui.cmake); only the toolchain and the display backend differ.
 *
 * Resources come from a windres PE-COFF object embedded as data by
 * cmake/soui.cmake (uires.idx -> rc2 -> main.rc -> COFF -> .incbin).  SAppCfg
 * mounts that blob as both the system and the application resource provider,
 * so layout XML, UIDEF and the APP_ICON RT_GROUP_ICON all go through
 * SResProviderPE / FindResourceA / LoadImageA -- no C arrays, no filesystem,
 * no hand-written .ico directory parsing.
 *
 * The host window paints through SOUI's memory render target -> BitBlt to
 * the swinx window DC -> cairo -> software framebuffer, and the whole fb is
 * then dumped to the host as PPM via ARM semihosting for visual inspection.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>
#include <string.h>
#include <thread> // resolves to the swinx FreeRTOS STL shim

// Which statically linked components SAppCfg's SComMgr2 may name.  This MUST
// come before the first SOUI header that reaches commgr2.h: it defaults
// SCOM_MASK to "all components", and every enabled bit puts a call to e.g.
// RENDER_SKIA::SCreateInstance() into the code.  This image links render-gdi
// and imgdecoder-stb, and nothing else.
#include <commask.h>
#define SCOM_MASK (scom_mask_render_gdi|scom_mask_imgdecoder_stb)

#include <souistd.h>
#include <SAppCfg.h> // SApplication + SAppCfg bring-up (wizard/CmakeApp flow)
#include <core/SHostWnd.h>

extern "C"
{
#include "uart.h"
}

// uiresbuilder-generated tables from uires.idx.  INIT_R_DATA makes this TU
// the one that *defines* UIRES / R; every other TU only sees the declarations.
#define INIT_R_DATA
#include "resource.h"

// Strong references to the embedded COFF blob.  swinx/src/resapi.cpp declares
// them weak so images without resources still link; with --gc-sections a
// weak-only reference is not enough to guarantee the .rodata.embedded section
// survives.  app_task also prints the size as a "resources are in the image"
// check.
extern "C" const unsigned char _binary_soui_coff_o_start[];
extern "C" const unsigned char _binary_soui_coff_o_end[];

// swinx finds the windres blob through linker symbols, so any non-NULL module
// handle works -- there is no HINSTANCE from WinMain on a bare target.
static const HINSTANCE kAppInstance = (HINSTANCE)1;

// ---------------------------------------------------------------------------
// framebuffer dump via ARM semihosting (from swinx_test/main_dump.cpp)
// ---------------------------------------------------------------------------
#define SH_SYS_OPEN 0x01
#define SH_SYS_CLOSE 0x02
#define SH_SYS_WRITE 0x05

static int sh_call(int op, long arg)
{
    register int r0 __asm("r0") = op;
    register long r1 __asm("r1") = arg;
    __asm __volatile("bkpt 0xAB"
                     : "+r"(r0)
                     : "r"(r1)
                     : "memory");
    return r0;
}

static int sh_open(const char *name)
{
    long args[3] = { (long)name, 4, (long)strlen(name) };
    return sh_call(SH_SYS_OPEN, (long)args);
}

static int sh_write(int handle, const void *buf, int len)
{
    long args[3] = { handle, (long)buf, len };
    return sh_call(SH_SYS_WRITE, (long)args);
}

static int sh_close(int handle)
{
    long args[1] = { handle };
    return sh_call(SH_SYS_CLOSE, (long)args);
}

#include "framebuffer.h"

static int dump_fb_ppm(const char *path)
{
    int fh = sh_open(path);
    if (fh < 0)
        return -1;

    char hdr[32];
    int n = 0;
    memcpy(hdr + n, "P6\n", 3);
    n += 3;

    char tmp[8];
    int v = SwinxFbWidth();
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0)
        hdr[n++] = tmp[--i];
    hdr[n++] = ' ';
    v = SwinxFbHeight();
    i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0)
        hdr[n++] = tmp[--i];
    hdr[n++] = '\n';
    hdr[n++] = '2';
    hdr[n++] = '5';
    hdr[n++] = '5';
    hdr[n++] = '\n';
    if (sh_write(fh, hdr, n) != 0)
    {
        sh_close(fh);
        return -2;
    }

    int w = SwinxFbWidth();
    int h = SwinxFbHeight();
    static uint8_t row[SWINX_FB_WIDTH * 3];
    const uint32_t *fb = SwinxFbBits();
    int stride = SwinxFbStride();
    for (int y = 0; y < h; ++y)
    {
        const uint32_t *src = fb + y * stride;
        for (int x = 0; x < w; ++x)
        {
            uint32_t px = src[x];
            row[x * 3 + 0] = (uint8_t)(px >> 16);
            row[x * 3 + 1] = (uint8_t)(px >> 8);
            row[x * 3 + 2] = (uint8_t)(px);
        }
        if (sh_write(fh, row, w * 3) != 0)
        {
            sh_close(fh);
            return -3;
        }
    }

    sh_close(fh);
    return 0;
}

// ---------------------------------------------------------------------------
// tiny uart helpers (no gtest here: smoke test)
// ---------------------------------------------------------------------------
static void tprint(const char *s) { uart_puts(s); }

static void tprint_int(int v)
{
    char buf[12];
    int i = 0;
    if (v < 0)
    {
        uart_putc('-');
        v = -v;
    }
    if (v == 0)
        buf[i++] = '0';
    while (v > 0)
    {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        uart_putc(buf[--i]);
}

// ---------------------------------------------------------------------------
// app task: the actual SApp smoke
// ---------------------------------------------------------------------------
static void app_task(void)
{
    tprint("== SOUI SApp on FreeRTOS ==\n");

    // 1. SApplication + SAppCfg -- the wizard/CmakeApp bring-up, unchanged.
    //    Both descriptors point at the same embedded COFF blob, exactly like
    //    soui_sim: SOUI system theme (SYS_XML_SKIN, ...) plus the app uires
    //    (UIDEF + LAYOUT + ICON).  SResProviderPE serves both through swinx's
    //    WindResResourceParser.
    SNS::SApplication app(kAppInstance);
    SNS::SAppCfg cfg;
    cfg.SetRender(SNS::Render_Gdi)
        .SetImgDecoder(SNS::ImgDecoder_Stb)
        .SetSysResPeHandle((HMODULE)kAppInstance)
        .SetAppResPeHandle((HMODULE)kAppInstance);

    BOOL bCfg = cfg.DoConfig(&app);
    tprint("[app] SAppCfg::DoConfig=");
    tprint_int(bCfg);
    tprint("\n");
    if (bCfg)
    {
        // Named ids from uiresbuilder's resource.h, same as a wizard app.
        app.InitXmlNamedID((const LPCWSTR *)&R.name, (const int *)&R.id,
                           (int)(sizeof(R.id) / sizeof(int)));
        tprint("[app] COFF resource blob bytes=");
        tprint_int((int)(_binary_soui_coff_o_end - _binary_soui_coff_o_start));
        tprint("\n");
    }
    if (!bCfg)
    {
        tprint("[app] SAppCfg::DoConfig failed\n");
        for (;;)
            taskYIELD();
    }

    // 2. host window from the embedded layout
    tprint("[app] before SHostWnd ctor\n");
    SNS::SHostWnd hostWnd(UIRES.LAYOUT.XML_MAINWND);
    tprint("[app] before CreateEx\n");
    HWND hWnd = hostWnd.CreateEx(NULL, WS_POPUP, 0, 0, 0, 320, 240);
    tprint("[app] SHostWnd CreateEx hwnd=0x");
    tprint_int((int)(uintptr_t)hWnd);
    tprint("\n");

    if (hWnd)
    {
        hostWnd.ShowWindow(SW_SHOWNORMAL);
        BOOL bUpd = UpdateWindow(hWnd); // synchronous WM_PAINT -> fb
        tprint("[app] UpdateWindow rc=");
        tprint_int(bUpd);
        tprint("\n");

        // 3. dump the framebuffer for visual inspection
        int rc = dump_fb_ppm("soui_fb.ppm");
        tprint("[app] dump rc=");
        tprint_int(rc);
        tprint("\n");
    }

    tprint("SOUISAPP FREERTOS DONE\n");

    // keep the app (and its message pump / timers) alive
    for (;;)
        taskYIELD();
}

extern "C" void entry(void)
{
    uart_init();

    std::thread t(app_task);
    t.detach(); // ownership transfers to the FreeRTOS task

    vTaskStartScheduler();

    for (;;)
        ;
}
