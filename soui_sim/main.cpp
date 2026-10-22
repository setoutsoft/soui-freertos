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
#include <windows.h>

#include <SAppCfg.h> // wizard-style application configuration

#include "MainDlg.h"
#include "soui_sim_hal.h"
#include "input.h" // SwinxInjectMouse (diagnostic auto-exit path)

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


static void demo_SWinxLogCallback(const char *pLogStr, int level){
    SLOG("swinx",level)<<pLogStr;
}

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
    SetSwinxLogCallback(demo_SWinxLogCallback,0);
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
    MSG msg;
    while(IsWindow(hWnd)){
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
static int s_appRc = 0; // RunApp() return code, surfaced to main() for exit()

static void app_task(void *)
{
    int rc = RunApp();
    s_appRc = rc;
    printf("[app] app task finished (rc=%d)\n", rc);
    fflush(stdout);
    // Single-application simulator: closing the SOUI window ends the process.
    //
    // End the scheduler first.  vTaskEndScheduler() deletes the kernel's timer
    // daemon and idle tasks and flips xPortRunning to FALSE; the main thread's
    // prvProcessSimulatedInterrupts() then returns out of vTaskStartScheduler()
    // and main() exits on the *native* thread (no live kernel threads left to
    // race the static destructors -- the earlier exit()-from-task race).
    //
    // CRUCIAL: do NOT let this task function return.  When a task function
    // returns, the kernel deletes it via vTaskDelete(NULL), whose critical
    // section exit trips configASSERT( xPortRunning ) in vPortExitCritical
    // (FreeRTOS-Kernel/portable/MSVC-MingW/port.c:748): the Win32 port asserts
    // the scheduler is still running while a task is torn down, but we have just
    // stopped it.  Terminate this thread the same way the port itself deletes
    // tasks (vPortTaskDelete -> ExitThread(0)) so the assert path is bypassed.
    // After this no FreeRTOS task thread is alive; main() then terminates via
    // _exit() (see main) without running static destructors, so the stopped
    // scheduler is never re-entered during teardown.
    vTaskEndScheduler();
    ExitThread(0);
}

// RtlCaptureStackBackTrace is exported by kernel32; declare it directly (this
// MinGW-w64's dbghelp.h is broken, so we avoid the header entirely).  ASLR is
// disabled at link time, so the raw addresses below map straight to the binary.
extern "C" USHORT WINAPI RtlCaptureStackBackTrace(ULONG FramesToSkip,
                                                  ULONG FramesToCapture,
                                                  void **BackTrace,
                                                  unsigned long *BackTraceHash);

extern "C" void fr_sim_assert(const char *file, int line)
{
    fprintf(stderr, "FreeRTOS ASSERT at %s:%d\n", file ? file : "?", line);
    fflush(stderr);

    // Dump the stack of the thread that tripped the assert as raw return
    // addresses.  With ASLR disabled they map directly to symbols/offsets in
    // the binary; resolve offline with:  addr2line -e soui_sim.exe -f -C <addr>
    const int kMax = 64;
    void *bt[kMax];
    USHORT n = RtlCaptureStackBackTrace(1, kMax, bt, NULL);
    for (USHORT i = 0; i < n; i++)
        fprintf(stderr, "  #%02u %p\n", i, bt[i]);
    fflush(stderr);
    _exit(3);
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

    // app_task already ended the scheduler (vTaskEndScheduler() -> xPortRunning
    // = FALSE) and the native thread now owns process teardown.  We must NOT let
    // the C runtime run static destructors here: the FreeRTOS-backed
    // std::mutex / std::recursive_mutex objects from the swinx stl/mutex shim
    // delete their underlying FreeRTOS semaphore in their destructor, which
    // calls vPortExitCritical() *after* the scheduler has stopped -- the Win32
    // port's vPortExitCritical asserts configASSERT( xPortRunning ) in that
    // state (FreeRTOS-Kernel/portable/MSVC-MingW/port.c).  _exit() terminates
    // without running static destructors or atexit handlers, so the stopped
    // scheduler is never touched during teardown.  The kernel file above is
    // vendored and intentionally left unmodified, so the fix lives here.
    _exit(s_appRc);
}
