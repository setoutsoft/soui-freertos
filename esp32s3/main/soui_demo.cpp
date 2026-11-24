/*
 * soui_demo.cpp -- the soui_sim demo, running on the ESP32-S3 panel.
 *
 * Body is <soui-freertos>/soui_sim/MainDlg.cpp with the SDL shell removed:
 * PNG/SVG image swap, two value animators (sweep + fade), a dropdown combobox
 * that retimes the animators, and the caption buttons.  What changed:
 *
 *   * image/svg resolve through SResProviderPE -> the windres COFF blob
 *     (GETRESPROVIDER->LoadImage / CreateSvgFromResId), i.e. the same path a
 *     Windows SOUI app takes.  PNG needs the imgdecoder-stb factory installed
 *     in app_main.cpp; SVG goes through nanosvg, which is part of SOUI core.
 *   * <edit> is the lightweight SEditLite (SOUI_EDIT_LITE) -- no richedit41 is
 *     linked into this firmware -- so it is a plain-text field with no IME.
 *     See README.md.
 */
#include "soui_demo.h"

#include <interface/SResProvider-i.h>
#include <control/SCmnCtrl.h> // SImageWnd::SetImage / SetSvg

#include "soui_board.h"      // soui_board_frame_count() for the fps readout

#include <esp_timer.h>

#include <cstdio>
#include <cstring>

using namespace SNS;

// Both animated-box modes (11.7 and 11.8) show the same LVGL load box; mode 2
// varies what the box draws, mode 3 varies two framework flags (see below).
#define SOUI_ANIM_SCAN (SOUI_FPS_SCAN_MODE == 2 || SOUI_FPS_SCAN_MODE == 3)

#if SOUI_FPS_ONLY
// ---------------------------------------------------------------------------
// Clip-free "fps only" window (A/B load for the rendering-path experiments)
// ---------------------------------------------------------------------------
// The original demo paints through SHostWnd::OnPrint -> SWindow::DispatchPaint,
// i.e. a window tree, per-window push/pop of clip state and 4~6 GDI draw calls
// per visible child.  To isolate the cost of that *structure* from the cost of
// the underlying GDI/cairo + framebuffer + SPI path, this window takes over
// WM_PAINT itself: it never calls OnPrint(), never builds a render target for a
// child, and never issues a single PushClipRect().  BeginPaint/EndPaint are
// kept, so the swinx canvas -> framebuffer -> present path is identical.
//
// What is drawn: a solid background fill over the dirty rect, then one line of
// text showing the same "finished WM_PAINTs / wall clock" fps the 3s console
// report uses.  The trailing frame counter changes every frame, so the pixels
// really are different on every repaint (no static-screen shortcut).
//
// Element-count scan: every 9s the number of INDEPENDENT visible elements per
// frame steps through 1/2/4/8/16/32/64.  Each element is its own FillRect()
// with a different sub-rectangle and a different brush -- i.e. a real extra
// draw call, not a repeated fill of one rect.  Everything stays inside the same
// fixed dirty rect, so the pixel count and the SPI flush time are constant:
// the only variable is how many separate drawing operations a frame performs.
// This answers "is the high fps just because too little is being drawn?".
// ---------------------------------------------------------------------------

