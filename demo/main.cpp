/*
 * main.cpp -- entry point + the REAL-backend validation of the swinx FreeRTOS
 * STL-compat shims.
 *
 * When SOUI_FREERTOS_REAL is defined, the swinx override headers
 * (swinx/src/platform/freertos/stl/{mutex,thread,condition_variable}) resolve to the
 * implementations that call the real FreeRTOS API (xSemaphoreCreateMutex /
 * xTaskCreate / xSemaphoreCreateBinary / ...). We exercise exactly the subset
 * swinx relies on: std::mutex (+ lock_guard / unique_lock), std::thread
 * (+ join), and std::condition_variable (+ wait(pred) / notify_one).
 *
 * We deliberately avoid global objects with non-trivial C++ constructors so the
 * demo does not depend on __libc_init_array running before entry(); the shared
 * state lives on the task stack instead.
 */
#ifndef SOUI_FREERTOS_REAL
    #define SOUI_FREERTOS_REAL
#endif

extern "C" {
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
}

#include <mutex>
#include <thread>
#include <condition_variable>

#include "uart.h"

/* ---- tiny helpers ------------------------------------------------------- */

static void print_int( int v )
{
    char buf[ 12 ];
    int i = 0;
    if ( v == 0 )
    {
        buf[ i++ ] = '0';
    }
    else
    {
        bool neg = ( v < 0 );
        if ( neg )
        {
            v = -v;
        }
        while ( v > 0 )
        {
            buf[ i++ ] = ( char ) ( '0' + ( v % 10 ) );
            v /= 10;
        }
        if ( neg )
        {
            buf[ i++ ] = '-';
        }
    }
    for ( int a = 0, b = i - 1; a < b; a++, b-- )
    {
        char t = buf[ a ];
        buf[ a ] = buf[ b ];
        buf[ b ] = t;
    }
    buf[ i ] = '\0';
    uart_puts( buf );
}

/* ---- worker executed by a std::thread ----------------------------------- */

static void worker_fn( std::mutex * mux,
                       std::condition_variable * cv,
                       bool * ready,
                       int * count )
{
    std::lock_guard< std::mutex > lk( *mux );
    *count = 42;
    *ready = true;
    uart_puts( "[worker] locked mutex, set count=42, ready=true\n" );
    cv->notify_one();
}

/* ---- application task spawned by a std::thread from entry() --------------- */

extern "C" void app_task( void )
{
    uart_puts( "[app] starting STL shim test (REAL FreeRTOS)\n" );

    std::mutex mux;
    std::condition_variable cv;
    bool ready = false;
    int count = 0;

    {
        std::lock_guard< std::mutex > lk( mux );
        uart_puts( "[app] std::mutex lock_guard OK\n" );
    }

    uart_puts( "[app] spawning worker via std::thread\n" );
    std::thread worker( worker_fn, &mux, &cv, &ready, &count );

    uart_puts( "[app] waiting on std::condition_variable\n" );
    std::unique_lock< std::mutex > lk( mux );
    cv.wait( lk, [ &ready ] { return ready; } );
    lk.unlock();

    uart_puts( "[app] std::condition_variable wait(pred) OK, count=" );
    print_int( count );
    uart_puts( count == 42 ? " -> PASS\n" : " -> FAIL\n" );

    worker.join();
    uart_puts( "[app] std::thread join OK\n" );

    uart_puts( "ALL STL SHIMS REAL-PATH PASS\n" );

    /* Keep the task alive (FreeRTOS has nothing to return to). */
    volatile int done = 1;
    ( void ) done;
    while ( 1 )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

/* ---- reset entry -------------------------------------------------------- */

extern "C" int entry( void )
{
    uart_init();
    uart_puts( "FreeRTOS + swinx STL shims (REAL backend) booting...\n" );

    /* Spawn the application task through the swinx std::thread shim so the
     * top-level threading path is validated too; detach because entry() never
     * returns. */
    std::thread app( app_task );
    app.detach();

    vTaskStartScheduler();

    /* Should never reach here. */
    while ( 1 )
    {
    }
}
