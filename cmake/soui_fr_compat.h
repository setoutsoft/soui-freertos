/*
 * soui_fr_compat.h -- force-included compat header for the FreeRTOS build of
 * utilities + SOUI core.
 *
 * History: this header once compensated for missing transitive includes in
 * SOUI headers (<sdef.h> for SSingleton2.h, <helper/SAttrCracker.h> for
 * SAnimation.h); those were fixed upstream with explicit #includes (2026-10-04)
 * so the entries are gone.  What remains:
 *   * the ISO C99 long-long limits (<limits.h> gives them in C but not in
 *     C++ on this toolchain) -- see below;
 *   * <swinx_stl.h> -- the FreeRTOS STL shims (<thread>/<mutex>/...) folded
 *     in here because CMake dedupes repeated "-include" options, so a second
 *     force-include would silently become a stray input file.
 *
 * Toolchain-wide gaps other than the ones spelled out here do not exist any
 * more; add new ones next to them rather than in the framework headers.
 */
#ifndef SWINX_FR_SOUI_COMPAT_H_
#define SWINX_FR_SOUI_COMPAT_H_

/*
 * LLONG_MIN / LLONG_MAX / ULLONG_MAX.
 *
 * GCC's own <limits.h> guards the ISO C99 part with
 *
 *     #if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
 *
 * and GCC does not define __STDC_VERSION__ when compiling C++ (that role
 * belongs to __cplusplus).  A plain "#include <limits.h>" therefore leaves
 * the three macros undefined in C++ mode, while newlib's <limits.h> only
 * provides them on the "no #include_next support" fallback path that GCC
 * never takes.  utilities/include/souicoll.h:110-119 uses all three in the
 * SLimits<long long>/SLimits<unsigned long long> specialisations and includes
 * nothing but <limits.h>, so every SOUI TU fails to compile here.
 *
 * <climits> covers exactly this (libstdc++'s forwarding header defines the
 * macros itself, guarded by #ifndef), but it is a C++-library header, and the
 * GitHub workflow cannot resolve it in this project.  So <limits.h> is pulled
 * in here and the same three definitions are spelled out below -- identical to
 * what libstdc++ does, so the values match a desktop build bit for bit.  The
 * #ifndef guards mean MSVC / glibc / MinGW hosts, where <limits.h> already
 * provides them, keep their own definitions.
 */
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

#include <swinx_stl.h>

#endif // SWINX_FR_SOUI_COMPAT_H_
