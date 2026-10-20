/*
 * swinx_test/main_input.cpp
 *
 * Input injection tests: driver-level SwinxInjectMouse / SwinxInjectKey ->
 * SConnection::postMsg -> pump (PeekMessage/TranslateMessage/DispatchMessage)
 * -> window procedure.  Covers same-task routing (screen->client conversion,
 * MK_ flags, cursor tracking, key state, WM_CHAR generation) and cross-task
 * injection (a FreeRTOS injector task wakes the UI task blocked in
 * WaitMessage through the queue's auto-reset event).
 */
#include "FreeRTOS.h"
#include "task.h"

#include "swinx_stl.h"
#include "SConnection.h"
#include "input.h"
#include <thread>

extern "C"
{
#include "uart.h"
}

#include "test_common.h"

// ---------------------------------------------------------------------------
// event recording wndproc
// ---------------------------------------------------------------------------
enum { MAX_REC = 16 };
struct RecMsg
{
    UINT msg;
    int wp;
    int x;
    int y;
};
static RecMsg s_rec[MAX_REC];
static int s_nRec = 0;
static HWND s_hInpWnd = NULL;

static void record(UINT msg, WPARAM wp, LPARAM lp)
{
    if (s_nRec < MAX_REC)
    {
        s_rec[s_nRec].msg = msg;
        s_rec[s_nRec].wp = (int)wp;
        s_rec[s_nRec].x = (short)LOWORD(lp);
        s_rec[s_nRec].y = (short)HIWORD(lp);
        s_nRec++;
    }
}

static LRESULT CALLBACK inp_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        record(msg, wp, lp);
        return 0;
    case WM_NCCREATE:
        return TRUE;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

static void register_class(const char *name)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = inp_wndproc;
    wc.lpszClassName = name;
    ATOM atom = RegisterClassA(&wc);
    CHECK(atom != 0);
}

// ---------------------------------------------------------------------------
// 1) same-task mouse: move + click, screen->client conversion, MK_ flags,
//    cursor position tracking
// ---------------------------------------------------------------------------
static void input_mouse_test(void)
{
    register_class("inpw");
    s_hInpWnd = CreateWindowExA(0, "inpw", "inp", WS_VISIBLE,
                                10, 20, 120, 90, NULL, NULL, NULL, NULL);
    CHECK(s_hInpWnd != NULL);
    pump(); // drain creation paint

    // platform semantics: a fresh window's hover state is HTNOWHERE, so the
    // FIRST injected client move is consumed by the HTNOWHERE->HTCLIENT
    // transition (it emits WM_NCMOUSELEAVE instead). Prime the hover state,
    // then test the steady-state routing.
    SwinxInjectMouse(s_hInpWnd, WM_MOUSEMOVE, 40, 50, 0);
    pump();

    s_nRec = 0;

    SwinxInjectMouse(s_hInpWnd, WM_MOUSEMOVE, 40, 50, 0);
    SwinxInjectMouse(s_hInpWnd, WM_LBUTTONDOWN, 40, 50, MK_LBUTTON);
    SwinxInjectMouse(s_hInpWnd, WM_LBUTTONUP, 40, 50, 0);
    pump();

    CHECK(s_nRec == 3);
    CHECK(s_rec[0].msg == WM_MOUSEMOVE);
    CHECK(s_rec[0].x == 30 && s_rec[0].y == 30); // screen (40,50) - origin (10,20)
    CHECK(s_rec[1].msg == WM_LBUTTONDOWN);
    CHECK(s_rec[1].wp == MK_LBUTTON);
    CHECK(s_rec[1].x == 30 && s_rec[1].y == 30);
    CHECK(s_rec[2].msg == WM_LBUTTONUP);
    CHECK(s_rec[2].x == 30 && s_rec[2].y == 30);

    // cursor position tracked in screen coordinates
    POINT pt;
    SConnection *conn = SConnMgr::instance()->getConnection();
    CHECK(conn->GetCursorPos(&pt));
    CHECK(pt.x == 40 && pt.y == 50);

    tprint("[inp] mouse move/click routing + cursor tracking OK\n");
}

// ---------------------------------------------------------------------------
// 2) same-task keyboard: key down/up, GetKeyState, WM_CHAR from the pump
// ---------------------------------------------------------------------------
static void input_key_test(void)
{
    s_nRec = 0;

    SwinxInjectKey(s_hInpWnd, WM_KEYDOWN, 'A', 30);
    pump();
    CHECK(s_nRec == 2);
    CHECK(s_rec[0].msg == WM_KEYDOWN && s_rec[0].wp == 'A');
    CHECK(s_rec[1].msg == WM_CHAR && s_rec[1].wp == 'A'); // TranslateMessage

    SConnection *conn = SConnMgr::instance()->getConnection();
    CHECK((conn->GetKeyState('A') & 0x8000) != 0); // down state visible

    SwinxInjectKey(s_hInpWnd, WM_KEYUP, 'A', 30);
    pump();
    CHECK(s_nRec == 3);
    CHECK(s_rec[2].msg == WM_KEYUP && s_rec[2].wp == 'A');
    CHECK((conn->GetKeyState('A') & 0x8000) == 0); // up clears the state

    tprint("[inp] key down/up + GetKeyState + WM_CHAR OK\n");
}

// ---------------------------------------------------------------------------
// 3) cross-task injection: a FreeRTOS injector task posts into the UI task's
//    queue and wakes it out of a blocked WaitMessage()
// ---------------------------------------------------------------------------
static HWND s_hInpWnd2 = NULL;
static volatile int s_injectorDone = 0;

static void injector_task(void *)
{
    // let the UI task reach WaitMessage() first (blocked, queue empty)
    vTaskDelay(pdMS_TO_TICKS(100));
    SwinxInjectMouse(s_hInpWnd2, WM_LBUTTONDOWN, 15, 25, 0);
    SwinxInjectMouse(s_hInpWnd2, WM_LBUTTONUP, 15, 25, 0);
    s_injectorDone = 1;
}

static void input_cross_task_test(void)
{
    register_class("inpw2");
    s_hInpWnd2 = CreateWindowExA(0, "inpw2", "inp2", WS_VISIBLE,
                                 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    CHECK(s_hInpWnd2 != NULL);
    pump();
    s_nRec = 0;
    s_injectorDone = 0;

    std::thread inj(injector_task, nullptr);
    inj.detach();

    // block until the injected events arrive: WaitMessage() sleeps on the
    // queue event and must be woken by the injector task's postMsg()
    while (s_nRec < 2)
    {
        WaitMessage();
        pump();
    }
    CHECK(s_injectorDone == 1);
    CHECK(s_rec[0].msg == WM_LBUTTONDOWN);
    CHECK(s_rec[0].x == 15 && s_rec[0].y == 25);
    CHECK(s_rec[1].msg == WM_LBUTTONUP);
    CHECK(s_rec[1].x == 15 && s_rec[1].y == 25);

    tprint("[inp] cross-task injection wakes WaitMessage OK\n");
}

// ---------------------------------------------------------------------------
// entry point called from app_task (main.cpp)
// ---------------------------------------------------------------------------
void run_input_tests(void)
{
    tprint("== input injection ==\n");
    input_mouse_test();
    input_key_test();
    input_cross_task_test();
}
