/*
 * main_syncobj.cpp
 *
 * Bare-metal port of demos/fun_test/test_sync.cpp and test_thread.cpp onto the
 * swinx FreeRTOS winobjs bridge (CreateEvent / CreateMutex / CreateSemaphore /
 * CreateThread / WaitForSingleObject / WaitForMultipleObjects / Interlocked*).
 * Exercises the same contracts the gtest suite enforces on the Linux/Win32
 * compat layer, printing PASS/FAIL over the lm3s6965 UART.
 *
 * The SOUI::SSemaphore case from test_sync.cpp is not ported here: it needs
 * the full SOUI core, which the standalone FreeRTOS test harness does not
 * build (it links only the swinx bridge files + the FreeRTOS kernel).
 */
#include "test_common.h"
#include <windows.h>   // sysapi.h decls (CreateEventA ... CloseHandle), core HANDLE

// access flags for the OpenXxx calls (not part of the FreeRTOS header set)
#ifndef EVENT_MODIFY_STATE
#define EVENT_MODIFY_STATE      0x0002
#define SYNCHRONIZE             0x00100000
#define SEMAPHORE_MODIFY_STATE  0x0002
#define MUTEX_ALL_ACCESS        0x001F0000
#endif

#include <cstdint>

// ---------------------------------------------------------------------------
// helpers shared by the cases
// ---------------------------------------------------------------------------
static DWORD WINAPI set_event_thread(LPVOID p)
{
    HANDLE evt = (HANDLE)p;
    Sleep(100);
    SetEvent(evt);
    return 0;
}

struct MutexCtx
{
    HANDLE mutex;
    HANDLE acquired;
    HANDLE releaseNow; // main task signals this before the worker releases
};

static DWORD WINAPI hold_and_release_mutex(LPVOID p)
{
    MutexCtx *c = (MutexCtx *)p;
    // Hold until the main task explicitly signals release: that way the
    // "wait must time out while held" probe never depends on Sleep racing
    // (same handshake strategy as test_sync.cpp).
    if (WaitForSingleObject(c->mutex, 1000) == WAIT_OBJECT_0)
        SetEvent(c->acquired);
    WaitForSingleObject(c->releaseNow, 5000);
    ReleaseMutex(c->mutex);
    return 0;
}

// ---------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------
static void event_manual_reset_test(void)
{
    HANDLE h = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(h != NULL);

    CHECK(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);

    CHECK(SetEvent(h));
    // manual-reset event stays signaled for multiple waits
    CHECK(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);

    CHECK(ResetEvent(h));
    CHECK(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);

    CloseHandle(h);
    tprint("[event] manual reset OK\n");
}

static void event_auto_reset_test(void)
{
    HANDLE h = CreateEventA(NULL, FALSE, FALSE, NULL);
    CHECK(h != NULL);

    SetEvent(h);
    CHECK(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);
    // auto-reset event is consumed by a single successful wait
    CHECK(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);

    CloseHandle(h);
    tprint("[event] auto reset OK\n");
}

static void event_initial_state_test(void)
{
    HANDLE h = CreateEventA(NULL, TRUE, TRUE, NULL);
    CHECK(h != NULL);
    CHECK(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);
    CloseHandle(h);
    tprint("[event] initial state OK\n");
}

static void event_named_open_test(void)
{
    HANDLE h1 = CreateEventA(NULL, TRUE, FALSE, "swinx_evt_test");
    CHECK(h1 != NULL);

    HANDLE h2 = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "swinx_evt_test");
    CHECK(h2 != NULL);

    // signaling through the second handle wakes a waiter on the first
    SetEvent(h2);
    CHECK(WaitForSingleObject(h1, 0) == WAIT_OBJECT_0);

    CloseHandle(h2);
    CloseHandle(h1);
    tprint("[event] named open OK\n");
}

