/*
 * msftedit_win32_host.h -- force-included for richedit41 only when the
 * FreeRTOS build runs on a *Windows host* (MinGW), and only to satisfy the CRT.
 *
 * richedit41 is compiled the way linux/macos/android/ios/wasm compile it: with
 * _WIN32 undefined, so the portable halves of its "#ifdef _WIN32" blocks are
 * the ones that get built (see the long note in SouiFreeRTOS.cmake).  A MinGW
 * host is the one toolchain that cannot do that by itself, because its CRT
 * headers refuse to load:
 *
 *     _mingw.h:  #if !defined(_WIN32) && !defined(__CYGWIN__)
 *                #error Only Win32 target is supported!
 *
 * Eight more headers carry the same guard: _mingw_stdarg.h, share.h,
 * sys/types.h, sys/stat.h, sys/timeb.h, sys/utime.h, sys/locking.h, time.h.
 * __CYGWIN__ is not an alternative -- it flips the CRT into its Cygwin
 * branches and _CRTIMP / errno_t / wint_t / intptr_t all break.
 *
 * So: define _WIN32 just long enough for those headers (plus <malloc.h>, whose
 * MinGW half provides _msize/_aligned_malloc that swinx's malloc.h shim
 * forwards to; without it <stdlib.h> pulls swinx's POSIX half, which wants
 * memalign()), then drop the macro again.  Everything loaded inside is
 * include-guarded by then, so the rest of the translation unit -- richedit's
 * own code and its own headers -- sees exactly what the non-Windows builds see.
 *
 * Two rules when editing this file:
 *   * only CRT headers may be included while _WIN32 is defined, and
 *   * never <windows.h>.  It pulls <tchar.h>, which latches LPCTSTR to the
 *     narrow form because _UNICODE is not defined yet.  re41's _common.h
 *     defines _UNICODE itself (line 29), but only if it is read before
 *     <tchar.h>; that ordering is what lets this build skip -D_UNICODE and
 *     match the other platforms.
 */
#ifndef SOUI_FR_MSFTEDIT_WIN32_HOST_H_
#define SOUI_FR_MSFTEDIT_WIN32_HOST_H_

#define _WIN32 1
#include <malloc.h>
#include <_mingw.h>
#include <_mingw_stdarg.h>
#include <share.h>
#include <sys/locking.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/utime.h>
#include <time.h>
#undef _WIN32

#endif // SOUI_FR_MSFTEDIT_WIN32_HOST_H_
