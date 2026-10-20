/*
 * test_common.h -- shared CHECK / fail counter / UART printing for the bare
 * metal swinx FreeRTOS tests. Both main.cpp (sync layer) and main_syncobj.cpp
 * (sync objects + threads) include this so a single g_fail counter drives the
 * final PASS/FAIL verdict printed over the lm3s6965 UART.
 */
#ifndef SWINX_TEST_COMMON_H_
#define SWINX_TEST_COMMON_H_

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

// defined in main_syncobj.cpp: the win32 sync-object / thread test battery
void run_winobj_tests(void);

// defined in main_conn.cpp: the SConnection (FreeRTOS platform) smoke tests
void run_sconnection_tests(void);

// defined in main_render.cpp: framebuffer renderer tests (core window path)
void run_render_tests(void);

// defined in main_input.cpp: input injection tests (mouse/key -> queue -> pump)
void run_input_tests(void);

// defined in main_dump.cpp: semihosting framebuffer dump to host fb.ppm
void run_dump_tests(void);

#endif // SWINX_TEST_COMMON_H_
