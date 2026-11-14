# =============================================================================
# soui_fr -- the same SOUI application as soui_sim, cross-compiled to bare
# metal (QEMU mps2-an385, Cortex-M3).
#
# The build system is the one soui_sim uses:
#
#   include(<soui-freertos>/cmake/soui.cmake)
#   soui_fr_project(<target> SOURCES ...)
#
# cmake/soui.cmake brings the whole environment in -- FreeRTOS kernel,
# swinx (the Win32 shim), SOUI core, render-gdi (cairo), imgdecoder-stb,
# cairo/pixman/libpng/zlib, every include dir and -D flag -- and picks the
# bare-metal combination (vendored cairo, ARM_CM3 port, no SDL host runtime,
# no richedit41) from the toolchain.  What stays in this file is only what is
# genuinely target-specific:
#
#   * the toolchain (arm-none-eabi, Cortex-M3) -- passed on the command line,
#     same as ../demo and ../swinx_test,
#   * the board: startup/syscalls/uart from ../demo plus the QEMU linker
#     script,
#   * the FreeRTOS kernel sources -- the framework only adds a kernel for host
#     builds (there it compiles the MSVC-MingW port); a bare-metal project
#     brings its own port and heap implementation,
#   * the framebuffer / semihosting driver, which lives inside soui_app.cpp.
#
# Configure and run:
#
#   cmake -S . -B build -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=../demo/cmake/toolchain-arm-freertos.cmake
#   cmake --build build
#   qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel build/soui_fr.elf
#
# The default build type is MinSizeRel (-Os): at -O2/-O3 the cairo stack alone
# pushes .text past the 4MB ZBT SSRAM of the board, which then HardFaults on
# the first overlapping access.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(project_name "soui_fr")
project(${project_name} C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# facts cmake/soui.cmake has to know *before* it is included
# -----------------------------------------------------------------------------
# FreeRTOSConfig.h: the framework probes ./config first, but this board shares
# its kernel configuration with ../demo and ../swinx_test, so point at it.
set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../demo/mps2")

# -----------------------------------------------------------------------------
# the FreeRTOS + swinx + SOUI environment (one include, like find_package)
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/soui.cmake)

if(NOT SOUI_FR_BARE_METAL)
    message(FATAL_ERROR
        "[soui_fr] ${project_name} is a bare-metal target, but this configure "
        "run picked the host toolchain.\n"
        "[soui_fr] Add -DCMAKE_TOOLCHAIN_FILE="
        "${CMAKE_CURRENT_SOURCE_DIR}/../demo/cmake/toolchain-arm-freertos.cmake")
endif()

set(DEMO_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../demo")
set(FREERTOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel")

# -----------------------------------------------------------------------------
# linker: board script, custom entry, dead-code elimination
# -----------------------------------------------------------------------------
# ../demo/startup.c provides Reset_Handler (CMSIS vector table -> entry());
# -nostartfiles keeps newlib's crt0 out of the image, --entry names the
# symbol because there is no crt0 to define _start.
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T${DEMO_DIR}/mps2/mps2-an385.ld -nostartfiles -Wl,--entry=Reset_Handler")

# SOUI_FR_CENSUS=ON  -> no --gc-sections: every whole-archive section is kept,
#                       so the link enumerates the FULL symbol gap surface.
#                       The result exceeds the 4MB SSRAM and only links.
# SOUI_FR_CENSUS=OFF -> runnable image (what the QEMU smoke test needs).
option(SOUI_FR_CENSUS "Full whole-archive census link (no gc-sections)" OFF)
if(NOT SOUI_FR_CENSUS)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif()

# -----------------------------------------------------------------------------
# sources: board + app + kernel, the way soui_sim groups them
# -----------------------------------------------------------------------------
set(APP_SRCS
    # ---- board (QEMU mps2-an385) -------------------------------------------
    ${DEMO_DIR}/startup.c
    ${DEMO_DIR}/syscalls.c
    ${DEMO_DIR}/mps2/uart_mps2.c
    # ---- application -------------------------------------------------------
    ${CMAKE_CURRENT_SOURCE_DIR}/soui_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_res_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks.c
    # ---- FreeRTOS kernel (bare metal: this project owns port + heap) --------
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_3.c)

# NO_RESOURCES: this app carries its resources as ROM tables behind
# mem_res_provider.cpp -- there is no uires.idx and no PE/windres blob, which
# is exactly the case soui_fr exists to prove (an ELF target supplying its own
# SResProvider).  INCLUDES: ../demo earns its place for <uart.h>.
soui_fr_project(${project_name}
    SOURCES         ${APP_SRCS}
    NO_RESOURCES
    INCLUDES        ${DEMO_DIR}
    OUTPUT_NAME     soui_fr.elf)

# Two include-order facts this target has to enforce (both are "the first hit
# is the wrong file", so they cannot be left to the framework's ordering):
#   1. ${FREERTOS_DIR}/include ahead of swinx/src: the kernel keeps list.h under
#      include/, list.c's #include "list.h" would otherwise fall through to
#      swinx/src/list.h (Wine's unrelated intrusive list) and List_t would be
#      undefined.
#   2. demo/mps2 ahead of ../demo: ../demo is on the path for <uart.h> and also
#      carries a *second* FreeRTOSConfig.h (the lm3s6965 one).  Without this the
#      kernel would be compiled against that config instead of the mps2 one --
#      it links, then behaves differently at run time (heap size, priorities).
target_include_directories(${project_name} BEFORE PRIVATE
    ${SOUI_FR_CONFIG_DIR}
    ${FREERTOS_DIR}/include)
