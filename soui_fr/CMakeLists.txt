# =============================================================================
# soui_fr -- QEMU mps2-an385（Cortex-M3）裸机工程：SOUI 应用冒烟 + swinx 平台层自测
#
# 本文件只做"工程声明"；板级文件、内核源、链接脚本、栈大小、-Os 默认值、两个
# target 的源清单都在 cmake/project.cmake 里 —— 那是本工程的入口。板级基座
# （startup/syscalls/UART/ld/FreeRTOSConfig.h）在 board/。
#
#   ./build.bat / ./build.sh        （推荐：脚本会自己找 arm-none-eabi-gcc / qemu）
#
# 手工等价命令：
#
#   cmake -S . -B build -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-freertos.cmake
#   cmake --build build
#   qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel build/soui_fr.elf
#   qemu-system-arm -M mps2-an385 -nographic -semihosting \
#       -kernel build/soui_fr_selftest.elf
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(project_name "soui_fr")
project(${project_name} C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/project.cmake)
