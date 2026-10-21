# =============================================================================
# swinx_test  --  swinx sync layer running on REAL FreeRTOS (QEMU lm3s6965evb)
#
# Compiles the swinx FreeRTOS sync bridge (src/platform/freertos/syncapi.cpp)
# plus a self-contained test that mirrors demos/fun_test/test_cs.cpp, links it
# against the real FreeRTOS kernel, and runs it under QEMU. This proves swinx's
# Win32 sync-compat API (CRITICAL_SECTION / SRWLOCK / INIT_ONCE) works on the
# actual FreeRTOS API path through the STL shims.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(swinx_freertos_sync_test C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# WCHAR_SIZE (consumed by swinx src/uniconv.h) from the build system: per-TU
# compiler builtin => 4 on arm-none-eabi (same as uniconv.h's fallback, kept
# for uniformity with soui_sim's MinGW host build where the builtin is 2).
add_compile_definitions(WCHAR_SIZE=__SIZEOF_WCHAR_T__)

set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel)
set(DEMO_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../demo)
# swinx source: submodule layout (soui4/soui-freertos/swinx_test) resolves
# ../..; standalone D:/work layout is the fallback.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../swinx)
    set(SWINX_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../../swinx)
else()
    set(SWINX_DIR   D:/work/soui4/swinx)
endif()

# Target board: "mps2" (QEMU mps2-an385, 4MB, fits the whole swinx module +
# cairo stack) or "lm3s" (QEMU lm3s6965evb, 256KB flash -- sync/winobjs tests
# only, too small for the full module link).
set(BOARD mps2 CACHE STRING "Target QEMU board (mps2|lm3s)")
if(BOARD STREQUAL "mps2")
    set(BOARD_UART   ${DEMO_DIR}/mps2/uart_mps2.c)
    set(BOARD_LD     ${DEMO_DIR}/mps2/mps2-an385.ld)
else()
    set(BOARD_UART   ${DEMO_DIR}/uart.c)
    set(BOARD_LD     ${DEMO_DIR}/lm3s6965.ld)
endif()

add_executable(swinx_test.elf
    ${DEMO_DIR}/startup.c
    ${DEMO_DIR}/syscalls.c
    ${BOARD_UART}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_syncobj.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_conn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_dump.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_common.cpp
    ${SWINX_DIR}/src/platform/freertos/syncapi.cpp
    ${SWINX_DIR}/src/platform/freertos/winobjs.cpp
    # ---- FreeRTOS kernel ----
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_3.c
)

target_link_libraries(swinx_test.elf swinx_core cairo_fr pixman_fr png_fr zlib_fr m)

# Include order matters:
#  * swinx/src/platform/freertos/stl BEFORE everything so <mutex>/<thread>/<condition_variable>
#    resolve to the FreeRTOS-backed shims on this platform only.
#  * board dir BEFORE demo so the board's FreeRTOSConfig.h (mps2: bigger heap) wins.
#  * swinx/include (ctypes.h etc.) and swinx/src/platform/freertos (freertos_sync.h).
#  * FreeRTOS include + port dirs come AFTER the demo dirs but BEFORE swinx/src:
#    FreeRTOS-Kernel headers must not be shadowed by swinx/src/list.h (Wine).
if(BOARD STREQUAL "mps2")
    set(BOARD_CFG_DIR ${DEMO_DIR}/mps2)
else()
    set(BOARD_CFG_DIR ${DEMO_DIR})
endif()
target_include_directories(swinx_test.elf BEFORE PRIVATE
    ${SWINX_DIR}/src/platform/freertos/stl
    ${SWINX_DIR}/src/platform/freertos/uuid
    ${BOARD_CFG_DIR}
    ${DEMO_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${SWINX_DIR}
    ${SWINX_DIR}/include
    ${SWINX_DIR}/src
    ${SWINX_DIR}/src/platform/freertos
    ${SWINX_DIR}/thirdparty/cairo/src
)

target_compile_definitions(swinx_test.elf PRIVATE
    SOUI_PLATFORM_FREERTOS
    SWINX_FR_DEFAULT_STACK_WORDS=8192
)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${BOARD_LD} -nostartfiles -Wl,--entry=Reset_Handler -Wl,--gc-sections")

# =============================================================================
# swinx_core -- swinx 本体编译迭代库（只归档不链接，EXCLUDE_FROM_ALL）
# 仿 mobile.cmake 模式：核心 src/*.cpp + cmnctl32；POSIX 系统API实现已迁移到
# src/platform/posix/（本工程不 GLOB 该目录），同名符号由
# src/platform/freertos/{syncapi,winobjs}.cpp（编在 exe 里）提供。
# =============================================================================
# src/platform/posix/ deliberately not globbed; only wgl.cpp needs excluding.
set(SWINX_CORE_EXCLUDES
    ${SWINX_DIR}/src/wgl.cpp          # GL/GLX 头依赖系统 GL（wgl.h include_next <GL/gl.h>）；FreeRTOS 无 GL 实现，符号无消费者
)
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
    ${SWINX_DIR}/src/platform/freertos/dlghelper.cpp)
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

