# =============================================================================
# soui_fr -- SOUI core + utilities compile/link attempt on FreeRTOS (mps2-an385)
#
# Phase 1 probe: batch-compile the platform-neutral subset of
#   utilities/src/*  and  SOUI/src/{SApp,SouiFactory,core,res.mgr,helper,
#   layout,matrix,event,animation,valueAnimator}
# against swinx (already FreeRTOS-ported), then link a whole-archive ELF to
# enumerate every still-missing symbol.  Excluded up front: COM/OLE (activex,
# msaa/accessible), richedit, Android/OHOS resource providers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(soui_freertos_attempt C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# WCHAR_SIZE (consumed by swinx src/uniconv.h) from the build system: per-TU
# compiler builtin => 4 on arm-none-eabi (same as uniconv.h's fallback, kept
# for uniformity with soui_sim's MinGW host build where the builtin is 2).
add_compile_definitions(WCHAR_SIZE=__SIZEOF_WCHAR_T__)

# SOUI source tree: submodule layout (soui4/soui-freertos/soui_fr) resolves
# ../..; standalone D:/work layout is the fallback.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../SOUI)
    set(SOUI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
else()
    set(SOUI_ROOT     D:/work/soui4)
endif()
set(FREERTOS_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel)
set(DEMO_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../demo)
set(SWINX_DIR     ${SOUI_ROOT}/swinx)
set(UTIL_DIR      ${SOUI_ROOT}/utilities)
set(SOUI_DIR      ${SOUI_ROOT}/SOUI)

set(BOARD mps2 CACHE STRING "Target QEMU board (mps2)")
set(BOARD_UART   ${DEMO_DIR}/mps2/uart_mps2.c)
set(BOARD_LD     ${DEMO_DIR}/mps2/mps2-an385.ld)
set(BOARD_CFG_DIR ${DEMO_DIR}/mps2)

# ---------------------------------------------------------------------------
# executable: startup + stub main; whole-archive link to enumerate gaps
# ---------------------------------------------------------------------------
add_executable(soui_fr.elf
    ${DEMO_DIR}/startup.c
    ${DEMO_DIR}/syscalls.c
    ${BOARD_UART}
    ${CMAKE_CURRENT_SOURCE_DIR}/soui_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_res_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_3.c
)

target_link_libraries(soui_fr.elf
    -Wl,--whole-archive soui4_fr utilities4_fr rendergdi_fr -Wl,--no-whole-archive
    swinx_core cairo_fr pixman_fr png_fr zlib_fr m)

target_include_directories(soui_fr.elf BEFORE PRIVATE
    ${SWINX_DIR}/src/platform/freertos/stl
    ${SWINX_DIR}/src/platform/freertos/uuid
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BOARD_CFG_DIR})
target_include_directories(soui_fr.elf PRIVATE
    ${DEMO_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${SWINX_DIR}/include
    ${SWINX_DIR}/src
    ${SWINX_DIR}/src/platform/freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/gen
    ${SOUI_ROOT}
    ${SOUI_ROOT}/components
    ${SOUI_DIR}
    ${UTIL_DIR}/include
    ${SOUI_DIR}/include
    ${SOUI_DIR}/src)
target_compile_definitions(soui_fr.elf PRIVATE
    SOUI_FREERTOS_REAL
    SOUI_PLATFORM_FREERTOS
    SWINX_FR_DEFAULT_STACK_WORDS=8192)
target_compile_options(soui_fr.elf PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:soui_fr_compat.h>)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${BOARD_LD} -nostartfiles -Wl,--entry=Reset_Handler")
# SOUI_FR_CENSUS=ON  -> no --gc-sections: every whole-archive section kept,
#                       the link enumerates the FULL gap surface (fat ELF,
#                       >4MB: does not run under QEMU mps2, ZBT2/RAM alias)
# SOUI_FR_CENSUS=OFF -> --gc-sections: runnable ELF for the QEMU boot smoke
option(SOUI_FR_CENSUS "Full whole-archive census link (no gc-sections)" OFF)
if(NOT SOUI_FR_CENSUS)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif()

# ---------------------------------------------------------------------------
# utilities4_fr
# ---------------------------------------------------------------------------
set(UTIL_SRCS
    ${UTIL_DIR}/src/utilities.cpp
    ${UTIL_DIR}/src/soui_mem_wrapper.cpp
    ${UTIL_DIR}/src/gdialpha.cpp
    ${UTIL_DIR}/src/string/sstringa.cpp
    ${UTIL_DIR}/src/string/sstringdata.cpp
    ${UTIL_DIR}/src/string/sstringw.cpp
    ${UTIL_DIR}/src/string/strcpcvt.cpp
    ${UTIL_DIR}/src/xml/SXml.cpp
    ${UTIL_DIR}/src/pugixml/pugixml.cpp
    ${UTIL_DIR}/src/helper/SAutoBuf.cpp
    ${UTIL_DIR}/src/helper/SCriticalSection.cpp
    ${UTIL_DIR}/src/helper/SRwLock.cpp
    ${UTIL_DIR}/src/helper/SSemaphore.cpp
    ${UTIL_DIR}/src/helper/STls.cpp
    ${UTIL_DIR}/src/helper/slog.cpp
)
add_library(utilities4_fr STATIC EXCLUDE_FROM_ALL ${UTIL_SRCS})

# ---------------------------------------------------------------------------
# soui4_fr (phase-1 subset)
# ---------------------------------------------------------------------------
file(GLOB SOUI_CORE_SRCS   ${SOUI_DIR}/src/core/*.cpp)
file(GLOB SOUI_RESMGR_SRCS ${SOUI_DIR}/src/res.mgr/*.cpp)
file(GLOB SOUI_HELPER_SRCS ${SOUI_DIR}/src/helper/*.cpp)
file(GLOB SOUI_LAYOUT_SRCS ${SOUI_DIR}/src/layout/*.cpp)
file(GLOB SOUI_MATRIX_SRCS ${SOUI_DIR}/src/matrix/*.cpp)
file(GLOB SOUI_EVENT_SRCS  ${SOUI_DIR}/src/event/*.cpp)
file(GLOB SOUI_ANIM_SRCS   ${SOUI_DIR}/src/animation/*.cpp)
file(GLOB SOUI_VANIM_SRCS  ${SOUI_DIR}/src/valueAnimator/*.cpp)
file(GLOB SOUI_CTRL_SRCS   ${SOUI_DIR}/src/control/*.cpp)
file(GLOB SOUI_AX_SRCS     ${SOUI_DIR}/src/activex/*.cpp)

set(SOUI_EXCLUDES
    # msaa / COM accessibility (oleacc, VARIANT marshalling)
    ${SOUI_DIR}/src/core/SwndAccessible.cpp
    # OHOS/Android resource providers (NDK APIs)
    ${SOUI_DIR}/src/res.mgr/SResProviderAndroidAsset.cpp
    ${SOUI_DIR}/src/res.mgr/SResProviderOhosRawFile.cpp
    # OHOS NAPI app glue (hilog / ace_ndk)
    ${SOUI_DIR}/src/helper/SOhosApp.cpp
)
foreach(_f ${SOUI_EXCLUDES})
    foreach(_lst SOUI_CORE_SRCS SOUI_RESMGR_SRCS SOUI_HELPER_SRCS
                 SOUI_LAYOUT_SRCS SOUI_MATRIX_SRCS SOUI_EVENT_SRCS
                 SOUI_ANIM_SRCS SOUI_VANIM_SRCS SOUI_CTRL_SRCS SOUI_AX_SRCS)
        list(REMOVE_ITEM ${_lst} ${_f})
    endforeach()
endforeach()

add_library(soui4_fr STATIC EXCLUDE_FROM_ALL
    ${SOUI_DIR}/src/SApp.cpp
    ${SOUI_DIR}/src/SouiFactory.cpp
    ${SOUI_CORE_SRCS}
    ${SOUI_RESMGR_SRCS}
    ${SOUI_HELPER_SRCS}
    ${SOUI_LAYOUT_SRCS}
    ${SOUI_MATRIX_SRCS}
    ${SOUI_EVENT_SRCS}
    ${SOUI_ANIM_SRCS}
    ${SOUI_VANIM_SRCS}
    ${SOUI_CTRL_SRCS}
    ${SOUI_AX_SRCS}
)

# SViewBase.cpp is the one upstream source that includes neither souistd.h
# nor any header defining CRect before use (upstream toolchains tolerate this
# via their own pre-include order); give it the full pre-include here.
set_source_files_properties(${SOUI_DIR}/src/control/SViewBase.cpp PROPERTIES
    COMPILE_OPTIONS "-include;souistd.h")

# ---------------------------------------------------------------------------
# common compile settings (mirrors swinx_test)
# ---------------------------------------------------------------------------
foreach(tgt utilities4_fr soui4_fr)
    target_include_directories(${tgt} BEFORE PRIVATE
        ${SWINX_DIR}/src/platform/freertos/stl
        ${SWINX_DIR}/src/platform/freertos/uuid
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${BOARD_CFG_DIR})
    target_include_directories(${tgt} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gen
        ${SWINX_DIR}/include
        ${SWINX_DIR}/src
        ${SWINX_DIR}/src/platform/freertos
        ${SOUI_ROOT}
        ${SOUI_ROOT}/components
        ${SOUI_DIR}
        ${UTIL_DIR}/include
        ${SOUI_DIR}/include
        ${SOUI_DIR}/src
        ${SOUI_ROOT}/third-part/nanosvg)
    target_compile_definitions(${tgt} PRIVATE
        SOUI_FREERTOS_REAL
        SOUI_PLATFORM_FREERTOS
        SOUI_EXPORTS
        ENABLE_THUNK=0)
    target_compile_options(${tgt} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include>
        $<$<COMPILE_LANGUAGE:CXX>:soui_fr_compat.h>
        -w -fexceptions -Os)
endforeach()

# ---------------------------------------------------------------------------
# rendergdi_fr (components/render-gdi, linux/cairo variant)
# ---------------------------------------------------------------------------
add_library(rendergdi_fr STATIC EXCLUDE_FROM_ALL
    ${SOUI_ROOT}/components/render-gdi/linux/render-gdi.cpp
    ${SOUI_ROOT}/components/render-gdi/GradientFillHelper.cpp)
target_include_directories(rendergdi_fr BEFORE PRIVATE
    ${SWINX_DIR}/src/platform/freertos/stl
    ${SWINX_DIR}/src/platform/freertos/uuid
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BOARD_CFG_DIR})
target_include_directories(rendergdi_fr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gen
    ${SWINX_DIR}/include
    ${SWINX_DIR}/src
    ${SWINX_DIR}/src/platform/freertos
    ${SOUI_ROOT}
    ${SOUI_ROOT}/components
    ${SOUI_ROOT}/components/render-gdi
    ${SOUI_ROOT}/components/render-gdi/linux
    ${SOUI_ROOT}/third-part/nanosvg
    ${SOUI_DIR}
    ${UTIL_DIR}/include
    ${SOUI_DIR}/include
    ${SOUI_DIR}/src)
target_compile_definitions(rendergdi_fr PRIVATE
    SOUI_FREERTOS_REAL
    SOUI_PLATFORM_FREERTOS
    SOUI_EXPORTS
    ENABLE_THUNK=0)
target_compile_options(rendergdi_fr PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:soui_fr_compat.h>
    -w -fexceptions -Os)

# ---------------------------------------------------------------------------
# swinx_core + cairo stack (same recipe as swinx_test/CMakeLists.txt)
# ---------------------------------------------------------------------------
# POSIX-backed system-API implementations live in src/platform/posix/ (not
# globbed here); only wgl.cpp needs an explicit exclude (GL headers).
set(SWINX_CORE_EXCLUDES
    ${SWINX_DIR}/src/wgl.cpp)   # GL/GLX 头依赖系统 GL；FreeRTOS 无 GL 实现，符号无消费者
file(GLOB SWINX_CORE_SRCS ${SWINX_DIR}/src/*.cpp)
foreach(_f ${SWINX_CORE_EXCLUDES})
    list(REMOVE_ITEM SWINX_CORE_SRCS ${_f})
endforeach()
file(GLOB SWINX_CMNCTL_SRCS ${SWINX_DIR}/src/cmnctl32/*.cpp ${SWINX_DIR}/src/cmnctl32/*.c)
set(SWINX_FREERTOS_PLATFORM_SRCS
    ${SWINX_DIR}/src/platform/freertos/SConnection.cpp
    ${SWINX_DIR}/src/platform/freertos/atoms.cpp
    ${SWINX_DIR}/src/platform/freertos/framebuffer.cpp
    ${SWINX_DIR}/src/platform/freertos/sysbridge.cpp
    ${SWINX_DIR}/src/platform/freertos/input.cpp
    ${SWINX_DIR}/src/platform/freertos/dlfreertos.cpp
    ${SWINX_DIR}/src/platform/freertos/dlghelper.cpp
    ${SWINX_DIR}/src/platform/freertos/syncapi.cpp
    ${SWINX_DIR}/src/platform/freertos/winobjs.cpp)
add_library(swinx_core STATIC EXCLUDE_FROM_ALL
    ${SWINX_CORE_SRCS} ${SWINX_CMNCTL_SRCS} ${SWINX_FREERTOS_PLATFORM_SRCS})
target_include_directories(swinx_core BEFORE PRIVATE
    ${SWINX_DIR}/src/platform/freertos/stl
    ${SWINX_DIR}/src/platform/freertos/uuid
    ${BOARD_CFG_DIR})
target_include_directories(swinx_core PRIVATE
    ${SWINX_DIR}
    ${SWINX_DIR}/include
    ${SWINX_DIR}/src
    ${SWINX_DIR}/src/platform/freertos
    ${SWINX_DIR}/src/gdi/cairo
    ${SWINX_DIR}/thirdparty/cairo/src
    ${SWINX_DIR}/thirdparty/libpng
    ${SWINX_DIR}/thirdparty/zlib
    ${DEMO_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3)
target_compile_definitions(swinx_core PRIVATE
    SOUI_PLATFORM_FREERTOS
    SWINX_FR_DEFAULT_STACK_WORDS=8192)
target_compile_options(swinx_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:swinx_stl.h>
    -w)

# gdi/cairo backend + freertos font bridge (same as swinx_test)
set(SWINX_GDI_SRCS
    ${SWINX_DIR}/src/gdi/cairo/gdi.cpp
    ${SWINX_DIR}/src/gdi/cairo/drawtext.cpp
    ${SWINX_DIR}/src/gdi/cairo/cairo_show_text2.cpp
    ${SWINX_DIR}/src/gdi/cairo/builtin_image.cpp
    ${SWINX_DIR}/src/gdi/freertos/textbridge.cpp)
target_sources(swinx_core PRIVATE ${SWINX_GDI_SRCS})

# ---------------------------------------------------------------------------
# pixman / zlib / png / cairo (same recipe as swinx_test)
# ---------------------------------------------------------------------------
set(PIXMAN_DIR ${SWINX_DIR}/thirdparty/pixman/pixman)
set(CAIRO_DIR  ${SWINX_DIR}/thirdparty/cairo/src)

set(PIXMAN_VERSION_MAJOR 0)
set(PIXMAN_VERSION_MINOR 46)
set(PIXMAN_VERSION_MICRO 5)
configure_file(${PIXMAN_DIR}/pixman-version.h.in ${CMAKE_CURRENT_BINARY_DIR}/gen/pixman-version.h @ONLY)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen/config.h" "\
/* config.h -- FreeRTOS (bare-metal newlib) build of vendored cairo */
#define CAIRO_NO_MUTEX 1
#define HAVE_GCC_LEGACY_ATOMICS 1
#define HAS_PIXMAN_GLYPHS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_TIME_H 1
#define HAVE_SETJMP_H 1
#define HAVE_UINT64_T 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 4
#define SIZEOF_VOID_P 4
#define PACKAGE_VERSION \"1.18.4\"
")

file(GLOB PIXMAN_SRCS ${PIXMAN_DIR}/pixman*.c)
foreach(_f pixman-mmx.c pixman-sse2.c pixman-ssse3.c pixman-vmx.c
           pixman-rvv.c pixman-mips-dspr2.c
           pixman-region.c)
    list(REMOVE_ITEM PIXMAN_SRCS ${PIXMAN_DIR}/${_f})
endforeach()
add_library(pixman_fr STATIC EXCLUDE_FROM_ALL ${PIXMAN_SRCS})
target_include_directories(pixman_fr PRIVATE
    ${PIXMAN_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_definitions(pixman_fr PRIVATE PIXMAN_NO_TLS PACKAGE=pixman)
target_compile_options(pixman_fr PRIVATE -w)

file(GLOB ZLIB_SRCS ${SWINX_DIR}/thirdparty/zlib/*.c)
foreach(_f example.c minigzip.c test/*.c)
    list(REMOVE_ITEM ZLIB_SRCS ${SWINX_DIR}/thirdparty/zlib/${_f})
endforeach()
add_library(zlib_fr STATIC EXCLUDE_FROM_ALL ${ZLIB_SRCS})
target_include_directories(zlib_fr PRIVATE ${SWINX_DIR}/thirdparty/zlib)
target_compile_options(zlib_fr PRIVATE -w)

file(GLOB PNG_SRCS ${SWINX_DIR}/thirdparty/libpng/*.c)
foreach(_f pngtest.c pngexample.c)
    list(REMOVE_ITEM PNG_SRCS ${SWINX_DIR}/thirdparty/libpng/${_f})
endforeach()
add_library(png_fr STATIC EXCLUDE_FROM_ALL ${PNG_SRCS})
target_include_directories(png_fr PRIVATE
    ${SWINX_DIR}/thirdparty/libpng
    ${SWINX_DIR}/thirdparty/zlib)
target_compile_options(png_fr PRIVATE -w)

file(GLOB CAIRO_SRCS ${CAIRO_DIR}/*.c)
set(CAIRO_EXCLUDE
    cairo-ft-font.c cairo-quartz-font.c
    cairo-quartz-image-surface.c cairo-quartz-surface.c
    cairo-pdf-interchange.c cairo-pdf-operators.c cairo-pdf-shading.c cairo-pdf-surface.c
    cairo-ps-surface.c cairo-svg-surface.c cairo-script-surface.c
    cairo-xcb-connection-core.c cairo-xcb-connection-render.c cairo-xcb-connection-shm.c
    cairo-xcb-connection.c cairo-xcb-resources.c cairo-xcb-screen.c cairo-xcb-shm.c
    cairo-xcb-surface-core.c cairo-xcb-surface-render.c cairo-xcb-surface.c
    cairo-xlib-core-compositor.c cairo-xlib-display.c cairo-xlib-fallback-compositor.c
    cairo-xlib-render-compositor.c cairo-xlib-screen.c cairo-xlib-source.c
    cairo-xlib-surface-shm.c cairo-xlib-surface.c cairo-xlib-visual.c cairo-xlib-xcb-surface.c
    cairo-image-mask-compositor.c
    cairo-svg-glyph-render.c cairo-colr-glyph-render.c
    cairo-cff-subset.c cairo-truetype-subset.c cairo-type1-fallback.c cairo-type1-subset.c
    cairo-type1-glyph-names.c cairo-type3-glyph-surface.c cairo-scaled-font-subsets.c
    cairo-paginated-surface.c cairo-deflate-stream.c
    cairo-tee-surface.c
    test-base-compositor-surface.c test-compositor-surface.c
    test-null-compositor-surface.c test-paginated-surface.c)
foreach(_f ${CAIRO_EXCLUDE})
    list(REMOVE_ITEM CAIRO_SRCS ${CAIRO_DIR}/${_f})
endforeach()
add_library(cairo_fr STATIC EXCLUDE_FROM_ALL ${CAIRO_SRCS})
target_include_directories(cairo_fr PRIVATE
    ${CAIRO_DIR}
    ${SWINX_DIR}/src/platform/freertos
    ${SWINX_DIR}/thirdparty/libpng
    ${SWINX_DIR}/thirdparty/zlib
    ${PIXMAN_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_options(cairo_fr PRIVATE -w)
