# =============================================================================
# soui_sim -- a normal SOUI application that happens to run on FreeRTOS.
#
# The project is laid out exactly like wizard/CmakeApp (stdafx / MainDlg /
# main.cpp / main.rc / uires), so the app code is the same code you would build
# for Windows; only the toolchain differs:
#
#   FreeRTOS kernel + swinx (the Win32 shim) + SOUI core + render-gdi (cairo)
#   instead of Win32/GDI, and an SDL2 window that presents the swinx
#   framebuffer and injects mouse events back through swinx's input path.
#
# cmake/soui.cmake brings all of that in (kernel, swinx, SOUI, cairo/pixman/
# libpng/zlib, the uires -> windres -> incbin resource pipeline, the SDL host
# runtime and the Windows runtime-DLL harvest).
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(project_name "soui_sim")
project(${project_name} C CXX)

# Default to Release so SASSERT/assert are compiled out, as on the QEMU target.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# the FreeRTOS + swinx + SOUI environment (one include, like find_package)
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/soui.cmake)

# -----------------------------------------------------------------------------
# sources / resources, grouped the way the SOUI wizard groups them
# -----------------------------------------------------------------------------
file(GLOB CURRENT_HEADERS *.h *.hpp)
file(GLOB CURRENT_SRCS *.cpp)
file(GLOB_RECURSE CURRENT_SOUIS ${CMAKE_CURRENT_SOURCE_DIR}/uires/*.*)
file(GLOB_RECURSE CURRENT_RC *.rc *.rc2)

source_group("Header Files" FILES ${CURRENT_HEADERS})
source_group("Source Files" FILES ${CURRENT_SRCS})
source_group("SoUI Resource" FILES ${CURRENT_SOUIS})
source_group("Resource Files" FILES ${CURRENT_RC})

soui_fr_project(${project_name}
    SOURCES         ${CURRENT_SRCS}
    RESOURCES       uires
    RESOURCE_SCRIPT main.rc
    SIM_HOST)

# Same as the wizard: one precompiled header, pulled in by every translation
# unit.  stdafx.h also carries the SCOM_MASK that selects the static COM
# components, so it has to be identical everywhere.
target_precompile_headers(${project_name} PRIVATE stdafx.h)
