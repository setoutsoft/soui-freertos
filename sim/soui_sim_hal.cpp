/*
 * hal.cpp -- SDL2 window/present/input bridge (see hal.h).
 *
 * Include order matters: SDL.h comes first and pulls the *system* windows.h.
 * The swinx windows.h shim is NOT included here; the few swinx entry points
 * this file needs are self-declared below with host-native types.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// --- self-declared swinx freertos-platform entry points --------------------
// framebuffer.h symbols are extern "C" (ARGB32 premultiplied, native uint32)
extern "C" {
uint32_t *SwinxFbBits(void);
int SwinxFbWidth(void);
int SwinxFbHeight(void);
int SwinxFbStride(void);
void SwinxSetFbPresentCb(void (*cb)(const void *dirty));
}

// input.h symbols are plain C++ (no extern "C" there), so the declarations
// below have to produce the same mangled names.  input.h itself cannot be
// included (it pulls the swinx <windows.h> shim, and SDL.h above has
// already brought in the real one), so repeat its signatures -- including
// the handle type: swinx models HWND as an opaque `struct HWND__ *` to
// match the Windows SDK, and declaring it as an integer no longer links.
struct HWND__;
typedef struct HWND__ * HWND;
void SwinxInjectMouse(HWND hWnd, unsigned int message, int x, int y, unsigned long long fwKeys);
void SwinxInjectKey(HWND hWnd, unsigned int message, unsigned int vk, unsigned int scanCode);

// Win32 message ids used for injection (values from the swinx shim)
static const unsigned int WM_MOUSEMOVE = 0x0200;
static const unsigned int WM_LBUTTONDOWN = 0x0201;
static const unsigned int WM_LBUTTONUP = 0x0202;
static const unsigned int WM_RBUTTONDOWN = 0x0204;
static const unsigned int WM_RBUTTONUP = 0x0205;
static const unsigned int WM_KEYDOWN = 0x0100;
static const unsigned int WM_KEYUP = 0x0101;
static const unsigned long long MK_LBUTTON = 0x0001;
static const unsigned long long MK_RBUTTON = 0x0002;

// ---------------------------------------------------------------------------
// SDL keysym -> Win32 virtual-key code
//
// SDL's `keysym.sym` is a *unicode-ish* code, not a VK: it only agrees with
// Win32 for ASCII letters/digits, and everything else sits above
// SDLK_SCANCODE_MASK (0x40000000).  The old code passed `sym & 0xFF`, so
//   SDLK_UP   (0x40000052) -> 0x52 'R'
//   SDLK_DOWN (0x40000051) -> 0x51 'Q'
//   SDLK_LEFT (0x40000050) -> 0x50 'P'
//   SDLK_DELETE(0x7F)      -> 0x7F (VK_DELETE is 0x2E)
// and the app only ever saw letters: Tab worked (SDLK_TAB == VK_TAB == 9) while
// the arrow keys turned into type-ahead characters, which is exactly the
// "combobox does not react to up/down" symptom.
//
// Punctuation needs the same treatment for the opposite reason: SDLK_COMMA is
// ',' (0x2C) but the WM_CHAR translator keys off VK_OEM_COMMA (0xBC); passing
// the ASCII value would produce no character at all.
// ---------------------------------------------------------------------------
static unsigned int SdlSymToVk(SDL_Keycode sym)
{
    // letters: VK_A..VK_Z share the ASCII values, SDL reports them lowercased
    if (sym >= SDLK_a && sym <= SDLK_z)
        return (unsigned int)('A' + (sym - SDLK_a));
    if (sym >= SDLK_0 && sym <= SDLK_9)
        return (unsigned int)('0' + (sym - SDLK_0));
    if (sym >= SDLK_F1 && sym <= SDLK_F12)
        return (unsigned int)(0x70 + (sym - SDLK_F1)); // VK_F1..VK_F12

    switch (sym)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:   return 0x0D; // VK_RETURN
    case SDLK_ESCAPE:     return 0x1B;
    case SDLK_BACKSPACE:  return 0x08;
    case SDLK_TAB:        return 0x09;
    case SDLK_SPACE:      return 0x20;
    case SDLK_DELETE:     return 0x2E;
    case SDLK_INSERT:     return 0x2D;
    case SDLK_HOME:       return 0x24;
    case SDLK_END:        return 0x23;
    case SDLK_PAGEUP:     return 0x21;
    case SDLK_PAGEDOWN:   return 0x22;
    case SDLK_UP:         return 0x26;
    case SDLK_DOWN:       return 0x28;
    case SDLK_LEFT:       return 0x25;
    case SDLK_RIGHT:      return 0x27;
    case SDLK_CAPSLOCK:   return 0x14; // VK_CAPITAL
    case SDLK_NUMLOCKCLEAR: return 0x90;
    case SDLK_SCROLLLOCK: return 0x91;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:     return 0x10; // VK_SHIFT (VK_LSHIFT/RSHIFT also work,
                                       // but GetKeyState(VK_SHIFT) is what SOUI asks)
    case SDLK_LCTRL:
    case SDLK_RCTRL:      return 0x11;
    case SDLK_LALT:
    case SDLK_RALT:       return 0x12;
    // punctuation -> the OEM VK the WM_CHAR translator knows
    case SDLK_SEMICOLON:  return 0xBA; // VK_OEM_1      ;:
    case SDLK_EQUALS:     return 0xBB; // VK_OEM_PLUS   =+
    case SDLK_COMMA:      return 0xBC; // VK_OEM_COMMA  ,<
    case SDLK_MINUS:      return 0xBD; // VK_OEM_MINUS  -_
    case SDLK_PERIOD:     return 0xBE; // VK_OEM_PERIOD .>
    case SDLK_SLASH:      return 0xBF; // VK_OEM_2      /?
    case SDLK_BACKQUOTE:  return 0xC0; // VK_OEM_3      `~
    case SDLK_LEFTBRACKET:  return 0xDB; // VK_OEM_4    [{
    case SDLK_BACKSLASH:    return 0xDC; // VK_OEM_5    \|
    case SDLK_RIGHTBRACKET: return 0xDD; // VK_OEM_6    ]}
    case SDLK_QUOTE:        return 0xDE; // VK_OEM_7    '"
    default:
        break;
    }
    return 0; // not mapped: do not inject a bogus key
}

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture *g_tex = NULL;
static HWND  g_targetHwnd = NULL;

// fb snapshot written by the present callback (same thread as the UI pump),
// consumed by SimHalRenderIfDirty on the next pump tick.  Size must match
// the swinx freertos framebuffer defaults (no shim headers allowed here).
#define SIM_FB_W 320
#define SIM_FB_H 240
static uint32_t g_snap[SIM_FB_W * SIM_FB_H];
static volatile int g_dirty = 0;
static int g_presents = 0; // commitCanvas -> present callback count

static int g_fbW = 320;
static int g_fbH = 240;

static void fb_present_cb(const void *dirty)

{
    (void)dirty;
    memcpy(g_snap, SwinxFbBits(), sizeof(uint32_t) * g_fbW * g_fbH);
    g_dirty = 1;
    ++g_presents;
    if (g_presents == 1)
        printf("[sim] first frame presented\n");

    // Optional headless check: SOUI_SIM_DUMP_FRAME=<n> writes the n-th
    // presented frame to sim_frame.bmp so a CI run can assert on real pixels
    // (the QEMU target dumps a PPM the same way).  Unset = no effect.
    //
    // This must happen HERE, not in SimHalRenderIfDirty(): one UpdateWindow
    // may call commitCanvas/present several times (each ReleaseDC blits once)
    // before the next pump tick, so checking g_presents later with "==" can
    // skip the requested frame forever.  The callback is 1:1 with a present.
    static int s_dumpAt = -1;
    if (s_dumpAt < 0)
    {
        const char *pEnv = getenv("SOUI_SIM_DUMP_FRAME");
        s_dumpAt = pEnv ? atoi(pEnv) : 0;
        printf("[sim] dumpAt=%d (SOUI_SIM_DUMP_FRAME=%s)\n",
               s_dumpAt, pEnv ? pEnv : "(unset)");
    }
    if (s_dumpAt > 0 && g_presents == s_dumpAt)
    {
        SDL_Surface *pSurf = SDL_CreateRGBSurfaceWithFormatFrom(
            g_snap, g_fbW, g_fbH, 32, g_fbW * sizeof(uint32_t), SDL_PIXELFORMAT_ARGB8888);
        if (pSurf)
        {
            if (SDL_SaveBMP(pSurf, "sim_frame.bmp") == 0)
                printf("[sim] frame #%d dumped to sim_frame.bmp\n", s_dumpAt);
            else
                printf("[sim] frame #%d dump failed: %s\n", s_dumpAt, SDL_GetError());
            SDL_FreeSurface(pSurf);
        }
    }
}

int SimHalInit(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("[sim] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    g_fbW = SwinxFbWidth();
    g_fbH = SwinxFbHeight();

    g_win = SDL_CreateWindow("SOUI5 on FreeRTOS (Windows simulator)",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             g_fbW * 2, g_fbH * 2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_win)
    {
        printf("[sim] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren)
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren)
    {
        printf("[sim] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(g_ren, g_fbW, g_fbH);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, g_fbW, g_fbH);
    if (!g_tex)
    {
        printf("[sim] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }
    SwinxSetFbPresentCb(fb_present_cb);
    printf("[sim] SDL window ready (%dx%d fb, 2x window)\n", g_fbW, g_fbH);
    return 0;
}

void SimHalSetTargetHwnd(void *hWnd)
{
    g_targetHwnd = (HWND )hWnd;
}

void SimHalPumpEvents(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
        case SDL_QUIT:
            printf("[sim] SDL_QUIT -- exiting\n");
            fflush(stdout);
            // _exit(): terminate without running C++ static destructors.  The
            // FreeRTOS-backed std::mutex objects (swinx stl/mutex shim) would
            // otherwise delete their semaphore in their destructor and call
            // vPortExitCritical() on the already-stopped scheduler, tripping
            // the Win32 port's configASSERT( xPortRunning ).  The kernel
            // (FreeRTOS-Kernel/portable/MSVC-MingW/port.c) is vendored and
            // intentionally left unmodified, so the teardown fix lives here.
            _exit(0);
            break;
        case SDL_MOUSEMOTION:
            SwinxInjectMouse(g_targetHwnd, WM_MOUSEMOVE, ev.motion.x, ev.motion.y,
                             (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) ? MK_LBUTTON : 0);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            unsigned int msg = 0;
            unsigned long long fw = 0;
            if (ev.button.button == SDL_BUTTON_LEFT)
            {
                msg = (ev.type == SDL_MOUSEBUTTONDOWN) ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                fw = MK_LBUTTON;
            }
            else if (ev.button.button == SDL_BUTTON_RIGHT)
            {
                msg = (ev.type == SDL_MOUSEBUTTONDOWN) ? WM_RBUTTONDOWN : WM_RBUTTONUP;
                fw = MK_RBUTTON;
            }
            if (msg)
                SwinxInjectMouse(g_targetHwnd, msg, ev.button.x, ev.button.y, fw);
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            // keysym.sym -> VK; the raw SDL value is NOT a VK (see SdlSymToVk)
            unsigned int vk = SdlSymToVk(ev.key.keysym.sym);
            if (vk)
            {
                SwinxInjectKey(g_targetHwnd,
                               (ev.type == SDL_KEYDOWN) ? WM_KEYDOWN : WM_KEYUP,
                               vk, (unsigned int)ev.key.keysym.scancode);
            }
            break;
        }
        default:
            break;
        }
    }
}

int SimHalRenderIfDirty(void)
{
    if (!g_dirty)
        return 0;
    g_dirty = 0;
    SDL_UpdateTexture(g_tex, NULL, g_snap, g_fbW * sizeof(uint32_t));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
    return 1;
}
/* Snapshot the swinx framebuffer to a .bmp.
 * Lets a headless run (SDL_VIDEODRIVER=dummy + SOUI_SIM_TESTUI) leave
 * inspectable pixels behind; the scripted scenarios in main.cpp call it
 * at the ticks where something should have changed on screen. */
int SimHalDumpNow(const char *path)
{
    SDL_Surface *pSurf = SDL_CreateRGBSurfaceWithFormatFrom(
        SwinxFbBits(), g_fbW, g_fbH, 32, g_fbW * sizeof(uint32_t), SDL_PIXELFORMAT_ARGB8888);
    if (!pSurf)
        return -1;
    int rc = SDL_SaveBMP(pSurf, path);
    SDL_FreeSurface(pSurf);
    printf("[sim] dump %s rc=%d\n", path, rc);
    return rc;
}
