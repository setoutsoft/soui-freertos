/*
 * soui_fr/hooks.c -- FreeRTOS diagnostic hooks for the SOUI link attempt.
 * Kept as a C TU so the linkage matches FreeRTOSConfig.h's plain declarations
 * (that header, when pulled into a C++ TU, declares the hooks with C++
 * linkage and conflicts with an extern "C" definition).
 */
#include "FreeRTOS.h"
#include "task.h"

#include "uart.h"

void vAssertCalled(const char *file, int line)
{
    (void)line;
    uart_puts("*** ASSERT *** ");
    uart_puts(file);
    uart_puts("\n");
    for (;;)
        taskDISABLE_INTERRUPTS();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    uart_puts("*** STACK OVERFLOW *** ");
    uart_puts(pcTaskName);
    uart_puts("\n");
    for (;;)
        taskDISABLE_INTERRUPTS();
}

void vApplicationMallocFailedHook(void)
{
    uart_puts("*** MALLOC FAILED ***\n");
    for (;;)
        taskDISABLE_INTERRUPTS();
}
