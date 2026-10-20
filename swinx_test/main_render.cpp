/*
 * swinx_test/main_render.cpp
 *
 * Framebuffer renderer test: drives the REAL core window path on FreeRTOS --
 * RegisterClassA -> CreateWindowExA -> InitWndDC (CreateWindowSurface, cairo
 * image surface) -> UpdateWindow (sync WM_PAINT) -> GDI FillRect into the
 * canvas -> EndPaint/ReleaseDC -> SConnection::commitCanvas -> software
 * framebuffer -> present callback.  Verifies blitted pixels and dirty rects
 * in the ARGB32 framebuffer.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "swinx_stl.h"
#include "SConnection.h"
#include "framebuffer.h"

extern "C"
{
#include "uart.h"
}

#include "test_common.h"

static const uint32_t CLR_BG = 0xFF202020; // fb clear colour (AARRGGBB)
static const uint32_t CLR_W1 = 0xFF3366AA; // window 1 fill
static const uint32_t CLR_W2 = 0xFFCC2211; // window 2 fill

static int s_presentCount = 0;
static RECT s_lastDirty;
static HWND s_hWnd2 = NULL; // kept across tests (no FindWindowA on this port)

static void fb_present_cb(const RECT *dirty)
{
    s_presentCount++;
    if (dirty)
    {
        s_lastDirty = *dirty;
        tprint("[fb] present #");
        tprint_int(s_presentCount);
        tprint(" dirty=");
        tprint_int(dirty->left);
        tprint(",");
        tprint_int(dirty->top);
        tprint(",");
        tprint_int(dirty->right);
        tprint(",");
        tprint_int(dirty->bottom);
        tprint("\n");
    }
}

static uint32_t fb_get(int x, int y)
{
    return SwinxFbBits()[y * SwinxFbStride() + x];
}

// shared paint body: flood the whole client with one colour (cairo clips the
// oversized rect to the window surface, so any window size works)
static void paint_client(HWND hWnd, COLORREF color, int cw, int ch)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    HBRUSH hbr = CreateSolidBrush(color);
    RECT rc = { 0, 0, cw, ch };
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);
    EndPaint(hWnd, &ps);
}

static LRESULT CALLBACK paint1_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        tprint("[fb] W1 paint: begin\n");
        paint_client(hWnd, RGB(0x33, 0x66, 0xAA), 320, 240);
        tprint("[fb] W1 paint: filled\n");
        return 0;
    }
    if (msg == WM_NCCREATE)
        return TRUE; // Win32: creation must continue
    return DefWindowProc(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK paint2_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        tprint("[fb] W2 paint: begin\n");
        paint_client(hWnd, RGB(0xCC, 0x22, 0x11), 320, 240);
        tprint("[fb] W2 paint: filled\n");
        return 0;
    }
    if (msg == WM_NCCREATE)
        return TRUE; // Win32: creation must continue
    return DefWindowProc(hWnd, msg, wp, lp);
}

static void register_class(const char *name, WNDPROC proc)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.lpszClassName = name;
    ATOM atom = RegisterClassA(&wc);
    CHECK(atom != 0);
}

// ---------------------------------------------------------------------------
// 1) framebuffer basics
// ---------------------------------------------------------------------------
static void render_fb_basic_test(void)
{
    CHECK(SwinxFbWidth() == 320);
    CHECK(SwinxFbHeight() == 240);
    CHECK(SwinxFbStride() == 320);
    CHECK(SwinxFbBits() != NULL);

    SwinxFbClear(CLR_BG);
    CHECK(fb_get(0, 0) == CLR_BG);
    CHECK(fb_get(319, 239) == CLR_BG);

    tprint("[fb] dimensions + clear OK\n");
}

// ---------------------------------------------------------------------------
// 2) core window create -> paint -> commitCanvas -> framebuffer
// ---------------------------------------------------------------------------
static void render_window_paint_test(void)
{
    SwinxFbClear(CLR_BG);
    int basePresent = 0; // delta base: creation itself paints once (WS_VISIBLE)
    s_presentCount = 0;
    register_class("fbw1", paint1_wndproc);
    tprint("[fb] class registered\n");
    HWND hWnd = CreateWindowExA(0, "fbw1", "fb1", WS_VISIBLE,
                                20, 30, 100, 80, NULL, NULL, NULL, NULL);
    tprint("[fb] window created\n");
    CHECK(hWnd != NULL);
    CHECK(UpdateWindow(hWnd)); // synchronous WM_PAINT through updateWindow()
    tprint("[fb] update done\n");

    CHECK(s_presentCount >= basePresent + 1); // one commit per paint (an
    // UpdateWindow internally commits up to 3x, idempotent same-rect blits)
    // dirty rect reported in screen coordinates == window rect
    CHECK(s_lastDirty.left == 20 && s_lastDirty.top == 30);
    CHECK(s_lastDirty.right == 120 && s_lastDirty.bottom == 110);

    // corners of the window rect are filled
    CHECK(fb_get(20, 30) == CLR_W1);
    CHECK(fb_get(25, 35) == CLR_W1);
    CHECK(fb_get(119, 109) == CLR_W1);
    // one pixel outside each edge stays background
    CHECK(fb_get(19, 35) == CLR_BG);
    CHECK(fb_get(121, 35) == CLR_BG);
    CHECK(fb_get(25, 111) == CLR_BG);

    tprint("[fb] window paint -> commitCanvas -> framebuffer OK\n");
}

// ---------------------------------------------------------------------------
// 3) second window: independent canvas, both windows visible in the fb
// ---------------------------------------------------------------------------
static void render_two_windows_test(void)
{
    int basePresent = s_presentCount;
    register_class("fbw2", paint2_wndproc);
    s_hWnd2 = CreateWindowExA(0, "fbw2", "fb2", WS_VISIBLE,
                              200, 50, 50, 50, NULL, NULL, NULL, NULL);
    CHECK(s_hWnd2 != NULL);
    CHECK(UpdateWindow(s_hWnd2));

    CHECK(s_presentCount >= basePresent + 1);
    CHECK(s_lastDirty.left == 200 && s_lastDirty.top == 50);
    CHECK(s_lastDirty.right == 250 && s_lastDirty.bottom == 100);

    CHECK(fb_get(210, 60) == CLR_W2);
    CHECK(fb_get(249, 99) == CLR_W2);
    CHECK(fb_get(251, 60) == CLR_BG); // just right of window 2
    CHECK(fb_get(25, 35) == CLR_W1);  // window 1 untouched

    tprint("[fb] two windows, independent blits OK\n");
}

// ---------------------------------------------------------------------------
// 4) resize: SetWindowPos -> ResizeSurface -> repaint at the new geometry
// ---------------------------------------------------------------------------
static void render_resize_test(void)
{
    CHECK(s_hWnd2 != NULL);
    int basePresent = s_presentCount;

    // move + grow to 80x60 at (100,150)
    CHECK(SetWindowPos(s_hWnd2, 0, 100, 150, 80, 60, SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(UpdateWindow(s_hWnd2));

    {
        RECT rc;
        GetWindowRect(s_hWnd2, &rc);
        tprint("[fb] resized rc=");
        tprint_int(rc.left);
        tprint(",");
        tprint_int(rc.top);
        tprint(",");
        tprint_int(rc.right);
        tprint(",");
        tprint_int(rc.bottom);
        tprint(" presentDelta=");
        tprint_int(s_presentCount - basePresent);
        tprint(" dirty=");
        tprint_int(s_lastDirty.left);
        tprint(",");
        tprint_int(s_lastDirty.top);
        tprint(",");
        tprint_int(s_lastDirty.right);
        tprint(",");
        tprint_int(s_lastDirty.bottom);
        tprint("\n");
    }

    CHECK(s_presentCount >= basePresent + 1);
    CHECK(s_lastDirty.left == 100 && s_lastDirty.top == 150);
    CHECK(s_lastDirty.right == 180 && s_lastDirty.bottom == 210);

    CHECK(fb_get(100, 150) == CLR_W2);
    CHECK(fb_get(179, 209) == CLR_W2);
    CHECK(fb_get(180, 155) == CLR_BG); // just right of the resized window
    CHECK(fb_get(120, 211) == CLR_BG); // just below

    tprint("[fb] resize -> ResizeSurface -> repaint OK\n");
}

// ---------------------------------------------------------------------------
// entry point called from app_task (main.cpp)
// ---------------------------------------------------------------------------
void run_render_tests(void)
{
    tprint("== framebuffer renderer ==\n");
    SwinxSetFbPresentCb(fb_present_cb);
    render_fb_basic_test();
    render_window_paint_test();
    render_two_windows_test();
    render_resize_test();
}