// SOUI_FPS_SCAN_MODE == 1 -- "blank WM_PAINT, full-screen refresh" (11.6):
//   the question is "if OnPaint draws NOTHING, but every frame still refreshes
//   all 320x240 pixels, how high can fps go?".  swinx on FreeRTOS has NO
//   "nothing was drawn -> skip the commit" shortcut: EndPaint() always goes
//   ReleaseDC -> commitCanvas -> blitCanvas -> swinxFbPresent, and the area
//   submitted comes from wndObj->invalid.hRgn, not from any GDI call.  So
//   BeginPaint/EndPaint with zero drawing in between still blits the whole
//   window canvas into the RGB565 framebuffer and pushes 153600 bytes over
//   SPI.  Four rotating stages isolate where that time goes:
//
//     0 empty_full  : invalidate 320x240, 0 GDI calls       <- the question
//     1 empty_small : invalidate 200x28,  0 GDI calls       <- minus pixels
//     2 fill_full   : invalidate 320x240, 1 FillRect + text <- plus 2 GDI calls
//     3 direct_full : no WM_PAINT at all, swinxFbPresent(NULL) once per pump
//                     round -> pure framebuffer -> SPI, no canvas compositing
//
//   Stage 3 counts one push as one finished refresh round via
//   soui_board_frame_tick(), which is the same "completed refresh / wall
//   clock" quantity the LVGL firmware reports as fps; app_main therefore
//   derives fps from frame_count instead of from the WM_PAINT count.
// ---------------------------------------------------------------------------

// SOUI_FPS_SCAN_MODE == 2 -- "the LVGL load animation, rebuilt in SOUI" (11.7):
//   the LVGL build reports ~150fps while showing one 60x60 box that moves 2px
//   per frame inside x=0..180 at y=90, LV_OPA_70 (main.c:207-228, 271-278).
//   Its per-frame work is: dirty rect = old U new = 62x60 = 3720px, redraw
//   background + box inside that rect, flush ~7.4KB.  This mode reproduces
//   exactly that, with no window tree and no clip pushes:
//
//     tick: move x by +-2 (ping-pong 0..180), InvalidateRect(old),
//           InvalidateRect(new)  -> RGN_OR merges them into one 62x60 rect
//     paint: FillRect(old, bg)      -> erases the 2px trailing strip
//            FillRect(new, red) or AlphaBlend(opa=178) -> draws the box
//
//   That is two draw calls per frame, and the same 3720px of drawing and SPI
//   traffic the LVGL firmware does.  Stages:
//     0 anim60        : opaque box, 2 draw calls
//     1 anim60+opa70  : 70% alpha box via AlphaBlend, 2 draw calls (== LVGL)
//     2 anim60+text   : stage 1 plus one DrawText (the on-panel fps label)
//     3 anim60+nodelay: stage 1 with vTaskDelay(0) in the pump, matching
//                       LVGL's LVGL_LOOP_MS=0 (no fixed 1ms per round)
// ---------------------------------------------------------------------------

// SOUI_FPS_SCAN_MODE == 3 -- "what do the two framework fixes buy?" (11.8):
//   identical load to mode 2 stage 0 (opaque box, 2 draw calls, one 62x60
//   invalidate per frame).  A = region.cpp's exact dirty-region fast path
//   (SwinxSetRegionFastPath), B = the pump's fixed 1ms vTaskDelay dropped.
//   Four stages rotate so one flash yields the full 2x2:
//     0 A=off B=off -> must reproduce 11.7's 111fps (sanity check)
//     1 A=on  B=off -> isolates the InvalidateRect() saving
//     2 A=on  B=on  -> both
//     3 A=off B=on  -> isolates the pump saving
//   With B on, app_main still feeds IDLE1 one tick every 250ms: the 5s task
//   watchdog checks both idle tasks, and a pure vTaskDelay(0) loop starves
//   IDLE1 (~0.4% of the wall clock instead of 11%).
// ---------------------------------------------------------------------------

// 200x28 = 5600 px dirty rect.  This is the SAME per-frame pixel volume as the
// original demo (148 kpix/s / 26.4 fps ~= 5600 px/frame), so any fps
// difference between the two comes from draw calls, not from pixel throughput.
static const RECT kFullRect = { 0, 0, 320, 240 };
static const RECT kFpsTextRect = { 60, 106, 260, 134 };

// Element-count scan (SOUI_FPS_SCAN_MODE == 0, README 11.5)
static const int kElementStages[] = { 1, 2, 4, 8, 16, 32, 64 };
static const int kElementStageCount =
    (int)(sizeof(kElementStages) / sizeof(kElementStages[0]));

