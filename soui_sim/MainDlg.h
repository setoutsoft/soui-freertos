/*
 * MainDlg.h -- main window of the demo.
 *
 * Modelled on demos/demo/MainDlg.h and wizard/CmakeApp/MainDlg.h: a non-modal
 * SOUI window deriving from SHostWnd, mixed with SDpiHandler so the layout
 * follows the monitor DPI.
 *
 * Layout id comes from uires.idx (UIRES.LAYOUT.XML_MAINWND), the command ids
 * from the uiresbuilder-generated resource.h (R.id.*), which is why the app
 * calls SApplication::InitXmlNamedID() during start-up.
 */
#ifndef SOUISIM_MAINDLG_H_
#define SOUISIM_MAINDLG_H_

#include <core/Svg.h>
#include <valueAnimator/SValueAnimator.h>

class CMainDlg : public SHostWnd
              , public SDpiHandler<CMainDlg>
              , public IAnimatorUpdateListener
{
  public:
    CMainDlg();
    ~CMainDlg();

    // ---- SOUI command handlers (EVENT_MAP below) ---------------------------
    void OnClose();
    void OnMinimize();
    void OnSwitchImage(); // btn_test: toggle PNG <-> SVG in img_view
    void OnToggleAnim();  // btn_anim: freeze / resume the value animators

    // ---- combobox events (EVENT_MAP below) --------------------------------
    void OnCbxSpeedChange(IEvtArgs *e);   // cbx_speed: EVT_CB_SELCHANGE
    void OnCbxBeforeCloseUp(IEvtArgs *e); // cbx_speed: EVT_CB_BEFORE_CLOSEUP

    BOOL OnInitDialog(HWND wndFocus, LPARAM lInitParam);

  protected:
    // named command (caption buttons) + id command (R.id.* from resource.h)
    EVENT_MAP_BEGIN()
        EVENT_NAME_COMMAND(L"btn_close", OnClose)
        EVENT_NAME_COMMAND(L"btn_min", OnMinimize)
        EVENT_ID_COMMAND(R.id.btn_test, OnSwitchImage)
        EVENT_ID_COMMAND(R.id.btn_anim, OnToggleAnim)
        EVENT_ID_HANDLER(R.id.cbx_speed, EventCBSelChange::EventID, OnCbxSpeedChange)
        EVENT_ID_HANDLER(R.id.cbx_speed, EventCBBeforeCloseUp::EventID, OnCbxBeforeCloseUp)
    EVENT_MAP_END2(SHostWnd)

    // native window messages, chained to SHostWnd
    BEGIN_MSG_MAP_EX(CMainDlg)
        CHAIN_MSG_MAP(SDpiHandler<CMainDlg>)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_CLOSE(OnClose)
        CHAIN_MSG_MAP(SHostWnd)
        REFLECT_NOTIFICATIONS_EX()
    END_MSG_MAP()

    // ---- IAnimatorUpdateListener ------------------------------------------
    STDMETHOD_(void, onAnimationUpdate)(THIS_ IValueAnimator *pAnimator) OVERRIDE;

  private:
    void LoadImages();
    void ApplyImage();
    void StartAnimation();
    void SetAnimSpeed(int nSweepMs, int nFadeMs);

    SAutoRefPtr<IBitmapS> m_png; // PNG:TEST_PNG, decoded by imgdecoder-stb
    SAutoRefPtr<ISvgObj> m_svg;  // SVG:TEST_SVG, parsed by the built-in nanosvg
    BOOL m_bShowSvg;

    SWindow *m_pMove; // anim_box: swept horizontally by SIntAnimator
    SWindow *m_pFade; // fade_box: alpha driven by SByteAnimator
    SAutoRefPtr<SIntAnimator> m_moveAni;
    SAutoRefPtr<SByteAnimator> m_fadeAni;
    BOOL m_bAnimFrozen;
};

#endif // SOUISIM_MAINDLG_H_
