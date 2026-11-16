# =============================================================================
# soui_sim -- a normal SOUI application that happens to run on FreeRTOS.
#
# 本文件只做"工程声明"；这个工程怎么编（任务栈、richedit、系统包、源文件、
# 资源、PCH）全在 cmake/project.cmake 里 —— 那是本工程的入口。
#
# The project is laid out exactly like wizard/CmakeApp (stdafx / MainDlg /
# main.cpp / main.rc / uires), so the app code is the same code you would build
# for Windows; only the toolchain differs:
#
#   FreeRTOS kernel + swinx (the Win32 shim) + SOUI core + render-gdi (cairo)
#   instead of Win32/GDI, and an SDL2 window that presents the swinx
#   framebuffer and injects mouse events back through swinx's input path.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(project_name "soui_sim")
project(${project_name} C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/project.cmake)
