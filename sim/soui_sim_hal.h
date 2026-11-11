/*
 * soui_sim_hal.h -- SDL2 window/present/input bridge for the SOUI FreeRTOS
 * simulator.  Shared by every project that builds with SIM_HOST.
 *
 * Deliberately keeps SDL out of the rest of the build: everything SDL lives
 * in soui_sim_hal.cpp, which self-declares the few swinx framebuffer/input
 * entry points it needs (avoiding the swinx windows.h shim vs SDL clash).
 *
 * The project-facing entry points are SimHalInit / PumpEvents / RenderIfDirty --
 * that is the whole contract.  Interaction is the real mouse and keyboard of
 * the host: whatever you do in the SDL window is injected into swinx and
 * handled by the real SOUI window code, no test harness involved.
 */
#ifndef SOUI_SIM_HAL_H_
#define SOUI_SIM_HAL_H_

/* plain C++ interface (hal.cpp is C++; consumed by the app's main.cpp) */

/* SDL_Init + window + renderer + texture + SwinxSetFbPresentCb.
 * Call from the app task (the scheduler owns the main thread). 0 = ok. */
int SimHalInit(void);

/* route SDL mouse/keyboard events into swinx (SwinxInjectMouse/Key) */
void SimHalPumpEvents(void);

/* target window for injected mouse events (SOUI host hwnd) */
void SimHalSetTargetHwnd(void *hWnd);

/* if the fb changed since last frame: copy fb -> texture -> render.
 * returns 1 when a frame was presented. */
int SimHalRenderIfDirty(void);

/* snapshot the framebuffer to path (.bmp); >0 on success */
int SimHalDumpNow(const char *path);

#endif // SOUI_SIM_HAL_H_
