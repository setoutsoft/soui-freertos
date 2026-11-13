/*
 * soui_demo.cpp -- the soui_sim demo, running on the ESP32-S3 panel.
 *
 * Body is <soui-freertos>/soui_sim/MainDlg.cpp with the SDL shell removed:
 * PNG/SVG image swap, two value animators (sweep + fade), a dropdown combobox
 * that retimes the animators, and the caption buttons.  What changed:
 *
 *   * image/svg come from the RAM resource table (generated/uires_res.cpp)
 *     instead of the windres blob -- GETRESPROVIDER->LoadImage() still needs an
 *     imgdecoder (not linked yet, so the PNG half stays NULL and the SVG half
 *     is what you see), CreateSvgFromResId() works because nanosvg is part of
 *     SOUI core.
 *   * <edit> needs richedit41 (not linked); if it fails, that one control is
 *     missing and the rest of the layout is unaffected -- see README.md.
 */
#include "soui_demo.h"

#include <interface/SResProvider-i.h>
#include <control/SCmnCtrl.h> // SImageWnd::SetImage / SetSvg

#include "soui_board.h"      // soui_board_present_count() for the fps readout
#include "soui_res_table.h"

#include <esp_timer.h>

#include <cstdio>
#include <cstring>

using namespace SNS;

// swinx records the dialog as the app's main window; the pump in app_main.cpp
// drives it with PeekMessage + IMessageLoop::OnMsg.
static CMainDlg *s_pDlg = NULL;

// fps_label refresh period.  500ms is short enough to look live and long
// enough that the label's own repaint cannot be mistaken for a frame-rate
// problem.
static const UINT_PTR kFpsTimerId = 1;
static const UINT kFpsTimerMs = 500;

CMainDlg::CMainDlg()
    : SHostWnd(soui_demo_res::kLayoutMain)
    , m_bShowSvg(FALSE)
    , m_pMove(NULL)
    , m_pFade(NULL)
    , m_pFps(NULL)
    , m_bAnimFrozen(FALSE)
    , m_fpsLastPresents(0)
    , m_fpsLastUs(0)
    , m_fpsAvgX10(0)
{
}

CMainDlg::~CMainDlg()
{
}

BOOL CMainDlg::OnInitDialog(HWND /*wndFocus*/, LPARAM /*lInitParam*/)
{
    SWindow *pRoot = GetRoot();
    if (!pRoot)
        return 1;

    m_pMove = pRoot->FindChildByName(L"anim_box");
    m_pFade = pRoot->FindChildByName(L"fade_box");
    m_pFps = pRoot->FindChildByName(L"fps_label");

    LoadImages();
    ApplyImage();
    StartAnimation();

    SetTimer(kFpsTimerId, kFpsTimerMs);
    return 0;
}

// ---------------------------------------------------------------------------
// frame-rate readout
// ---------------------------------------------------------------------------
// What is counted: soui_board_frame_count(), i.e. finished WM_PAINTs (render +
// present).  That is the number the vendor LVGL build reports as "fps" (one
// refresh pass), so the two firmwares are directly comparable.  It is
// deliberately NOT soui_board_present_count(): one frame can emit two presents
// (the window commit, then recomposing an overlapping window), which would
// report roughly double the frame rate.
//
// Two deviations from "count / second":
//   * the raw value is smoothed with a 0.7/0.3 EMA -- at ~10 frames/s a
//     single 500ms window is only ~5 samples and the number would flicker;
//   * formatting is integer fixed-point ("%d.%d fps"), because newlib-nano's
//     printf has no %f and pulling in the float formatter costs ~4KB flash.
void CMainDlg::OnTimer(UINT_PTR idEvent)
{
    if (idEvent != kFpsTimerId)
    {
        SetMsgHandled(FALSE);
        return;
    }

    const uint32_t nFrames = soui_board_frame_count();
    const int64_t nowUs = esp_timer_get_time();

    if (m_fpsLastUs != 0 && nowUs > m_fpsLastUs)
    {
        const uint32_t dCount = nFrames - m_fpsLastPresents;
        // fps*10 without a float divide: (dCount*10e6)/dtUs.  dCount*10e6 can
        // overflow 32 bits at ~430 frames per window, which is far past
        // anything this panel can do; still do it in 64 bits to be safe.
        const int64_t fpsX10 = (int64_t)dCount * 10000000LL / (nowUs - m_fpsLastUs);

        // EMA so the label does not jitter between two adjacent integers.
        m_fpsAvgX10 = m_fpsAvgX10 > 0 ? (m_fpsAvgX10 * 7 + (int)fpsX10 * 3) / 10 : (int)fpsX10;

        // This port is built without _UNICODE (see swinx/include/tchar.h), so
        // SWindow::SetWindowText takes a narrow UTF-8 string and the wide
        // SStringW overload does not exist -- use SetWindowTextU8.
        char buf[24];
        snprintf(buf, sizeof(buf), "%d.%d fps", m_fpsAvgX10 / 10, m_fpsAvgX10 % 10);
        if (m_pFps && strcmp(buf, m_fpsShown.c_str()) != 0)
        {
            m_fpsShown = buf;
            m_pFps->SetWindowTextU8(buf);
        }
    }

    m_fpsLastPresents = nFrames;
    m_fpsLastUs = nowUs;
}

