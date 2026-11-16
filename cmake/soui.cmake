# =============================================================================
# soui.cmake -- "SOUI on FreeRTOS" 的**机制层**。
#
# 分工（2026-11-16 起）：
#
#   <工程>/cmake/project.cmake    工程入口。这个工程怎么编：要哪几层、哪些源
#                                 文件、栈多大、<edit> 用轻量实现还是 RichEdit、
#                                 宿主用不用系统包、资源目录、额外 -D/-O、输出名……
#   cmake/soui.cmake              本文件。机制：定位环境、include 依赖描述、
#                                 提供三个函数。这里不写"某个工程怎么编"。
#   cmake/SouiFreeRTOS.cmake      依赖描述：cairo/pixman/libpng/zlib、swinx、
#                                 SOUI、FreeRTOS 内核 → INTERFACE targets。
#
# 于是每个工程的 CMakeLists.txt 只剩三行，真正的配置在它自己的 cmake 目录里：
#
#   cmake_minimum_required(VERSION 3.20)
#   project(my_app C CXX)
#   include(cmake/project.cmake)
#
# 而 cmake/project.cmake 长这样：
#
#   set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")   # FreeRTOSConfig.h
#   set(SOUI_FR_STACK_WORDS 16384)                                 # 任务栈（字）
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/soui.cmake)
#   soui_fr_project(my_app SOURCES main.cpp SIM_HOST)
#
# include 之前可以设置的工程变量（全部可选，取值与含义见 SouiFreeRTOS.cmake
# 的 "工程可覆盖项" 一节）：SOUI_FR_CONFIG_DIR / SOUI_FR_STACK_WORDS /
# SOUI_FR_PORT_DIR / SOUI_FR_EDIT_LITE / SOUI_FR_USE_RICHEDIT /
# SOUI_FR_USE_SYSTEM_DEPS / SOUI_FR_WARNINGS / SOUI_FR_DEFINITIONS /
# SOUI_FR_COMPILE_OPTIONS / SOUI_FR_SWINX_PLATFORM_SRCS。
#
# Public API（除第一个参数外都可选）：
#
#   soui_fr_project(<target>
#       SOURCES <src>...              application sources (required)
#       [COMPONENTS all|thirdparty|swinx|soui|freertos]
#                                     要哪几层。默认 all = 内核 + 三方库 +
#                                     swinx + SOUI（典型应用）；COMPONENTS swinx
#                                     只装 swinx 平台层（平台层自测工程）。
#       [RESOURCES <dir>]             dir holding uires.idx (default: ./uires)
#       [RESOURCE_SCRIPT <file.rc>]   custom .rc (default: generated)
#       [NO_RESOURCES]                plain-text app without PE resources
#       [FREERTOS_CONFIG <dir>]       dir holding FreeRTOSConfig.h (default: ./config)
#       [SIM_HOST]                    link the SDL2 simulator runtime
#       [INCLUDES <dir>...]           extra include dirs for the app only
#       [LIBRARIES <lib>...]          extra link libraries for the app only
#       [DEFINITIONS <def>...]        extra -D for the app only
#       [COMPILE_OPTIONS <opt>...]    extra compile options for the app only
#       [LINK_OPTIONS <opt>...]       extra link options for the app only
#       [OUTPUT_NAME <name>]          executable base name (default: <target>))
#
#   RESOURCES is a Windows-host step: it shells out to <soui4>/tools/
#   uiresbuilder.exe and to windres.  On a non-Windows host it is skipped and
#   the app is built without a PE resource blob (an ELF target supplies its own
#   SResProvider), so the same CMakeLists configures everywhere.
#
#   soui_fr_add_resources(<out_var> [DIR <dir>] [SCRIPT <rc>] [NAME <n>])
#       just the resource pipeline, for projects that build their own targets.
#
#   soui_fr_add_sim_host(<target>)
#       link the SDL2 host runtime into an existing target (same as SIM_HOST).
#
#   soui_fr_add_all() / soui_fr_link_app(<target>) / soui_fr::* targets
#       the lower-level API from SouiFreeRTOS.cmake, still available.
#
# Cross builds (arm-none-eabi toolchain file) need no change: the same
# environment picks the bare-metal combination automatically (vendored cairo,
# ARM_CM3 port, no SDL, no runtime DLL harvest).
# =============================================================================
include_guard(GLOBAL)