// Blank-paint scan (SOUI_FPS_SCAN_MODE == 1, README 11.6)
enum EBlankStage
{
    kEmptyFull   = 0,
    kEmptySmall  = 1,
    kFillFull    = 2,
    kDirectFull  = 3,
    kBlankStageCount
};
static const char *const kBlankStageName[kBlankStageCount] = {
    "empty_full(320x240,0 GDI)",
    "empty_small(200x28,0 GDI)",
    "fill_full(320x240,2 GDI)",
    "direct_full(no WM_PAINT)"
};

// Animated-box scan (SOUI_FPS_SCAN_MODE == 2, README 11.7).  Geometry copied
// from the LVGL load box so the two firmwares do identical work per frame.
#if SOUI_FPS_SCAN_MODE == 2
enum EAnimStage
{
    kAnimOpaque       = 0,
    kAnimOpa70        = 1,
    kAnimOpa70NoDelay = 2,
    kAnimStageCount
};
static const char *const kAnimStageName[kAnimStageCount] = {
    "anim60(2px,opaque,2 GDI)",
    "anim60(2px,opa70,2 GDI)",
    "anim60(2px,opa70,nodelay)"
};
#endif

// Optimisation A/B scan (SOUI_FPS_SCAN_MODE == 3, README 11.8).  Same box,
// always opaque, always two draw calls and one 62x60 invalidate per frame --
// the only variables are the two framework flags:
//   A = InvalidateRect()'s dirty-region fast path (region.cpp
//       add_rect_to_region_fast: exact single-rect cases, no region alloc)
//   B = the pump's fixed 1ms vTaskDelay removed (yield only, with a 1-tick
//       IDLE1 feed every 250ms so the 5s task watchdog stays quiet)
#if SOUI_FPS_SCAN_MODE == 3
enum EOptStage
{
    kOptBase      = 0, // A off, B off  -- must reproduce 11.7 stage 0 (111fps)
    kOptA         = 1, // A on,  B off
    kOptAB        = 2, // A on,  B on
    kOptB         = 3, // A off, B on
    kOptStageCount
};
static const char *const kOptStageName[kOptStageCount] = {
    "A=off B=off(baseline)",
    "A=on  B=off",
    "A=on  B=on",
    "A=off B=on"
};

// region.cpp exports these with C linkage (swinx/src/region.cpp).
extern "C" void SwinxSetRegionFastPath(int bEnable);
extern "C" int SwinxGetRegionFastPath(void);

static BOOL stageUsesFastPath(int nStage)
{
    return nStage == kOptA || nStage == kOptAB;
}
static BOOL stageUsesNoDelay(int nStage)
{
    return nStage == kOptAB || nStage == kOptB;
}
// Applied on every stage switch, so the two flags never leak across stages.
static void applyOptStage(int nStage)
{
    SwinxSetRegionFastPath(stageUsesFastPath(nStage) ? 1 : 0);
}
#endif

static const int kBoxSize = 60;
static const int kBoxY = 90;
static const int kBoxMaxX = 180;
static const int kBoxStep = 2;
static const int kOpa70 = 178; // LV_OPA_70 == 178/255

// The fps readout for the box stages sits below the box path (y=90..150) so
// the text and the box do not overwrite each other in stage 2.
static const RECT kAnimTextRect = { 40, 196, 280, 224 };

static int s_boxX = 0;
static int s_boxDir = kBoxStep;
static RECT s_oldRect; // previous box position, erased this frame
static RECT s_newRect; // current box position
static BOOL s_animInit = FALSE;

static const int kBrushCount = 8;
static const int64_t kStageUs = 9LL * 1000000LL; // 3 console windows per stage

