# =============================================================================
# templates/app -- copy this directory to start a new "SOUI on FreeRTOS" app.
#
#   cp -r soui-freertos/templates/app  my_app
#   cmake -S my_app -B my_app/build -G Ninja
#   ninja -C my_app/build && ./my_app/build/hello_freertos.exe
#
# The build system is the 2 lines below.  cmake/soui.cmake brings in the
# FreeRTOS kernel, swinx, SOUI core, cairo/pixman/libpng/zlib, the resource
# pipeline (uires.idx -> windres -> embedded blob) and the SDL host runtime;
# see the API header at the top of that file for the full option list.
#
# Cross build (arm-none-eabi, bare metal): same file, just add
#   -DCMAKE_TOOLCHAIN_FILE=<...>/arm-none-eabi.cmake
# and drop SIM_HOST -- the bare-metal combination (vendored cairo, ARM_CM3
# port, no SDL) is selected automatically.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hello_freertos C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate soui-freertos.  Walking up from this file finds it for any project that
# lives inside the workspace (templates/app -> soui-freertos, or a sibling copy
# under soui-freertos/).  A project created OUTSIDE the workspace passes
#   -DSOUI_FR_ENV_DIR=D:/work/soui4/soui-freertos
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

include(${SOUI_FR_ENV_DIR}/cmake/soui.cmake)

# RESOURCES defaults to ./uires (found automatically).  FREERTOS_CONFIG is
# spelled out here to show the knob; omit it and ./config is picked up too.
soui_fr_project(hello_freertos
    SOURCES          main.cpp
    FREERTOS_CONFIG  config
    SIM_HOST)
