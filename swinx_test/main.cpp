/*
 * swinx_test/main.cpp
 *
 * Self-contained swinx sync-layer test for REAL FreeRTOS. Exercises the swinx
 * Win32 sync-compat API (CRITICAL_SECTION / SRWLOCK / INIT_ONCE) -- the same
 * surface covered by demos/fun_test/test_cs.cpp -- on the actual FreeRTOS API
 * path through the STL shims, and prints PASS/FAIL over the lm3s6965 UART.
 *
 * No gtest, no Win32 events: cross-thread ordering is achieved by holding a
 * lock in the main task while a worker task races it, exactly like the
 * handshaking strategy in test_cs.cpp.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <mutex>
#include <thread>
#include <cstdint>

#include "freertos_sync.h"   // swinx FreeRTOS sync decls (no mega windows.h)

extern "C"
{
#include "uart.h"
}

// ---------------------------------------------------------------------------
// tiny UART helpers
// ---------------------------------------------------------------------------
#include "test_common.h"   // shared CHECK / g_fail / tprint

static void uart_print(const char *s)
{
    uart_puts(s);
}

static void uart_print_int(int v)
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
// 1) CRITICAL_SECTION -- recursive (single task)
// ---------------------------------------------------------------------------
static void cs_recursive_test(void)
{
    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);

    EnterCriticalSection(&cs);
    EnterCriticalSection(&cs);
    CHECK(TryEnterCriticalSection(&cs));   // still owned -> succeeds
    LeaveCriticalSection(&cs);
    LeaveCriticalSection(&cs);
    LeaveCriticalSection(&cs);

    DeleteCriticalSection(&cs);
    uart_print("[cs] recursive enter/leave OK\n");
}

// ---------------------------------------------------------------------------
// 2) CRITICAL_SECTION -- cross-thread mutual exclusion
// ---------------------------------------------------------------------------
static CRITICAL_SECTION g_cs;
static volatile LONG g_counter = 0;

static void cs_worker(void)
{
    for (int i = 0; i < 1000; ++i)
    {
        EnterCriticalSection(&g_cs);
        g_counter++;            // guarded: no lost updates
        LeaveCriticalSection(&g_cs);
    }
}

static void cs_cross_thread_test(void)
{
    InitializeCriticalSection(&g_cs);
    g_counter = 0;

    std::thread t1(cs_worker);
    std::thread t2(cs_worker);
    t1.join();
    t2.join();

    CHECK(g_counter == 2000);
    DeleteCriticalSection(&g_cs);
    uart_print("[cs] cross-thread mutual exclusion OK (");
    uart_print_int((int)g_counter);
    uart_print(" increments)\n");
}

// ---------------------------------------------------------------------------
// 3) SRWLOCK -- exclusive + shared + try variants + concurrent readers
// ---------------------------------------------------------------------------
static void srw_exclusive_basic_test(void)
{
    SRWLOCK lock;
    InitializeSRWLock(&lock);

    AcquireSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockExclusive(&lock);   // re-acquirable after release
    ReleaseSRWLockExclusive(&lock);

    AcquireSRWLockShared(&lock);
    ReleaseSRWLockShared(&lock);
    AcquireSRWLockShared(&lock);
    ReleaseSRWLockShared(&lock);

    // try variants
    CHECK(TryAcquireSRWLockExclusive(&lock));
    CHECK(!TryAcquireSRWLockExclusive(&lock));   // already held exclusive
    CHECK(!TryAcquireSRWLockShared(&lock));
    ReleaseSRWLockExclusive(&lock);

    CHECK(TryAcquireSRWLockShared(&lock));
    CHECK(TryAcquireSRWLockShared(&lock));       // concurrent readers allowed
    CHECK(!TryAcquireSRWLockExclusive(&lock));   // exclusive blocked while shared
    ReleaseSRWLockShared(&lock);
    ReleaseSRWLockShared(&lock);

    CHECK(TryAcquireSRWLockExclusive(&lock));
    ReleaseSRWLockExclusive(&lock);

    UninitializeSRWLock(&lock);
    uart_print("[srw] exclusive/shared/try variants OK\n");
}

static SRWLOCK g_rw;
static volatile LONG g_shared_ok = 0;

static void srw_shared_worker(void)
{
    // Must succeed WHILE the main task still holds the shared lock.
    AcquireSRWLockShared(&g_rw);
    g_shared_ok = 1;
    ReleaseSRWLockShared(&g_rw);
}

static void srw_concurrent_readers_test(void)
{
    InitializeSRWLock(&g_rw);
    g_shared_ok = 0;

    AcquireSRWLockShared(&g_rw);          // main holds shared
    std::thread w(srw_shared_worker);
    w.join();                             // worker acquires shared concurrently
    CHECK(g_shared_ok == 1);
    ReleaseSRWLockShared(&g_rw);

    UninitializeSRWLock(&g_rw);
    uart_print("[srw] concurrent shared readers OK\n");
}

// ---------------------------------------------------------------------------
// 4) INIT_ONCE -- runs the init function exactly once across tasks
// ---------------------------------------------------------------------------
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_once_val = 0;

static BOOL WINAPI once_fn(PINIT_ONCE, PVOID, PVOID *Context)
{
    g_once_val = 12345;
    if (Context)
        *Context = NULL;
    return TRUE;
}

static void once_worker(void)
{
    LPVOID c = NULL;
    InitOnceExecuteOnce(&g_once, once_fn, NULL, &c);
}

static void initonce_test(void)
{
    InitOnceInitialize(&g_once);
    g_once_val = 0;

    std::thread t1(once_worker);
    std::thread t2(once_worker);
    t1.join();
    t2.join();

    CHECK(g_once_val == 12345);          // set exactly once, visible to both
    uart_print("[initonce] one-time init OK (val=");
    uart_print_int((int)g_once_val);
    uart_print(")\n");
}

// ---------------------------------------------------------------------------
// app task + reset handler
// ---------------------------------------------------------------------------
static void app_task(void)
{
    uart_print("swinx sync layer on REAL FreeRTOS starting...\n");

    cs_recursive_test();
    cs_cross_thread_test();
    srw_exclusive_basic_test();
    srw_concurrent_readers_test();
    initonce_test();

    // sync objects (events / mutexes / semaphores / threads) -- the surface
    // covered by demos/fun_test/test_sync.cpp and test_thread.cpp
    run_winobj_tests();

    // SConnection (FreeRTOS platform, no xcb) -- pulls the swinx core archive
    // into the link and smoke-tests the message pump / timers
    run_sconnection_tests();

    // framebuffer renderer: core window path (create -> paint -> commitCanvas)
    run_render_tests();

    // input injection: mouse/key -> SConnection queue -> pump -> wndproc
    run_input_tests();

    // dump the whole framebuffer to the host via semihosting (fb.ppm)
    run_dump_tests();

    if (g_fail == 0)
        uart_print("SWINX WINOBJS FREERTOS PASS\n");
    else
    {
        uart_print("SWINX WINOBJS FREERTOS FAIL (");
        uart_print_int(g_fail);
        uart_print(" checks failed)\n");
    }

    for (;;)
        taskYIELD();
}

extern "C" void entry(void)
{
    uart_init();

    std::thread t(app_task);
    t.detach();                 // ownership transfers to the FreeRTOS task

    vTaskStartScheduler();

    for (;;)
        ;
}
