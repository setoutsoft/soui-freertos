#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * FreeRTOSConfig.h -- SOUI Windows-host simulator (MSVC-MingW port)
 * Based on lvgl_simulator's config, extended for swinx/SOUI needs.
 *-----------------------------------------------------------*/

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( 1000000000UL )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 8 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 1024 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 64 * 1024 * 1024 ) ) /* unused by heap_3 */
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configQUEUE_REGISTRY_SIZE               8
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           1
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_STREAM_BUFFERS                0
#define configUSE_NEWLIB_REENTRANT              0

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* Software timer definitions (SOUI timers land here). */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( 5 )
#define configTIMER_QUEUE_LENGTH                64
#define configTIMER_TASK_STACK_DEPTH            ( 16384 )

/* API inclusions. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskGetHandle                  1

#define configASSERT( x )    if( ( x ) == 0 ) { fr_sim_assert( __FILE__, __LINE__ ); for( ;; ); }
#ifdef __cplusplus
extern "C" {
#endif
extern void fr_sim_assert( const char *file, int line );
#ifdef __cplusplus
}
#endif

#define configSTACK_DEPTH_TYPE                  uint32_t

#endif /* FREERTOS_CONFIG_H */
