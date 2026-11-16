/*
 * syscalls.c -- minimal newlib stubs for a bare-metal Cortex-M target.
 *
 * Only what the swinx STL shims actually touch is implemented:
 *   * _sbrk  : heap backing for malloc / operator new used by std::function,
 *              std::thread's type-erased functor, etc.
 *   * _exit  : reached if anything calls abort() (e.g. the -fno-exceptions
 *              throw->abort neutralising macro in swinx_stl.h).
 * Everything else is a no-op so the image links cleanly.
 */
#include <stdint.h>
#include <errno.h>
#include <sys/reent.h>
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern uint32_t _end;
extern uint32_t _heap_limit;

/* ---- newlib heap thread-safety -------------------------------------------
 * newlib's malloc/free call __malloc_lock/__malloc_unlock around every heap
 * operation, but on bare metal the default implementations are no-ops. With
 * swinx allocating C++ objects (msg queue nodes, sync objects, ...) from
 * multiple tasks concurrently, the unprotected heap interleaves chunks and
 * silently corrupts neighbouring allocations -- which then surfaces as
 * garbage kernel-object handles and a permanently stuck xSemaphoreTake.
 * Guard the heap with one RECURSIVE FreeRTOS mutex: newlib itself recurses
 * into the lock (_realloc_r calls _malloc_r/_free_r internally), so a plain
 * mutex would self-deadlock on the first realloc.
 * ------------------------------------------------------------------------- */
static SemaphoreHandle_t s_heapMutex = NULL;

static SemaphoreHandle_t heap_mutex_get( void )
{
    if( s_heapMutex == NULL )
    {
        taskENTER_CRITICAL();
        if( s_heapMutex == NULL )
            s_heapMutex = xSemaphoreCreateRecursiveMutex();
        taskEXIT_CRITICAL();
    }
    return s_heapMutex;
}

/* With heap_3 the FreeRTOS kernel allocates through newlib malloc, i.e.
 * through the very heap lock being created -- a chicken-and-egg recursion if
 * the lock is lazily created on first use after the scheduler runs (the
 * creating xQueueCreate -> pvPortMalloc -> __malloc_lock -> create ... loop).
 * Eagerly create the lock BEFORE vTaskStartScheduler() (scheduler state
 * NOT_STARTED, so __malloc_lock skips taking it and the creation malloc
 * completes without locking).  Called from startup.c Reset_Handler. */
void heap_lock_early_init( void )
{
    ( void ) heap_mutex_get();
}

void __malloc_lock( struct _reent * r )
{
    ( void ) r;
    /* Take the mutex only when the scheduler is RUNNING.  NOT_STARTED means
     * a single execution flow; SUSPENDED (vTaskSuspendAll window, e.g. inside
     * xTaskCreate allocating the TCB/stack through this very heap) means task
     * switching is stopped, so heap access is single-flow by construction --
     * and a blocking take there would trip queue.c's "cannot block while
     * suspended" assert. */
    if( xTaskGetSchedulerState() != taskSCHEDULER_RUNNING )
        return;
    xSemaphoreTakeRecursive( heap_mutex_get(), portMAX_DELAY );
}

void __malloc_unlock( struct _reent * r )
{
    ( void ) r;
    if( s_heapMutex != NULL &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
    {
        xSemaphoreGiveRecursive( s_heapMutex );
    }
}

void * _sbrk( intptr_t incr )
{
    static uint8_t * heap = ( uint8_t * ) &_end;
    uint8_t * prev = heap;
    uint8_t * next = heap + incr;

    if ( next > ( uint8_t * ) &_heap_limit )
    {
        errno = ENOMEM;
        return ( void * ) -1;
    }

    heap = next;
    return prev;
}

void _exit( int status )
{
    ( void ) status;
    while ( 1 )
    {
    }
}

/* newlib's abort()/raise() path pulls in _kill_r / _getpid_r, which in turn
 * need _kill / _getpid defined or the link fails (and emits a bogus
 * "dangerous relocation" against the undefined symbols). Bare metal has no
 * signals/processes, so we return the "not supported" errno. */
int _kill( int pid, int sig )
{
    ( void ) pid;
    ( void ) sig;
    errno = ENOSYS;
    return -1;
}

int _getpid( void )
{
    return 1;
}

int _write( int fd, const void * buf, size_t count )
{
    ( void ) fd;
    /* route printf / fprintf(stderr) / newlib output to the board UART */
    const char * p = ( const char * ) buf;
    for ( size_t i = 0; i < count; i++ )
    {
        uart_putc( p[ i ] );
    }
    return ( int ) count;
}

int _read( int fd, void * buf, size_t count )
{
    ( void ) fd;
    ( void ) buf;
    ( void ) count;
    return 0;
}

int _close( int fd )
{
    ( void ) fd;
    return 0;
}

int _lseek( int fd, int ptr, int dir )
{
    ( void ) fd;
    ( void ) ptr;
    ( void ) dir;
    return 0;
}

int _fstat( int fd, void * st )
{
    ( void ) fd;
    ( void ) st;
    return 0;
}

int _isatty( int fd )
{
    ( void ) fd;
    return 1;
}

/* TEMP bisect stub: master's log.cpp references this; sysapi.cpp (the POSIX
 * definition) is excluded from the FreeRTOS build. */
__attribute__((weak)) void OutputDebugStringA( const char * s )
{
    ( void ) s;
}

/* C++ runtime hooks: we have no static destructors to register. */
__attribute__( ( weak ) ) int __cxa_atexit( void ( *f )( void * ), void * arg, void * dso )
{
    ( void ) f;
    ( void ) arg;
    ( void ) dso;
    return 0;
}

__attribute__( ( weak ) ) void __cxa_finalize( void * dso )
{
    ( void ) dso;
}

void * __dso_handle = ( void * ) 0;
