#!/usr/bin/env bash
# =============================================================================
# soui_sim/build.sh -- 构建 Windows / Linux / macOS 宿主模拟器（SOUI on FreeRTOS）
#
# 为什么需要这个脚本：构建本工程要 cmake + ninja + 一个 C++17 编译器，并且 SDL2
# 必须在 CMake 找得到的位置。这些东西"装了但不在 PATH 上"是常态，于是文档里那
# 两行 cmake 命令换台机器就报错。脚本做三件事：
#
#   1. 把常见安装目录补进 PATH（MSYS2 / Git for Windows / Homebrew / apt），
#      真找不到就直说缺什么、怎么装，而不是抛一段 CMake 内部报错；
#   2. configure + build，默认 Release，并行度按 CPU 核数；
#   3. 可选 --run 直接起窗口。
#
# 用法：
#   ./build.sh                  配置 + 构建
#   ./build.sh --run            构建完直接运行
#   ./build.sh --clean          先删 build/ 再全量构建
#   ./build.sh --reconfigure    强制重新 configure
#   ./build.sh --system-deps    链系统 cairo/pixman/libpng/zlib（默认由 pkg-config 探测）
#   ./build.sh --vendored-deps  用仓库内 vendored 源码（可复现）
#   ./build.sh -j8              指定并行度
#   ./build.sh -- -DSDL2_DIR=...  其余参数原样传给 cmake
#
# Windows 上也可以直接跑 build.bat（它转调本脚本）。改编译配置请改
# cmake/project.cmake —— 那是本工程的构建入口，本脚本只管环境。
# =============================================================================
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

RUN=0
CLEAN=0
RECONFIG=0
JOBS=""
DEPS=""
CMAKE_ARGS=""

usage() {
    cat <<'EOF'
soui_sim 构建脚本

  ./build.sh [选项] [-- 额外 cmake 参数]

  --run             构建完直接运行模拟器
  --clean           先删除 build/ 再全量构建
  --reconfigure     强制重新 configure
  --system-deps     链系统 cairo/pixman/libpng/zlib
  --vendored-deps   用 vendored 源码（可复现）
  -jN               并行度（默认 CPU 核数）
  -h, --help        本帮助
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --run)           RUN=1 ;;
        --clean)         CLEAN=1 ;;
        --reconfigure)   RECONFIG=1 ;;
        --system-deps)   DEPS="-DSOUI_FR_USE_SYSTEM_DEPS=ON" ;;
        --vendored-deps) DEPS="-DSOUI_FR_USE_SYSTEM_DEPS=OFF" ;;
        -j*)             JOBS="${1#-j}" ;;
        --jobs=*)        JOBS="${1#--jobs=}" ;;
        --)              shift; CMAKE_ARGS="$*"; break ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "未知参数: $1（--help 看用法）" >&2; exit 2 ;;
    esac
    shift
done

# -----------------------------------------------------------------------------
# 1. 找工具
# -----------------------------------------------------------------------------
# Windows 上这个脚本跑在 Git Bash 或 MSYS2 bash 里。这里有两个坑，都踩过：
#
#  (a) MSYS2 有两套 cmake：/usr/bin/cmake 是 *msys* 版，它会把 C:/msys64/...
#      改写成 /mingw64/... 再交给原生 ninja，后者不认这种路径，交叉配置直接
#      失败在 "C compiler is not able to compile a simple test program"。
#      所以 msys 版只用不选，选到它就明确报错。
#  (b) PATH 里必须放 Windows 形式（C:/...），原生 cmake 不认 /c/...
is_win() { case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) return 0 ;; *) return 1 ;; esac; }

to_win() {  # POSIX 路径 -> Windows 形式；非 Windows 原样返回
    if is_win; then
        if command -v cygpath >/dev/null 2>&1; then
            cygpath -m "$1"
        else
            printf '%s:/%s\n' "$(printf '%s' "${1:1:1}" | tr 'a-z' 'A-Z')" "${1:3}"
        fi
    else
        printf '%s\n' "$1"
    fi
}

