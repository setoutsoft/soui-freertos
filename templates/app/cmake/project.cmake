# =============================================================================
# 新工程模板的入口 —— 复制 templates/app 到别处后，这个文件是本工程的配置中心。
#
# 只有两件事要改：
#   1. FREERTOS_CONFIG / SOURCES / 资源目录（本文件下半部分）
#   2. 需要跨整个编译树（含 swinx/SOUI/cairo）的选项时，在 include 之前
#      set(SOUI_FR_COMPILE_OPTIONS ...) / set(SOUI_FR_DEFINITIONS ...)
#
# 机制（内核/三方库/swinx/SOUI 的依赖图、uires -> windres 资源流水线、
# SDL2 宿主运行时）在 <soui-freertos>/cmake/soui.cmake，不需要动。
# =============================================================================

# -----------------------------------------------------------------------------
# 定位 soui-freertos
# -----------------------------------------------------------------------------
# 从本文件往上找带 cmake/soui.cmake 的目录：工程放在工作区内（sibling of
# templates/app，或 soui-freertos 下任意一层）都能命中。放在工作区之外的工程
# 用 -DSOUI_FR_ENV_DIR=<path to soui-freertos> 显式指定。
if(NOT SOUI_FR_ENV_DIR)
    set(_probe "${CMAKE_CURRENT_LIST_DIR}")
    foreach(_i RANGE 6)
        if(EXISTS "${_probe}/cmake/soui.cmake")
            set(SOUI_FR_ENV_DIR "${_probe}")
            break()
        endif()
        get_filename_component(_probe "${_probe}/.." ABSOLUTE)
    endforeach()
    unset(_probe)
endif()
if(NOT SOUI_FR_ENV_DIR)
    message(FATAL_ERROR
        "soui-freertos not found above ${CMAKE_CURRENT_LIST_DIR}. "
        "Configure with -DSOUI_FR_ENV_DIR=<path to soui-freertos>.")
endif()

# -----------------------------------------------------------------------------
# 本工程的编译配置（必须在 include 之前给）
# -----------------------------------------------------------------------------
set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")   # FreeRTOSConfig.h
set(SOUI_FR_STACK_WORDS 32768)                                 # CreateThread 默认栈（字）
set(SOUI_FR_EDIT_LITE ON)                                      # <edit> 用轻量 SEditLite

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${SOUI_FR_ENV_DIR}/cmake/soui.cmake)

# -----------------------------------------------------------------------------
# 目标：源文件 + 资源（RESOURCES 默认 ./uires，这里写出来当样板）
# -----------------------------------------------------------------------------
soui_fr_project(hello_freertos
    SOURCES          main.cpp
    RESOURCES        uires
    COMPONENTS       all
    SIM_HOST)
