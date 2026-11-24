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

// A/B load: 1 = a minimal "fps only" window that takes over WM_PAINT and
// draws with plain GDI (no SWindow tree, no DispatchPaint, no PushClipRect).
// 0 = the original soui_sim demo (caption, skins, two animators, combobox) --
// the real UI, which is what README 11.9 measures.
//
// Defined in this header rather than as a CMake compile definition: a change
// to target_compile_definitions() invalidates every TU in the soui component
// (~1400 objects), while this only rebuilds app_main.cpp + soui_demo.cpp.
#ifndef SOUI_FPS_ONLY
#define SOUI_FPS_ONLY 0
#endif

// Which runtime scan the minimal window runs (README 11.5 / 11.6 / 11.7 / 11.8):
//   0 = element-count scan: 1/2/4/8/16/32/64 FillRects in a 200x28 dirty rect.
//   1 = blank-paint scan: 0 GDI calls in WM_PAINT but a FULL-SCREEN 320x240
//       refresh every frame, plus three control stages.
//   2 = animated-box scan: one 60x60 box moving 2px/frame with a 62x60 dirty
//       rect -- the same load as the LVGL firmware's ~150fps figure -- plus
//       alpha / text / no-delay control stages.
//   3 = the same animated box, used to A/B two framework optimisations:
//       (A) InvalidateRect()'s dirty-region fast path, (B) the pump's fixed
//       1ms vTaskDelay.  Four stages: A/B, A, A+B, B.
// All of them keep the same one-flash / one-capture workflow: stages rotate
// every 9s and are tagged in the 3s console report.
#ifndef SOUI_FPS_SCAN_MODE
#define SOUI_FPS_SCAN_MODE 3
#endif

#include <souistd.h>
#include <core/SHostWnd.h>
#include <helper/SDpiHelper.hpp>
#include <core/Svg.h>
#include <valueAnimator/SValueAnimator.h>
#include "SoftKeyboard.h"

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
    void OnToggleKeyboard(); // btn_kb: show / hide the soft keyboard

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
        //EVENT_NAME_COMMAND(L"btn_close", OnClose)
        //EVENT_NAME_COMMAND(L"btn_min", OnMinimize)
        EVENT_ID_COMMAND(R.id.btn_test, OnSwitchImage)
        EVENT_ID_COMMAND(R.id.btn_anim, OnToggleAnim)
        EVENT_ID_COMMAND(R.id.btn_kb, OnToggleKeyboard)
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
    souifr::CSoftKeyboard m_softKb;
    BOOL m_bAnimFrozen;

    // frame-rate averaging state (see OnTimer)
    uint32_t m_fpsLastPresents;
    int64_t m_fpsLastUs;
    int m_fpsAvgX10;        // fixed point, one decimal -- no %f on newlib-nano
    SNS::SStringA m_fpsShown; // narrow: this port has no _UNICODE (tchar.h)
};

/* Creates the demo window (or the minimal fps-only window, see SOUI_FPS_ONLY),
 * runs OnInitDialog and paints the first frame.
 * Returns NULL if the layout could not be created (caller falls back). */
HWND soui_demo_create_window(SNS::SApplication *pApp);

#if SOUI_FPS_ONLY
/* Asks for one more repaint of the fps text.  Called once per pump round so
 * the frame rate is limited only by how fast a frame can be produced, not by
 * a timer or by the animator's 16ms cadence. */
void soui_fps_tick(HWND hWnd);

/* Current number of independent visible elements drawn per frame
 * (scan stage 1/2/4/8/16/32/64), printed in the 3s report so one flash
 * yields a full "element count -> fps" curve. */
int soui_fps_elements(void);

/* Human-readable tag for the active scan stage, e.g.
 * "blank mode=0 empty_full" or "elements=8".  Printed by the 3s report. */
const char *soui_fps_scan_label(void);

/* Ticks to sleep at the end of one pump round (1 normally; 0 in the stages
 * that remove the fixed 1ms, matching LVGL's vTaskDelay(0)).  In the 0 case
 * app_main still feeds IDLE1 one tick every 250ms so the 5s task watchdog
 * does not fire. */
int soui_fps_pump_delay(void);

/* Cumulative time inside InvalidateRect() and how many calls produced it.
 * Only meaningful in the animated-box scan; splits "frame - paint" into the
 * invalidate cost and the rest of the pump. */
uint64_t soui_fps_inval_us(void);
uint32_t soui_fps_inval_count(void);
#endif

#endif // SOUI_ESP32S3_SOUI_DEMO_H_