prepend_path() {   # 目录补到 PATH 最前面；不存在或已存在就跳过
    [ -d "$1" ] || return 0
    local p
    p="$(to_win "$1")"
    case ":$PATH:" in
        *":$p:"*) ;;
        *) PATH="$p:$PATH"; export PATH ;;
    esac
}

if is_win; then
    # 注意顺序：后 prepend 的优先级更高。MSYS2 的 mingw64 打头，原生 CMake 压轴。
    # /c/msys64/usr/bin 故意不进来 —— 那是 msys 工具集（见上面 (a)）。
    for d in /c/msys64/mingw64/bin /c/msys64/ucrt64/bin /mingw64/bin \
             /c/ProgramData/chocolatey/bin \
             "/c/Program Files/CMake/bin" "/c/Program Files (x86)/CMake/bin"; do
        prepend_path "$d"
    done
else
    for d in /usr/local/bin /opt/homebrew/bin; do
        prepend_path "$d"
    done
fi

die_missing() {
    echo "" >&2
    echo "缺少 $1。" >&2
    shift
    printf '%s\n' "$@" >&2
    exit 1
}

command -v cmake >/dev/null 2>&1 || die_missing cmake \
    "  Windows: 装 CMake（https://cmake.org/download/ 或 pacman -S mingw-w64-x86_64-cmake）" \
    "  Linux:   sudo apt install cmake        macOS: brew install cmake"
command -v ninja >/dev/null 2>&1 || die_missing ninja \
    "  Windows(MSYS2): pacman -S mingw-w64-x86_64-ninja" \
    "  Linux: sudo apt install ninja-build    macOS: brew install ninja"
if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 \
   && ! command -v clang++ >/dev/null 2>&1; then
    die_missing "C++ 编译器（需要 C++17）" \
        "  Windows(MSYS2): pacman -S mingw-w64-x86_64-toolchain" \
        "  Linux: sudo apt install g++        macOS: xcode-select --install"
fi

CMAKE_BIN="$(command -v cmake)"
case "$CMAKE_BIN" in
    */usr/bin/cmake|/usr/bin/cmake|*/usr/bin/cmake.exe|C:/msys64/usr/bin/cmake.exe)
        die_missing "原生（非 msys 版）CMake" \
            "  当前选中的是 MSYS2 的 msys 版 cmake：$CMAKE_BIN" \
            "  它会把 C:/msys64/... 改写成 /mingw64/... 再交给 ninja，配置必然失败。" \
            "  装一个原生 CMake 即可（https://cmake.org/download/），脚本会自动优先用它。" ;;
esac

CXX_BIN="$(command -v c++ 2>/dev/null || command -v g++ 2>/dev/null || command -v clang++)"


# cmake 一律吃绝对路径：Git Bash 的 cwd 是 /d/work/... 形式，原生 cmake 会把它
# 和 cache 里记的 D:/work/... 当成两个不同目录，直接报
# "CMakeCache.txt directory is different"。
SRC_DIR="$(to_win "$HERE")"
BUILD_DIR="$(to_win "$HERE")/build"

