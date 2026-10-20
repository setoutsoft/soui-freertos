/*
 * soui_fr/entry.cpp -- minimal FreeRTOS entry for the SOUI link attempt.
 * Does not use SOUI yet: the purpose of this target is to pull the
 * whole-archive'd soui4_fr / utilities4_fr into the link so the linker
 * enumerates every unresolved symbol (the gap report).
 */
#include "FreeRTOS.h"
#include "task.h"

#include <thread>   // resolves to the swinx FreeRTOS STL shim

#include <stdint.h>
#include <string.h>

// real SOUI symbol reference: with --gc-sections this keeps the SStringA
// chain (utilities string + swinx heap) alive in the runnable variant
#include <string/sstringa.h>

extern "C"
{
#include "uart.h"
}

extern "C" void entry(void)
{
    uart_init();
    uart_puts("soui4 freertos link attempt: linked OK\n");

    // SOUI smoke: construct an SStringA on the swinx FreeRTOS heap
    {
        SNS::SStringA s("hello from SOUI string on FreeRTOS");
        uart_puts("[smoke] SStringA len=");
        char b[12];
        int v = s.GetLength();
        int i = 0;
        if (v == 0)
            b[i++] = '0';
        while (v > 0)
        {
            b[i++] = (char)('0' + v % 10);
            v /= 10;
        }
        b[i] = 0;
        uart_puts(b);
        uart_puts("\n");
    }

    std::thread t([] {
        uart_puts("soui_fr app task running\n");
        for (;;)
            taskYIELD();
    });
    t.detach();

    vTaskStartScheduler();
    for (;;)
        ;
}
