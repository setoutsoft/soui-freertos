/*
 * soui_idf_compat.h -- force-included preamble for every C++ translation unit
 * of the `soui` ESP-IDF component (and, via INCLUDE_DIRS, available to main).
 *
 * ESP-IDF counterpart of <soui-freertos>/cmake/soui_fr_compat.h, with two
 * deliberate differences:
 *
 *  * It does NOT include <swinx_stl.h>.  That header is written for the
 *    bare-metal ARM build, where -fno-exceptions is used and the standard
 *    concurrency headers are replaced by src/platform/freertos/stl/ shims.
 *    ESP-IDF ships a complete libstdc++ (and we enable
 *    CONFIG_COMPILER_CXX_EXCEPTIONS), so the real <mutex>/<thread>/... are
 *    used instead.  The stl/ directory is kept OFF the include path for the
 *    same reason -- see components/soui/CMakeLists.txt.
 *
 *  * SViewBase.cpp (the one upstream source that uses CRect before including
 *    anything that defines it) pulls in <souistd.h> through the opt-in
 *    SOUI_FR_INCLUDE_SOUISTD flag instead of a second "-include" compiler
 *    option.  CMake deduplicates repeated "-include" options and would turn
 *    the second header into a stray input file.
 */
#ifndef SOUI_IDF_COMPAT_H_
#define SOUI_IDF_COMPAT_H_

// LLONG_MIN / LLONG_MAX / ULLONG_MAX.
//
// GCC's own <limits.h> guards the ISO C99 long-long limits with
//   #if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
// and GCC does not define __STDC_VERSION__ when compiling C++ (that role
// belongs to __cplusplus), so a plain "#include <limits.h>" leaves the three
// macros undefined in C++ mode.  utilities/include/souicoll.h:110-119 needs
// them in SLimits<long long>/SLimits<unsigned long long> and includes nothing
// but <limits.h>, so every SOUI TU fails without this.
//
// <climits> covers exactly this (libstdc++'s forwarding header defines the
// macros itself, guarded by #ifndef), but it is a C++-library header and the
// GitHub workflow cannot resolve it in this project, so <limits.h> is pulled
// in and the definitions are spelled out below -- identical to libstdc++'s, so
// the values match a desktop build bit for bit.  Same block in
// <soui-freertos>/cmake/soui_fr_compat.h for the bare-metal ARM build.
#include <limits.h>

#ifndef LLONG_MIN
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1)
#endif
#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1)
#endif

// swinx_stl.h's container/string prelude, minus the exception and shim parts.
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <deque>
#include <unordered_map>
#include <utility>

#if defined(SOUI_FR_INCLUDE_SOUISTD)
#include <souistd.h>
#endif

#endif // SOUI_IDF_COMPAT_H_