// ---------------------------------------------------------------------------
// image demo: PNG needs an imgdecoder, SVG needs only nanosvg
// ---------------------------------------------------------------------------
void CMainDlg::LoadImages()
{
    m_png.Attach(GETRESPROVIDER->LoadImage(_T("PNG:TEST_PNG")));
    m_svg.Attach(CreateSvgFromResId(_T("SVG:TEST_SVG")));
}

void CMainDlg::ApplyImage()
{
    SImageWnd *pImg = sobj_cast<SImageWnd>(GetRoot()->FindChildByName(L"img_view"));
    if (!pImg)
        return;
    if (m_bShowSvg && m_svg)
        pImg->SetSvg(m_svg);
    else if (m_png)
        pImg->SetImage(m_png);
}

// ---------------------------------------------------------------------------
// animation demo -- value animators on the SOUI timeline
// ---------------------------------------------------------------------------
void CMainDlg::StartAnimation()
{
    if (m_pMove)
    { // horizontal sweep 14..260 px, ping-pong, forever
        m_moveAni.Attach(new SIntAnimator());
        m_moveAni->setRange(14, 260);
        m_moveAni->setDuration(1200);
        m_moveAni->setRepeatCount(-1);
        m_moveAni->setRepeatMode(REVERSE);
        m_moveAni->addUpdateListener(this);
        m_moveAni->start(m_pMove->GetContainer());
    }

    if (m_pFade)
    { // alpha 40..255, ping-pong, forever
        m_fadeAni.Attach(new SByteAnimator());
        m_fadeAni->setRange(40, 255);
        m_fadeAni->setDuration(900);
        m_fadeAni->setRepeatCount(-1);
        m_fadeAni->setRepeatMode(REVERSE);
        m_fadeAni->addUpdateListener(this);
        m_fadeAni->start(m_pFade->GetContainer());
    }
}

void CMainDlg::onAnimationUpdate(IValueAnimator *pAnimator)
{
    if (m_bAnimFrozen)
        return; // keep producing values, just stop drawing them

    if (pAnimator == m_moveAni && m_pMove)
    {
        CRect rc;
        m_pMove->GetWindowRect(&rc);
        rc.OffsetRect(m_moveAni->getValue() - rc.left, 0);
        m_pMove->Move(&rc); // Move() floats the window out of the layout flow
    }
    else if (pAnimator == m_fadeAni && m_pFade)
    {
        m_pFade->SetAlpha((BYTE)m_fadeAni->getValue());
    }
}

// ---------------------------------------------------------------------------
// combobox demo: the row picks the animator durations
// ---------------------------------------------------------------------------
void CMainDlg::OnCbxSpeedChange(IEvtArgs *e)
{
    EventCBSelChange *evt = sobj_cast<EventCBSelChange>(e);
    if (!evt)
        return;
    switch (evt->nCurSel)
    {
    case 0:
        SetAnimSpeed(2400, 1800);
        break;
    case 2:
        SetAnimSpeed(600, 450);
        break;
    default:
        SetAnimSpeed(1200, 900);
        break;
    }
}

void CMainDlg::SetAnimSpeed(int nSweepMs, int nFadeMs)
{
    if (m_moveAni)
        m_moveAni->setDuration(nSweepMs);
    if (m_fadeAni)
        m_fadeAni->setDuration(nFadeMs);
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------
void CMainDlg::OnClose()
{
    SNativeWnd::DestroyWindow();
}

void CMainDlg::OnMinimize()
{
    // no task bar on the panel; keep the window as-is
}

void CMainDlg::OnSwitchImage()
{
    m_bShowSvg = !m_bShowSvg;
    ApplyImage();
}

void CMainDlg::OnToggleAnim()
{
    m_bAnimFrozen = !m_bAnimFrozen;
}

// ---------------------------------------------------------------------------
// entry used by app_main.cpp
// ---------------------------------------------------------------------------
HWND soui_demo_create_window(SApplication *pApp)
{
    (void)pApp;

    s_pDlg = new CMainDlg();
    HWND hWnd = s_pDlg->Create(NULL, 0, 0, 320, 240);
    if (!hWnd)
    {
        delete s_pDlg;
        s_pDlg = NULL;
        return NULL;
    }
    // WM_INITDIALOG is what runs OnInitDialog -> images + animators.
    s_pDlg->SendMessage(WM_INITDIALOG);
    s_pDlg->ShowWindow(SW_SHOWNORMAL);
    UpdateWindow(hWnd); // synchronous WM_PAINT -> fb -> present callback
    return hWnd;
}
