# -----------------------------------------------------------------------------
# CopyRuntimeDlls.cmake -- collect the runtime DLLs of an executable next to it.
#
#   cmake -DEXE=<exe> -DOUT_DIR=<dir> -DHINT_BIN=<dir>[;<dir>...]
#         [-DQUIET=ON] -P CopyRuntimeDlls.cmake
#
# Why this exists
# ---------------
# When the host simulator links the *system* cairo/pixman/libpng/zlib (the
# SOUI_FR_USE_SYSTEM_DEPS path) the executable imports libcairo-2.dll and
# friends.  Windows resolves those through PATH, and PATH is frequently
# polluted by an unrelated MinGW prefix (WorkBuddy/PortableGit, Android
# platform-tools, xampp, ...).  A shadowed libwinpthread-1.dll / zlib1.dll /
# libiconv-2.dll of a different release then makes the loader fail with
# 0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND before main() -- a crash that looks
# like a code bug but has nothing to do with this project.
#
# The application directory is searched *before* PATH, so copying the correct
# DLL set next to the executable makes the build independent of the ambient
# environment.  Only DLLs that actually resolve inside one of HINT_BIN (i.e.
# the toolchain's bin directory) are copied; Windows system DLLs are skipped.
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
    message(STATUS "[soui_fr] runtime-deps: '${EXE}' not built yet, nothing to do")
    return()
endif()

if(NOT OUT_DIR)
    get_filename_component(OUT_DIR "${EXE}" DIRECTORY)
endif()

# Normalise the hint directories for prefix comparison.
set(_hints "")
foreach(d IN LISTS HINT_BIN)
    if(d)
        file(TO_CMAKE_PATH "${d}" _d)
        list(APPEND _hints "${_d}")
    endif()
endforeach()

get_filename_component(_exe_dir "${EXE}" DIRECTORY)
set(_search_dirs ${_hints} "${_exe_dir}")

get_filename_component(OUT_DIR "${OUT_DIR}" ABSOLUTE)
file(TO_CMAKE_PATH "${OUT_DIR}" OUT_DIR)

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    DIRECTORIES ${_search_dirs}
    RESOLVED_DEPENDENCIES_VAR _deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    PRE_EXCLUDE_REGEXES
        "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
    POST_EXCLUDE_REGEXES
        ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
)

set(_copied 0)
set(_present 0)
set(_foreign "")
foreach(dep IN LISTS _deps)
    file(TO_CMAKE_PATH "${dep}" _dep)
    get_filename_component(_dep_dir "${_dep}" DIRECTORY)
    if(_dep_dir STREQUAL OUT_DIR)
        # already collected by a previous build
        math(EXPR _present "${_present} + 1")
        continue()
    endif()
    set(_in_hint FALSE)
    foreach(h IN LISTS _hints)
        string(FIND "${_dep_dir}/" "${h}/" _pos)
        if(_pos EQUAL 0)
            set(_in_hint TRUE)
            break()
        endif()
    endforeach()
    if(_in_hint)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                "${_dep}" "${OUT_DIR}")
        math(EXPR _copied "${_copied} + 1")
    else()
        # Resolved somewhere we do not control -- report it, copying a foreign
        # copy is exactly what breaks the loader.
        list(APPEND _foreign "${_dep}")
    endif()
endforeach()

if(NOT QUIET)
    message(STATUS "[soui_fr] runtime-deps: ${_copied} copied, ${_present} already in ${OUT_DIR}")
    foreach(u IN LISTS _unresolved)
        message(STATUS "[soui_fr] runtime-deps: unresolved ${u}")
    endforeach()
    foreach(f IN LISTS _foreign)
        message(STATUS "[soui_fr] runtime-deps: ignored (outside toolchain) ${f}")
    endforeach()
endif()
