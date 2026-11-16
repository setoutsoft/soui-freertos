/*
 * test_common.cpp -- shared CHECK / fail counter / UART printing.
 *
 * FreeRTOS 的故障钩子（vAssertCalled / 栈溢出 / malloc 失败）不在这里：
 * ../hooks.c 是 app 与 selftest 两个 target 共用的那一份，同一符号不能定义两遍。
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
