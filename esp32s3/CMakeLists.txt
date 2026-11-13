# =============================================================================
# soui-freertos/esp32s3 -- SOUI4 on ESP32-S3 (ESP-IDF firmware)
#
# Target board: 2.8" IPS ESP32-S3 (ILI9341V + FT6X36), 16MB flash, 8MB Octal
# PSRAM, native USB-Serial/JTAG.  README.md has the verified build/flash flow,
# the 11 defects fixed on the way and the remaining limitations.
# ---------------------------------------------------------------------------
# STATUS: built, flashed and verified on hardware (2026-11-13)
# ---------------------------------------------------------------------------
# `soui_esp32s3.bin` (3,268,288 B) boots on the 2.8" IPS board, the first frame
# reaches the panel and the message loop runs -- no panic.  The 11
# build/runtime defects that had to be fixed on the way (BOM'd partitions.csv,
# GCC 14 promoted warnings, gnu++2b u8 literals, pthread_self clash,
# weak-symbol shadowing, printf("%s", NULL) in newlib, ...) are documented in
# README.md section 7.
#
# BUILD (PowerShell, after loading idf-env.ps1):
#   $env:CCACHE_DISABLE='1'
#   cd build; ninja -j 2 1> ..\b.out 2> ..\b.err
#     * -j2 is mandatory: higher parallelism randomly wedges cc1plus here
#     * use plain ninja, not `idf.py build`: idf.py swallows linker stderr
#   idf.py -p COM4 -b 921600 flash
#
# STILL OPEN (all in README.md section 10):
#   1) resources are in-memory XML strings; uires -> windres -> .rdata is not
#      collected by IDF linker scripts -- use EMBED_FILES / a partition later.
#   2) imgdecoder-stb / richedit41 are not linked, so <img>/<edit> are no-ops.
#   3) polled SPI, no DMA: a full 320x240 present costs ~470ms on first frame.
#   4) CJK text needs an embedded TTF + FreeType; textbridge.cpp only exposes
#      cairo's toy font today (ASCII is fine).
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

# <soui4>/soui-freertos/esp32s3 -> ../.. == <soui4>.  Same "fixed submodule
# layout, no machine-specific fallback" rule as SouiFreeRTOS.cmake:57-67.
get_filename_component(SOUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${SOUI_ROOT}/SOUI/include/souistd.h")
    message(FATAL_ERROR
        "[soui_esp32s3] ${SOUI_ROOT} 下没有 SOUI/include/souistd.h。"
        "esp32s3 必须位于 soui4 工作目录下的 soui-freertos/ 中。")
endif()
set(SOUI_ROOT "${SOUI_ROOT}" CACHE INTERNAL "soui4 source tree" FORCE)

# ---------------------------------------------------------------------------
# The panel is driven by main/soui_board_esp32.cpp (strong ILI9341V / FT6X36
# implementations), so nothing here is required for the verified firmware.
# Kept as an escape hatch for reusing the vendor's lvgl_esp32_drivers instead.
#
# Preferred (ASCII path, works with IDF): copy
#   <vendor>/1-示例程序_Demo/ESP-IDF/2.8inch_ESP32-S3_LVGL/components/lvgl_esp32_drivers
# into this project's components/ directory.  IDF picks it up automatically.
#
# Alternative: pass -DSOUI_BOARD_SDK_DIR=<vendor ESP-IDF demo root>; the path
# MUST contain no non-ASCII characters (the vendor tree does not, so prefer a
# copy).  The driver component brings its own Kconfig: the ILI9341 controller,
# pins and FT6X36 touch options then have to be added to sdkconfig.defaults.
# ---------------------------------------------------------------------------
set(SOUI_BOARD_SDK_DIR "" CACHE PATH
    "Optional vendor ESP-IDF demo root providing lvgl_esp32_drivers")
if(SOUI_BOARD_SDK_DIR)
    if(EXISTS "${SOUI_BOARD_SDK_DIR}/components/lvgl_esp32_drivers")
        set(EXTRA_COMPONENT_DIRS
            "${SOUI_BOARD_SDK_DIR}/components/lvgl_esp32_drivers")
    else()
        message(WARNING
            "[soui_esp32s3] ${SOUI_BOARD_SDK_DIR}/components/lvgl_esp32_drivers "
            "不存在，跳过板级屏驱组件（屏仍由 main/soui_board_esp32.cpp 驱动）。")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(soui_esp32s3)
