/*
 * soui_fr/selftest/main.cpp -- swinx 平台层在**真实 FreeRTOS** 上的自测入口。
 *
 * 这个 target（soui_fr_selftest.elf）不链 SOUI，只链 swinx 平台层 + cairo +
 * FreeRTOS 内核，验的是 demos/fun_test 覆盖不到的那一段：桌面测试跑在 Win32 /
 * posix 兼容层上，而这几条腿必须走 FreeRTOS 的队列、task 和软件帧缓冲。
 *
 * 五条腿（各自文件头写明断言的语义）：
 *   t_conn.cpp     SConnection：post/get、WM_PAINT / WM_MOUSEMOVE 合并、tick
 *                  驱动定时器、TranslateMessage 的 VK -> WM_CHAR
 *   t_sysapi.cpp   共用 sysapi.cpp：StrToIntEx / MulDiv / GetSystemMetrics /
 *                  GetDoubleClickTime（后两条要经 SConnection，故排在 conn 之后）
 *   t_render.cpp   核心建窗 -> cairo 绘制 -> commitCanvas -> 软件帧缓冲像素断言
 *   t_input.cpp    SwinxInjectMouse/Key -> 队列 -> 消息泵 -> 窗口过程
 *                  （含 capture 路由、跨任务唤醒 WaitMessage）
 *   t_dump.cpp     半主机把整个帧缓冲写成 fb.ppm，供人工目检
 *
 * 不在这里的：sync 对象（event / mutex / semaphore / thread / CRITICAL_SECTION /
 * SRWLOCK / INIT_ONCE）。那些契约在 demos/fun_test/test_sync.cpp、
 * test_thread.cpp、test_cs.cpp 里，桌面每次构建都跑；本工程原先那份是逐条抄过去
 * 的，保留只会让两处各漂各的。
 */
#include "FreeRTOS.h"
#include "task.h"

#include <thread>   // resolves to the swinx FreeRTOS STL shim

#include "test_common.h"

extern "C"
{
#include "uart.h"
}

static void app_task(void)
{
    tprint("swinx platform self-test on REAL FreeRTOS starting...\n");

    // 1) 消息泵 / 定时器 / TranslateMessage
    run_sconnection_tests();

    // 2) sysapi 语义（GetSystemMetrics / GetDoubleClickTime 依赖 conn 已建立）
    run_sysapi_tests();

    // 3) 建窗 -> 绘制 -> commitCanvas -> 帧缓冲
    run_render_tests();

    // 4) 输入注入 -> 队列 -> 泵 -> 窗口过程
    run_input_tests();

    // 5) 整帧半主机 dump（QEMU 需要 -semihosting）
    run_dump_tests();

    if (g_fail == 0)
        tprint("SOUI_FR SELFTEST PASS\n");
    else
    {
        tprint("SOUI_FR SELFTEST FAIL (");
        tprint_int(g_fail);
        tprint(" checks failed)\n");
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
