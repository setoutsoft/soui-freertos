/*
 * test_common.h -- shared CHECK / fail counter / UART printing for the bare
 * metal swinx self-test (soui_fr_selftest.elf).  Every leg includes this so a
 * single g_fail counter drives the final PASS/FAIL verdict printed over the
 * mps2 UART.
 */
#ifndef SOUI_FR_SELFTEST_COMMON_H_
#define SOUI_FR_SELFTEST_COMMON_H_

extern int g_fail;

void tprint(const char *s);
void tprint_int(int v);

void check_fail(const char *expr, int line);

#define CHECK(cond)                                        \
    do                                                     \
    {                                                      \
        if (!(cond))                                       \
            check_fail(#cond, __LINE__);                   \
    } while (0)

// defined in t_conn.cpp: SConnection message pump / timers / TranslateMessage
void run_sconnection_tests(void);

// defined in t_render.cpp: core window create -> paint -> commitCanvas -> fb
void run_render_tests(void);

// defined in t_input.cpp: input injection (mouse/key -> queue -> pump -> wndproc)
void run_input_tests(void);

// defined in t_dump.cpp: semihosting framebuffer dump to host fb.ppm
void run_dump_tests(void);

// defined in t_sysapi.cpp: src/platform/common/sysapi.cpp 的语义
// （StrToIntEx / MulDiv / GetSystemMetrics / GetDoubleClickTime）。
// 需要 SConnection 已初始化，调用点放在 run_sconnection_tests() 之后。
void run_sysapi_tests(void);

#endif // SOUI_FR_SELFTEST_COMMON_H_
