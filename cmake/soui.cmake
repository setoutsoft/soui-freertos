# =============================================================================
# soui.cmake -- the ONE file a "SOUI on FreeRTOS" project has to include.
#
#   cmake_minimum_required(VERSION 3.20)
#   project(my_app C CXX)
#   include(<soui-freertos>/cmake/soui.cmake)
#   soui_fr_project(my_app SOURCES main.cpp SIM_HOST)
#
# That is the whole build system of a new project.  Everything else -- the
# FreeRTOS kernel, swinx, SOUI core, cairo/pixman/libpng/zlib, the include
# dirs, the -D flags, the uires -> windres -> incbin resource pipeline and the
# Windows runtime-DLL harvest -- is set up here.
#
# Layout:
#
#   soui.cmake              this file: the public facade (include this)
#     SouiFreeRTOS.cmake    dependency description (INTERFACE targets)
#     CopyRuntimeDlls.cmake POST_BUILD helper for the system-deps build
#     soui_fr_compat.h      force-included compat header
#   ../sim/                 optional SDL2 host runtime (window + input)
#
# Public API (all optional except the first):
#
#   soui_fr_project(<target>
#       SOURCES <src>...              application sources (required)
#       [RESOURCES <dir>]             dir holding uires.idx (default: ./uires)
#       [RESOURCE_SCRIPT <file.rc>]   custom .rc (default: generated)
#       [NO_RESOURCES]                plain-text app without PE resources
#       [FREERTOS_CONFIG <dir>]       dir holding FreeRTOSConfig.h (default: ./config)
#       [SIM_HOST]                    link the SDL2 simulator runtime
#       [INCLUDES <dir>...]           extra include dirs for the app only
#       [LIBRARIES <lib>...]          extra link libraries for the app only
#       [OUTPUT_NAME <name>]          executable base name (default: <target>))
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
# Cross builds (arm-none-eabi toolchain file) need no change: the same include
# picks the bare-metal combination automatically (vendored cairo, ARM_CM3 port,
# no SDL, no runtime DLL harvest).
#
# Finding this file from a project outside the workspace: walk up until a
# directory holds cmake/soui.cmake, or pass -DSOUI_FR_ENV_DIR.  templates/app
# carries the ready-made snippet.
# =============================================================================
include_guard(GLOBAL)