get_filename_component(SOUI_FR_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(SOUI_FR_ENV_DIR "${SOUI_FR_CMAKE_DIR}" DIRECTORY)

include("${SOUI_FR_CMAKE_DIR}/SouiFreeRTOS.cmake")

set(SOUI_FR_SIM_DIR "${SOUI_FR_ENV_DIR}/sim")

# =============================================================================
# resource pipeline: uires.idx -> windres COFF -> .incbin blob
#
# Mirrors demos/demo and __cmake/windres.cmake (OHOS branch):
#
#   <dir>/uires.idx --(uiresbuilder)--> <name>_res/soui_res.rc2
#   <name>.rc  #includes soui_res.rc2 + theme_sys_res.rc2
#   <name>.rc  --(windres)--> PE-COFF object
#     --(.balign 4 + .incbin)--> data blob exposing
#                                _binary_soui_coff_o_start / _end
#
# At runtime swinx's resapi.cpp finds that blob through the weak linker symbols
# (dlsym has nothing to find on this platform) and WindResResourceParser serves
# FindResource/LoadResource to SResProviderPE.
#
# Why the extra assembly step: the COFF object cannot simply be linked, it is
# located by symbol, so it has to become *data* inside the image.
#
# uires.idx -> rc2 这一段要用 <soui4>/tools/uiresbuilder.exe，那是 Windows 程序，
# 只有 Windows 宿主能跑。rc2 生成之后是纯文本，后面的 windres -> PE-COFF -> .incbin
# 在任何装了 mingw（windres）的宿主上都能做，因此非 Windows 只是"跳过 uiresbuilder、
# 复用现成的 rc2"，而不是整条资源链都不做；连 rc2 都没有时才按"无资源"返回，
# 由 soui_fr/mem_res_provider.cpp 那种自带 provider 兜底。
# 为了让"复用"真的成立，rc2 + resource.h 输出到 <项目源码>/<name>_res/ 并入库，
# 而不是 build 目录（那个目录换平台/换机器就没了）。
# =============================================================================
function(soui_fr_add_resources out_var)
    set(_one DIR SCRIPT NAME WORK_DIR RC_OUT_DIR)
    cmake_parse_arguments(ARG "" "${_one}" "" ${ARGN})

    if(NOT ARG_NAME)
        set(ARG_NAME res)
    endif()

    if(NOT ARG_DIR)
        set(ARG_DIR "uires")
    endif()
    if(NOT ARG_WORK_DIR)
        # rc2 file paths are "uires/..." / "theme_sys_res/...", i.e. relative to
        # the project source dir -- windres has to run there.
        set(ARG_WORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    get_filename_component(ARG_DIR "${ARG_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT ARG_RC_OUT_DIR)
        # rc2 和 resource.h 是**跨平台可复用**的文本产物：非 Windows 宿主就是靠
        # 复用它们继续跑 windres。所以默认放在源码目录并入库（<项目>/<name>_res/），
        # 而不是 build 目录 -- 否则换个平台/换台机器就没有这层资源描述可用。
        # 真正的中间产物（.rc 脚本、.coff.o、.S、.blob.o）仍留在 build 目录。
        set(ARG_RC_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_NAME}_res")
    endif()

    file(MAKE_DIRECTORY "${ARG_RC_OUT_DIR}")

    # ---- step 0: the project's own .rc (generated when not supplied) --------
    # 生成的 .rc 只是一层 include 门面，属于中间产物，放 build 目录；手写的
    # RESOURCE_SCRIPT（通常是源码里的 main.rc）不受影响。
    if(NOT ARG_SCRIPT)
        set(ARG_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.rc")
        file(WRITE "${ARG_SCRIPT}"
"// generated by cmake/soui.cmake -- app resources + SOUI system resources
#include \"soui_res.rc2\"   // generated from uires.idx
#include \"theme_sys_res.rc2\"  // soui-sys-resource (skins/msgbox/editmenu/theme)
")
    endif()

    # ---- step 1: uires.idx -> soui_res.rc2 (+ resource.h) -------------------
    # 工具固定在"soui4 工作目录/tools/"下：SOUI_FR_ROOT 由 SouiFreeRTOS.cmake 从
    # 本文件位置推得（soui-freertos 是 soui4 的子模块，位置固定），不读环境变量、
    # 不写死盘符。
    set(_rc2 "${ARG_RC_OUT_DIR}/soui_res.rc2")
    set(_res_h "${ARG_RC_OUT_DIR}/resource.h")

    if(CMAKE_HOST_WIN32)
        if(NOT EXISTS "${ARG_DIR}/uires.idx")
            message(FATAL_ERROR
                "[soui_fr] ${ARG_DIR}/uires.idx not found. Point RESOURCES at the "
                "directory that holds uires.idx, or build with NO_RESOURCES.")
        endif()

        set(_uiresbuilder "${SOUI_FR_ROOT}/tools/uiresbuilder.exe")
        if(NOT EXISTS "${_uiresbuilder}")
            message(FATAL_ERROR
                "[soui_fr] uiresbuilder not found: ${_uiresbuilder}\n"
                "[soui_fr] 它应当在 soui4 工作目录的 tools/ 下（当前 SOUI_FR_ROOT="
                "${SOUI_FR_ROOT}）。")
        endif()

        # uiresbuilder's -p is a *path prefix*, copied verbatim into the generated
        # rc2 (DEFINE_UIRES(name, TYPE, "uires/xml/foo.xml")).  Feed it the relative
        # form so the rc2 stays relocatable and windres can resolve it from
        # WORK_DIR; an absolute prefix would also work but bakes the source path
        # into the resource table.
        file(RELATIVE_PATH _dir_prefix "${ARG_WORK_DIR}" "${ARG_DIR}")
        if(_dir_prefix STREQUAL "")
            set(_dir_prefix ".")
        endif()

        file(GLOB_RECURSE _res_files
            "${ARG_DIR}/uidef/*" "${ARG_DIR}/xml/*" "${ARG_DIR}/image/*"
            "${ARG_DIR}/svg/*" "${ARG_DIR}/values/*")

        # resource.h is what a SOUI app includes to get "LAYOUT:NAME" constants
        # and, with INIT_R_DATA, the UIRES / R (named id) tables.  It is a second
        # OUTPUT of the same command so that any target compiling against it is
        # ordered after uiresbuilder has actually written it (a BYPRODUCT would
        # only be an order-only hint, and the compiler could pick up a stale
        # header).
        add_custom_command(OUTPUT "${_rc2}" "${_res_h}"
            COMMAND "${_uiresbuilder}" -p "${_dir_prefix}" -i "${ARG_DIR}/uires.idx"
                    -r "${_rc2}" -h "${_res_h}" idtable
            DEPENDS "${_uiresbuilder}" "${ARG_DIR}/uires.idx" ${_res_files}
            # uiresbuilder resolves "<prefix>/<path>" against the *working
            # directory* to open each layout and harvest its name="..." attributes
            # for R.id/R.name.  Without this it silently produces an empty R table.
            WORKING_DIRECTORY "${ARG_WORK_DIR}"
            COMMENT "[uiresbuilder] ${ARG_NAME}: uires.idx -> soui_res.rc2 + resource.h"
            VERBATIM)
    else()
        # 非 Windows：uiresbuilder.exe 跑不了，但 rc2 只是一份文本资源清单，
        # 在 Windows 上生成一次就能一直用。复用 build 目录里现成的那份；没有就
        # 只能按"无资源"返回（windres 没有输入可编）。
        if(NOT EXISTS "${_rc2}" OR NOT EXISTS "${_res_h}")
            message(STATUS
                "[soui_fr] 非 Windows 宿主且没有现成的 soui_res.rc2（${ARG_RC_OUT_DIR}），"
                "跳过资源（${ARG_NAME}）：uiresbuilder 只能在 Windows 上运行，"
                "请先在 Windows 上构建一次，再把该目录拷过来。")
            set(${out_var} "" PARENT_SCOPE)
            set(${out_var}_DIR "" PARENT_SCOPE)
            return()
        endif()
        message(STATUS "[soui_fr] 非 Windows 宿主，复用现成的资源描述：${_rc2}")
    endif()

    # ---- step 2: .rc -> PE-COFF --------------------------------------------
    # 只在 Windows 才有 windres 是不够的假设：mingw 在 Linux/macOS 上也提供 windres，
    # 所以这一步不按平台分支，只按工具是否存在。
    find_program(SOUI_FR_WINDRES NAMES windres x86_64-w64-mingw32-windres)
    if(NOT SOUI_FR_WINDRES)
        message(FATAL_ERROR
            "[soui_fr] windres not found -- 编译 ${ARG_SCRIPT} 需要 mingw 的 windres"
            "（Windows 随 MSYS2/MinGW 提供；Linux/macOS 装 mingw-w64 后同样可用）。")
    endif()

    file(GLOB_RECURSE _sys_res_files "${SOUI_FR_ROOT}/soui-sys-resource/theme_sys_res/*")
    set(_coff "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.coff.o")

    # --include-dir (long form): on recent binutils the short -I means
    # "input format", not "include path".
    # windres shells out to `gcc -E -xc -DRC_INVOKED`; a cross toolchain's PATH
    # has no host gcc, which windres reports only as "preprocessing failed.".
    # Prepend the windres directory for this one command, like esp32s3 does.
    get_filename_component(_windres_dir "${SOUI_FR_WINDRES}" DIRECTORY)
    set(_windres_env "${CMAKE_COMMAND}" -E env
        "--modify" "PATH=path_list_prepend:${_windres_dir}")
    add_custom_command(OUTPUT "${_coff}"
        COMMAND ${_windres_env} ${SOUI_FR_WINDRES} --target=pe-x86-64
                --include-dir "${ARG_RC_OUT_DIR}"
                --include-dir "${SOUI_FR_ROOT}/soui-sys-resource"
                -i "${ARG_SCRIPT}" -o "${_coff}" -O coff
        DEPENDS "${ARG_SCRIPT}" "${_rc2}" ${_sys_res_files}
        WORKING_DIRECTORY "${ARG_WORK_DIR}"
        COMMENT "windres: ${ARG_NAME}.rc (app + sys rc2) -> COFF"
        VERBATIM)

    # ---- step 3: COFF -> .incbin data blob --------------------------------
    # Bare metal: the mps2-an385 linker script only collects .text*/.rodata*
    # into FLASH, so a plain .rdata section would be an orphan section.
    # .balign 8 because coffparser / icon directories do uint32 loads that GCC
    # may fuse into LDRD, which faults on Cortex-M3 unless the address is
    # 8-byte aligned (x86 tolerates anything).
    # Host builds keep the PE-friendly .rdata name (COFF section names are
    # limited to 8 characters, so ".rodata.embedded" cannot be used there).
    if(SOUI_FR_BARE_METAL)
        set(_blob_section ".section .rodata.embedded,\"a\"")
        set(_blob_align ".balign 8")
    else()
        set(_blob_section ".section .rdata")
        set(_blob_align ".balign 4")
    endif()
    set(_asm "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.S")
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.blob.o")
    file(GENERATE OUTPUT "${_asm}" CONTENT "
    ${_blob_section}
    ${_blob_align}
    .globl _binary_soui_coff_o_start
    .globl _binary_soui_coff_o_end
_binary_soui_coff_o_start:
    .incbin \"${_coff}\"
_binary_soui_coff_o_end:
")
    add_custom_command(OUTPUT "${_obj}"
        COMMAND "${CMAKE_C_COMPILER}" -x assembler-with-cpp -c "${_asm}" -o "${_obj}"
        DEPENDS "${_asm}" "${_coff}"
        COMMENT "embedding ${ARG_NAME} resource COFF into the image"
        VERBATIM)

    set(${out_var} "${_obj}" PARENT_SCOPE)
    # the directory holding the generated resource.h, so the caller can put it
    # on the include path (that is how a SOUI app gets R.id.* / UIRES.*)
    set(${out_var}_DIR "${ARG_RC_OUT_DIR}" PARENT_SCOPE)
endfunction()

# =============================================================================
# optional SDL2 host runtime
#
# sim/soui_sim_hal.cpp owns every SDL call (window, ARGB8888 texture, present
# callback, event injection) so the rest of the build never sees <SDL.h> -- it
# has to stay out of the windows.h shim's way.  Interaction is the real mouse
# and keyboard; F12 in the window dumps the current frame to a BMP.
#
# Bare-metal builds never call this: a firmware has its own display driver.
# =============================================================================
function(soui_fr_add_sim_host target)
    if(SOUI_FR_BARE_METAL)
        message(FATAL_ERROR
            "[soui_fr] SIM_HOST is for desktop builds; a bare-metal target "
            "drives its own panel instead (see soui_fr/src/hal_*.c).")
    endif()
    if(NOT EXISTS "${SOUI_FR_SIM_DIR}/soui_sim_hal.cpp")
        message(FATAL_ERROR "[soui_fr] simulator runtime missing: ${SOUI_FR_SIM_DIR}")
    endif()

    if(NOT TARGET soui_fr_sim_host)
        find_package(SDL2 REQUIRED)
        add_library(soui_fr_sim_host STATIC "${SOUI_FR_SIM_DIR}/soui_sim_hal.cpp")
        target_include_directories(soui_fr_sim_host PUBLIC "${SOUI_FR_SIM_DIR}")
        target_link_libraries(soui_fr_sim_host PUBLIC SDL2::SDL2)
        add_library(soui_fr::sim_host ALIAS soui_fr_sim_host)
        # The HAL talks to swinx through opaque handles (HWND/HANDLE are integers
        # on this platform), so it needs the platform target's -fpermissive and
        # its -D set -- exactly what it inherited while it lived in the app.
        # Linking it PRIVATE keeps those flags out of the consumer's interface.
        if(NOT TARGET soui_fr_platform)
            soui_fr_create_platform_target()
        endif()
        target_link_libraries(soui_fr_sim_host PRIVATE soui_fr_platform)
        # SDL drags in the real windows.h and the HAL pokes swinx internals;
        # its warnings are noise in an otherwise quiet build.
        target_compile_options(soui_fr_sim_host PRIVATE -w)
    endif()

    target_link_libraries(${target} PRIVATE soui_fr_sim_host)
endfunction()

# =============================================================================
# soui_fr_project -- create a complete application target
# =============================================================================
function(soui_fr_project target)
    set(_one FREERTOS_CONFIG OUTPUT_NAME)
    set(_multi SOURCES RESOURCES RESOURCE_SCRIPT INCLUDES LIBRARIES
               COMPONENTS DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS)
    set(_flags SIM_HOST NO_RESOURCES)
    cmake_parse_arguments(ARG "${_flags}" "${_one}" "${_multi}" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "[soui_fr] soui_fr_project(${target}) needs SOURCES")
    endif()

    # ---- 组件选择 -----------------------------------------------------------
    # 默认 all：内核 + 三方库 + swinx + SOUI，也就是一个典型 SOUI 应用要的全部。
    # 只要平台层（swinx_test 那类"测 swinx 自己"的工程）就写 COMPONENTS swinx：
    # 省掉 SOUI 的 ~120 个 TU，也省掉 whole-archive 的 COM 注册表。
    set(_comps ${ARG_COMPONENTS})
    if(NOT _comps)
        set(_comps all)
    endif()
    if("all" IN_LIST _comps)
        soui_fr_add_all()
        set(_with_soui ON)
    else()
        set(_with_soui OFF)
        soui_fr_create_platform_target()
        # add_* 彼此依赖（soui -> swinx -> thirdparty），选到哪一层就把下面补齐。
        if("soui" IN_LIST _comps)
            soui_fr_add_soui()
            set(_with_soui ON)
        elseif("swinx" IN_LIST _comps)
            soui_fr_add_swinx()
        elseif("thirdparty" IN_LIST _comps)
            soui_fr_add_thirdparty()
        endif()
        if("freertos" IN_LIST _comps AND NOT SOUI_FR_BARE_METAL)
            soui_fr_add_freertos_host()
        endif()
    endif()

    # ---- FreeRTOSConfig.h ---------------------------------------------------
    # SouiFreeRTOS.cmake bakes SOUI_FR_CONFIG_DIR into the INTERFACE targets at
    # include time, so a per-project override is applied afterwards by extending
    # those targets (they only exist once soui_fr_add_all() has run).
    if(ARG_FREERTOS_CONFIG)
        if(NOT IS_ABSOLUTE "${ARG_FREERTOS_CONFIG}")
            set(ARG_FREERTOS_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_FREERTOS_CONFIG}")
        endif()
        if(NOT EXISTS "${ARG_FREERTOS_CONFIG}/FreeRTOSConfig.h")
            message(FATAL_ERROR
                "[soui_fr] ${ARG_FREERTOS_CONFIG}/FreeRTOSConfig.h not found")
        endif()
        target_include_directories(soui_fr_platform INTERFACE "${ARG_FREERTOS_CONFIG}")
        if(TARGET freertos_config)
            target_include_directories(freertos_config SYSTEM INTERFACE
                "${ARG_FREERTOS_CONFIG}")
        endif()
    endif()

    # ---- resources ---------------------------------------------------------
    set(_res_objs "")
    set(_res_dir "")
    if(ARG_NO_RESOURCES)
        # nothing: the app builds its own SResProvider (or has no UI at all)
    else()
        if(NOT ARG_RESOURCES)
            set(ARG_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/uires")
        endif()
        soui_fr_add_resources(_res_obj
            DIR "${ARG_RESOURCES}"
            SCRIPT "${ARG_RESOURCE_SCRIPT}"
            NAME "${target}")
        list(APPEND _res_objs "${_res_obj}")
        set(_res_dir "${_res_obj_DIR}")
    endif()

    # ---- executable --------------------------------------------------------
    add_executable(${target} ${ARG_SOURCES} ${_res_objs})
    if(_res_dir)
        # "#include "resource.h"" (uiresbuilder output) + build ordering: the
        # generated header is listed as a source so it exists before any TU of
        # this target is compiled.
        target_include_directories(${target} PRIVATE "${_res_dir}")
        target_sources(${target} PRIVATE "${_res_dir}/resource.h")
    endif()
    if(ARG_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${ARG_OUTPUT_NAME}")
    endif()

    if(_with_soui)
        # SOUI libraries (whole-archive: keeps the static COM registrations alive)
        soui_fr_link_app(${target})
    else()
        # 没有 SOUI 就没有静态 COM 注册表，不需要 whole-archive：直接链
        # INTERFACE 目标，链接器按引用拉取归档成员。
        target_link_libraries(${target} PRIVATE soui_fr_platform)
        if(TARGET soui_fr_swinx)
            target_link_libraries(${target} PRIVATE soui_fr_swinx)
        endif()
        if(TARGET soui_fr_freertos)
            target_link_libraries(${target} PRIVATE soui_fr_freertos)
        endif()
        if(NOT WIN32)
            target_link_libraries(${target} PRIVATE m)   # cairo/pixman 的 libm
        endif()
        # swinx 用 FreeRTOS 原语重实现了一票 Win32 API；宿主上同名符号也在
        # 导入库里，保留先到的那个（swinx）定义。
        if(WIN32 AND NOT MSVC)
            target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
        endif()
    endif()

    if(ARG_SIM_HOST)
        soui_fr_add_sim_host(${target})
    endif()

    if(ARG_INCLUDES)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDES})
    endif()
    if(ARG_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES})
    endif()
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
    endif()
    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${target} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()
    if(ARG_LINK_OPTIONS)
        target_link_options(${target} PRIVATE ${ARG_LINK_OPTIONS})
    endif()

    message(STATUS "[soui_fr] project '${target}': components=${_comps} "
                   "sources=${ARG_SOURCES} res=${_res_objs} SIM_HOST=${ARG_SIM_HOST}")
endfunction()
