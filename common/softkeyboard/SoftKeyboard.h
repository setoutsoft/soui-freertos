/*
 * SoftKeyboard.h -- reusable on-screen keyboard for SOUI-on-FreeRTOS.
 *
 * Modelled on LVGL 8.4's lv_keyboard: a keyboard is a data-driven button
 * matrix plus a small event handler that translates a button into an action
 * on the bound edit control.  The four keymaps below are the ASCII QWERTY /
 * upper / number / special maps of lv_keyboard.c, with the FontAwesome
 * special-key symbols replaced by plain ASCII labels so no extra glyphs are
 * needed on the panel (the vendor tree this port targets is the "ascii"
 * LVGL build).
 *
 * Layout / styling live in the application XML: the app declares an empty
 * <window name="softkb" .../> host, and this class creates the buttons at
 * runtime from the keymaps.  Text is committed through SwinxInjectText()
 * (UTF-8 -> WM_IME_CHAR) and navigation keys through SwinxInjectKey(), so
 * SEditLite / SRichEdit need no changes at all.
 */
#ifndef SOUI_FR_SOFTKEYBOARD_H_
#define SOUI_FR_SOFTKEYBOARD_H_

#include <souistd.h>

namespace souifr {

// Host projects differ: soui_sim's stdafx.h has a global `using namespace SNS`,
// the ESP-IDF app does not (it spells every SOUI type SNS::X).  Import the
// names here so this shared header compiles in both builds.
using namespace SNS;

enum SoftKeyMode
{
    SKM_LOWER = 0,
    SKM_UPPER,
    SKM_NUMBER,
    SKM_SPECIAL,
    SKM_COUNT
};

class CSoftKeyboard
{
  public:
    CSoftKeyboard();
    ~CSoftKeyboard();

    // pParent: window tree that owns the host (usually GetRoot()).
    // pszHostName: name of the (initially hidden) <window> that holds the keys.
    // hTarget: native host HWND the injected messages are posted to.
    // pEdit: optional edit control to focus when the keyboard is shown.
    BOOL Init(SWindow *pParent, LPCWSTR pszHostName, HWND hTarget, SWindow *pEdit);

    void SetTarget(HWND hTarget, SWindow *pEdit);

    BOOL Show();
    void Hide();
    BOOL Toggle();
    // Plugged into swinx ShowSoftKeyboard(): edit focus shows the keyboard,
    // kill-focus hides it. Returns FALSE when hWnd belongs elsewhere.
    BOOL OnHostSoftKeyboard(HWND hWnd, BOOL bShow);
    BOOL IsVisible() const { return m_bVisible; }

    SoftKeyMode GetMode() const { return m_mode; }
    void SetMode(SoftKeyMode mode);

  private:
    enum { MAX_KEYS = 48 };

    BOOL OnKey(IEvtArgs *e);
    void ApplyMap();
    void InputText(const char *pszUtf8);
    void InjectVk(UINT vk);

    SWindow *m_pHost;
    SWindow *m_pEdit;
    HWND m_hTarget;
    SoftKeyMode m_mode;
    SWindow *m_apKeys[MAX_KEYS];
    int m_nCreated;
    BOOL m_bVisible;
    BOOL m_bInited;
};

} // namespace souifr

#endif // SOUI_FR_SOFTKEYBOARD_H_