get_filename_component(SOUI_FR_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(SOUI_FR_ENV_DIR "${SOUI_FR_CMAKE_DIR}" DIRECTORY)

# -----------------------------------------------------------------------------
# pre-include probing
#
# These two directories have to be known *before* SouiFreeRTOS.cmake runs,
# because it bakes them into the INTERFACE targets.  Both follow the layout a
# new project gets by copying soui-freertos/templates/app, but stay overridable
# by -D or by setting the variable before this include.
# -----------------------------------------------------------------------------
if(NOT SOUI_FR_CONFIG_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/FreeRTOSConfig.h")
    set(SOUI_FR_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
endif()
if(NOT SOUI_FR_CONFIG_DIR)
    message(WARNING
        "[soui_fr] no FreeRTOSConfig.h found (looked in ${CMAKE_CURRENT_SOURCE_DIR}/config). "
        "Set SOUI_FR_CONFIG_DIR or pass FREERTOS_CONFIG to soui_fr_project().")
endif()

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

    # uiresbuilder's -p is a *path prefix*, copied verbatim into the generated
    # rc2 (DEFINE_UIRES(name, TYPE, "uires/xml/foo.xml")).  Feed it the relative
    # form so the rc2 stays relocatable and windres can resolve it from
    # WORK_DIR; an absolute prefix would also work but bakes the source path
    # into the resource table.
    file(RELATIVE_PATH _dir_prefix "${ARG_WORK_DIR}" "${ARG_DIR}")
    if(_dir_prefix STREQUAL "")
        set(_dir_prefix ".")
    endif()
    if(NOT ARG_RC_OUT_DIR)
        set(ARG_RC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}_res")
    endif()

    if(NOT EXISTS "${ARG_DIR}/uires.idx")
        message(FATAL_ERROR
            "[soui_fr] ${ARG_DIR}/uires.idx not found. Point RESOURCES at the "
            "directory that holds uires.idx, or build with NO_RESOURCES.")
    endif()

    file(MAKE_DIRECTORY "${ARG_RC_OUT_DIR}")

    # ---- step 0: the project's own .rc (generated when not supplied) --------
    if(NOT ARG_SCRIPT)
        set(ARG_SCRIPT "${ARG_RC_OUT_DIR}/${ARG_NAME}.rc")
        file(WRITE "${ARG_SCRIPT}"
"// generated by cmake/soui.cmake -- app resources + SOUI system resources
#include \"soui_res.rc2\"   // generated from uires.idx
#include \"theme_sys_res.rc2\"  // soui-sys-resource (skins/msgbox/editmenu/theme)
")
    endif()

    # ---- step 1: uires.idx -> soui_res.rc2 (+ resource.h) -------------------
    set(_uiresbuilder "${SOUI_FR_ROOT}/tools/uiresbuilder.exe")
    if(NOT EXISTS "${_uiresbuilder}")
        message(FATAL_ERROR "[soui_fr] uiresbuilder not found: ${_uiresbuilder}")
    endif()
    set(_rc2 "${ARG_RC_OUT_DIR}/soui_res.rc2")

    file(GLOB_RECURSE _res_files
        "${ARG_DIR}/uidef/*" "${ARG_DIR}/xml/*" "${ARG_DIR}/image/*"
        "${ARG_DIR}/svg/*" "${ARG_DIR}/values/*")

    # resource.h is what a SOUI app includes to get "LAYOUT:NAME" constants and,
    # with INIT_R_DATA, the UIRES / R (named id) tables.  It is a second OUTPUT
    # of the same command so that any target compiling against it is ordered
    # after uiresbuilder has actually written it (a BYPRODUCT would only be an
    # order-only hint, and the compiler could pick up a stale header).
    add_custom_command(OUTPUT "${_rc2}" "${ARG_RC_OUT_DIR}/resource.h"
        COMMAND "${_uiresbuilder}" -p "${_dir_prefix}" -i "${ARG_DIR}/uires.idx"
                -r "${_rc2}" -h "${ARG_RC_OUT_DIR}/resource.h" idtable
        DEPENDS "${_uiresbuilder}" "${ARG_DIR}/uires.idx" ${_res_files}
        # uiresbuilder resolves "<prefix>/<path>" against the *working directory*
        # to open each layout and harvest its name="..." attributes for R.id/R.name.
        # Without this it silently produces an empty R table.
        WORKING_DIRECTORY "${ARG_WORK_DIR}"
        COMMENT "[uiresbuilder] ${ARG_NAME}: uires.idx -> soui_res.rc2 + resource.h"
        VERBATIM)

    # ---- step 2: .rc -> PE-COFF --------------------------------------------
    find_program(SOUI_FR_WINDRES NAMES windres x86_64-w64-mingw32-windres)
    if(NOT SOUI_FR_WINDRES)
        message(FATAL_ERROR "[soui_fr] windres not found -- needed to compile ${ARG_SCRIPT}")
    endif()

    file(GLOB_RECURSE _sys_res_files "${SOUI_FR_ROOT}/soui-sys-resource/theme_sys_res/*")
    set(_coff "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.coff.o")

    # --include-dir (long form): on recent binutils the short -I means
    # "input format", not "include path".
    add_custom_command(OUTPUT "${_coff}"
        COMMAND ${SOUI_FR_WINDRES} --target=pe-x86-64
                --include-dir "${ARG_RC_OUT_DIR}"
                --include-dir "${SOUI_FR_ROOT}/soui-sys-resource"
                -i "${ARG_SCRIPT}" -o "${_coff}" -O coff
        DEPENDS "${ARG_SCRIPT}" "${_rc2}" ${_sys_res_files}
        WORKING_DIRECTORY "${ARG_WORK_DIR}"
        COMMENT "windres: ${ARG_NAME}.rc (app + sys rc2) -> COFF"
        VERBATIM)

    # ---- step 3: COFF -> .incbin data blob --------------------------------
    # .balign 4 because coffparser does uint16/uint32 loads inside the blob and
    # unaligned accesses fault on Cortex-M3 (x86 tolerates them, ARM does not).
    set(_asm "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.S")
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.blob.o")
    file(GENERATE OUTPUT "${_asm}" CONTENT "
    .section .rdata
    .balign 4
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
    set(_multi SOURCES RESOURCES RESOURCE_SCRIPT INCLUDES LIBRARIES)
    set(_flags SIM_HOST NO_RESOURCES)
    cmake_parse_arguments(ARG "${_flags}" "${_one}" "${_multi}" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "[soui_fr] soui_fr_project(${target}) needs SOURCES")
    endif()

    soui_fr_add_all()

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

    # SOUI libraries (whole-archive: keeps the static COM registrations alive)
    soui_fr_link_app(${target})

    if(ARG_SIM_HOST)
        soui_fr_add_sim_host(${target})
    endif()

    if(ARG_INCLUDES)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDES})
    endif()
    if(ARG_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES})
    endif()

    message(STATUS "[soui_fr] project '${target}': "
                   "${ARG_SOURCES} ${_res_objs} SIM_HOST=${ARG_SIM_HOST}")
endfunction()
