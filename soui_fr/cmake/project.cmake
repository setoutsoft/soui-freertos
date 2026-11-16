# =============================================================================
# soui_fr 的工程入口 —— QEMU mps2-an385（Cortex-M3）上的两个裸机 target
#
#   soui_fr.elf           与 soui_sim 同一份 UI 应用：SApp + XML 布局 + cairo
#                         出图，跑完把帧缓冲半主机 dump 成 PPM
#   soui_fr_selftest.elf  swinx 平台层自测：消息泵/定时器、输入注入、
#                         framebuffer 提交、半主机 dump（-DSOUI_FR_SELFTEST=OFF
#                         可以关掉）
#
# 两个 target 共用同一套板级基座（board/）和同一份内核配置，区别只有
# "链不链 SOUI"：app 是 COMPONENTS all，selftest 是 COMPONENTS swinx。
#
# 构建与运行（推荐用工程自带的脚本，它会自己找工具链）：
#
#   ./build.bat            （Windows；Git Bash / cmd 都能用）
#   ./build.sh             （Linux / macOS / MSYS2）
#   ./build.bat --run      （构建完直接在 QEMU 里跑 app + selftest）
#
# 手工等价命令：
#
#   cmake -S . -B build -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-freertos.cmake
#   cmake --build build
#   qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel build/soui_fr.elf
#   qemu-system-arm -M mps2-an385 -nographic -semihosting \
#       -kernel build/soui_fr_selftest.elf
#
# 默认构建类型是 MinSizeRel（-Os）：-O2/-O3 下光 cairo 就把 .text 顶过板子的
# 4MB ZBT SSRAM，重叠访问即 HardFault。
# =============================================================================

# -----------------------------------------------------------------------------
# 本工程的编译配置（必须在 include 之前给）
# -----------------------------------------------------------------------------
# FreeRTOSConfig.h：板级那份（在 board/mps2/，和 ld 脚本、UART 驱动放一起）。
set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/board/mps2")

# CreateThread() 默认栈，单位"字"：16384 字 = 64KB。一帧真的要走 cairo/pixman
# 的 mask 合成路径，48KB 才勉强够；实测边界见 cmake/SouiFreeRTOS.cmake。
set(SOUI_FR_STACK_WORDS 16384)

# <edit> 走轻量实现（SEditLite）。裸机本来也没法用 richedit41 那条路：它要
# 完整强度的 Win32 shim，QEMU 探针不背这一层。
set(SOUI_FR_EDIT_LITE ON)

# 裸机永远用 vendored cairo/pixman（镜像不能依赖宿主包）；显式写出来是为了
# 让"本工程怎么编"在这一个文件里能读完。
set(SOUI_FR_USE_SYSTEM_DEPS OFF)

# 平台层自测 target（soui_fr_selftest.elf）。默认编：它是 swinx 的 FreeRTOS
# 实现唯一一处"消息泵 / 输入注入 / 帧缓冲提交"的机器断言。census 链接
# （SOUI_FR_CENSUS=ON）只关心 app 的符号面，跳过它省一轮链接。
option(SOUI_FR_SELFTEST "Build soui_fr_selftest.elf (swinx platform tests)" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# the FreeRTOS + swinx + SOUI environment (one include, like find_package)
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/soui.cmake)

