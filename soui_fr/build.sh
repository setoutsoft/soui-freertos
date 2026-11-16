#!/usr/bin/env bash
# =============================================================================
# soui_fr/build.sh -- 构建 QEMU mps2-an385（Cortex-M3）裸机工程，可选直接实跑
#
# 这个工程要的是 arm-none-eabi 交叉工具链（+ 想看结果时还要 qemu-system-arm），
# 它们通常"装了但不在 PATH 上"。脚本做三件事：
#
#   1. 找工具：PATH → MSYS2 → Arm GNU Toolchain 安装目录（按版本通配）→ apt；
#      找不到就说清楚装什么，而不是抛 CMake 的 "no CMAKE_C_COMPILER"；
#   2. configure + build（默认 MinSizeRel/-Os：-O2 下 cairo 会把 .text 顶过板子
#      的 4MB SSRAM），产出两个 ELF：
#         build/soui_fr.elf           SOUI 应用冒烟（出图 + 半主机 dump）
#         build/soui_fr_selftest.elf  swinx 平台层自测（消息泵/输入/帧缓冲）
#   3. 可选 --run：用 QEMU 依次跑两个镜像，并检查日志里的完成标记。
#
# 用法：
#   ./build.sh                  配置 + 构建
#   ./build.sh --run            构建完在 QEMU 里跑两个镜像
#   ./build.sh --run app        只跑 soui_fr.elf
#   ./build.sh --run selftest   只跑 soui_fr_selftest.elf
#   ./build.sh --clean          先删 build/ 再全量构建
#   ./build.sh --reconfigure    强制重新 configure
#   ./build.sh --census         census 链接（全量符号面，只链不跑）
#   ./build.sh --no-selftest    不编 selftest target
#   ./build.sh -j8              指定并行度
#   ./build.sh -- -DSOUI_FR_STACK_WORDS=16384   其余参数原样传给 cmake
#
# Windows 上也可以直接跑 build.bat（它转调本脚本）。
# =============================================================================
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

RUN=""
CLEAN=0
RECONFIG=0
JOBS=""
CMAKE_ARGS=""
OPT_SELFTEST=""
OPT_CENSUS=""

usage() {
    cat <<'EOF'
soui_fr 构建脚本（QEMU mps2-an385 裸机）

  ./build.sh [选项] [-- 额外 cmake 参数]

  --run [app|selftest]  构建完在 QEMU 里实跑（默认两个都跑）
  --clean               先删除 build/ 再全量构建
  --reconfigure         强制重新 configure
  --census              census 链接（--whole-archive、不 gc-sections，只链不跑）
  --no-selftest         不编 soui_fr_selftest.elf
  -jN                   并行度（默认 CPU 核数）
  -h, --help            本帮助
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --run)
            RUN="all"
            if [ $# -ge 2 ]; then
                case "$2" in
                    app|selftest) RUN="$2"; shift ;;
                esac
            fi
            ;;
        --clean)        CLEAN=1 ;;
        --reconfigure)  RECONFIG=1 ;;
        --census)       OPT_CENSUS="-DSOUI_FR_CENSUS=ON" ;;
        --no-selftest)  OPT_SELFTEST="-DSOUI_FR_SELFTEST=OFF" ;;
        -j*)            JOBS="${1#-j}" ;;
        --jobs=*)       JOBS="${1#--jobs=}" ;;
        --)             shift; CMAKE_ARGS="$*"; break ;;
        -h|--help)      usage; exit 0 ;;
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
#      改写成 /mingw64/... 再交给原生 ninja，后者不认这种路径，于是交叉配置失败在
#      "C compiler is not able to compile a simple test program"。msys 版只用不选。
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
    # 注意顺序：后 prepend 的优先级更高。MSYS2 的 mingw64 打头（Ninja、arm 工具链
    # 都在里面），原生 CMake 压轴。/c/msys64/usr/bin 故意不进来 —— 见上面 (a)。
    for d in /c/msys64/mingw64/bin /c/msys64/ucrt64/bin /mingw64/bin /mingw32/bin \
             /c/ProgramData/chocolatey/bin \
             "/c/Program Files/CMake/bin" "/c/Program Files (x86)/CMake/bin"; do
        prepend_path "$d"
    done
    # Arm GNU Toolchain 的安装目录带版本号，按通配展开（x86 版与 arm64 版都算）。
    for d in "/c/Program Files (x86)"/Arm*GNU*Toolchain*/bin \
             "/c/Program Files"/*Arm*GNU*Toolchain*/bin \
             "/c/Program Files (x86)"/GNU*Arm*Embedded*Toolchain*/bin \
             "/c/Program Files"/GNU*Arm*Embedded*Toolchain*/bin; do
        prepend_path "$d"
    done
    for d in "/c/Program Files/qemu"; do
        prepend_path "$d"
    done
