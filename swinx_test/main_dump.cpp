/*
 * swinx_test/main_dump.cpp
 *
 * Semihosting framebuffer dump: runs the real core window path (create ->
 * cairo paint -> commitCanvas -> software framebuffer) and then dumps the
 * whole ARGB32 framebuffer to the HOST filesystem as a P6 PPM via the ARM
 * semihosting file calls (SYS_OPEN / SYS_WRITE / SYS_CLOSE).  Run QEMU with
 * -semihosting; the resulting fb.ppm can be converted to PNG and inspected
 * visually.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>
#include <string.h>

#include "swinx_stl.h"
#include "SConnection.h"
#include "framebuffer.h"

extern "C"
{
#include "uart.h"
}

#include "test_common.h"

// ---------------------------------------------------------------------------
// ARM semihosting (ARM32: bkpt 0xAB, r0 = op, r1 = arg block)
// ---------------------------------------------------------------------------
#define SH_SYS_OPEN 0x01
#define SH_SYS_CLOSE 0x02
#define SH_SYS_WRITE 0x05

static int sh_call(int op, long arg)
{
    register int r0 __asm("r0") = op;
    register long r1 __asm("r1") = arg;
    __asm __volatile("bkpt 0xAB"
                     : "+r"(r0)
                     : "r"(r1)
                     : "memory");
    return r0;
}

// mode 4 == "wb" in the semihosting fopen mode table
static int sh_open(const char *name)
{
    long args[3] = { (long)name, 4, (long)strlen(name) };
    return sh_call(SH_SYS_OPEN, (long)args);
}

static int sh_write(int handle, const void *buf, int len)
{
    long args[3] = { handle, (long)buf, len };
    return sh_call(SH_SYS_WRITE, (long)args); // returns bytes NOT written
}

static int sh_close(int handle)
{
    long args[1] = { handle };
    return sh_call(SH_SYS_CLOSE, (long)args);
}

// ---------------------------------------------------------------------------
// dump the whole framebuffer as P6 PPM
// ---------------------------------------------------------------------------
static int dump_fb_ppm(const char *path)
{
    int fh = sh_open(path);
    if (fh < 0)
        return -1;

    // header
    char hdr[32];
    int n = 0;
    const char *magic = "P6\n";
    memcpy(hdr + n, magic, 3);
    n += 3;
    // dimensions (fb is fixed 320x240, print via tiny formatter)
    char tmp[8];
    int v = SwinxFbWidth();
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0)
        hdr[n++] = tmp[--i];
    hdr[n++] = ' ';
    v = SwinxFbHeight();
    i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0)
        hdr[n++] = tmp[--i];
    hdr[n++] = '\n';
    hdr[n++] = '2';
    hdr[n++] = '5';
    hdr[n++] = '5';
    hdr[n++] = '\n';
    if (sh_write(fh, hdr, n) != 0)
    {
        sh_close(fh);
        return -2;
    }

    // pixel rows: ARGB32 (0xAARRGGBB in a little-endian uint32) -> RGB888
    int w = SwinxFbWidth();
    int h = SwinxFbHeight();
    static uint8_t row[SWINX_FB_WIDTH * 3]; // 960 bytes, static: no stack risk
    const uint32_t *fb = SwinxFbBits();
    int stride = SwinxFbStride();
    for (int y = 0; y < h; ++y)
    {
        const uint32_t *src = fb + y * stride;
        for (int x = 0; x < w; ++x)
        {
            uint32_t px = src[x];
            row[x * 3 + 0] = (uint8_t)(px >> 16); // R
            row[x * 3 + 1] = (uint8_t)(px >> 8);  // G
            row[x * 3 + 2] = (uint8_t)(px);       // B
        }
        if (sh_write(fh, row, w * 3) != 0)
        {
            sh_close(fh);
            return -3;
        }
    }

    sh_close(fh);
    return 0;
}

// ---------------------------------------------------------------------------
// dump-scene window: base fill + inner rect + stripes (easy to verify by eye)
// ---------------------------------------------------------------------------
static const uint32_t CLR_DUMP_BG = 0xFF103018; // fb clear (dark green)
static const uint32_t CLR_BASE = 0xFF2070C0;    // window base fill (blue)
static const uint32_t CLR_INNER = 0xFFE0B020;   // inner rect (amber)
static const uint32_t CLR_STRIPE = 0xFFD0D0D0;  // stripes (light gray)

static LRESULT CALLBACK dump_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        HBRUSH hbr;
        RECT rc;

        // base fill (oversized rect, cairo clips to the window surface)
        hbr = CreateSolidBrush(RGB(0x20, 0x70, 0xC0));
        rc.left = 0; rc.top = 0; rc.right = 1000; rc.bottom = 1000;
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        // inner rect at client (10,10)-(70,50)
        hbr = CreateSolidBrush(RGB(0xE0, 0xB0, 0x20));
        rc.left = 10; rc.top = 10; rc.right = 70; rc.bottom = 50;
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        // three horizontal stripes at client y=60,64,68, x 0..90
        hbr = CreateSolidBrush(RGB(0xD0, 0xD0, 0xD0));
        for (int y = 60; y <= 68; y += 4)
        {
            rc.left = 0; rc.top = y; rc.right = 90; rc.bottom = y + 2;
            FillRect(hdc, &rc, hbr);
        }
        DeleteObject(hbr);

        EndPaint(hWnd, &ps);
        return 0;
    }
    if (msg == WM_NCCREATE)
        return TRUE; // Win32: creation must continue
    return DefWindowProc(hWnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// entry point called from app_task (main.cpp)
// ---------------------------------------------------------------------------
void run_dump_tests(void)
{
    tprint("== fb semihosting dump ==\n");

    // build the scene: dark-green background, one window at (40,50) 120x90
    SwinxFbClear(CLR_DUMP_BG);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = dump_wndproc;
    wc.lpszClassName = "dumpw";
    ATOM atom = RegisterClassA(&wc);
    CHECK(atom != 0);

    HWND hWnd = CreateWindowExA(0, "dumpw", "dump", WS_VISIBLE,
                                40, 50, 120, 90, NULL, NULL, NULL, NULL);
    CHECK(hWnd != NULL);
    CHECK(UpdateWindow(hWnd)); // synchronous paint -> commitCanvas -> fb

    // quick pixel sanity before the dump
    uint32_t *fb = SwinxFbBits();
    int stride = SwinxFbStride();
    CHECK(fb[50 * stride + 40] == CLR_BASE);    // window top-left
    CHECK(fb[60 * stride + 50] == CLR_INNER);   // inside inner rect
    CHECK(fb[110 * stride + 85] == CLR_STRIPE); // on a stripe
    CHECK(fb[45 * stride + 35] == CLR_DUMP_BG); // background, outside window
    CHECK(fb[100 * stride + 200] == CLR_DUMP_BG);

    int rc = dump_fb_ppm("fb.ppm");
    tprint("[dump] semihosting write rc=");
    tprint_int(rc);
    tprint("\n");
    CHECK(rc == 0);

    tprint("[dump] fb.ppm written OK\n");
}
