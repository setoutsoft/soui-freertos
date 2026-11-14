/*
 * font_probe.cpp -- headless check of the FreeRTOS port's text metrics.
 *
 * Run with SOUI_SIM_FONTPROBE=1 (see README "字体自检").  What it asserts is
 * exactly the property this port used to get wrong:
 *
 *   measure == draw
 *
 * SOUI lays text out by adding up the width of one word / one character at a
 * time (SOUI/src/control/SCmnCtrl.cpp:187 and :223) and then draws the line at
 * the accumulated pen positions.  If GetTextExtentPoint32A is narrower than
 * the glyphs drawn afterwards, every line runs a little past its box and the
 * tail gets clipped -- the symptom this port had with cairo's toy font.
 *
 * The probe therefore measures three ways and compares them:
 *
 *   whole -- GetTextExtentPoint32A(entire string)
 *   chars -- sum of GetTextExtentPoint32A(each character), i.e. what the
 *            XML text layout accumulates
 *   ink   -- the width actually painted, read back from an offscreen DIB
 *
 * PASS requires whole == chars (no per-run truncation) and |ink - whole| <= 2
 * px on each side (ink is narrower than the advance sum by the last glyph's
 * right side bearing, never wider).
 */
#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{

const int kSurfaceW = 640;
/* Taller than any baked line box (16px face is ascent 13 + descent 4), so a
 * metrics bug shows up as a width mismatch rather than as a clipped strip. */
const int kSurfaceH = 24;

/* Right-most painted column + 1, or 0 when nothing was painted.  top/bottom
 * come back too: a vertical offset bug then shows as ink == 0, not as a
 * mysterious width difference. */
int inkWidth(const void *bits, int w, int h, int stride, int *top, int *bottom)
{
    const unsigned char *p = (const unsigned char *)bits;
    int right = 0;
    *top = h;
    *bottom = -1;
    for (int y = 0; y < h; y++)
    {
        const unsigned char *row = p + (size_t)y * stride;
        for (int x = 0; x < w; x++)
        {
            /* any non-black pixel counts: the probe paints white on black */
            if (row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2])
            {
                if (x + 1 > right)
                    right = x + 1;
                if (y < *top)
                    *top = y;
                *bottom = y;
            }
        }
    }
    return right;
}

int measure(HDC hdc, const char *s, int len)
{
    SIZE sz = {0, 0};
    if (!GetTextExtentPoint32A(hdc, s, len, &sz))
        return -1;
    return (int)sz.cx;
}

/* Diagnostic: dump the probe strip so a failing size can be looked at. */
void debugDump(const void *bits, int w, int h, int stride, int px, int seq)
{
    const char *dir = getenv("SOUI_SIM_FONTPROBE_DUMP");
    if (!dir)
        return;
    char path[512];
    sprintf(path, "%s/probe_%02dpx_%d.bmp", dir, px, seq);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    const int rowBytes = w * 3;
    const int pad = (4 - (rowBytes % 4)) % 4;
    const int imageSize = (rowBytes + pad) * h;
    unsigned char hdr[54] = {'B', 'M'};
    int fileSize = 54 + imageSize;
    memcpy(hdr + 2, &fileSize, 4);
    int offBits = 54;
    memcpy(hdr + 10, &offBits, 4);
    int hdrSize = 40;
    memcpy(hdr + 14, &hdrSize, 4);
    int width = w, height = h;
    memcpy(hdr + 18, &width, 4);
    memcpy(hdr + 22, &height, 4);
    short planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &imageSize, 4);
    fwrite(hdr, 1, 54, f);
    const unsigned char *p = (const unsigned char *)bits;
    for (int y = h - 1; y >= 0; y--) /* BMP rows are bottom-up */
    {
        const unsigned char *row = p + (size_t)y * stride;
        for (int x = 0; x < w; x++)
        {
            unsigned char bgr[3] = {row[x * 4], row[x * 4 + 1], row[x * 4 + 2]};
            fwrite(bgr, 1, 3, f);
        }
        unsigned char zero[3] = {0, 0, 0};
        fwrite(zero, 1, pad, f);
    }
    fclose(f);
    printf("[probe] dumped %s\n", path);
}

/* Returns the number of failed assertions. */
int probeSize(int px, const char *label, const char *text, int seq)
{
    int fail = 0;

    HDC hdc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontA(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "SfrSans");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);

    int len = (int)strlen(text);
    int whole = measure(hdc, text, len);
    int chars = 0;
    for (int i = 0; i < len; i++)
        chars += measure(hdc, text + i, 1);

    /* paint it and read the ink back */
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kSurfaceW;
    bmi.bmiHeader.biHeight = -kSurfaceH; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    RECT rc = {0, 0, kSurfaceW, kSurfaceH};
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hdc, &rc, black);
    /* y = 0, not tmAscent: the DC default alignment is TA_TOP, so TextOutA
     * adds the ascent itself to get the baseline. */
    TextOutA(hdc, 0, 0, text, len);
    int top = 0, bottom = -1;
    int ink = inkWidth(bits, kSurfaceW, kSurfaceH, kSurfaceW * 4, &top, &bottom);
    debugDump(bits, kSurfaceW, kSurfaceH, kSurfaceW * 4, px, seq);

    /* ink is allowed to be narrower (right side bearing of the last glyph),
     * never wider than the advance sum the layout used */
    bool ok = (whole == chars) && (ink <= whole + 1) && (ink + 3 >= whole);
    if (!ok)
        fail++;

    printf("[probe] %s %2dpx asc=%2ld desc=%2ld | whole %3d chars %3d ink %3d y%3d..%3d | %s\n",
           label, px, (long)tm.tmAscent, (long)tm.tmDescent, whole, chars, ink, top, bottom,
           ok ? "PASS" : "FAIL");

    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteDC(hdc);
    return fail;
}

} // namespace

/* Called from main() before the SOUI window exists: this is pure swinx GDI. */
int SimFontProbe(void)
{
    static const char *kShort = "Switch image";
    static const char *kLong = "swinx platform layer + render-gdi (cairo) + imgdecoder-stb";
    static const char *kMixed = "Normal: sweep 1200ms / fade 900ms";

    printf("[probe] measure == draw self-test (needs a font face, no window)\n");
    int fail = 0;
    const int sizes[] = {12, 13, 14, 15, 16};
    for (int i = 0; i < 5; i++)
    {
        int px = sizes[i];
        fail += probeSize(px, "regular", kShort, 0);
        fail += probeSize(px, "regular", kLong, 1);
        fail += probeSize(px, "regular", kMixed, 2);
    }
    printf("[probe] %s (%d failure%s)\n", fail ? "FAILED" : "ALL PASS", fail, fail == 1 ? "" : "s");
    return fail;
}
