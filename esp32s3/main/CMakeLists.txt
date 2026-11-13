# ---------------------------------------------------------------------------
# main -- app_main() + board glue + in-memory XML resource provider
#   <soui4>/soui-freertos/esp32s3/main/../../.. == <soui4>
# ---------------------------------------------------------------------------
get_filename_component(SOUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SWINX_DIR "${SOUI_ROOT}/swinx")

# `REQUIRES soui` exports soui's INCLUDE_DIRS, but swinx/include/windows.h does
#   #include "dlfcn.h"
# and the only dlfcn.h in the tree is the FreeRTOS shim in
# swinx/src/platform/freertos, which lives in soui's PRIV_INCLUDE_DIRS and is
# therefore NOT exported.  Add it here (plus the kernel's inner include dir,
# see components/soui/CMakeLists.txt) instead of promoting those paths to
# PUBLIC, so other components do not get swinx's dirent.h/dlcfn.h shims.
set(_idf_root "$ENV{IDF_PATH}")
if(NOT _idf_root AND IDF_PATH)
    set(_idf_root "${IDF_PATH}")
endif()
# Forward slashes: IDF writes INCLUDE_DIRS into component_requires.cmake as C
# string literals, where a Windows "\w" is an invalid character escape.
file(TO_CMAKE_PATH "${_idf_root}" _idf_root)
set(FREERTOS_KERNEL_INC "${_idf_root}/components/freertos/FreeRTOS-Kernel/include/freertos")

idf_component_register(
    SRCS
        "app_main.cpp"
        "soui_demo.cpp"
        "soui_board.cpp"
        "soui_board_esp32.cpp"
        "mem_res_provider.cpp"
        # 56 (type,name,blob) entries: the soui_sim uires + soui_sys_res SVG
        # theme, emitted by tools/gen_uires.py.  Regenerate when the uires
        # change -- plain string literals, because IDF's linker scripts do not
        # collect the windres ".section .rdata" blob.
        "generated/uires_res.cpp"
    INCLUDE_DIRS
        "."
        "${SWINX_DIR}"
        "${SWINX_DIR}/include"
        "${SWINX_DIR}/src"
        "${SWINX_DIR}/src/platform/freertos"
        "${FREERTOS_KERNEL_INC}"
    # IDF 5.x split the monolithic `driver` component; `main` does not
    # implicitly depend on everything, so the GPIO/SPI/I2C drivers that
    # soui_board_esp32.cpp includes must be named explicitly.
    REQUIRES
        soui
        freertos
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        # esp_timer_get_time(): us-resolution timing of the SPI push, so the
        # pump can report "ms per frame" split into render vs bus time.
        esp_timer
)

# -w: SOUI's public headers are written for MSVC and trip IDF's -Werror=all
#     with ~80 diagnostics (unused-value, unknown-pragmas, multistatement-macros,
#     reorder, overloaded-virtual).  The soui component is built the same way.
# -include soui_idf_compat.h: same force-included preamble as the soui
#     component.  Without it this TU has no <climits>, and SOUI's SWnd.h uses
#     LLONG_MAX / ULLONG_MAX.
# -std=gnu++17: same reason as the soui component (u8"" literal semantics).
# -fexceptions: SOUI is compiled with exceptions; keep the app TU ABI identical
#     (sdkconfig.defaults also turns on CONFIG_COMPILER_CXX_EXCEPTIONS).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -w
    -fexceptions
    -std=gnu++17
    -include soui_idf_compat.h)
