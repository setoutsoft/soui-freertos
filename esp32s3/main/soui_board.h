/*
 * soui_board.h -- board glue between the swinx software framebuffer and the
 * 2.8" ESP32-S3 board (ILI9341V SPI LCD + FT6X36 I2C touch).
 *
 * The three soui_board_lcd_* / soui_board_touch_* hooks are implemented by
 * soui_board_esp32.cpp (ILI9341V over SPI2 + FT6X36 over I2C0, pin map taken
 * from the vendor LVGL demo).  There is exactly ONE definition of each: an
 * earlier revision also had weak stubs in soui_board.cpp, but a weak
 * definition inside the same static archive satisfies the reference before
 * the strong one is ever extracted (see the comment at the top of
 * soui_board.cpp).
 */
#ifndef SOUI_BOARD_H_
#define SOUI_BOARD_H_

#include <stdint.h>
#include <windows.h>   /* HWND, RECT, WM_*, MK_* from the swinx Win32 shim */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once, from the UI task, before the first UpdateWindow(). */
void soui_board_init(void);

/* Call from the UI task's message pump.  Touch *sampling* runs in its own
 * task (see soui_board.cpp); this call only replays the captured samples into
 * SwinxInjectMouse(), which must happen on the UI thread so the injected
 * messages are ordered with that thread's paint/timer messages. */
void soui_board_poll_touch(HWND hWnd);

/* Number of presents (dirty-rect blits) handed to the LCD since boot.
 * Diagnostic only: the pump logs it periodically to show that frames are
 * still reaching the panel.  Note one logical frame can produce several
 * presents (a window commit plus a composeAbove pass), so this is NOT a frame
 * counter -- use soui_board_frame_count() for that. */
uint32_t soui_board_present_count(void);
/* Total pixels converted+flushed since boot (same diagnostic purpose). */
uint32_t soui_board_present_pixels(void);

/* Completed frames since boot: bumped by app_main's message pump once per
 * finished WM_PAINT (render + present).  This is the frame rate the demo's
 * fps label shows, and it is the same quantity the vendor LVGL firmware
 * reports as "fps" (one refresh pass), so the two are directly comparable.
 * Unlike present_count it does not double count a frame that had to
 * recompose an overlapping window. */
void soui_board_frame_tick(void);
uint32_t soui_board_frame_count(void);

/* Time spent inside the SPI push and the number of pushed rects since boot.
 * present_count / flush_us splits "per-frame cost" into rendering (present -
 * flush) and bus transfer, which is what decides whether a tap can be painted
 * quickly. */
uint64_t soui_board_flush_us(void);
uint32_t soui_board_flush_rects(void);

/* 0 restores InvalidateRect()'s pre-11.8 sequence (temp HRGN + CombineRgn);
 * 1 (default) uses the exact fast path.  Only for A/B measurement. */
void SwinxSetRegionFastPath(int bEnable);

/* Debug: dump the software framebuffer (== what the panel received) as raw
 * RGB565 hex, one hex line per sampled row.  step=4 gives an 80x60 image;
 * step=2 gives 160x120.  Printing takes seconds, so the pixels are snapshotted
 * into PSRAM first and IDLE1 is fed every few lines. */
void soui_board_dump_fb(int step);
/* Same, but only for a screen rectangle, at 1:1 if step is 1 -- used to inspect
 * a few hundred pixels of text at full resolution. */
void soui_board_dump_rect(int x0, int y0, int x1, int y1, int step);

/* Debug: fill the framebuffer with a 4-quadrant colour map (TL red, TR green,
 * BL blue, BR yellow) + a 1px white frame, present it, then push a single
 * 10x10 white square at (100,100) as its own partial present.  Used to verify
 * the FB->panel mapping and the sub-rect present path. */
void soui_board_test_pattern(void);

/* ---- weak board hooks ---------------------------------------------------- */

/* Power on / configure the panel. Default: a single warning log. */
void soui_board_lcd_init(void);

/* Push one tight RGB565 rectangle. (x1,y1)-(x2,y2) are screen coordinates
 * with EXCLUSIVE right/bottom -- the same convention as swinx's RECT and
 * SConnection::commitCanvas() (SConnection.cpp:1403-1430). rgb565 holds
 * (x2-x1)*(y2-y1) uint16_t values in row-major order; byte order for the
 * SPI bus is the driver's concern. */
void soui_board_lcd_flush(int x1, int y1, int x2, int y2, const uint16_t *rgb565);

/* Route (1): same rect, but rgb565 points into a full-width RGB565
 * framebuffer (stridePx >= x2-x1) instead of a tight rectangle.  The driver
 * byte-swaps into its DMA staging chunks while walking the rows. */
void soui_board_lcd_flush_strided(int x1, int y1, int x2, int y2,
                                  const uint16_t *rgb565, int stridePx);

/* Non-blocking touch poll. Return 1 when a sample is available and fill
 * x/y/pressed (pressed: 1 = down, 0 = up). Default: always returns 0. */
int soui_board_touch_read(int *x, int *y, int *pressed);

#ifdef __cplusplus
}
#endif

#endif // SOUI_BOARD_H_