if(NOT SOUI_FR_BARE_METAL)
    message(FATAL_ERROR
        "[soui_fr] ${project_name} is a bare-metal target, but this configure "
        "run picked the host toolchain.\n"
        "[soui_fr] Add -DCMAKE_TOOLCHAIN_FILE="
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-arm-freertos.cmake")
endif()

set(BOARD_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/board")
set(FREERTOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel")

# -----------------------------------------------------------------------------
# linker: board script, custom entry, dead-code elimination
# -----------------------------------------------------------------------------
# board/startup.c provides Reset_Handler (CMSIS vector table -> entry());
# -nostartfiles keeps newlib's crt0 out of the image, --entry names the
# symbol because there is no crt0 to define _start.
set(LD_SCRIPT "${BOARD_DIR}/mps2/mps2-an385.ld")
set(SOUI_FR_LINK_OPTIONS
    -T${LD_SCRIPT}
    -nostartfiles
    -Wl,--entry=Reset_Handler)

# SOUI_FR_CENSUS=ON  -> no --gc-sections: every whole-archive section is kept,
#                       so the link enumerates the FULL symbol gap surface.
#                       The result exceeds the 4MB SSRAM and only links.
# SOUI_FR_CENSUS=OFF -> runnable image (what the QEMU smoke test needs).
option(SOUI_FR_CENSUS "Full whole-archive census link (no gc-sections)" OFF)
if(NOT SOUI_FR_CENSUS)
    list(APPEND SOUI_FR_LINK_OPTIONS -Wl,--gc-sections)
endif()

# -----------------------------------------------------------------------------
# 板级 + 内核：两个 target 共用
# -----------------------------------------------------------------------------
# startup.c / syscalls.c / UART 驱动 / ld 脚本 / FreeRTOSConfig.h 都在 board/：
# 它们是"这块板"的事实，不属于某一个 target。
set(BOARD_SRCS
    ${BOARD_DIR}/startup.c
    ${BOARD_DIR}/syscalls.c
    ${BOARD_DIR}/mps2/uart_mps2.c
    # hooks.c：FreeRTOSConfig.h 要求的诊断钩子（assert / 栈溢出 / malloc 失败），
    # 两个 target 都要，否则内核里的那几个符号会落空。
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks.c)

set(KERNEL_SRCS
    # ---- FreeRTOS kernel (bare metal: this project owns port + heap) --------
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_3.c)

# -----------------------------------------------------------------------------
# target 1: soui_fr.elf —— SApp 冒烟
# -----------------------------------------------------------------------------
# Resources: the same windres COFF pipeline soui_sim uses -- uires.idx ->
# soui_res.rc2/resource.h -> main.rc -> PE-COFF -> .incbin data blob.  At
# runtime swinx's WindResResourceParser serves FindResource/LoadResource and
# the app uses the standard SResProviderPE (no ROM C arrays, no filesystem).
# On bare metal the blob goes into .rodata.embedded, 8-byte aligned (see
# cmake/soui.cmake), so the mps2 linker script places it in FLASH.
soui_fr_project(${project_name}
    SOURCES         ${BOARD_SRCS} ${KERNEL_SRCS}
                    ${CMAKE_CURRENT_SOURCE_DIR}/soui_app.cpp
    RESOURCES       uires
    RESOURCE_SCRIPT main.rc
    COMPONENTS      all
    INCLUDES        ${BOARD_DIR}
    LINK_OPTIONS    ${SOUI_FR_LINK_OPTIONS}
    OUTPUT_NAME     soui_fr.elf)

# Two include-order facts this target has to enforce (both are "the first hit
# is the wrong file", so they cannot be left to the framework's ordering):
#   1. ${FREERTOS_DIR}/include ahead of swinx/src: the kernel keeps list.h under
#      include/, list.c's #include "list.h" would otherwise fall through to
#      swinx/src/list.h (Wine's unrelated intrusive list) and List_t would be
#      undefined.
#   2. board/mps2 ahead of everything: the kernel must be compiled against the
#      board's FreeRTOSConfig.h (SOUI_FR_CONFIG_DIR already points there; this
#      pins the order anyway, because the include path also carries headers
#      that would otherwise be found first).
target_include_directories(${project_name} BEFORE PRIVATE
    ${SOUI_FR_CONFIG_DIR}
    ${FREERTOS_DIR}/include)

# -----------------------------------------------------------------------------
# target 2: soui_fr_selftest.elf —— swinx 平台层自测
# -----------------------------------------------------------------------------
# 这套用例原来是一个独立工程（swinx_test/）。它验的东西 demos/fun_test 覆盖不
# 到 —— 消息泵/定时器走 FreeRTOS 队列、输入注入走 SwinxInjectMouse/Key、帧缓冲
# 提交走 SwinxFb* —— 所以并进本工程当第二个 target，而不是搬去桌面。
if(SOUI_FR_SELFTEST AND NOT SOUI_FR_CENSUS)
    soui_fr_project(${project_name}_selftest
        SOURCES
            ${BOARD_SRCS} ${KERNEL_SRCS}
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/t_conn.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/t_render.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/t_input.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/t_dump.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/t_sysapi.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/selftest/test_common.cpp
        COMPONENTS      swinx          # 不链 SOUI，也就没有 whole-archive
        NO_RESOURCES
        INCLUDES        ${BOARD_DIR}
        LINK_OPTIONS    ${SOUI_FR_LINK_OPTIONS}
        OUTPUT_NAME     soui_fr_selftest.elf)

    # Same include-order rule as the app: board config first, then the kernel.
    target_include_directories(${project_name}_selftest BEFORE PRIVATE
        ${SOUI_FR_CONFIG_DIR}
        ${FREERTOS_DIR}/include)
endif()