# =============================================================================
# pixman / cairo -- vendored software rasterizer backing the gdi/cairo backend
# (image surface only: no freetype / fontconfig / png on FreeRTOS)
# =============================================================================
set(PIXMAN_DIR ${SWINX_DIR}/thirdparty/pixman/pixman)
set(CAIRO_DIR  ${SWINX_DIR}/thirdparty/cairo/src)

# pixman-version.h is a build-time generated header
set(PIXMAN_VERSION_MAJOR 0)
set(PIXMAN_VERSION_MINOR 46)
set(PIXMAN_VERSION_MICRO 5)
configure_file(${PIXMAN_DIR}/pixman-version.h.in ${CMAKE_CURRENT_BINARY_DIR}/gen/pixman-version.h @ONLY)

# cairo's config.h (cairoint.h includes it unconditionally).  Bare-metal
# newlib values: 32-bit int/long/pointer, 8-byte long long, uint64_t present,
# no pthread / no dlsym / no filesystem extras.
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
           pixman-region.c)  # template: included by pixman-region16/32/64f.c
    list(REMOVE_ITEM PIXMAN_SRCS ${PIXMAN_DIR}/${_f})
endforeach()
add_library(pixman_fr STATIC EXCLUDE_FROM_ALL ${PIXMAN_SRCS})
target_include_directories(pixman_fr PRIVATE
    ${PIXMAN_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_definitions(pixman_fr PRIVATE PIXMAN_NO_TLS PACKAGE=pixman)  # single-threaded render
target_compile_options(pixman_fr PRIVATE -w)

# vendored libpng + zlib: PNG support for cairo (memory-stream IO only, no
# filesystem) -- keeps the gdi backend's builtin images functional
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
# OS surface backends / templates / test files: not compiled on FreeRTOS.
# The image surface + png is the whole render path for this port.
set(CAIRO_EXCLUDE
    # freetype / quartz font backends
    cairo-ft-font.c cairo-quartz-font.c
    # OS surface backends
    cairo-quartz-image-surface.c cairo-quartz-surface.c
    cairo-pdf-interchange.c cairo-pdf-operators.c cairo-pdf-shading.c cairo-pdf-surface.c
    cairo-ps-surface.c cairo-svg-surface.c cairo-script-surface.c
    cairo-xcb-connection-core.c cairo-xcb-connection-render.c cairo-xcb-connection-shm.c
    cairo-xcb-connection.c cairo-xcb-resources.c cairo-xcb-screen.c cairo-xcb-shm.c
    cairo-xcb-surface-core.c cairo-xcb-surface-render.c cairo-xcb-surface.c
    cairo-xlib-core-compositor.c cairo-xlib-display.c cairo-xlib-fallback-compositor.c
    cairo-xlib-render-compositor.c cairo-xlib-screen.c cairo-xlib-source.c
    cairo-xlib-surface-shm.c cairo-xlib-surface.c cairo-xlib-visual.c cairo-xlib-xcb-surface.c
    # templates included by other .c files
    cairo-image-mask-compositor.c
    # freetype-only glyph renderers (pull cairo-ft.h, unreferenced without ft)
    cairo-svg-glyph-render.c cairo-colr-glyph-render.c
    # font subsetting: only used by the pdf/ps exporters
    cairo-cff-subset.c cairo-truetype-subset.c cairo-type1-fallback.c cairo-type1-subset.c
    cairo-type1-glyph-names.c cairo-type3-glyph-surface.c cairo-scaled-font-subsets.c
    cairo-paginated-surface.c cairo-deflate-stream.c
    # misc not used here
    cairo-tee-surface.c
    test-base-compositor-surface.c test-compositor-surface.c
    test-null-compositor-surface.c test-paginated-surface.c)
foreach(_f ${CAIRO_EXCLUDE})
    list(REMOVE_ITEM CAIRO_SRCS ${CAIRO_DIR}/${_f})
endforeach()
add_library(cairo_fr STATIC EXCLUDE_FROM_ALL ${CAIRO_SRCS})
target_include_directories(cairo_fr PRIVATE
    ${CAIRO_DIR}
    ${SWINX_DIR}/src/platform/freertos    # cairo-features.h stub (image+png)
    ${SWINX_DIR}/thirdparty/libpng
    ${SWINX_DIR}/thirdparty/zlib
    ${PIXMAN_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_options(cairo_fr PRIVATE -w)

# gdi/cairo backend (without the fontconfig/freetype font stack) + the
# FreeRTOS font bridge that completes the FontFallback.h surface
set(SWINX_GDI_SRCS
    ${SWINX_DIR}/src/gdi/cairo/gdi.cpp
    ${SWINX_DIR}/src/gdi/cairo/drawtext.cpp
    ${SWINX_DIR}/src/gdi/cairo/cairo_show_text2.cpp
    ${SWINX_DIR}/src/gdi/cairo/builtin_image.cpp
    ${SWINX_DIR}/src/gdi/freertos/textbridge.cpp)
target_sources(swinx_core PRIVATE ${SWINX_GDI_SRCS})

# Reuse the cross toolchain (arm-none-eabi-gcc, -DSOUI_FREERTOS_REAL, cortex-m3).
# Pass it on the command line: -DCMAKE_TOOLCHAIN_FILE=../demo/cmake/toolchain-arm-freertos.cmake
