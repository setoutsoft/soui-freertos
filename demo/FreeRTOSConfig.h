/*
 * FreeRTOSConfig.h -- configuration for the lm3s6965evb QEMU demo that
 * exercises the swinx FreeRTOS STL-compat shims on the REAL FreeRTOS API.
 *
 * Tuned for a Cortex-M3 with 4 priority bits. configCHECK_HANDLER_INSTALLATION
 * is 0 so we do not depend on the exact vector-table wiring being asserted at
 * scheduler start; the handlers are still placed at the correct indices in
 * startup.c.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      72000000UL
#define configSYSTICK_CLOCK_HZ                  configCPU_CLOCK_HZ
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 512 )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  0

/* Hook / facility switches required by the kernel headers. */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                       0
#define configUSE_TIMERS                          0
#define configUSE_EVENT_GROUPS                    1
#define configUSE_TRACE_FACILITY                  0
#define configUSE_STATS_FORMATTING_FUNCTIONS      0
#define configUSE_CO_ROUTINES                     0
#define configUSE_MALLOC_FAILED_HOOK              0
#define configUSE_APPLICATION_TASK_TAG            0

#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 48 * 1024 ) )

/* Cortex-M3 implements 4 priority bits (the top 4 of the 8-bit priority
 * register are used). */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY     0x0f
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 0x0a
#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define configASSERT_DEFINED                     0
#define configCHECK_HANDLER_INSTALLATION         0
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetIdleTaskHandle           0
#define INCLUDE_uxTaskGetStackHighWaterMark      0

#endif /* FREERTOS_CONFIG_H */
