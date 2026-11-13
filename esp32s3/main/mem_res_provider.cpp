/*
 * mem_res_provider.cpp -- in-memory IResProvider for the ESP32-S3 bring-up.
 *
 * Two sources feed it:
 *
 *   CreateMemResProvider()
 *       the built-in 320x240 "hello" layout below, used by the first-bring-up
 *       path in app_main.cpp (and as the fallback if the demo layout fails).
 *
 *   CreateMemResProviderFromTable(g_appResTable / g_sysResTable)
 *       the generated tables from main/generated/uires_res.cpp, i.e. the real
 *       soui_sim uires + the SOUI system resources (tools/gen_uires.py).
 *
 * Why not the normal resource chain: uires -> windres ends up in
 * ".section .rdata", which the ESP-IDF linker scripts do not collect, so the
 * weak symbols swinx looks for stay NULL on this target.
 *
 * Only raw buffers are served.  LoadImage()/LoadSvg() go through the resource
 * provider for the *bytes*; decoding still needs an imgdecoder (not linked) or
 * nanosvg (built into SOUI core, so SVG skins work).
 */
#include <souistd.h> // SStringT / SMap, needed by <res.mgr/SResProvider.h>
#include <interface/SResProvider-i.h>
#include <helper/obj-ref-impl.hpp>
#include <res.mgr/SResProvider.h> // SResLoadFromMemory
#include <string.h>

#include "soui_res_table.h"

SNSBEGIN

// ---------------------------------------------------------------------------
// built-in fallback resources (ROM)
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

static const SouiResEntry k_builtinTable[] = {
    {"LAYOUT", "APP_MAIN", K_LAYOUT_APP_MAIN, (unsigned int)sizeof(K_LAYOUT_APP_MAIN) - 1},
};
static const int k_builtinCount = (int)(sizeof(k_builtinTable) / sizeof(k_builtinTable[0]));

// ---------------------------------------------------------------------------
// MemResProvider
// ---------------------------------------------------------------------------
class MemResProvider : public TObjRefImpl<IResProvider>
{
  public:
    MemResProvider(const SouiResEntry *pTable, int nCount)
        : m_table(pTable), m_count(nCount)
    {
    }

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
        // Same two steps SResProviderPE::LoadImage() takes: fetch the bytes,
        // then hand them to the render factory's image decoder
        // (SResLoadFromMemory::LoadImage -> GETRENDERFACTORY->CreateBitmap).
        // Without an installed decoder this returns NULL, which is what made
        // SResProviderMgr log "resource content PNG:... not found!".
        const SouiResEntry *e = res_find(pszType, pszResName);
        if (!e)
            return NULL;
        return SResLoadFromMemory::LoadImage(const_cast<char *>(e->data), e->len);
    }

    STDMETHOD_(IImgX *, LoadImgX)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        const SouiResEntry *e = res_find(pszType, pszResName);
        if (!e)
            return NULL;
        return SResLoadFromMemory::LoadImgX(const_cast<char *>(e->data), e->len);
    }

    STDMETHOD_(size_t, GetRawBufferSize)(THIS_ LPCTSTR pszType, LPCTSTR pszResName) OVERRIDE
    {
        const SouiResEntry *e = res_find(pszType, pszResName);
        return e ? e->len : 0;
    }

    STDMETHOD_(BOOL, GetRawBuffer)
    (THIS_ LPCTSTR pszType, LPCTSTR pszResName, LPVOID pBuf, size_t size) OVERRIDE
    {
        const SouiResEntry *e = res_find(pszType, pszResName);
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

  private:
    const SouiResEntry *res_find(LPCTSTR pszType, LPCTSTR pszResName) const
    {
        if (!pszType || !pszResName)
            return NULL;
        for (int i = 0; i < m_count; ++i)
        {
            if (strcmp(m_table[i].type, pszType) == 0 && strcmp(m_table[i].name, pszResName) == 0)
                return &m_table[i];
        }
        return NULL;
    }

    const SouiResEntry *m_table;
    int m_count;
};

SNSEND

SNS::IResProvider *CreateMemResProvider(void)
{
    // the built-in table lives inside SNS (it is plain ROM data, but keeping
    // it next to the provider keeps one file self-contained)
    return new SNS::MemResProvider(SNS::k_builtinTable, SNS::k_builtinCount);
}

SNS::IResProvider *CreateMemResProviderFromTable(const SouiResEntry *pTable, int nCount)
{
    if (!pTable || nCount <= 0)
        return NULL;
    return new SNS::MemResProvider(pTable, nCount);
}