static void event_cross_thread_test(void)
{
    HANDLE h = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(h != NULL);

    // pass the event handle itself (not a pointer to it)
    HANDLE th = CreateThread(NULL, 0, set_event_thread, h, 0, NULL);
    CHECK(th != NULL);

    DWORD ts0 = GetTickCount();
    CHECK(WaitForSingleObject(h, 3000) == WAIT_OBJECT_0);
    CHECK(GetTickCount() - ts0 >= 50);

    CHECK(WaitForSingleObject(th, 3000) == WAIT_OBJECT_0);
    CloseHandle(th);
    CloseHandle(h);
    tprint("[event] cross-thread signal OK\n");
}

static void event_wait_timeout_test(void)
{
    HANDLE h = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(h != NULL);
    DWORD ts0 = GetTickCount();
    CHECK(WaitForSingleObject(h, 120) == WAIT_TIMEOUT);
    CHECK(GetTickCount() - ts0 >= 100);
    CloseHandle(h);
    tprint("[event] wait timeout OK\n");
}

// ---------------------------------------------------------------------------
// WaitForMultipleObjects
// ---------------------------------------------------------------------------
static void wait_multiple_all_test(void)
{
    HANDLE evts[2] = {
        CreateEventA(NULL, TRUE, FALSE, NULL),
        CreateEventA(NULL, TRUE, FALSE, NULL),
    };
    CHECK(evts[0] != NULL && evts[1] != NULL);

    // wait-all times out until both are signaled
    SetEvent(evts[0]);
    CHECK(WaitForMultipleObjects(2, evts, TRUE, 100) == WAIT_TIMEOUT);
    SetEvent(evts[1]);
    CHECK(WaitForMultipleObjects(2, evts, TRUE, 100) == WAIT_OBJECT_0);

    CloseHandle(evts[0]);
    CloseHandle(evts[1]);
    tprint("[waitmulti] wait-all OK\n");
}

static void wait_multiple_any_test(void)
{
    HANDLE evts[2] = {
        CreateEventA(NULL, TRUE, FALSE, NULL),
        CreateEventA(NULL, TRUE, FALSE, NULL),
    };
    CHECK(evts[0] != NULL && evts[1] != NULL);

    SetEvent(evts[1]);
    // returns the index of the first signaled object
    CHECK(WaitForMultipleObjects(2, evts, FALSE, 100) == WAIT_OBJECT_0 + 1);

    CloseHandle(evts[0]);
    CloseHandle(evts[1]);
    tprint("[waitmulti] wait-any OK\n");
}

// ---------------------------------------------------------------------------
// mutex
// ---------------------------------------------------------------------------
static void mutex_ownership_test(void)
{
    HANDLE m = CreateMutexA(NULL, FALSE, NULL);
    CHECK(m != NULL);

    CHECK(WaitForSingleObject(m, 100) == WAIT_OBJECT_0);
    CHECK(ReleaseMutex(m));
    // released: re-acquirable
    CHECK(WaitForSingleObject(m, 100) == WAIT_OBJECT_0);
    CHECK(ReleaseMutex(m));

    CloseHandle(m);
    tprint("[mutex] acquire/release OK\n");
}

static void mutex_cross_thread_test(void)
{
    HANDLE m = CreateMutexA(NULL, FALSE, NULL);
    CHECK(m != NULL);

    HANDLE acquired = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(acquired != NULL);
    HANDLE releaseNow = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(releaseNow != NULL);

    MutexCtx ctx = {m, acquired, releaseNow};

    HANDLE th = CreateThread(NULL, 0, hold_and_release_mutex, &ctx, 0, NULL);
    CHECK(th != NULL);

    // wait until the worker holds the mutex
    CHECK(WaitForSingleObject(acquired, 3000) == WAIT_OBJECT_0);

    // the mutex is held by the worker (it is blocked on releaseNow, so it
    // cannot release behind our back): wait must time out
    CHECK(WaitForSingleObject(m, 150) == WAIT_TIMEOUT);

    // after the worker releases, we can acquire it
    SetEvent(releaseNow);
    CHECK(WaitForSingleObject(m, 3000) == WAIT_OBJECT_0);
    ReleaseMutex(m);

    CHECK(WaitForSingleObject(th, 3000) == WAIT_OBJECT_0);
    CloseHandle(th);
    CloseHandle(releaseNow);
    CloseHandle(acquired);
    CloseHandle(m);
    tprint("[mutex] cross-thread exclusion OK\n");
}