else
    for d in /usr/local/bin /opt/homebrew/bin /opt/gcc-arm-none-eabi/bin \
             /opt/homebrew/Caskroom/gcc-arm-embedded/*/gcc-arm-eabi/bin \
             /opt/homebrew/Caskroom/gcc-arm-embedded/*/gcc-arm-none-eabi/bin; do
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
command -v arm-none-eabi-gcc >/dev/null 2>&1 || die_missing "arm-none-eabi-gcc（ARM 交叉工具链）" \
    "  Windows(MSYS2): pacman -S mingw-w64-x86_64-arm-none-eabi-gcc   ← 最简单" \
    "  或者装 Arm GNU Toolchain: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads" \
    "  Linux: sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi" \
    "  macOS: brew install --cask gcc-arm-embedded" \
    "  已经装在别处：把它的 bin 目录加进 PATH 再跑本脚本"

CMAKE_BIN="$(command -v cmake)"
case "$CMAKE_BIN" in
    */usr/bin/cmake|/usr/bin/cmake|*/usr/bin/cmake.exe|C:/msys64/usr/bin/cmake.exe)
        die_missing "原生（非 msys 版）CMake" \
            "  当前选中的是 MSYS2 的 msys 版 cmake：$CMAKE_BIN" \
            "  它会把 C:/msys64/... 改写成 /mingw64/... 再交给 ninja，交叉配置必然失败。" \
            "  装一个原生 CMake 即可（https://cmake.org/download/），脚本会自动优先用它。" ;;
esac

# cmake 一律吃绝对路径：Git Bash 的 cwd 是 /d/work/... 形式，原生 cmake 会
# 把它和 cache 里记的 D:/work/... 当成两个不同目录，直接报
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
echo "   cmake          : $CMAKE_BIN ($("$CMAKE_BIN" --version | head -1))"
echo "   arm-none-eabi  : $(command -v arm-none-eabi-gcc) ($(arm-none-eabi-gcc -dumpversion))"
echo "   并行           : -j$JOBS"

# -----------------------------------------------------------------------------
# 2. configure + build
# -----------------------------------------------------------------------------
if [ "$CLEAN" = 1 ] && [ -d build ]; then
    echo "== 删除 build/ =="
    rm -rf build
fi

CONFIG_LOG=build/configure.log

configure() {
    local -a args=()
    # 首次配置才需要指定 generator / 工具链 / 构建类型；之后它们已在 cache 里。
    if [ ! -f build/CMakeCache.txt ]; then
        args+=(-G Ninja
               -DCMAKE_TOOLCHAIN_FILE="$SRC_DIR/cmake/toolchain-arm-freertos.cmake"
               -DCMAKE_BUILD_TYPE=MinSizeRel)
    fi
    if [ -n "$OPT_SELFTEST" ]; then args+=($OPT_SELFTEST); fi
    if [ -n "$OPT_CENSUS" ];   then args+=($OPT_CENSUS);   fi
    # shellcheck disable=SC2206
    if [ -n "$CMAKE_ARGS" ];   then args+=($CMAKE_ARGS);   fi
    "$CMAKE_BIN" -S "$SRC_DIR" -B "$BUILD_DIR" "${args[@]}" 2>&1 | tee "$CONFIG_LOG"
    return "${PIPESTATUS[0]}"
}

# 为什么这里要判断"要不要 configure"：
#   * 没有 cache 时必须配；
#   * 传了 -D 参数（--census / --no-selftest / -- 之后）必须配；
#   * 其余情况可以省掉 —— 任何 CMakeLists / *.cmake / cache 变化都会让 ninja
#     自己重跑 cmake（生成的 build.ninja 里带 RERUN_CMAKE 规则），结果一样，
#     但少一次生成阶段就能少一次踩下面那个坑的机会，速度也快。
need_configure=0
if [ "$RECONFIG" = 1 ] || [ ! -f build/CMakeCache.txt ]; then
    need_configure=1
fi
if [ -n "$OPT_SELFTEST" ] || [ -n "$OPT_CENSUS" ] || [ -n "$CMAKE_ARGS" ]; then
    need_configure=1
fi

mkdir -p build