if [ -z "$JOBS" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    else
        JOBS=4
    fi
fi

echo "== 工具 =="
echo "   cmake : $CMAKE_BIN ($("$CMAKE_BIN" --version | head -1))"
echo "   ninja : $(command -v ninja)"
echo "   c++   : $CXX_BIN"
echo "   并行  : -j$JOBS"

# -----------------------------------------------------------------------------
# 2. configure + build
# -----------------------------------------------------------------------------
if [ "$CLEAN" = 1 ] && [ -d build ]; then
    echo "== 删除 build/ =="
    rm -rf build
fi

CONF_LOG="build/configure.log"

configure() {
    local -a args=()
    # 已有 cache 的目录不能再传 -G（生成器不一致 cmake 会直接报错）；构建类型也已在 cache 里。
    if [ ! -f build/CMakeCache.txt ]; then
        args+=(-G Ninja -DCMAKE_BUILD_TYPE=Release)
    fi
    if [ -n "$DEPS" ];       then args+=($DEPS);       fi
    # shellcheck disable=SC2206
    if [ -n "$CMAKE_ARGS" ]; then args+=($CMAKE_ARGS); fi
    # 注意用 PIPESTATUS：`configure | tee` 这条管道的退出码是 tee 的，不是 configure 的，
    # 直接 if ! 判断会把 configure 的失败吞掉。
    "$CMAKE_BIN" -S "$SRC_DIR" -B "$BUILD_DIR" "${args[@]}" 2>&1 | tee "$CONF_LOG"
    return "${PIPESTATUS[0]}"
}

# 只在必要时 configure：首次、--reconfigure、或传了 -D 参数。其余情况交给 ninja 的
# RERUN_CMAKE 规则（cmake/project.cmake 一改，构建时就会自动重新生成）。
need_configure=0
if [ "$RECONFIG" = 1 ] || [ ! -f build/CMakeCache.txt ]; then
    need_configure=1
fi
if [ -n "$DEPS" ] || [ -n "$CMAKE_ARGS" ]; then
    need_configure=1
fi

mkdir -p build

if [ "$need_configure" = 1 ]; then
    echo "== configure =="
    if ! configure; then
        # 与 soui_fr/build.sh 同一个 Windows 坑：ninja 在生成阶段 `-t restat` 会把
        # .ninja_log 重压缩后 rename 回去，此时文件被杀毒 / 索引器占着就报
        # "failed recompaction: Permission denied"，而且构建历史一起作废。
        if grep -qE "failed recompaction|Permission denied" "$CONF_LOG" 2>/dev/null; then
            echo "" >&2
            echo "   ! build/ 下的 ninja 状态文件被别的进程占用，清理后重试一次。" >&2
            rm -f build/.ninja_log.recompact build/.ninja_log.restat build/.ninja_lock 2>/dev/null || true
            if ! configure; then
                echo "" >&2
                echo "配置仍然失败：build/.ninja_log 被占用。关闭占用它的程序（杀毒、IDE 索引、" >&2
                echo "残留的 ninja.exe），或者直接重建：  ./build.sh --clean" >&2
                exit 1
            fi
        elif grep -qi "SDL2" "$CONF_LOG" 2>/dev/null; then
            echo "" >&2
            echo "configure 失败：找不到 SDL2。" >&2
            echo "  Windows(MSYS2): pacman -S mingw-w64-x86_64-SDL2" >&2
            echo "  Linux: sudo apt install libsdl2-dev    macOS: brew install sdl2" >&2
            echo "  装在非标准位置: ./build.sh -- -DSDL2_DIR=/path/to/SDL2" >&2
            exit 1
        else
            echo "" >&2
            echo "配置失败，完整日志：$HERE/$CONF_LOG" >&2
            exit 1
        fi
    fi
else
    echo "== configure =="
    echo "   （复用 build/ 里已有的配置；需要强制重配请加 --reconfigure）"
fi

echo "== build =="
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel "$JOBS"

EXE=build/soui_sim.exe
[ -f "$EXE" ] || EXE=build/soui_sim
echo ""
echo "构建完成：$HERE/$EXE"

if [ "$RUN" = 1 ]; then
    echo "== run =="
    exec "./$EXE"
fi

cat <<EOF

下一步：
  运行模拟器            ./build.sh --run
  无头自检（CI 同款）   SOUI_SIM_AUTOEXIT=1 SOUI_SIM_DUMP_FRAME=1 ./$EXE
  换皮肤 / 改布局       build 里的 uires 由 uiresbuilder 增量生成，重跑本脚本即可
  改编译配置            cmake/project.cmake（栈大小、<edit> 实现、系统依赖……）
EOF
