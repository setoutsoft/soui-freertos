# =============================================================================
# soui_sim 的工程入口 —— 这个工程"怎么编"全部写在这里。
#
# 机制（依赖图、资源流水线、soui_fr_project()）在 ../../cmake/soui.cmake，
# 本文件只回答本工程自己的问题：链哪些层、任务栈多大、要不要 richedit、
# 宿主用不用系统 cairo、资源目录、编译标准、PCH……
#
# 加编译选项的两个位置：
#   * 只影响应用自己的源文件    -> soui_fr_project(... COMPILE_OPTIONS/DEFINITIONS)
#   * 连 swinx/SOUI/cairo 一起  -> include 之前 set(SOUI_FR_COMPILE_OPTIONS ...)
#                                  （变量清单见 cmake/SouiFreeRTOS.cmake 文件头）
# =============================================================================

# -----------------------------------------------------------------------------
# 本工程的编译配置（必须在 include 之前给）
# -----------------------------------------------------------------------------
# FreeRTOSConfig.h: 宿主模拟器的内核配置（堆/优先级与 QEMU 板级那份不同）
set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")

# CreateThread() 的默认栈，单位是"字"（32768 字 = 128KB）。UI 线程要跑
# cairo/pixman，pixman 的 general path 单帧就在栈上要 ~40KB，
# 实测边界见 cmake/SouiFreeRTOS.cmake 里的记录。
set(SOUI_FR_STACK_WORDS 32768)

# <edit> 控件走轻量实现（SEditLite，SOUI_EDIT_LITE），不编 richedit41。
# XML 属性面和控制行为与 RichEdit 版一致，省掉 ~250 个 2001 年代 Win32 C++
# 源文件（这是整个构建里最长的一段）。确实需要 RTF / 字符级排版时，把这里改
# 成 OFF 并加 set(SOUI_FR_USE_RICHEDIT ON)。
set(SOUI_FR_EDIT_LITE ON)

# 宿主默认优先链系统的 cairo/pixman/libpng/zlib（pkg-config 能证明存在时），
# 省掉 ~250 个文件的编译；要可复现的 vendored 源码就传
#   -DSOUI_FR_USE_SYSTEM_DEPS=OFF

# 编译标准 / 默认构建类型（工程策略，不是机制）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# the FreeRTOS + swinx + SOUI environment (one include, like find_package)
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/soui.cmake)

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
    COMPONENTS      all
    SIM_HOST)

# Same as the wizard: one precompiled header, pulled in by every translation
# unit.  stdafx.h also carries the SCOM_MASK that selects the static COM
# components, so it has to be identical everywhere.
target_precompile_headers(${project_name} PRIVATE stdafx.h)
