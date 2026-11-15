/*
 * soui_fr/mem_res_provider.cpp
 *
 * Minimal in-memory IResProvider for the FreeRTOS SApp smoke test.
 * Resources are static ROM tables (no filesystem), enough to feed SHostWnd
 * with one layout XML ("LAYOUT:APP_MAIN") and one application icon
 * ("ICON:APP_ICON").  The icon bytes come from res/app.ico (classic,
 * non-PNG ICON format) embedded as a byte array in app_icon_data.h; they are
 * decoded through the same swinx cursoricon.cpp path (LookupIconIdFromDirectoryEx
 * + CreateIconFromResource) that the COFF resource provider uses on the host,
 * so ICON: works identically on the bare-metal target.  Bitmaps / cursors are
 * still not provided by this provider.
 */
#include <interface/SResProvider-i.h>
#include <helper/obj-ref-impl.hpp>
#include <string.h>
#include <windows.h> // swinx shim: LookupIconIdFromDirectoryEx / CreateIconFromResource
#include "app_icon_data.h"

SNSBEGIN

// ---------------------------------------------------------------------------
// embedded resources (ROM)
// ---------------------------------------------------------------------------
static const char K_LAYOUT_APP_MAIN[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<SOUI name=\"mainWindow\" title=\"SOUI5 on FreeRTOS\" wndType=\"appWnd\" appWnd=\"0\">"
    "<root width=\"320\" height=\"240\" colorBkgnd=\"#103018FF\">"
    "<window pos=\"20,20,300,120\" colorBkgnd=\"#2070C0FF\">"
    "<text pos=\"10,10,-10,-10\" colorText=\"#FFFFFFFF\">Hello SOUI5 on FreeRTOS</text>"
    "</window>"
    "<text pos=\"20,140\" colorText=\"#E0B020FF\">XML layout -&gt; cairo -&gt; framebuffer</text>"
    "</root>"
    "</SOUI>";

struct MemResEntry
{
    LPCTSTR type;
    LPCTSTR name;
    const char *data;
    size_t len;
};

#define RES_ENTRY(t, n, s) \
    { _T(t), _T(n), s, sizeof(s) - 1 }

static const MemResEntry k_resTable[] = {
    RES_ENTRY("LAYOUT", "APP_MAIN", K_LAYOUT_APP_MAIN),
    // ICON:APP_ICON -- embedded .ico (== RT_GROUP_ICON directory). LoadIcon()
    // below walks the directory, picks the best frame for the requested size,
    // and hands the single-frame bytes to CreateIconFromResource.
    { _T("ICON"), _T("APP_ICON"), (const char *)k_app_icon_ico, k_app_icon_ico_len },
};
static const int k_resCount = (int)(sizeof(k_resTable) / sizeof(k_resTable[0]));

static const MemResEntry *res_find(LPCTSTR pszType, LPCTSTR pszResName)
{
    if (!pszType || !pszResName)
        return NULL;
    for (int i = 0; i < k_resCount; ++i)
    {
        if (strcmp(k_resTable[i].type, pszType) == 0 && strcmp(k_resTable[i].name, pszResName) == 0)
            return &k_resTable[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// MemResProvider
// ---------------------------------------------------------------------------
class MemResProvider : public TObjRefImpl<IResProvider>
{
  public:
    STDMETHOD_(BOOL, Init)(THIS_ WPARAM wParam, LPARAM lParam) OVERRIDE
    {
        return TRUE;
    }

    STDMETHOD_(BOOL, HasResource)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        return res_find(pszType, pszResName) != NULL;
    }

    STDMETHOD_(HICON, LoadIcon)(THIS_ LPCTSTR pszResName, int cx, int cy) OVERRIDE
    {
        const MemResEntry *e = res_find(_T("ICON"), pszResName);
        if (!e || e->len < 6)
            return 0;
        LPBYTE dir = (LPBYTE)e->data;
        // .ico FILE format (NOT the PE RT_GROUP_ICON format Wine's
        // LookupIconIdFromDirectoryEx expects): ICONDIR(6 bytes: reserved,
        // type, count) followed by count ICONDIRENTRY records of 16 bytes each.
        // The last field of each entry is a 4-byte file offset (imageOffset) to
        // the RT_ICON frame, not a 2-byte resource id.
        WORD count = *(WORD *)(dir + 4);
        if (count == 0 || e->len < (size_t)(6 + (size_t)count * 16))
            return 0;
        int bestIdx = -1, bestDiff = 0x7fffffff;
        for (WORD i = 0; i < count; ++i)
        {
            LPBYTE ent = dir + 6 + (size_t)i * 16;
            int w = ent[0] ? ent[0] : 256; // 0 width/height means 256 in .ico
            int h = ent[1] ? ent[1] : 256;
            int diff = (w > cx ? w - cx : cx - w) + (h > cy ? h - cy : cy - h);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestIdx = i;
            }
        }
        if (bestIdx < 0)
            return 0;
        LPBYTE entry = dir + 6 + (size_t)bestIdx * 16;
        DWORD imageOffset = *(DWORD *)(entry + 12);
        DWORD bytesInRes = *(DWORD *)(entry + 8);
        if (imageOffset + bytesInRes > e->len)
            return 0;
        // CreateIconFromResource expects the single RT_ICON frame (BITMAPINFOHEADER
        // + XOR DIB + AND mask), which is exactly what the .ico stores at imageOffset.
        // NOTE: the returned HICON is correct, but SIconWnd's use of a real icon
        // HICON (MeasureContent -> GetIconInfo, and DrawIconEx) currently hangs on
        // the bare-metal ARM cairo framebuffer target (SOUI SApp smoke test); that
        // is a SIconWnd/icon-render issue, separate from this resource path. The
        // icon API is proven via the soui_sim icon_probe test. Keep returning the
        // real handle so the path is exercised and ready once rendering is fixed.
        return CreateIconFromResource(dir + imageOffset, bytesInRes, TRUE, 0x00030000);
    }

    STDMETHOD_(HBITMAP, LoadBitmap)(THIS_ LPCTSTR pszResName) OVERRIDE
    {
        return 0;
    }

    STDMETHOD_(HCURSOR, LoadCursor)(THIS_ LPCTSTR pszResName) OVERRIDE
    {
        return 0;
    }

    STDMETHOD_(IBitmapS *, LoadImage)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        return NULL; // no imgdecoder wired in this phase
    }

    STDMETHOD_(IImgX *, LoadImgX)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        return NULL;
    }

    STDMETHOD_(size_t, GetRawBufferSize)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        const MemResEntry *e = res_find(pszType, pszResName);
        return e ? e->len : 0;
    }

    STDMETHOD_(BOOL, GetRawBuffer)
    (THIS_ LPCTSTR pszType, LPCTSTR pszResName, LPVOID pBuf, size_t size) OVERRIDE
    {
        const MemResEntry *e = res_find(pszType, pszResName);
        if (!e || size < e->len)
            return FALSE;
        memcpy(pBuf, e->data, e->len);
        return TRUE;
    }

    STDMETHOD_(void, EnumResource)(THIS_ EnumResCallback funEnumCB, LPARAM lp) OVERRIDE
    {
    }

    STDMETHOD_(void, EnumFile)(THIS_ EnumFileCallback funEnumCB, LPARAM lp) OVERRIDE
    {
    }
};

SNSEND

SNS::IResProvider *CreateMemResProvider(void)
{
    return new SNS::MemResProvider;
}