static int s_stage = 0;
static int s_elements = 1;
static int64_t s_stageStartUs = 0;
static BOOL s_firstInvalidate = TRUE;
static BOOL s_bootFrame = TRUE;

// Time spent inside InvalidateRect() in the animated-box scan.  frame - paint
// is the whole inter-frame gap; this splits off the invalidate part of it.
static uint64_t s_invalUs = 0;
static uint32_t s_invalCount = 0;

uint64_t soui_fps_inval_us(void)
{
    return s_invalUs;
}
uint32_t soui_fps_inval_count(void)
{
    return s_invalCount;
}

// framebuffer.h is not on this target's public include path; the symbol is
// declared extern "C" in swinx/src/platform/freertos/framebuffer.h.
extern "C" void swinxFbPresent(const RECT *dirty);

int soui_fps_elements(void)
{
#if SOUI_FPS_SCAN_MODE == 0
    return s_elements;
#else
    return 0;
#endif
}

// Pump delay per round.  LVGL's aggressive scan uses vTaskDelay(0), i.e. only
// a yield, no fixed 1ms; the stages that remove it match that so the
// comparison is not biased by 1ms of scheduling per frame.
int soui_fps_pump_delay(void)
{
#if SOUI_FPS_SCAN_MODE == 2
    return s_stage == kAnimOpa70NoDelay ? 0 : 1;
#elif SOUI_FPS_SCAN_MODE == 3
    return stageUsesNoDelay(s_stage) ? 0 : 1;
#else
    return 1;
#endif
}

const char *soui_fps_scan_label(void)
{
    static char buf[96];
#if SOUI_FPS_SCAN_MODE == 0
    snprintf(buf, sizeof(buf), "elements=%d", s_elements);
#elif SOUI_FPS_SCAN_MODE == 1
    snprintf(buf, sizeof(buf), "blank mode=%d %s", s_stage, kBlankStageName[s_stage]);
#elif SOUI_FPS_SCAN_MODE == 2
    snprintf(buf, sizeof(buf), "anim mode=%d %s x=%d", s_stage,
             kAnimStageName[s_stage], s_boxX);
#else
    snprintf(buf, sizeof(buf), "opt mode=%d %s fp=%d x=%d", s_stage,
             kOptStageName[s_stage], SwinxGetRegionFastPath(), s_boxX);
#endif
    return buf;
}

