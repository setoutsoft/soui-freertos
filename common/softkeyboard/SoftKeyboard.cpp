/*
 * SoftKeyboard.cpp -- see SoftKeyboard.h.
 *
 * The keymaps mirror lv_keyboard.c (LVGL 8.4): a one-dimensional string
 * array where "\n" ends a row and NULL ends the map, plus a parallel width
 * / action table (the role of lv_btnmatrix_ctrl_t in LVGL).  Special keys
 * are identified by their action, never by parsing the displayed text.
 */
#include "SoftKeyboard.h"

#include <SApp.h>
#include <event/SEventSlot.h>

#include "input.h" // swinx/platform/freertos: SwinxInjectText / SwinxInjectKey

namespace souifr {

namespace {
CSoftKeyboard *s_pHostKeyboard = NULL;

BOOL HostSoftKeyboard(HWND hWnd, BOOL bShow)
{
    return s_pHostKeyboard ? s_pHostKeyboard->OnHostSoftKeyboard(hWnd, bShow) : FALSE;
}
} // namespace

enum SKAction
{
    SKA_TEXT = 0,
    SKA_BACKSPACE,
    SKA_ENTER,
    SKA_LEFT,
    SKA_RIGHT,
    SKA_CLOSE,
    SKA_OK,
    SKA_MODE_LOWER,
    SKA_MODE_UPPER,
    SKA_MODE_NUMBER,
    SKA_MODE_SPECIAL
};

struct SKKey
{
    const char *pszText;
    int nWidth;
    SKAction action;
};

#define SK_NL { "\n", 0, SKA_TEXT }
#define SK_END { NULL, 0, SKA_TEXT }
#define SK_TEXT(t, w) { t, w, SKA_TEXT }
#define SK_ACT(t, w, a) { t, w, a }

static const SKKey s_mapLower[] = {
    SK_ACT("1#", 1, SKA_MODE_SPECIAL),
    SK_TEXT("q", 1), SK_TEXT("w", 1), SK_TEXT("e", 1), SK_TEXT("r", 1), SK_TEXT("t", 1),
    SK_TEXT("y", 1), SK_TEXT("u", 1), SK_TEXT("i", 1), SK_TEXT("o", 1), SK_TEXT("p", 1),
    SK_ACT("<-", 2, SKA_BACKSPACE), SK_NL,

    SK_ACT("ABC", 2, SKA_MODE_UPPER),
    SK_TEXT("a", 1), SK_TEXT("s", 1), SK_TEXT("d", 1), SK_TEXT("f", 1), SK_TEXT("g", 1),
    SK_TEXT("h", 1), SK_TEXT("j", 1), SK_TEXT("k", 1), SK_TEXT("l", 1),
    SK_ACT("Enter", 2, SKA_ENTER), SK_NL,

    SK_TEXT("_", 1), SK_TEXT("-", 1),
    SK_TEXT("z", 1), SK_TEXT("x", 1), SK_TEXT("c", 1), SK_TEXT("v", 1), SK_TEXT("b", 1),
    SK_TEXT("n", 1), SK_TEXT("m", 1),
    SK_TEXT(".", 1), SK_TEXT(",", 1), SK_TEXT(":", 1), SK_NL,

    SK_ACT("Hide", 1, SKA_CLOSE), SK_ACT("<", 1, SKA_LEFT), SK_TEXT(" ", 4),
    SK_ACT(">", 1, SKA_RIGHT), SK_ACT("OK", 2, SKA_OK), SK_END
};

static const SKKey s_mapUpper[] = {
    SK_ACT("1#", 1, SKA_MODE_SPECIAL),
    SK_TEXT("Q", 1), SK_TEXT("W", 1), SK_TEXT("E", 1), SK_TEXT("R", 1), SK_TEXT("T", 1),
    SK_TEXT("Y", 1), SK_TEXT("U", 1), SK_TEXT("I", 1), SK_TEXT("O", 1), SK_TEXT("P", 1),
    SK_ACT("<-", 2, SKA_BACKSPACE), SK_NL,

    SK_ACT("abc", 2, SKA_MODE_LOWER),
    SK_TEXT("A", 1), SK_TEXT("S", 1), SK_TEXT("D", 1), SK_TEXT("F", 1), SK_TEXT("G", 1),
    SK_TEXT("H", 1), SK_TEXT("J", 1), SK_TEXT("K", 1), SK_TEXT("L", 1),
    SK_ACT("Enter", 2, SKA_ENTER), SK_NL,

    SK_TEXT("_", 1), SK_TEXT("-", 1),
    SK_TEXT("Z", 1), SK_TEXT("X", 1), SK_TEXT("C", 1), SK_TEXT("V", 1), SK_TEXT("B", 1),
    SK_TEXT("N", 1), SK_TEXT("M", 1),
    SK_TEXT(".", 1), SK_TEXT(",", 1), SK_TEXT(":", 1), SK_NL,

    SK_ACT("Hide", 1, SKA_CLOSE), SK_ACT("<", 1, SKA_LEFT), SK_TEXT(" ", 4),
    SK_ACT(">", 1, SKA_RIGHT), SK_ACT("OK", 2, SKA_OK), SK_END
};

static const SKKey s_mapNumber[] = {
    SK_TEXT("1", 1), SK_TEXT("2", 1), SK_TEXT("3", 1), SK_TEXT("4", 1), SK_TEXT("5", 1),
    SK_TEXT("6", 1), SK_TEXT("7", 1), SK_TEXT("8", 1), SK_TEXT("9", 1), SK_TEXT("0", 1),
    SK_ACT("<-", 2, SKA_BACKSPACE), SK_NL,

    SK_ACT("abc", 2, SKA_MODE_LOWER),
    SK_TEXT("+", 1), SK_TEXT("-", 1), SK_TEXT("*", 1), SK_TEXT("/", 1), SK_TEXT(".", 1),
    SK_TEXT(",", 1), SK_TEXT(":", 1), SK_TEXT(";", 1), SK_TEXT("(", 1), SK_TEXT(")", 1), SK_NL,

    SK_ACT("Hide", 1, SKA_CLOSE), SK_ACT("<", 1, SKA_LEFT), SK_TEXT(" ", 4),
    SK_ACT(">", 1, SKA_RIGHT), SK_ACT("OK", 2, SKA_OK), SK_END
};

static const SKKey s_mapSpecial[] = {
    SK_TEXT("[", 1), SK_TEXT("]", 1), SK_TEXT("{", 1), SK_TEXT("}", 1), SK_TEXT("#", 1),
    SK_TEXT("%", 1), SK_TEXT("<", 1), SK_TEXT(">", 1), SK_TEXT("+", 1), SK_TEXT("-", 1), SK_NL,

    SK_ACT("abc", 2, SKA_MODE_LOWER),
    SK_TEXT("~", 1), SK_TEXT("`", 1), SK_TEXT("&", 1), SK_TEXT("*", 1), SK_TEXT("(", 1),
    SK_TEXT(")", 1), SK_TEXT(":", 1), SK_TEXT(";", 1), SK_TEXT("!", 1), SK_TEXT("?", 1), SK_NL,

    SK_ACT("Hide", 1, SKA_CLOSE), SK_TEXT("\\", 1), SK_TEXT("\"", 1), SK_TEXT("'", 1),
    SK_TEXT("_", 1), SK_TEXT("=", 1), SK_ACT("<-", 2, SKA_BACKSPACE), SK_END
};

static const SKKey *MapFor(SoftKeyMode mode)
{
    switch (mode)
    {
    case SKM_UPPER:   return s_mapUpper;
    case SKM_NUMBER:  return s_mapNumber;
    case SKM_SPECIAL: return s_mapSpecial;
    default:          return s_mapLower;
    }
}

// Number of real keys (row separators and the terminator excluded).
static int CountKeys(const SKKey *map)
{
    int n = 0;
    for (int i = 0; map[i].pszText; ++i)
        if (map[i].pszText[0] != '\n')
            ++n;
    return n;
}

CSoftKeyboard::CSoftKeyboard()
    : m_pHost(NULL)
    , m_pEdit(NULL)
    , m_hTarget(NULL)
    , m_mode(SKM_LOWER)
    , m_nCreated(0)
    , m_bVisible(FALSE)
    , m_bInited(FALSE)
{
    for (int i = 0; i < MAX_KEYS; ++i)
        m_apKeys[i] = NULL;
}

CSoftKeyboard::~CSoftKeyboard()
{
    // The buttons are children of m_pHost; the SOUI window tree owns them.
    if (s_pHostKeyboard == this)
    {
        s_pHostKeyboard = NULL;
        SwinxSetSoftKeyboardHandler(NULL);
    }
}

BOOL CSoftKeyboard::Init(SWindow *pParent, LPCWSTR pszHostName, HWND hTarget, SWindow *pEdit)
{
    if (m_bInited || !pParent)
        return FALSE;

    m_pHost = pParent->FindChildByName(pszHostName);
    if (!m_pHost)
        return FALSE;

    m_hTarget = hTarget;
    m_pEdit = pEdit;
    m_mode = SKM_LOWER;

    // Every keymap has at most 40 keys; create that many buttons once and
    // re-label / hide them on mode changes (the same reuse LVGL's btnmatrix
    // does with its map[] pointer).
    int nMax = 0;
    for (int m = 0; m < SKM_COUNT; ++m)
    {
        int n = CountKeys(MapFor((SoftKeyMode)m));
        if (n > nMax)
            nMax = n;
    }
    if (nMax > MAX_KEYS)
        nMax = MAX_KEYS;

    for (int i = 0; i < nMax; ++i)
    {
        wchar_t szXml[128];
        swprintf(szXml, 128,
                 L"<button name='sk_%d' class='softkey' pos='0,0,1,1' "
                 L"focusable='0' animate='0'/>",
                 i);
        SXmlDoc doc;
        if (!doc.load_buffer(szXml, wcslen(szXml) * sizeof(wchar_t), xml_parse_default,
                             sizeof(wchar_t) == 2 ? enc_utf16 : enc_utf32))
            continue;
        SXmlNode node = doc.root().first_child();
        if (!node)
            continue;

        // The factory returns IWindow* in the ESP-IDF build and SWindow* in
        // the simulator build; sobj_cast works for both.
        SWindow *pBtn = sobj_cast<SWindow>(
            SApplication::getSingleton().CreateWindowByName(L"button"));
        if (!pBtn)
            continue;
        pBtn->SetContainer(m_pHost->GetContainer());
        if (!pBtn->InitFromXml(&node))
        {
            pBtn->Release();
            continue;
        }
        pBtn->SetID(i + 1);
        // SubscribeEvent clones the slot, so a stack slot is safe here.
        MemberFunctionSlot<CSoftKeyboard, IEvtArgs> slot(&CSoftKeyboard::OnKey, this);
        pBtn->SubscribeEvent(EventCmd::EventID, &slot);
        m_pHost->InsertChild(pBtn);
        m_apKeys[m_nCreated++] = pBtn;
    }

    m_bInited = TRUE;
    // From now on SEditLite / SRichEdit focus changes drive this keyboard.
    s_pHostKeyboard = this;
    SwinxSetSoftKeyboardHandler(&HostSoftKeyboard);
    ApplyMap();
    return TRUE;
}

void CSoftKeyboard::SetTarget(HWND hTarget, SWindow *pEdit)
{
    m_hTarget = hTarget;
    m_pEdit = pEdit;
}

void CSoftKeyboard::ApplyMap()
{
    if (!m_bInited || !m_pHost)
        return;

    const SKKey *map = MapFor(m_mode);

    CRect rcHost;
    m_pHost->GetClientRect(&rcHost);
    const int w = rcHost.right - rcHost.left;
    const int h = rcHost.bottom - rcHost.top;
    if (w <= 0 || h <= 0)
        return;

    const int pad = 3; // keeps the keys off the popup border
    const int gap = 2;
    const int availW = w - pad * 2;
    const int availH = h - pad * 2;
    // SWindow::Move() takes HOST coordinates (m_rcWindow is shared by the whole
    // tree), not coordinates relative to the parent.  GetClientRect() also
    // returns host coordinates, so carry the host origin into every key rect;
    // using (0,0) here piles all four rows on top of each other at the origin.
    const int originX = rcHost.left + pad;
    const int originY = rcHost.top + pad;

    // Row count comes from the map itself, as in LVGL's allocate_btn_areas().
    int rows = 1;
    for (int i = 0; map[i].pszText; ++i)
        if (map[i].pszText[0] == '\n')
            ++rows;
    const int rowH = (availH - gap * (rows - 1)) / rows;
    if (rowH <= 0)
        return;

    int keyIdx = 0;
    int row = 0;
    int i = 0;
    while (map[i].pszText && keyIdx < m_nCreated)
    {
        // Count the keys in this row and their total weight.
        int n = 0;
        int totalW = 0;
        while (map[i + n].pszText && map[i + n].pszText[0] != '\n')
        {
            totalW += map[i + n].nWidth;
            ++n;
        }
        if (n == 0 || totalW <= 0)
            break;

        int x = originX;
        const int y = originY + row * (rowH + gap);
        for (int k = 0; k < n && keyIdx < m_nCreated; ++k)
        {
            const SKKey &key = map[i + k];
            int kw = (availW - gap * (n - 1)) * key.nWidth / totalW;
            CRect rc(x, y, x + kw, y + rowH);
            SWindow *pBtn = m_apKeys[keyIdx];
            if (pBtn)
            {
                pBtn->SetWindowTextU8(key.pszText);
                pBtn->Move(&rc);
                pBtn->SetVisible(TRUE, TRUE);
            }
            x += kw + gap;
            ++keyIdx;
        }
        ++row;
        i += n;
        if (map[i].pszText && map[i].pszText[0] == '\n')
            ++i;
    }

    // Modes with fewer keys reuse the same buttons; hide the leftovers.
    for (; keyIdx < m_nCreated; ++keyIdx)
        if (m_apKeys[keyIdx])
            m_apKeys[keyIdx]->SetVisible(FALSE, TRUE);
}

BOOL CSoftKeyboard::OnHostSoftKeyboard(HWND hWnd, BOOL bShow)
{
#if defined(SOUI_KB_AUTOTEST)
    printf("[kb] host show h=%p target=%p show=%d visible=%d\n",
           (void *)hWnd, (void *)m_hTarget, (int)bShow, (int)m_bVisible);
#endif
    // One host window per port; a different HWND is not the edit focus.
    if (hWnd && m_hTarget && hWnd != m_hTarget)
        return FALSE;
    if (bShow)
    {
        // Show() sets focus on m_pEdit; skip it when already up so the
        // focus event cannot recurse back into ShowSoftKeyboard().
        if (!m_bVisible)
            Show();
    }
    else
        Hide();
    return TRUE;
}

void CSoftKeyboard::SetMode(SoftKeyMode mode)
{
    if (mode < 0 || mode >= SKM_COUNT || mode == m_mode)
        return;
    m_mode = mode;
    ApplyMap();
}

BOOL CSoftKeyboard::Show()
{
    if (!m_bInited || !m_pHost)
        return FALSE;
    if (!m_bVisible)
    {
        m_pHost->SetVisible(TRUE, TRUE);
        m_bVisible = TRUE;
    }
    if (m_pEdit)
        m_pEdit->SetFocus();
    return TRUE;
}

void CSoftKeyboard::Hide()
{
    if (m_bInited && m_pHost && m_bVisible)
    {
        m_pHost->SetVisible(FALSE, TRUE);
        m_bVisible = FALSE;
    }
}

BOOL CSoftKeyboard::Toggle()
{
    if (m_bVisible)
    {
        Hide();
        return FALSE;
    }
    return Show();
}

void CSoftKeyboard::InputText(const char *pszUtf8)
{
    if (m_hTarget)
        SwinxInjectText(m_hTarget, pszUtf8);
}

void CSoftKeyboard::InjectVk(UINT vk)
{
    if (!m_hTarget)
        return;
    SwinxInjectKey(m_hTarget, WM_KEYDOWN, vk, 0);
    SwinxInjectKey(m_hTarget, WM_KEYUP, vk, 0);
}

BOOL CSoftKeyboard::OnKey(IEvtArgs *e)
{
    SWindow *pSender = sobj_cast<SWindow>(e ? e->Sender() : NULL);
    if (!pSender)
        return FALSE;

    int id = pSender->GetID() - 1; // buttons were created with ID = index + 1
    if (id < 0 || id >= m_nCreated)
        return FALSE;

    // Re-resolve the action from the current map: the visible key order is
    // exactly the map order, so index N always refers to map[N].
    const SKKey *map = MapFor(m_mode);
    int idx = 0;
    const SKKey *key = NULL;
    for (int i = 0; map[i].pszText; ++i)
    {
        if (map[i].pszText[0] == '\n')
            continue;
        if (idx == id)
        {
            key = &map[i];
            break;
        }
        ++idx;
    }
    if (!key)
        return FALSE;

    switch (key->action)
    {
    case SKA_TEXT:
        InputText(key->pszText);
        break;
    case SKA_BACKSPACE:
        InjectVk(VK_BACK);
        break;
    case SKA_ENTER:
        InjectVk(VK_RETURN);
        break;
    case SKA_LEFT:
        InjectVk(VK_LEFT);
        break;
    case SKA_RIGHT:
        InjectVk(VK_RIGHT);
        break;
    case SKA_CLOSE:
        Hide();
        return TRUE;
    case SKA_OK:
        InjectVk(VK_RETURN);
        Hide();
        break;
    case SKA_MODE_LOWER:
        SetMode(SKM_LOWER);
        break;
    case SKA_MODE_UPPER:
        SetMode(SKM_UPPER);
        break;
    case SKA_MODE_NUMBER:
        SetMode(SKM_NUMBER);
        break;
    case SKA_MODE_SPECIAL:
        SetMode(SKM_SPECIAL);
        break;
    default:
        break;
    }

    // Buttons are focusable='0'; put the caret back anyway so injected text
    // always lands in the edit control on ports that still move focus.
    if (m_pEdit)
        m_pEdit->SetFocus();
    return TRUE;
}

} // namespace souifr
