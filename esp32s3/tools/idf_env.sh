#!/usr/bin/env bash
# idf_env.sh -- assemble the ESP-IDF 5.4.1 environment by hand.
#
# Why this exists (rather than just `source export.sh`):
#   * `source $IDF_PATH/export.sh` refuses to run under Git Bash: idf_tools'
#     check-python-dependencies exits with "MSys/Mingw is not supported";
#   * `export.ps1` fails too, with a case-sensitivity bug in IDF 5.4.1 --
#     it passes the shell name as "powershell.EXE" and activate.py's whitelist
#     only has "powershell.exe".
# The toolchain itself is installed and healthy, so this wrapper just puts the
# same directories on PATH that export.ps1 would have.  It is also what the
# README's build instructions use.
#
# Usage:
#   source tools/idf_env.sh          # sets PATH / IDF_PATH / IDF_TOOLS_PATH
#   tools/idf_env.sh ninja -C build  # or: run one command and exit
#
# Optional overrides: IDF_ROOT, IDF_TOOLS_DIR.

_idf_root="${IDF_ROOT:-/d/work/xiaozi/2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0}"
_idf_tools="${IDF_TOOLS_DIR:-$_idf_root/.espressif}"

if [ ! -d "$_idf_root/esp-idf-v5.4.1" ]; then
    echo "idf_env.sh: ESP-IDF not found at $_idf_root/esp-idf-v5.4.1" >&2
    echo "            set IDF_ROOT to the directory that contains esp-idf-v5.4.1" >&2
    return 1 2>/dev/null || exit 1
fi

_idf_xtensa=$(echo "$_idf_tools"/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin)
_idf_ccache=$(echo "$_idf_tools"/tools/ccache/*/ccache-*/)
_idf_ninja=$(echo "$_idf_tools"/tools/ninja/*)
_idf_cmake=$(echo "$_idf_tools"/tools/cmake/*/bin)
_idf_pyenv=$(echo "$_idf_tools"/python_env/idf5.4_py3.13_env/Scripts)

export IDF_PATH="$_idf_root/esp-idf-v5.4.1"
export IDF_TOOLS_PATH="$_idf_tools"
export PATH="$_idf_xtensa:$_idf_ccache:$_idf_ninja:$_idf_cmake:$_idf_pyenv:$IDF_PATH/tools:$PATH"

# Not part of PATH but required by the CMake configure step:
#   ESP_ROM_ELF_DIR -> tools/cmake/gdbinit.cmake does
#   file(TO_CMAKE_PATH $ENV{ESP_ROM_ELF_DIR} ...), which aborts the whole
#   configure ("must be called with exactly three arguments") when unset --
#   the symptom is `ninja: error: rebuilding 'build.ninja': subcommand failed`
#   the moment a CMakeLists.txt changes.
export ESP_ROM_ELF_DIR="$(echo "$_idf_tools"/tools/esp-rom-elfs/*/)"
# Same class of problem: openocd's script dir (only warns, but keep it set).
export OPENOCD_SCRIPTS="$(echo "$_idf_tools"/tools/openocd-esp32/*/openocd-esp32/share/openocd/scripts)"

unset _idf_root _idf_tools _idf_xtensa _idf_ccache _idf_ninja _idf_cmake _idf_pyenv

# Called with arguments -> run them and exit; sourced -> just export.
if [ "$#" -gt 0 ]; then
    "$@"
    exit $?
fi
