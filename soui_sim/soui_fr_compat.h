/*
 * soui_fr_compat.h -- force-included compat header for the FreeRTOS build of
 * utilities + SOUI core.
 *
 * History: this header once compensated for missing transitive includes in
 * SOUI headers (<sdef.h> for SSingleton2.h, <helper/SAttrCracker.h> for
 * SAnimation.h); those were fixed upstream with explicit #includes (2026-10-04)
 * so the entries are gone.  What remains:
 *   * <cstdint> -- newlib does not transitively provide them the
 *     way MSVC/glibc toolchains do; other toolchain-wide gaps can be added
 *     here as they surface.
 *   * <swinx_stl.h> -- the FreeRTOS STL shims (<thread>/<mutex>/...) folded
 *     in here because CMake dedupes repeated "-include" options, so a second
 *     force-include would silently become a stray input file.
 */
#ifndef SWINX_FR_SOUI_COMPAT_H_
#define SWINX_FR_SOUI_COMPAT_H_


#include <swinx_stl.h>

#endif // SWINX_FR_SOUI_COMPAT_H_