void soui_fps_tick(HWND hWnd)
{
    const int64_t now = esp_timer_get_time();
    if (s_stageStartUs == 0)
        s_stageStartUs = now;
    if (now - s_stageStartUs >= kStageUs)
    {
        s_stageStartUs = now;
#if SOUI_FPS_SCAN_MODE == 0
        s_stage = (s_stage + 1) % kElementStageCount;
        s_elements = kElementStages[s_stage];
#elif SOUI_FPS_SCAN_MODE == 1
        s_stage = (s_stage + 1) % kBlankStageCount;
#elif SOUI_FPS_SCAN_MODE == 2
        s_stage = (s_stage + 1) % kAnimStageCount;
#else
        s_stage = (s_stage + 1) % kOptStageCount;
        applyOptStage(s_stage); // A/B flag follows the stage
#endif
    }

#if SOUI_FPS_SCAN_MODE == 3
    static BOOL s_optInit = FALSE;
    if (!s_optInit)
    { // stage 0 = both optimisations off, so the baseline is the real thing
        s_optInit = TRUE;
        applyOptStage(s_stage);
    }
#endif

#if SOUI_FPS_SCAN_MODE == 1
    if (s_stage == kDirectFull)
    {
        // No InvalidateRect -> no WM_PAINT at all.  Push the whole software
        // framebuffer straight to the LCD (on_present() reads a NULL dirty
        // rect as 0,0,320,240).  This is the ceiling for "just ship pixels":
        // no message dispatch, no canvas blit, no GDI state, no clip.
        swinxFbPresent(NULL);
        soui_board_frame_tick(); // one completed refresh round, same fps basis
        return;
    }
#endif

#if SOUI_ANIM_SCAN
    // One boot frame covers the whole panel (clears the previous firmware),
    // after that the box animation invalidates only old U new == 62x60 px.
    if (!s_animInit)
    {
        s_animInit = TRUE;
        SetRect(&s_newRect, s_boxX, kBoxY, s_boxX + kBoxSize, kBoxY + kBoxSize);
        s_oldRect = s_newRect;
        ::InvalidateRect(hWnd, &kFullRect, FALSE);
        return;
    }
    const RECT rcPrev = s_newRect;
    s_boxX += s_boxDir;
    if (s_boxX >= kBoxMaxX)
    {
        s_boxX = kBoxMaxX;
        s_boxDir = -kBoxStep;
    }
    else if (s_boxX <= 0)
    {
        s_boxX = 0;
        s_boxDir = kBoxStep;
    }
    SetRect(&s_newRect, s_boxX, kBoxY, s_boxX + kBoxSize, kBoxY + kBoxSize);
    s_oldRect = rcPrev;
    // One invalidate of the union bounding box (62x60) instead of two: RGN_OR
    // would merge the 58px overlap into exactly this rect anyway, so the
    // submitted pixels are identical while one CreateRectRgnIndirect +
    // CombineRgn + postMsg round is saved.  Timed, because InvalidateRect()
    // turned out to be a measurable part of the inter-frame gap -- and in
    // mode 3 the same timer shows what the region fast path takes off it.
    RECT rcUnion;
    rcUnion.left = s_oldRect.left < s_newRect.left ? s_oldRect.left : s_newRect.left;
    rcUnion.top = s_oldRect.top < s_newRect.top ? s_oldRect.top : s_newRect.top;
    rcUnion.right = s_oldRect.right > s_newRect.right ? s_oldRect.right : s_newRect.right;
    rcUnion.bottom = s_oldRect.bottom > s_newRect.bottom ? s_oldRect.bottom : s_newRect.bottom;
    const int64_t tInval = esp_timer_get_time();
    ::InvalidateRect(hWnd, &rcUnion, FALSE);
    s_invalUs += (uint64_t)(esp_timer_get_time() - tInval);
    ++s_invalCount;
    return;
#endif

    RECT rc;
    if (s_firstInvalidate)
    { // first frame must cover the whole panel (clear the previous firmware)
        rc = kFullRect;
        s_firstInvalidate = FALSE;
    }
#if SOUI_FPS_SCAN_MODE == 1
    else if (s_stage == kEmptySmall)
        rc = kFpsTextRect;
    else
        rc = kFullRect;
#else
    else
        rc = kFpsTextRect;
#endif
    ::InvalidateRect(hWnd, &rc, FALSE);
}

class CFpsWnd : public SNS::SHostWnd
{
  public:
    CFpsWnd()
        : SHostWnd(UIRES.LAYOUT.XML_MAINWND) // a valid root keeps OnCreate/OnSize happy
        , m_hBg(NULL)
        , m_hBox(NULL)
        , m_hdcBox(NULL)
        , m_hbmBox(NULL)
        , m_lastUs(0)
        , m_lastFrames(0)
        , m_fpsX10(0)
    {
        m_hBg = ::CreateSolidBrush(RGB(18, 22, 30));
        // Pre-created brushes: creating/destroying a brush inside the paint
        // loop would add an unrelated cost to every element.
        for (int i = 0; i < kBrushCount; ++i)
            m_hElem[i] = ::CreateSolidBrush(RGB(26 + i * 8, 46 + i * 5, 74 + i * 12));
#if SOUI_ANIM_SCAN
        // LV_PALETTE_RED (#F44336) as an opaque brush, plus a 60x60 scratch DC
        // painted solid red.  AlphaBlend() with SourceConstantAlpha=178 then
        // produces exactly LV_OPA_70, and both objects are created once.
        m_hBox = ::CreateSolidBrush(RGB(0xF4, 0x43, 0x36));
        m_hdcBox = ::CreateCompatibleDC(NULL);
        if (m_hdcBox)
        {
            m_hbmBox = ::CreateCompatibleBitmap(m_hdcBox, kBoxSize, kBoxSize);
            if (m_hbmBox)
            {
                ::SelectObject(m_hdcBox, m_hbmBox);
                RECT rcBox = { 0, 0, kBoxSize, kBoxSize };
                ::FillRect(m_hdcBox, &rcBox, m_hBox);
            }
        }
#endif
    }

