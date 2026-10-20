/*
 * soui_fr/mem_res_provider.cpp
 *
 * Minimal in-memory IResProvider for the FreeRTOS SApp smoke test.
 * Resources are static ROM string tables (no filesystem), enough to feed
 * SHostWnd with one layout XML ("LAYOUT:APP_MAIN").  Icons / bitmaps /
 * cursors are not provided (no imgdecoder wired in this phase): the test
 * layout deliberately uses color fills and text only.
 */
#include <interface/SResProvider-i.h>
#include <helper/obj-ref-impl.hpp>
#include <string.h>

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
        return 0;
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
