/*
 * MainDlg.cpp -- main window of the demo.
 */
#include "stdafx.h"
#include "MainDlg.h"

#include <control/SCmnCtrl.h> // SImageWnd::SetImage / SetSvg

CMainDlg::CMainDlg()
    : SHostWnd(UIRES.LAYOUT.XML_MAINWND)
    , m_bShowSvg(FALSE)
    , m_pMove(NULL)
    , m_pFade(NULL)
    , m_bAnimFrozen(FALSE)
{
}

CMainDlg::~CMainDlg()
{
}

BOOL CMainDlg::OnInitDialog(HWND /*wndFocus*/, LPARAM /*lInitParam*/)
{
    SWindow *pRoot = GetRoot();
    m_pMove = pRoot->FindChildByName(L"anim_box");
    m_pFade = pRoot->FindChildByName(L"fade_box");

    LoadImages();
    ApplyImage();
    StartAnimation();
    return 0;
}

// ---------------------------------------------------------------------------
// image demo: PNG and SVG both come from the PE resource blob
//
//   SApplication::LoadImage("TYPE:NAME") -- res provider chain + imgdecoder
//   CreateSvgFromResId("SVG:NAME")       -- nanosvg, built into SOUI core
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
//
// SHostWnd::OnTimelineRequestChanged() arms a 16ms pulse timer as soon as the
// first ITimelineHandler registers; ITimelineHandler::OnNextFrame() then
// advances every started animator and the update listener repaints through the
// normal Invalidate() path.  Nothing here polls or sleeps: if the timeline or
// the repaint path is broken, the pixels simply freeze.
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
// commands
// ---------------------------------------------------------------------------
void CMainDlg::OnClose()
{
    SNativeWnd::DestroyWindow();
}

void CMainDlg::OnMinimize()
{
    // the simulator has no task bar; keep the window as-is
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
