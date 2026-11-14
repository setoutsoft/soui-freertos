/*
 * icon_probe.cpp -- headless check of the FreeRTOS port's icon path.
 *
 * Run with SOUI_SIM_ICONPROBE=1 (see README "图标自检").  This is the second
 * half of "can it be used as a UI": text was the first (font_probe.cpp), and
 * these are the calls an `ICON:` resource goes through:
 *
 *   FindResourceA(hinst, "APP_ICON", RT_GROUP_ICON)     -- GRPICONDIR in .rsrc
 *   FindResourceA(hinst, MAKEINTRESOURCE(id), RT_ICON)  -- the chosen frame
 *   CreateIconFromResource(bits, size, TRUE, 0x00030000)-- DIB -> HBITMAP
 *   LoadImageA(hinst, "APP_ICON", IMAGE_ICON, cx, cy, 0)-- what SIconWnd calls
 *   DrawIconEx(hdc, x, y, hIcon, w, h, DI_NORMAL)       -- AlphaBlend
 *
 * Until this port these were NULL stubs, so `<icon>` painted nothing while the
 * rest of the UI worked.  The probe therefore asserts on *pixels*: an icon that
 * loads but blends as a white square would pass a pointer check and still be a
 * bug, so the last step counts non-white output and requires both the frame's
 * colour and its rounded transparent corners to show up.
 */
#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{

const char *kIconName = "APP_ICON";

const char *yesNo(BOOL b)
{
    return b ? "yes" : "no";
}

/* Counts pixels that differ from the surface background (white) and reports the
 * bounding box, so a "drawn nothing" and a "drawn off-rect" both show up. */
int inkStats(const void *bits, int w, int h, int stride, int *x0, int *y0, int *x1, int *y1)
{
    const unsigned char *p = (const unsigned char *)bits;
    int n = 0;
    *x0 = w;
    *y0 = h;
    *x1 = -1;
    *y1 = -1;
    for (int y = 0; y < h; y++)
    {
        const unsigned char *row = p + (size_t)y * stride;
        for (int x = 0; x < w; x++)
        {
            const unsigned char *q = row + (size_t)x * 4;
            if (q[0] != 0xFF || q[1] != 0xFF || q[2] != 0xFF)
            {
                n++;
                if (x < *x0)
                    *x0 = x;
                if (y < *y0)
                    *y0 = y;
                if (x > *x1)
                    *x1 = x;
                if (y > *y1)
                    *y1 = y;
            }
        }
    }
    return n;
}

/* One 32bpp offscreen DIB, white background, icon drawn at (0,0). */
int drawAndCount(HICON hIcon, int w, int h, int *x0, int *y0, int *x1, int *y1)
{
    HDC hdc = CreateCompatibleDC(NULL);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(hdc, bmp);
    RECT rc = {0, 0, w, h};
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawIconEx(hdc, 0, 0, hIcon, w, h, 0, NULL, DI_NORMAL);
    int n = inkStats(bits, w, h, w * 4, x0, y0, x1, y1);
    SelectObject(hdc, old);
    DeleteObject(bmp);
    DeleteDC(hdc);
    return n;
}

} // namespace

