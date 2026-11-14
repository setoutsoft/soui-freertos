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
# Expected layout -- both directories live next to this script, are NOT part of
# the repository and are not referenced by any absolute path:
#
#   esp32s3/tools/esp-idf-v5.4.1   ESP-IDF v5.4.1        -> IDF_PATH
#   esp32s3/tools/.espressif       toolchain + python env -> IDF_TOOLS_PATH
#
# See README.md section 3 ("这些工具从哪里来") for how to obtain them.
# Paths are resolved from $BASH_SOURCE, so the current directory does not
# matter: `./tools/idf_env.sh ...` works from esp32s3/ as well as from the repo
# root or from inside build/.
#
# Usage:
#   source tools/idf_env.sh          # sets PATH / IDF_PATH / IDF_TOOLS_PATH
#   tools/idf_env.sh ninja -C build  # or: run one command and exit
#
# Optional overrides (absolute or relative, resolved against the CWD):
#   IDF_ROOT        directory that contains esp-idf-v5.4.1/   (default: this dir)
#   IDF_TOOLS_DIR   directory that contains tools/, python_env/
#                                                             (default: $IDF_ROOT/.espressif)

# Directory of this script, for both `source` and direct execution.
_idf_here=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
_idf_root="${IDF_ROOT:-$_idf_here}"
_idf_tools="${IDF_TOOLS_DIR:-$_idf_root/.espressif}"

if [ ! -d "$_idf_root/esp-idf-v5.4.1" ]; then
    echo "idf_env.sh: ESP-IDF not found at $_idf_root/esp-idf-v5.4.1" >&2
    echo "            expected layout (see README.md 3):" >&2
    echo "              esp32s3/tools/esp-idf-v5.4.1   <- ESP-IDF v5.4.1" >&2
    echo "              esp32s3/tools/.espressif      <- toolchain + python env" >&2
    echo "            or set IDF_ROOT to the directory that contains esp-idf-v5.4.1" >&2
    return 1 2>/dev/null || exit 1
fi
if [ ! -d "$_idf_tools/tools" ]; then
    _idf_inst=install.sh
    [ "${OS:-}" = "Windows_NT" ] && _idf_inst=install.bat
    echo "idf_env.sh: ESP-IDF tools not found at $_idf_tools" >&2
    echo "            run the IDF installer with IDF_TOOLS_PATH pointing here:" >&2
    echo "              IDF_TOOLS_PATH=$_idf_tools \\" >&2
    echo "                $_idf_root/esp-idf-v5.4.1/$_idf_inst esp32s3" >&2
    echo "            or set IDF_TOOLS_DIR to an existing install" >&2
    unset _idf_inst
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

unset _idf_here _idf_root _idf_tools _idf_xtensa _idf_ccache _idf_ninja _idf_cmake _idf_pyenv

# Called with arguments -> run them and exit; sourced -> just export.
if [ "$#" -gt 0 ]; then
    "$@"
    exit $?
fi
