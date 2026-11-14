/*
 * soui_fr/soui_app.cpp
 *
 * Real SOUI application smoke test on FreeRTOS:
 *   SApplication + SAppCfg (render-gdi/cairo + imgdecoder-stb + in-memory
 *   ResProvider + SHostWnd), i.e. the same bring-up sequence a desktop SOUI
 *   app runs in _tWinMain().  The build side is identical to soui_sim's
 *   (cmake/soui.cmake); only the toolchain and the display backend differ.
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

// in-memory resource provider (mem_res_provider.cpp)
extern SNS::IResProvider *CreateMemResProvider(void);

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

    // 1. SApplication + SAppCfg
    //    DoConfig() creates the render factory (render-gdi / cairo), wires the
    //    image decoder (imgdecoder-stb) and mounts the resource provider -- the
    //    same steps a desktop app leaves to the wizard-generated _tWinMain().
    //    The one port-specific twist is the resource side: there is no
    //    filesystem here, so the provider is a ROM table built by
    //    mem_res_provider.cpp and handed over with SetAppResMem().
    SNS::SApplication app((HINSTANCE)1);
    SNS::IResProvider *pRes = CreateMemResProvider();
    pRes->Init(0, 0);

    SNS::SAppCfg cfg;
    cfg.SetRender(SNS::Render_Gdi)
        .SetImgDecoder(SNS::ImgDecoder_Stb)
        .SetAppResMem(pRes); // provider owns the ROM tables; no uidef ini in it
    pRes->Release();         // SAppCfg holds its own reference now

    BOOL bCfg = cfg.DoConfig(&app);
    tprint("[app] SAppCfg::DoConfig=");
    tprint_int(bCfg);
    tprint("\n");
    if (!bCfg)
    {
        tprint("[app] SAppCfg::DoConfig failed\n");
        for (;;)
            taskYIELD();
    }

    // 2. host window from the embedded layout
    tprint("[app] before SHostWnd ctor\n");
    SNS::SHostWnd hostWnd(_T("LAYOUT:APP_MAIN"));
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