    ~CFpsWnd()
    {
        if (m_hBg)
            ::DeleteObject(m_hBg);
        for (int i = 0; i < kBrushCount; ++i)
            if (m_hElem[i])
                ::DeleteObject(m_hElem[i]);
#if SOUI_ANIM_SCAN
        if (m_hBox)
            ::DeleteObject(m_hBox);
        if (m_hbmBox)
            ::DeleteObject(m_hbmBox);
        if (m_hdcBox)
            ::DeleteDC(m_hdcBox);
#endif
    }

  protected:
    BEGIN_MSG_MAP_EX(CFpsWnd)
        MSG_WM_ERASEBKGND(OnEraseBkgnd) // no hidden background erasing either
        MSG_WM_PAINT(OnPaintFps) // handled here: SHostWnd::OnPrint is NEVER reached
        CHAIN_MSG_MAP(SNS::SHostWnd)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(HDC) { return TRUE; }

    void OnPaintFps(HDC)
    {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(m_hWnd, &ps);
        if (dc)
        {
            // bText decides whether this frame also draws the fps readout.
            // In the "empty" stages (11.6) and the box stages (11.7) it stays
            // FALSE so the draw-call count is exactly the one under test.
            BOOL bText = FALSE;

#if SOUI_FPS_SCAN_MODE == 0
            // Element scan: N independent FillRects, each a distinct sub-rect
            // + a distinct pre-created brush, i.e. a genuine extra GDI call.
            // All lie inside the fixed dirty rect, so pixel volume and the SPI
            // flush never change with N.
            ::FillRect(dc, &ps.rcPaint, m_hBg);
            RECT rcBand = kFpsTextRect;
            rcBand.top = kFpsTextRect.bottom - 8; // bottom 8px strip
            const int bandW = rcBand.right - rcBand.left;
            for (int i = 0; i < s_elements; ++i)
            {
                RECT rc = rcBand;
                rc.left = rcBand.left + (int)((int64_t)i * bandW / s_elements);
                rc.right = rcBand.left + (int)((int64_t)(i + 1) * bandW / s_elements);
                ::FillRect(dc, &rc, m_hElem[i % kBrushCount]);
            }
            bText = TRUE;
#elif SOUI_FPS_SCAN_MODE == 1
            // One visible boot frame so the panel is not simply black.  After
            // that, the two "empty" stages issue ZERO GDI calls; the canvas
            // keeps the boot pixels and EndPaint() still commits the whole
            // invalid region, so the picture on the panel is static while the
            // full-screen blit + SPI push run at full speed.
            bText = s_bootFrame || (s_stage == kFillFull);
            if (bText)
                ::FillRect(dc, &ps.rcPaint, m_hBg);
            s_bootFrame = FALSE;
#else
            // Animated box: a boot frame paints the whole panel once, then
            // every frame is exactly two draw calls inside a 62x60 clip --
            // erase the 2px trailing strip, draw the box.  The clip comes from
            // invalid.hRgn, so FillRect() on the 60x60 rects only touches the
            // 3720 px that actually need to change.
            const BOOL bBoot = s_bootFrame;
            if (bBoot)
            {
                ::FillRect(dc, &kFullRect, m_hBg);
                s_bootFrame = FALSE;
            }
            else if (!IsRectEmpty(&s_oldRect))
            {
                ::FillRect(dc, &s_oldRect, m_hBg);
            }

#if SOUI_FPS_SCAN_MODE == 2
            if (s_stage == kAnimOpaque || !m_hdcBox)
#else
            if (TRUE) // mode 3: drawing is identical in all four stages, only
                      // the two framework flags differ, so it stays opaque
#endif
            {
                ::FillRect(dc, &s_newRect, m_hBox);
            }
            else
            {
                BLENDFUNCTION bf;
                bf.BlendOp = AC_SRC_OVER;
                bf.BlendFlags = 0;
                bf.SourceConstantAlpha = (BYTE)kOpa70;
                bf.AlphaFormat = 0;
                ::AlphaBlend(dc, s_newRect.left, s_newRect.top, kBoxSize, kBoxSize,
                             m_hdcBox, 0, 0, kBoxSize, kBoxSize, bf);
            }
            bText = bBoot; // only the boot frame draws text, so every steady
                           // frame is exactly two draw calls
#endif

            // fps bookkeeping: pure arithmetic, no GDI.  Kept in every stage
            // so the "0 draw calls" condition is not disturbed.
            const uint32_t frames = soui_board_frame_count();
            const int64_t now = esp_timer_get_time();
            if (!m_lastUs)
            {
                m_lastUs = now;
                m_lastFrames = frames;
            }
            else if (now - m_lastUs >= 500000)
            {
                const int fx10 = (int)((int64_t)(frames - m_lastFrames) * 10000000LL /
                                      (now - m_lastUs));
                m_fpsX10 = m_fpsX10 > 0 ? (m_fpsX10 * 7 + fx10 * 3) / 10 : fx10;
                m_lastFrames = frames;
                m_lastUs = now;
            }

            char buf[64];
#if SOUI_FPS_SCAN_MODE == 0
            snprintf(buf, sizeof(buf), "%d.%d fps  e=%d  #%u",
                     m_fpsX10 / 10, m_fpsX10 % 10, s_elements, (unsigned)frames);
#else
            snprintf(buf, sizeof(buf), "%d.%d fps  m=%d  #%u",
                     m_fpsX10 / 10, m_fpsX10 % 10, s_stage, (unsigned)frames);
#endif

            if (bText)
            {
                const COLORREF oldText = ::SetTextColor(dc, RGB(90, 230, 140));
                const int oldBk = ::SetBkMode(dc, TRANSPARENT);
#if SOUI_ANIM_SCAN
                RECT rc = kAnimTextRect; // below the box path, so both stay visible
#else
                RECT rc = kFpsTextRect;
#endif
                ::DrawTextA(dc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                ::SetBkMode(dc, oldBk);
                ::SetTextColor(dc, oldText);
            }
        }
        // Even with zero drawing above, EndPaint -> ReleaseDC -> commitCanvas
        // still blits invalid.hRgn and pushes it to the panel.
        ::EndPaint(m_hWnd, &ps);
    }

  private:
    HBRUSH m_hBg;
    HBRUSH m_hElem[kBrushCount];
    HBRUSH m_hBox;  // LV_PALETTE_RED, opaque
    HDC m_hdcBox;   // 60x60 scratch DC filled with m_hBox
    HBITMAP m_hbmBox;
    int64_t m_lastUs;
    uint32_t m_lastFrames;
    int m_fpsX10;
};

HWND soui_demo_create_window(SNS::SApplication *pApp)
{
    (void)pApp;
    static CFpsWnd *s_pFpsWnd = NULL;
    s_pFpsWnd = new CFpsWnd();
    HWND hWnd = s_pFpsWnd->Create(NULL, 0, 0, 320, 240);
    if (!hWnd)
    {
        delete s_pFpsWnd;
        s_pFpsWnd = NULL;
        return NULL;
    }
    s_pFpsWnd->ShowWindow(SW_SHOWNORMAL);
    soui_fps_tick(hWnd); // whole-screen invalidate for the first frame
    UpdateWindow(hWnd);  // synchronous WM_PAINT -> framebuffer -> present
    return hWnd;
}

#else // !SOUI_FPS_ONLY -- the original soui_sim demo

// swinx records the dialog as the app's main window; the pump in app_main.cpp
// drives it with PeekMessage + IMessageLoop::OnMsg.
static CMainDlg *s_pDlg = NULL;

// fps_label refresh period.  500ms is short enough to look live and long
// enough that the label's own repaint cannot be mistaken for a frame-rate
// problem.
static const UINT_PTR kFpsTimerId = 1;
static const UINT kFpsTimerMs = 500;

#if SOUI_KB_AUTOTEST
// Boot-time scripted finger test for the soft keyboard. Feeds the real
// touch queue (btn_kb -> q -> ABC -> A), then prints the edit text and
// dumps the framebuffer so the result can be checked over serial.
static const UINT_PTR kKbTestTimerId = 2;
static const UINT kKbTestTimerMs = 100;
#endif

CMainDlg::CMainDlg()
    : SHostWnd(UIRES.LAYOUT.XML_MAINWND)
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

