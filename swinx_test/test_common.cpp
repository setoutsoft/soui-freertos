/*
 * test_common.cpp -- shared CHECK / fail counter / UART printing.
 */
extern "C"
{
#include "FreeRTOS.h"
#include "task.h"
}

#include "test_common.h"

extern "C"
{
#include "uart.h"
}

int g_fail = 0;

void tprint(const char *s)
{
    uart_puts(s);
}

void tprint_int(int v)
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

void check_fail(const char *expr, int line)
{
    g_fail++;
    tprint("FAIL: ");
    tprint(expr);
    tprint(" (line ");
    tprint_int(line);
    tprint(")\n");
}

// ---------------------------------------------------------------------------
// FreeRTOS fault hooks (debug): print over UART and spin so the point of
// failure is visible on the console instead of silently hanging.
// ---------------------------------------------------------------------------
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *pcTaskName)
{
    tprint("\n*** STACK OVERFLOW in task: ");
    tprint(pcTaskName ? pcTaskName : "?");
    tprint(" ***\n");
    for (;;)
        ;
}

extern "C" void vApplicationMallocFailedHook(void)
{
    tprint("\n*** MALLOC FAILED ***\n");
    for (;;)
        ;
}

extern "C" void vAssertCalled(const char *file, int line)
{
    tprint("\n*** ASSERT in ");
    tprint(file);
    tprint(":");
    tprint_int(line);
    tprint(" ***\n");
    /* TEMP debug: scan the current task stack for FLASH return addresses
     * (thumb bit set, inside ssram1) and print them for addr2line. */
    {
        char local;
        uintptr_t sp = (uintptr_t)&local;
        uintptr_t msp = 0, psp = 0;
        __asm volatile("mrs %0, msp" : "=r"(msp));
        __asm volatile("mrs %0, psp" : "=r"(psp));
        tprint("ASSERT msp=");
        tprint_int((int)msp);
        tprint(" psp=");
        tprint_int((int)psp);
        tprint(" local=");
        tprint_int((int)sp);
        tprint("\n");
        uintptr_t base = psp ? psp : msp;
        for (int i = 0; i < 48; ++i)
        {
            uintptr_t v = ((uintptr_t *)base)[i];
            tprint_int((int)v);
            tprint(" ");
        }
        tprint("\n");
    }
    for (;;)
        ;
}