static void mutex_named_open_test(void)
{
    HANDLE m1 = CreateMutexA(NULL, FALSE, "swinx_mtx_test");
    CHECK(m1 != NULL);

    HANDLE m2 = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "swinx_mtx_test");
    CHECK(m2 != NULL);

    // acquire through one handle, release through the other (same task)
    CHECK(WaitForSingleObject(m1, 100) == WAIT_OBJECT_0);
    CHECK(ReleaseMutex(m2));
    CHECK(WaitForSingleObject(m2, 100) == WAIT_OBJECT_0);
    ReleaseMutex(m1);

    CloseHandle(m2);
    CloseHandle(m1);
    tprint("[mutex] named open OK\n");
}

// ---------------------------------------------------------------------------
// semaphore
// ---------------------------------------------------------------------------
static void semaphore_counting_test(void)
{
    HANDLE s = CreateSemaphoreA(NULL, 0, 2, NULL);
    CHECK(s != NULL);

    CHECK(WaitForSingleObject(s, 0) == WAIT_TIMEOUT);

    LONG prev = -1;
    CHECK(ReleaseSemaphore(s, 2, &prev));
    CHECK(prev == 0);

    CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(s, 0) == WAIT_TIMEOUT);

    CloseHandle(s);
    tprint("[sem] counting OK\n");
}

static void semaphore_initial_count_test(void)
{
    HANDLE s = CreateSemaphoreA(NULL, 2, 2, NULL);
    CHECK(s != NULL);
    CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(s, 0) == WAIT_TIMEOUT);
    CloseHandle(s);
    tprint("[sem] initial count OK\n");
}

static void semaphore_named_open_test(void)
{
    HANDLE s1 = CreateSemaphoreA(NULL, 0, 2, "swinx_sem_test");
    CHECK(s1 != NULL);

    HANDLE s2 = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, "swinx_sem_test");
    CHECK(s2 != NULL);

    // signaling through the opened handle is visible on the original
    LONG prev = -1;
    CHECK(ReleaseSemaphore(s2, 1, &prev));
    CHECK(prev == 0);
    CHECK(WaitForSingleObject(s1, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(s1, 0) == WAIT_TIMEOUT);

    CloseHandle(s2);
    CloseHandle(s1);
    tprint("[sem] named open OK\n");
}

// ---------------------------------------------------------------------------
// interlocked
// ---------------------------------------------------------------------------
static void interlocked_test(void)
{
    LONG v = 41;
    CHECK(InterlockedIncrement(&v) == 42);
    CHECK(v == 42);

    int64_t v64 = 0x7ffffffffffffffeLL;
    CHECK(InterlockedIncrement64(&v64) == 0x7fffffffffffffffLL);
    CHECK(v64 == 0x7fffffffffffffffLL);

    LONG w = 10;
    CHECK(InterlockedCompareExchange(&w, 99, 10) == 10); // match: returns old
    CHECK(w == 99);
    CHECK(InterlockedCompareExchange(&w, 55, 10) == 99); // no match: returns old
    CHECK(w == 99);

    LONG a = 7;
    CHECK(InterlockedExchangeAdd(&a, 5) == 7);
    CHECK(a == 12);

    tprint("[interlocked] increment/compare/add OK\n");
}

// ---------------------------------------------------------------------------
// threads
// ---------------------------------------------------------------------------
static volatile LONG s_counter = 0;

