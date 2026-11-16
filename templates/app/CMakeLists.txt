# =============================================================================
# templates/app -- copy this directory to start a new "SOUI on FreeRTOS" app.
#
#   cp -r soui-freertos/templates/app  my_app
#   cmake -S my_app -B my_app/build -G Ninja
#   ninja -C my_app/build && ./my_app/build/hello_freertos.exe
#
# 本文件只做"工程声明"；这个工程怎么编（FreeRTOSConfig、任务栈、richedit、
# 源文件、资源、要不要 SDL 宿主）全在 cmake/project.cmake 里 —— 复制过去以后
# 改那一个文件就够了，不会牵连其它工程。
#
# Cross build (arm-none-eabi, bare metal): same file, just add
#   -DCMAKE_TOOLCHAIN_FILE=<...>/arm-none-eabi.cmake
# and drop SIM_HOST in cmake/project.cmake -- the bare-metal combination
# (vendored cairo, ARM_CM3 port, no SDL) is selected automatically.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hello_freertos C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/project.cmake)
