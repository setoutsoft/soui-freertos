/*
 * swinx_test/main_conn.cpp
 *
 * SConnection smoke test on REAL FreeRTOS.  Pulls the swinx core archive
 * (libswinx_core.a) into the link by referencing SConnMgr and exercises the
 * FreeRTOS-platform connection: message post/get, WM_PAINT / WM_MOUSEMOVE
 * coalescing, tick-driven timers, and TranslateMessage.  Prints results over
 * the lm3s6965 UART.
 *
 * SConnection here is the FreeRTOS implementation (no xcb): the queue is a
 * Win32-style list guarded by CountMutex, waitMsg() blocks on an auto-reset
 * winobjs event, timers fire from updateMsgQueue() using FreeRTOS ticks.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "swinx_stl.h"      // swinx_stl namespace -> std (shim-backed)
#include "SConnection.h"    // FreeRTOS-platform connection

extern "C"
{
#include "uart.h"
}

#include "test_common.h"    // shared CHECK / g_fail / tprint

static const HWND kWnd = (HWND)0x1234;

// ---------------------------------------------------------------------------
// 1) post/get roundtrip + WM_PAINT / WM_MOUSEMOVE coalescing
// ---------------------------------------------------------------------------
static void conn_post_get_test(void)
{
    SConnMgr *mgr = SConnMgr::instance();
    CHECK(mgr != NULL);
    SConnection *conn = mgr->getConnection();
    CHECK(conn != NULL);

    MSG msg;

    // post + get roundtrip
    conn->postMsg(kWnd, WM_USER + 1, 111, 222);
    CHECK(conn->getMsg(&msg, NULL, 0, 0));
    CHECK(msg.message == WM_USER + 1);
    CHECK(msg.wParam == 111 && msg.lParam == 222);

    // empty queue -> getMsg fails
    CHECK(!conn->getMsg(&msg, NULL, 0, 0));

    // WM_PAINT coalescing: two posts merge into one queued paint
    conn->postMsg(kWnd, WM_PAINT, 0, 0);
    conn->postMsg(kWnd, WM_PAINT, 0, 0);
    CHECK(conn->getMsg(&msg, NULL, 0, 0) && msg.message == WM_PAINT);
    CHECK(!conn->getMsg(&msg, NULL, 0, 0));

    // WM_MOUSEMOVE coalescing: latest wins
    conn->postMsg(kWnd, WM_MOUSEMOVE, 1, 0);
    conn->postMsg(kWnd, WM_MOUSEMOVE, 2, 0);
    CHECK(conn->getMsg(&msg, NULL, 0, 0) && msg.message == WM_MOUSEMOVE);
    CHECK(msg.wParam == 2);
    CHECK(!conn->getMsg(&msg, NULL, 0, 0));

    tprint("[conn] post/get + paint/mousemove coalescing OK\n");
}

// ---------------------------------------------------------------------------
// 2) tick-driven timers: SetTimer fires WM_TIMER, KillTimer stops it
// ---------------------------------------------------------------------------
static void conn_timer_test(void)
{
    SConnection *conn = SConnMgr::instance()->getConnection();

    UINT_PTR id = conn->SetTimer(kWnd, 0, 50, NULL);
    CHECK(id != 0);

    MSG msg;
    BOOL gotTimer = FALSE;
    for (int i = 0; i < 40 && !gotTimer; ++i)
    {
        if (conn->waitMsg(100))
        {
            if (conn->peekMsg(&msg, NULL, 0, 0, PM_REMOVE) && msg.message == WM_TIMER)
                gotTimer = TRUE;
        }
    }
    CHECK(gotTimer);

    CHECK(conn->KillTimer(kWnd, id));
    // after KillTimer the queue must stay quiet for the full window
    CHECK(!conn->waitMsg(200));

    tprint("[conn] SetTimer fires via tick + KillTimer stops OK\n");
}

// ---------------------------------------------------------------------------
// 3) TranslateMessage: ASCII key-down -> WM_CHAR
// ---------------------------------------------------------------------------
static void conn_translate_test(void)
{
    SConnection *conn = SConnMgr::instance()->getConnection();

    MSG kd;
    kd.hwnd = kWnd;
    kd.message = WM_KEYDOWN;
    kd.wParam = 'A';
    kd.lParam = 0;
    CHECK(conn->TranslateMessage(&kd));

    MSG msg;
    CHECK(conn->getMsg(&msg, NULL, 0, 0));
    CHECK(msg.message == WM_CHAR);
    CHECK(msg.wParam == 'A');

    tprint("[conn] TranslateMessage key->char OK\n");
}

// ---------------------------------------------------------------------------
// entry point called from app_task (main.cpp)
// ---------------------------------------------------------------------------
void run_sconnection_tests(void)
{
    tprint("== SConnection (FreeRTOS, no xcb) smoke ==\n");
    conn_post_get_test();
    conn_timer_test();
    conn_translate_test();
}
