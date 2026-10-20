/*
 * main.cpp -- entry point of the SOUI-on-FreeRTOS simulator demo.
 *
 * The application body is the same one a Win32 SOUI app has inside
 * _tWinMain(): SApplication + SAppCfg bring-up, InitXmlNamedID, create the
 * main window, pump messages.  Only the surrounding shell differs:
 *
 *   * everything runs in a FreeRTOS task (main() just starts the scheduler),
 *   * the framebuffer is presented to an SDL2 window and SDL mouse events are
 *     injected through swinx (sim/soui_sim_hal.cpp),
 *   * the message pump has to stay non-blocking, so the SDL pump runs next to
 *     PeekMessage() instead of app.Run() (which would block in GetMessage).
 */
#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>

#include <SAppCfg.h> // wizard-style application configuration

#include "MainDlg.h"
#include "soui_sim_hal.h"

#include "FreeRTOS.h"
#include "task.h"

// resource.h is generated from uires/uires.idx by uiresbuilder at build time.
// INIT_R_DATA makes this TU the one that *defines* the UIRES / R tables; every
// other TU only sees the extern declarations.
#define INIT_R_DATA
#include "resource.h"

// swinx finds the windres-embedded resource blob through weak linker symbols,
// so any non-NULL module handle works -- there is no HINSTANCE from WinMain.
static const HINSTANCE kAppInstance = (HINSTANCE)1;

// =============================================================================
// the application (what a Win32 build would put in _tWinMain)
// =============================================================================
static int RunApp(void)
{
    // ---- 1. SDL window the swinx framebuffer is presented to ---------------
    if (SimHalInit() != 0)
    {
        printf("[app] simulator window init failed\n");
        return -1;
    }

    // ---- 2. SApplication + SAppCfg ----------------------------------------
    // Same shape as wizard/CmakeApp/main.cpp: the config object creates the
    // render factory, registers the image decoder, loads the system resources
    // and installs the application resource provider.
    SApplication app(kAppInstance);
    SAppCfg cfg;
    cfg.SetRender(Render_Gdi)          // render-gdi (cairo) is what this build links
        .SetImgDecoder(ImgDecoder_Stb) // imgdecoder-stb, also linked statically
        .SetSysResPeHandle(kAppInstance)
        .SetAppResPeHandle(kAppInstance);
    if (!cfg.DoConfig(&app))
    {
        printf("[app] SAppCfg::DoConfig failed\n");
        return -1;
    }
    printf("[app] SApplication ready (render-gdi + imgdecoder-stb)\n");

    // Makes "<id> = R.id.xxx" usable in code, e.g. the EVENT_ID_COMMAND entries
    // of CMainDlg.  R comes from the uiresbuilder-generated resource.h.
    app.InitXmlNamedID((const LPCWSTR *)&R.name, (const int *)&R.id,
                       (int)(sizeof(R.id) / sizeof(int)));

    // ---- 3. main window ----------------------------------------------------
    CMainDlg dlgMain;
    HWND hWnd = dlgMain.Create(NULL, 0, 0, 320, 240);
    if (!hWnd)
    {
        printf("[app] create main window failed\n");
        return -1;
    }
    dlgMain.SendMessage(WM_INITDIALOG);
    dlgMain.ShowWindow(SW_SHOWNORMAL);
    UpdateWindow(hWnd); // synchronous WM_PAINT -> framebuffer -> SDL
    SimHalSetTargetHwnd((void *)hWnd);
    printf("[app] main window up, hwnd=%p\n", (void *)hWnd);

    // ---- 4. message loop ---------------------------------------------------
    // Whatever you do with the mouse/keyboard in the SDL window is injected
    // into this hwnd and handled by the real SOUI window code.
    MSG msg;
    while (IsWindow(hWnd))
    {
        SimHalPumpEvents();    // SDL events -> swinx input
        SimHalRenderIfDirty(); // swinx framebuffer -> SDL texture

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    printf("[app] main window closed\n");
    return 0;
}

// =============================================================================
// FreeRTOS shell
// =============================================================================
static void app_task(void *)
{
    int rc = RunApp();
    printf("[app] app task finished (rc=%d)\n", rc);
    fflush(stdout);
    // Single-application simulator: closing the SOUI window (or the SDL
    // window) ends the process.  On a real target the task would simply be
    // deleted and the firmware would keep running.
    exit(rc);
}

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
    // unbuffered stdout: logs must reach the pipe before any hang/crash
    setvbuf(stdout, NULL, _IONBF, 0);

    // large stack: SOUI window creation paths are deep (the ARM target uses
    // 8192 words; the Win port's StackType_t is size_t)
    if (xTaskCreate(app_task, "soui_app", 1 << 19, NULL, 4, NULL) != pdPASS)
    {
        printf("Failed to create soui_app task\n");
        return 1;
    }

    vTaskStartScheduler();

    for (;;)
    {
    }
    return 0;
}
