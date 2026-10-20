/*
 * templates/app/main.cpp -- the smallest complete "SOUI on FreeRTOS" app.
 *
 * What happens here (in order):
 *   1. FreeRTOS starts and app_task() runs as a normal task.
 *   2. SimHalInit() opens the SDL window and registers the framebuffer present
 *      callback with swinx.  On a bare-metal target this step is your panel
 *      driver instead -- everything else stays the same.
 *   3. The usual SOUI bring-up, exactly as in wizard/CmakeApp/main.cpp:
 *      SApplication + SAppCfg (render factory, image decoder, PE resources).
 *   4. A loop that pumps SDL input into swinx, presents dirty frames and drains
 *      the Win32 message queue (PeekMessage; DispatchMessage).
 *
 * The layout lives in uires/xml/main.xml and is addressed below as
 * "LAYOUT:MAIN" (the <file name="MAIN"> entry in uires/uires.idx).
 */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <thread> // resolves to the swinx FreeRTOS STL shim

// Static COM components: soui.cmake links render-gdi and imgdecoder-stb, and
// SAppCfg's SComMgr2 only emits the factories listed in SCOM_MASK.
#include <commask.h>
#define SCOM_MASK (scom_mask_render_gdi | scom_mask_imgdecoder_stb)

#include <souistd.h>
#include <SAppCfg.h> // SApplication + SAppCfg bring-up
#include <core/SHostWnd.h>

#include "soui_sim_hal.h"

static void app_task(void *)
{
    printf("== hello_freertos: SOUI on FreeRTOS ==\n");

    if (SimHalInit() != 0)
    {
        printf("[app] hal init failed\n");
        vTaskDelete(NULL);
        return;
    }

    // 1. application object + configuration (render / img decoder / resources)
    HINSTANCE hInst = (HINSTANCE)1; // module-agnostic: the resource blob is
                                    // located through weak linker symbols
    SNS::SApplication app(hInst);
    SNS::SAppCfg cfg;
    cfg.SetRender(SNS::Render_Gdi)          // render-gdi (cairo), linked statically
        .SetImgDecoder(SNS::ImgDecoder_Stb) // imgdecoder-stb, also static
        .SetSysResPeHandle(hInst)           // + app resources, one windres blob
        .SetAppResPeHandle(hInst);
    if (!cfg.DoConfig(&app))
    {
        printf("[app] SAppCfg::DoConfig failed\n");
        vTaskDelete(NULL);
        return;
    }

    // 2. host window from the embedded layout
    SNS::SHostWnd hostWnd(_T("LAYOUT:MAIN"));
    HWND hWnd = hostWnd.CreateEx(NULL, WS_POPUP, 0, 0, 0, 320, 240);
    if (!hWnd)
    {
        printf("[app] CreateEx failed\n");
        vTaskDelete(NULL);
        return;
    }
    hostWnd.ShowWindow(SW_SHOWNORMAL);
    UpdateWindow(hWnd); // synchronous first paint
    SimHalSetTargetHwnd((void *)hWnd);

    printf("[app] running (click around, close the window to quit)\n");
    for (;;)
    {
        SimHalPumpEvents();     // SDL events -> swinx input
        SimHalRenderIfDirty();  // swinx framebuffer -> SDL texture

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ---------------------------------------------------------------------------
// FreeRTOS hooks.  Keep them: the kernel calls these by name.
// ---------------------------------------------------------------------------
extern "C" void fr_sim_assert(const char *file, int line)
{
    printf("FreeRTOS ASSERT at %s:%d\n", file, line);
    fflush(stdout);
}

extern "C" void vApplicationMallocFailedHook(void)
{
    printf("FreeRTOS malloc failed\n");
    fflush(stdout);
    for (;;)
    {
    }
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("*** STACK OVERFLOW *** %s\n", pcTaskName ? pcTaskName : "?");
    fflush(stdout);
    for (;;)
    {
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // logs must survive a hang/crash
    // SOUI's window-creation path is deep: 512 KiB on the host (the ARM target
    // uses 8192 words of its own stack).
    if (xTaskCreate(app_task, "app", 1 << 19, NULL, 4, NULL) != pdPASS)
    {
        printf("failed to create app task\n");
        return 1;
    }
    vTaskStartScheduler();
    for (;;)
    {
    }
    return 0;
}