if [ "$need_configure" = 1 ]; then
    echo "== configure =="
    if ! configure; then
        # Windows 专有坑（踩过）：ninja 在生成阶段会 `-t restat`，其中一步是把
        # .ninja_log 重压缩后 rename 回去。此时若 .ninja_log 正被杀毒 / 编辑器索引 /
        # 上一次被强杀的 ninja 进程占着，rename 报 "failed recompaction: Permission
        # denied"，CMake 直接以 Generate step failed 收场；更麻烦的是构建历史一起
        # 作废，下一次构建会把几百个 TU 全部重编，看起来像"脚本每次都在全量重编"。
        # 处理：清掉残留的临时文件重试一次；仍失败就给出可操作的提示。
        if grep -qE "failed recompaction|Permission denied" "$CONFIG_LOG" 2>/dev/null; then
            echo "" >&2
            echo "   ! build/ 下的 ninja 状态文件被别的进程占用，清理后重试一次。" >&2
            rm -f build/.ninja_log.recompact build/.ninja_log.restat build/.ninja_lock 2>/dev/null || true
            if ! configure; then
                echo "" >&2
                echo "配置仍然失败：build/.ninja_log 被占用。" >&2
                echo "  关闭占用它的程序（杀毒实时扫描、VS Code / CLion 的索引、残留的 ninja.exe），" >&2
                echo "  或者直接重建整个 build 目录：  ./build.sh --clean" >&2
                exit 1
            fi
        else
            echo "" >&2
            echo "配置失败，完整日志：$HERE/$CONFIG_LOG" >&2
            exit 1
        fi
    fi
else
    echo "== configure =="
    echo "   （复用 build/ 里已有的配置；需要强制重配请加 --reconfigure）"
fi

echo "== build =="
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
for f in build/soui_fr.elf build/soui_fr_selftest.elf; do
    if [ -f "$f" ]; then
        arm-none-eabi-size "$f" | tail -1 | sed "s|^|$(basename "$f")\t|"
    fi
done

# -----------------------------------------------------------------------------
# 3. 可选：QEMU 实跑
# -----------------------------------------------------------------------------
QEMU=""
find_qemu() {
    command -v qemu-system-arm >/dev/null 2>&1 && { QEMU="$(command -v qemu-system-arm)"; return 0; }
    for c in /c/msys64/mingw64/bin/qemu-system-arm.exe "/c/Program Files/qemu/qemu-system-arm.exe" \
             /usr/bin/qemu-system-arm /opt/homebrew/bin/qemu-system-arm; do
        [ -x "$c" ] && { QEMU="$c"; return 0; }
    done
    return 1
}

run_one() {  # run_one <elf> <日志完成标记>
    local elf="$1" want="$2" log pid i
    log="$(mktemp 2>/dev/null || echo ./qemu-run.log)"
    echo "== QEMU: $(basename "$elf") =="
    # 镜像走完自己的链路后会进入 for(;;) 保活，永远不会自己退出。所以不能等进程
    # 结束（那样每个镜像都得干等 120s，两个就是 4 分钟），而是盯着日志：
    # 完成标记一出现就 kill，正常情况 1～3 秒收工。120s 只是兜底上限。
    "$QEMU" -M mps2-an385 -nographic -semihosting -kernel "$elf" >"$log" 2>&1 &
    pid=$!
    i=0
    while [ "$i" -lt 240 ]; do
        if grep -qF "$want" "$log" 2>/dev/null; then break; fi
        if ! kill -0 "$pid" 2>/dev/null; then break; fi   # 自己退了（崩溃 / 异常退出）
        sleep 0.5
        i=$((i + 1))
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    tr -d '\r' <"$log" | tail -12
    if grep -qF "$want" "$log" 2>/dev/null \
       && ! grep -qE "ASSERT|STACK OVERFLOW|HardFault|qemu: fatal" "$log"; then
        echo "-- PASS ($want)"
        return 0
    fi
    if [ "$i" -ge 240 ]; then
        echo "-- FAIL：120s 内日志没有出现 '$want'（镜像挂死？），完整日志：$log" >&2
        return 1
    fi
    echo "-- FAIL：日志里没有 '$want'（或出现了故障标记），完整日志：$log" >&2
    return 1
}

if [ -n "$RUN" ]; then
    find_qemu || die_missing "qemu-system-arm" \
        "  Windows(MSYS2): pacman -S mingw-w64-x86_64-qemu" \
        "  Linux: sudo apt install qemu-system-arm     macOS: brew install qemu"
    echo "   qemu           : $QEMU"
    rc=0
    if [ "$RUN" = "all" ] || [ "$RUN" = "app" ]; then
        run_one build/soui_fr.elf "SOUISAPP FREERTOS DONE" || rc=1
    fi
    if [ "$RUN" = "all" ] || [ "$RUN" = "selftest" ]; then
        run_one build/soui_fr_selftest.elf "SOUI_FR SELFTEST PASS" || rc=1
    fi
    [ "$rc" = 0 ] || exit 1
    echo ""
    echo "两个镜像都跑通了。（帧缓冲 dump 在 $HERE/soui_fb.ppm 和 fb.ppm）"
    exit 0
fi

cat <<EOF

下一步：
  在 QEMU 里实跑        ./build.sh --run
  只跑平台层自测        ./build.sh --run selftest
  全量符号面（census）  ./build.sh --census
  改编译配置            cmake/project.cmake（板级文件在 board/）
EOF