    // Soft keyboard host + injected-text target (same component as soui_sim).
    m_softKb.Init(pRoot, L"softkb", GetHwnd(), pRoot->FindChildByName(L"edt_input"));

    LoadImages();
    ApplyImage();
    StartAnimation();

    SetTimer(kFpsTimerId, kFpsTimerMs);
#if SOUI_KB_AUTOTEST
    SetTimer(kKbTestTimerId, kKbTestTimerMs);
#endif
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
#if SOUI_KB_AUTOTEST
    if (idEvent == kKbTestTimerId)
    {
        static int s_tick = 0;
        const int tick = ++s_tick;
        int x = -1, y = -1, down = 0;
        switch (tick)
        {
        case 1:  x = 80;  y = 128; break;           // MOVE first, as in sim
        case 2:  x = 80;  y = 128; down = 1; break; // focus <edit>
        case 3:  x = 80;  y = 128; break;
        case 8: { // edit focus alone is expected to have opened the keyboard
            SWindow *pKb = GetRoot() ? GetRoot()->FindChildByName(L"softkb") : NULL;
            SWindow *pEdit = GetRoot() ? GetRoot()->FindChildByName(L"edt_input") : NULL;
            printf("[kbtest] edit focused=%d\n",
                   pEdit && pEdit->IsFocused() ? 1 : 0);
            printf("[kbtest] focus auto-show=%d\n",
                   pKb && pKb->IsVisible(TRUE) ? 1 : 0);
            break;
        }
        case 10: x = 40;  y = 155; down = 1; break; // q
        case 11: x = 40;  y = 155; break;
        case 14: x = 25;  y = 178; down = 1; break; // ABC -> upper
        case 15: x = 25;  y = 178; break;
        case 18: x = 60;  y = 178; down = 1; break; // A
        case 19: x = 60;  y = 178; break;
        case 23: {
            SWindow *pEdit = GetRoot() ? GetRoot()->FindChildByName(L"edt_input") : NULL;
            printf("[kbtest] edit text='%s'\n",
                   pEdit ? pEdit->GetWindowText().c_str() : "<no edit>");
            soui_board_dump_fb(2);
            soui_board_dump_rect(14, 116, 306, 140, 1);
            printf("[kbtest] DONE\n");
            break;
        }
        default: break;
        }
        if (x >= 0)
            soui_board_inject_touch(x, y, down);
        if (tick >= 24)
            KillTimer(kKbTestTimerId);
        return;
    }
#endif

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

void CMainDlg::OnToggleKeyboard()
{
    m_softKb.Toggle();
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

#endif // SOUI_FPS_ONLY
