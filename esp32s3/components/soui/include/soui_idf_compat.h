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

#include <climits>
#include <cstdint>

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