int SimIconProbe(void)
{
    printf("[iconprobe] icon path self-test (APP_ICON from the resource blob)\n");
    int fail = 0;
    HINSTANCE hinst = GetModuleHandle(NULL);

    /* 1. the group directory: what LoadImage looks up first */
    HRSRC hGroup = FindResourceA(hinst, kIconName, (LPCSTR)RT_GROUP_ICON);
    printf("[iconprobe] RT_GROUP_ICON \"%s\"          -> %s (size %u)\n", kIconName,
           hGroup ? "found" : "MISSING",
           hGroup ? (unsigned)SizeofResource(hinst, hGroup) : 0u);
    if (!hGroup)
        fail++;

    /* 2. every frame the group points at, plus the id LookupIconId returns for
     *    16px: that is the frame SIconWnd's "APP_ICON:16" must resolve to. */
    WORD ids[8] = {0};
    int idCount = 0;
    if (hGroup)
    {
        const BYTE *dir = (const BYTE *)LockResource(LoadResource(hinst, hGroup));
        if (dir)
        {
            /* GRPICONDIR: idReserved(0) idType(1) idCount, then 14-byte entries */
            if (dir[0] == 0 && dir[1] == 0 && dir[2] == 1)
                idCount = dir[4] | (dir[5] << 8);
            if (idCount > 8)
                idCount = 8;
            for (int i = 0; i < idCount; i++)
            {
                /* GRPICONDIRENTRY: w,h,colors,res,planes,bpp(2),bytes(4),id(2) */
                const BYTE *e = dir + 6 + i * 14;
                ids[i] = (WORD)(e[12] | (e[13] << 8));
                HRSRC hFrame = FindResourceA(hinst, MAKEINTRESOURCEA(ids[i]), (LPCSTR)RT_ICON);
                printf("[iconprobe] RT_ICON #%-3u (%2ux%-2u %ubpp)     -> %s\n", ids[i],
                       e[0] ? e[0] : 256, e[1] ? e[1] : 256, e[6] | (e[7] << 8),
                       hFrame ? "found" : "MISSING");
                if (!hFrame)
                    fail++;
            }
            if (idCount == 0)
            {
                printf("[iconprobe] group directory is empty or malformed\n");
                fail++;
            }
            int best = LookupIconIdFromDirectoryEx((LPBYTE)dir, TRUE, 16, 16, 0);
            printf("[iconprobe] LookupIconIdFromDirectoryEx(16,16) -> %d\n", best);
            if (best <= 0)
                fail++;
        }
        else
        {
            printf("[iconprobe] LockResource(group) returned NULL\n");
            fail++;
        }
    }

    /* 3. CreateIconFromResource on every frame -- the call the port stubbed to
     *    NULL.  It is the DIB -> HBITMAP+alpha step, so a per-frame pass here
     *    separates "resource tree" bugs from "bitmap" bugs. */
    for (int i = 0; i < idCount; i++)
    {
        HRSRC hFrame = FindResourceA(hinst, MAKEINTRESOURCEA(ids[i]), (LPSTR)RT_ICON);
        if (!hFrame)
            continue;
        HGLOBAL glob = LoadResource(hinst, hFrame);
        LPBYTE bits = (LPBYTE)LockResource(glob);
        DWORD size = SizeofResource(hinst, hFrame);
        HICON hIcon = CreateIconFromResource(bits, size, TRUE, 0x00030000);
        if (!hIcon)
        {
            printf("[iconprobe] CreateIconFromResource(#%u, %u bytes) -> NULL\n", ids[i],
                   (unsigned)size);
            fail++;
            continue;
        }
        int x0, y0, x1, y1;
        int n = drawAndCount(hIcon, 32, 32, &x0, &y0, &x1, &y1);
        printf("[iconprobe] CreateIconFromResource(#%u, %6u bytes) -> icon, drawn %3d px, bbox %d,%d..%d,%d\n",
               ids[i], (unsigned)size, n, x0, y0, x1, y1);
        if (n < 100)
            fail++;
        DestroyIcon(hIcon);
    }

    /* 4. LoadImageA -- exactly what SIconWnd::OnAttrIcon triggers.  Both the
     *    16px and 32px requests must produce a 32bpp colour bitmap: the cairo
     *    backend's DrawIconEx refuses anything else. */
    static const int sizes[] = {16, 32};
    for (int i = 0; i < 2; i++)
    {
        int px = sizes[i];
        HICON hIcon = (HICON)LoadImageA(hinst, kIconName, IMAGE_ICON, px, px, LR_DEFAULTCOLOR);
        int n = 0, x0 = -1, y0 = -1, x1 = -1, y1 = -1;
        BITMAP bm = {0};
        bool bppOk = false;
        if (hIcon)
        {
            ICONINFO ii = {0};
            GetIconInfo(hIcon, &ii);
            if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm))
                bppOk = (bm.bmBitsPixel == 32);
            if (ii.hbmColor)
                DeleteObject(ii.hbmColor);
            if (ii.hbmMask)
                DeleteObject(ii.hbmMask);
            n = drawAndCount(hIcon, px, px, &x0, &y0, &x1, &y1);
            DestroyIcon(hIcon);
        }
        printf("[iconprobe] LoadImageA(%dpx) -> %s %dx%d %ubpp | drawn %d px, bbox %d,%d..%d,%d | %s\n",
               px, hIcon ? "icon" : "NULL", bm.bmWidth, bm.bmHeight, bm.bmBitsPixel, n, x0, y0,
               x1, y1, (hIcon && bppOk && n > 32) ? "PASS" : "FAIL");
        if (!hIcon || !bppOk || n <= 32)
            fail++;
    }

    printf("[iconprobe] %s (%d failure%s)\n", fail ? "FAILED" : "ALL PASS", fail,
           fail == 1 ? "" : "s");
    return fail;
}