static DWORD WINAPI counter_thread(LPVOID)
{
    for (int i = 0; i < 1000; i++)
        InterlockedIncrement(&s_counter);
    return 1234;
}

static void thread_create_and_join_test(void)
{
    s_counter = 0;
    tid_t tid = 0;
    HANDLE th = CreateThread(NULL, 0, counter_thread, NULL, 0, &tid);
    CHECK(th != NULL);
    CHECK(tid != GetCurrentThreadId());

    CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
    CHECK(s_counter == 1000);
    CloseHandle(th);
    tprint("[thread] create and join OK\n");
}

static void thread_multiple_test(void)
{
    s_counter = 0;
    const int N = 4;
    HANDLE ths[4] = {0};
    for (int i = 0; i < N; i++)
    {
        ths[i] = CreateThread(NULL, 0, counter_thread, NULL, 0, NULL);
        CHECK(ths[i] != NULL);
    }
    CHECK(WaitForMultipleObjects(N, ths, TRUE, 10000) == WAIT_OBJECT_0);
    CHECK(s_counter == 1000 * N);
    for (int i = 0; i < N; i++)
        CloseHandle(ths[i]);
    tprint("[thread] multiple threads OK\n");
}

static DWORD WINAPI waiting_thread(LPVOID p)
{
    HANDLE evt = (HANDLE)p;
    WaitForSingleObject(evt, 5000);
    return 0;
}

static void thread_suspend_resume_test(void)
{
    HANDLE evt = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(evt != NULL);

    HANDLE th = CreateThread(NULL, 0, waiting_thread, evt, 0, NULL);
    CHECK(th != NULL);

    Sleep(50); // let the thread enter its wait
    DWORD prev = SuspendThread(th);
    CHECK(prev == 0);
    CHECK(SuspendThread(th) == 1); // suspend count grows

    CHECK(ResumeThread(th) == 2);
    CHECK(ResumeThread(th) == 1);
    CHECK(ResumeThread(th) == 0); // fully resumed

    SetEvent(evt);
    CHECK(WaitForSingleObject(th, 3000) == WAIT_OBJECT_0);

    CloseHandle(th);
    CloseHandle(evt);
    tprint("[thread] suspend/resume OK\n");
}

static void thread_id_uniqueness_test(void)
{
    s_counter = 0;
    tid_t t1 = 0, t2 = 0;
    HANDLE h1 = CreateThread(NULL, 0, counter_thread, NULL, 0, &t1);
    HANDLE h2 = CreateThread(NULL, 0, counter_thread, NULL, 0, &t2);
    CHECK(h1 != NULL && h2 != NULL);
    CHECK(t1 != t2);
    CHECK(t1 != (tid_t)GetCurrentThreadId());

    HANDLE ths[2] = {h1, h2};
    CHECK(WaitForMultipleObjects(2, ths, TRUE, 5000) == WAIT_OBJECT_0);
    CloseHandle(h1);
    CloseHandle(h2);
    tprint("[thread] id uniqueness OK\n");
}

// ---------------------------------------------------------------------------
// entry point called by main.cpp's app_task
// ---------------------------------------------------------------------------
void run_winobj_tests(void)
{
    tprint("swinx win32 sync objects on REAL FreeRTOS starting...\n");

    event_manual_reset_test();
    event_auto_reset_test();
    event_initial_state_test();
    event_named_open_test();
    event_cross_thread_test();
    event_wait_timeout_test();

    wait_multiple_all_test();
    wait_multiple_any_test();

    mutex_ownership_test();
    mutex_cross_thread_test();
    mutex_named_open_test();

    semaphore_counting_test();
    semaphore_initial_count_test();
    semaphore_named_open_test();

    interlocked_test();

    thread_create_and_join_test();
    thread_multiple_test();
    thread_suspend_resume_test();
    thread_id_uniqueness_test();
}
