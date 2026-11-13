/*
 * soui_demo.h -- the soui_sim demo window, ported to ESP32-S3.
 *
 * Identical structure to <soui-freertos>/soui_sim/MainDlg.h (SHostWnd + SDpiHandler
 * + IAnimatorUpdateListener, EVENT_MAP / MSG_MAP), including how the layout id
 * and the command ids are written: UIRES.LAYOUT.XML_MAINWND and R.id.* come
 * from the uiresbuilder-generated resource.h, which is what a wizard-style
 * SOUI app uses.  The only difference is that nothing here depends on SDL --
 * input arrives from soui_board_poll_touch() in the pump, which posts real
 * WM_LBUTTONDOWN / WM_MOUSEMOVE messages.
 */
#ifndef SOUI_ESP32S3_SOUI_DEMO_H_
#define SOUI_ESP32S3_SOUI_DEMO_H_

#include <souistd.h>
#include <core/SHostWnd.h>
#include <helper/SDpiHelper.hpp>
#include <core/Svg.h>
#include <valueAnimator/SValueAnimator.h>

// R.id.* / R.name / UIRES.* (generated from uires/uires.idx by uiresbuilder).
// Declarations only here -- app_main.cpp defines the tables (INIT_R_DATA).
#include "resource.h"

class CMainDlg : public SNS::SHostWnd
              , public SNS::SDpiHandler<CMainDlg>
              , public SNS::IAnimatorUpdateListener
{
  public:
    CMainDlg();
    ~CMainDlg();

    // ---- SOUI command handlers --------------------------------------------
    void OnClose();
    void OnMinimize();
    void OnSwitchImage(); // btn_test: toggle PNG <-> SVG in img_view
    void OnToggleAnim();  // btn_anim: freeze / resume the value animators

    // ---- combobox events --------------------------------------------------
    void OnCbxSpeedChange(SNS::IEvtArgs *e); // cbx_speed: EVT_CB_SELCHANGE

    // ---- frame-rate readout -----------------------------------------------
    // 500ms WM_TIMER: soui_board_present_count() delta / dt, printed into
    // fps_label.  Present count (not animation ticks) is what the user sees,
    // and it is the same number the vendor LVGL build calls "fps".
    void OnTimer(UINT_PTR idEvent);

    BOOL OnInitDialog(HWND wndFocus, LPARAM lInitParam);

  protected:
    EVENT_MAP_BEGIN()
        EVENT_NAME_COMMAND(L"btn_close", OnClose)
        EVENT_NAME_COMMAND(L"btn_min", OnMinimize)
        EVENT_ID_COMMAND(R.id.btn_test, OnSwitchImage)
        EVENT_ID_COMMAND(R.id.btn_anim, OnToggleAnim)
        EVENT_ID_HANDLER(R.id.cbx_speed, SNS::EventCBSelChange::EventID, OnCbxSpeedChange)
    EVENT_MAP_END2(SNS::SHostWnd)

    BEGIN_MSG_MAP_EX(CMainDlg)
        CHAIN_MSG_MAP(SNS::SDpiHandler<CMainDlg>)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_CLOSE(OnClose)
        CHAIN_MSG_MAP(SNS::SHostWnd)
        REFLECT_NOTIFICATIONS_EX()
    END_MSG_MAP()

    STDMETHOD_(void, onAnimationUpdate)(THIS_ SNS::IValueAnimator *pAnimator) OVERRIDE;

  private:
    void LoadImages();
    void ApplyImage();
    void StartAnimation();
    void SetAnimSpeed(int nSweepMs, int nFadeMs);

    SNS::SAutoRefPtr<SNS::IBitmapS> m_png; // PNG:TEST_PNG (needs an imgdecoder)
    SNS::SAutoRefPtr<SNS::ISvgObj> m_svg;  // SVG:TEST_SVG  (nanosvg, built in)
    BOOL m_bShowSvg;

    // Two animations, deliberately of different cost classes:
    //   anim_box -- position driven (SIntAnimator), only old+new rect repaint;
    //   fade_box -- a layered window's alpha (SByteAnimator), which forces an
    //               offscreen surface + a whole-rect AlphaBlend per frame.
    // Keeping both is what makes the fps readout meaningful: it is the mix a
    // real app sees, not a best case.
    SNS::SWindow *m_pMove; // anim_box: swept by SIntAnimator
    SNS::SWindow *m_pFade; // fade_box: alpha driven by SByteAnimator
    SNS::SWindow *m_pFps;  // fps_label: text rewritten by OnTimer
    SNS::SAutoRefPtr<SNS::SIntAnimator> m_moveAni;
    SNS::SAutoRefPtr<SNS::SByteAnimator> m_fadeAni;
    BOOL m_bAnimFrozen;

    // frame-rate averaging state (see OnTimer)
    uint32_t m_fpsLastPresents;
    int64_t m_fpsLastUs;
    int m_fpsAvgX10;        // fixed point, one decimal -- no %f on newlib-nano
    SNS::SStringA m_fpsShown; // narrow: this port has no _UNICODE (tchar.h)
};

/* Creates the demo window, runs OnInitDialog and paints the first frame.
 * Returns NULL if the layout could not be created (caller falls back). */
HWND soui_demo_create_window(SNS::SApplication *pApp);

#endif // SOUI_ESP32S3_SOUI_DEMO_H_
