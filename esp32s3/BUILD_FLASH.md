# SOUI4 / ESP32-S3 Demo：编译与烧录速查

> 全部命令在 **Git Bash** 里执行，工作目录固定为
> `/d/work/soui4/soui-freertos/esp32s3`。
> 本文每条命令都已在本机实测通过（2026-11-13，固件 `soui_esp32s3.bin` = 4,495,680 B）。

---

## 0. 三行版本
# 编译
```bash
cd /d/work/soui4/soui-freertos/esp32s3
./tools/idf_env.sh ninja -C build -j2                                  
```
# 烧录
```bash
cd build && ../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 -b 921600 write_flash "@flash_args"
```

烧完回工程根目录看日志：

```bash
cd /d/work/soui4/soui-freertos/esp32s3 && python tools/serial_capture.py COM4 12
```

上面是 **Git Bash** 写法。想在 Windows PowerShell 或 cmd 里跑是可以的，但有三处
必须改（`@flash_args` 要加引号、`python` 要指向 IDF 的 venv），见 **第 7 节**。

下面是把这三行拆开讲清楚的部分。

---

## 1. 前提

| 项 | 值 |
|---|---|
| 工程目录 | `D:\work\soui4\soui-freertos\esp32s3`（`soui-freertos` 必须在 `soui4` 目录下，`CMakeLists.txt` 靠 `../../` 找 `SOUI/include/souistd.h`） |
| ESP-IDF | v5.4.1，`D:\work\xiaozi\2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0\esp-idf-v5.4.1` |
| 工具链 | Xtensa GCC 14.2.0 / CMake 3.30.2 / Ninja 1.12.1 / esptool 4.13.0 / Python 3.13.16 |
| 串口 | `COM4`（原生 USB-Serial/JTAG，303A:1001） |
| 烧录波特率 | 921600（462800/460800 也能用，只是慢一倍） |
| 日志波特率 | 115200 8N1（由 `CONFIG_ESP_CONSOLE_UART_BAUDRATE` 决定） |
| Flash | 16 MB，`dio` / 80 MHz |
| 屏 | ILI9341V，SPI，横屏 320×240 |

**不要用官方的 `export.sh` / `export.ps1`**（Git Bash 撞 `MSys/Mingw is not supported`，
PowerShell 撞 `powershell.EXE` 大小写 bug）。环境统一由 `tools/idf_env.sh` 装配：
它把工具链目录塞进 `PATH`，并补上两个必需的变量 `ESP_ROM_ELF_DIR`、`OPENOCD_SCRIPTS`。

`tools/idf_env.sh` 两种用法：

```bash
./tools/idf_env.sh ninja -C build -j2      # 跑一条命令后退出（推荐，本文都用这个）
source tools/idf_env.sh                    # 只装配环境，后续命令直接敲
```

如果 IDF 装在别处，用 `IDF_ROOT` 覆盖：

```bash
IDF_ROOT=/d/work/xxx ./tools/idf_env.sh ninja -C build -j2
```

确认串口编号：

```bash
./tools/idf_env.sh python -m serial.tools.list_ports
# 2 ports found
# COM3
# COM4
```

---

## 2. 编译

### 2.1 日常增量编译（99% 的情况）

```bash
cd /d/work/soui4/soui-freertos/esp32s3
export CCACHE_DISABLE=1
./tools/idf_env.sh ninja -C build -j2 1> b.out 2> b.err
echo "exit=$?"
```

预期输出（`b.err` 末尾）：

```
soui_esp32s3.bin binary size 0x449940 bytes. Smallest app partition is 0x600000 bytes. 0x1b66c0 bytes (29%) free.
Bootloader binary size 0x57c0 bytes. 0x2840 bytes (31%) free.
```

`exit=0` 即成功。失败时看 `b.err`（重定向是为了不让并行输出打散报错）。

改了 `CMakeLists.txt` 不用手动重配，ninja 会自己重跑 cmake 的 regeneration 规则；
改了 `sdkconfig` 靠 IDF 的 `confgen` 依赖触发。这两种情况下 `exit` 仍应为 0，
但注意 2.3 的第 3 条。

### 2.2 从零配置（只在删掉了 `build/` 时）

```bash
cd /d/work/soui4/soui-freertos/esp32s3
./tools/idf_env.sh cmake -B build -G Ninja -DPYTHON_DEPS_CHECKED=1 .
./tools/idf_env.sh ninja -C build -j2 1> b.out 2> b.err
```

**`-DPYTHON_DEPS_CHECKED=1` 不是可选的。** 少了它，配置阶段必定失败：

```
-- Checking Python dependencies...
ERROR: MSys/Mingw is not supported. Please follow the getting started guide ...
CMake Error at .../tools/cmake/build.cmake:422 (message):
  Some Python dependencies must be installed.
```

原因：`esp-idf-v5.4.1/tools/idf_tools.py` 的 `__main__` 开头有一句

```python
if 'MSYSTEM' in os.environ:
    fatal('MSys/Mingw is not supported. ...')
```

Git Bash 里 `MSYSTEM=MINGW64` 一定存在，而 `cmake` 配置时会调
`idf_tools.py check-python-dependencies`。`-DPYTHON_DEPS_CHECKED=1` 让
`project.cmake:44` 把 `__CHECK_PYTHON` 置 0，跳过这一步。

（现有 `build/CMakeCache.txt` 里已经缓存了 `PYTHON_DEPS_CHECKED=1`，所以
**平时的增量编译和 CMakeLists 触发的重配都不会撞到这个错误**；只有新建 build 目录才会。）

源目录是 `.` 而不是 `..`——`esp32s3/` 自己就是 CMake 工程根（`project(soui_esp32s3)`）。

### 2.3 三条硬规则

1. **必须 `-j2`。** 本机默认并行度会随机把 `cc1plus` 挂死（CPU 归零、20 分钟无进展），
   清进程、关 ccache 都无效，降到 `-j2` 立刻恢复。挂死了就：

   ```bash
   Stop-Process cc1plus,cc1,ninja -Force    # PowerShell
   ./tools/idf_env.sh ninja -C build -j2     # 增量续编，不需要 clean
   ```

2. **只用 `ninja`，`idf.py` 一律不用。** `idf.py` 在这台机器上打印一行
   `MSys/Mingw is no longer supported` 然后**退出码 0、什么都不做**：

   ```
   $ ./tools/idf_env.sh idf.py --version
   MSys/Mingw is no longer supported. ...
   $ echo $?
   0
   ```

   所以 `idf.py build` / `idf.py flash` / `-C` / `menuconfig` 全部无效。
   编译走 `ninja -C build`，烧录走 `esptool ... @flash_args`。

3. **全量编译 15~20 分钟。** 源码含 cairo + pixman + richedit41，`-j2` 下就是这个
   速度，别以为是挂死。全量触发条件：改 `sdkconfig`、改全局 `-O` 级别、删 `build/`。

### 2.4 改了 UI 资源要重跑生成器

`main/generated/uires_res.cpp` 是把 `uires` 目录（XML / PNG / SVG）编成 C++ 表的产物，
改了布局就必须重新生成，否则屏幕上看到的还是旧界面：

```bash
python tools/gen_uires.py \
    --app-uires /d/work/soui4/soui-freertos/soui_sim/uires \
    --sys-res /d/work/soui4/demos/android-demo/app/src/main/assets/soui_sys_res/theme_sys_res \
    --out main/generated/uires_res.cpp --strip-window-cache
```

`--strip-window-cache` 不是可选项：不加的话根窗口的 `cache="1"` 会让每帧整窗重绘
加整幅 AlphaBlend。生成后再走 2.1 编译。

（这个脚本用系统 `python` 就跑得了，不需要 IDF 环境。）

### 2.5 产物

```
build/soui_esp32s3.bin                      ← 应用，约 4.5 MB
build/bootloader/bootloader.bin             ← 0x0
build/partition_table/partition-table.bin   ← 0x8000
```

---

## 3. 烧录

### 3.1 地址表

`build/flash_args` 由 CMake 生成，内容固定为：

```
--flash_mode dio --flash_freq 80m --flash_size 16MB
0x0     bootloader/bootloader.bin
0x10000 soui_esp32s3.bin
0x8000  partition_table/partition-table.bin
```

分区表（`partitions.csv`）：

| Name | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | `0x6000` |
| `phy_init` | data/phy | `0xf000` | `0x1000` |
| `factory` | app | `0x10000` | `0x600000`（6 MB） |

### 3.2 命令

必须在 `build/` 目录下执行——`@flash_args` 里的路径是相对 `build/` 的：

```bash
cd /d/work/soui4/soui-freertos/esp32s3/build
../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 -b 921600 \
    write_flash "@flash_args"
```

预期输出（末尾）：

```
Wrote 4495680 bytes (2164546 compressed) at 0x00010000 in 33.7 seconds (effective 1066.0 kbit/s)...
Hash of data verified.
Compressed 3072 bytes to 104...
Writing at 0x00008000... (100 %)
Wrote 3072 bytes (104 compressed) at 0x00008000 in 0.1 seconds ...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

看到 `Hash of data verified` + `Hard resetting via RTS pin` 就是成功，板子会自动重启进新固件。

注意 `Hard resetting via RTS pin` 只是复位，**不会**自动打印日志——要看日志得跑第 4 节。

### 3.3 参数说明

| 参数 | 说明 |
|---|---|
| `--chip esp32s3` | 必须显式指定，否则 esptool 会去探测（慢且在这块板上偶发失败） |
| `-p COM4` | 换了 USB 口就变，用 1 节的 `list_ports` 查 |
| `-b 921600` | 4.5 MB 约 34 s。降到 `460800` 约 70 s，只在 921600 报 `Failed to connect` 时用 |
| `@flash_args` | 读地址表，等价于手写三个 `0x0 xxx.bin` |

只烧应用（bootloader / 分区表没动时）：

```bash
../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 -b 921600 \
    write_flash 0x10000 soui_esp32s3.bin
```

省不了多少时间（大头就是 4.5 MB 的 app），但改了分区表之后想单独回刷 app 时有用。

擦全片（回到出厂状态，慎用）：

```bash
../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 erase_flash
```

擦完必须重新走 3.2 全量烧录。

---

## 4. 观察日志

```bash
cd /d/work/soui4/soui-freertos/esp32s3
python tools/serial_capture.py COM4 12
```

脚本做的事：拉 DTR/RTS 复位进运行模式 → 以 115200 抓 12 秒 → 打印。非交互，
可以直接重定向到文件：

```bash
python tools/serial_capture.py COM4 30 > cap.txt 2>&1
grep -E "fps=|panic|abort|WDT" cap.txt
```

正常启动 + 稳态的样子：

```
I (1357) soui_app: == SOUI4 on ESP32-S3 (soui_sim demo) ==
I (7385) soui_app: fps=21.3, paint=41.7ms, flush=0.9ms | presents=278 (+278/3s, 4.34/frame), pixels=507800 (+507800/3s), 169 kpix/s
I (10492) soui_app: fps=22.7, paint=39.4ms, flush=0.4ms | presents=568 (+290/3s, 4.26/frame), pixels=720208 (+212408/3s), 71 kpix/s
```

健康基线（双动画：橙块扫动 + 蓝块 alpha 呼吸）：

| 指标 | 正常范围 | 含义 |
|---|---|---|
| 首个 `== SOUI4 ... ==` | 1.0~1.5 s | 固件起来 |
| 首帧上屏 | 约 2.6 s | 布局 + SVG 皮肤 + cairo 首帧 + 整屏 SPI |
| `fps` | 20~23 | **看这个**，等于 `1000/(paint+flush)` |
| `paint` | 39~46 ms | 一帧的绘制耗时 |
| `flush` | < 1.5 ms | 提交到屏 |
| `presents` | 4~5 次/帧 | **不是帧率**，是脏矩形 blit 次数 |
| `panic` / `abort` / `WDT` | 无 | 有就是崩了 |

`presents` 和 `fps` 别搞混：一帧会发好几次脏矩形提交，早期把 `presents/3` 当帧率
汇报过，差了 4 倍多。

---

## 5. 常见故障对照

| 现象 | 原因 | 处理 |
|---|---|---|
| `ninja: error: rebuilding 'build.ninja': subcommand failed` | `ESP_ROM_ELF_DIR` 没设（`gdbinit.cmake` 的 `file(TO_CMAKE_PATH)` 参数不足） | 用 `./tools/idf_env.sh` 包住命令，别直接敲 `ninja` |
| 配置阶段 `ERROR: MSys/Mingw is not supported` | 新建 build 目录时没带 `-DPYTHON_DEPS_CHECKED=1` | 见 2.2 |
| `idf.py` 只打一行警告、`exit=0`、什么都没编 | idf.py 在这台机器上直接空转 | 不要用 idf.py，见 2.3 |
| 编译卡住、CPU 归零、20 分钟没动静 | 并行度太高挂死 `cc1plus` | 杀进程后 `-j2` 增量续编 |
| 烧录 `Failed to connect to ESP32-S3` | 串口被占用 / 没进下载模式 | 关掉串口助手；按住 BOOT 再点复位；降 `-b 460800` |
| 烧完屏幕没反应、日志停在 `rst:0x1` | 只烧了 app 而分区表/引导不匹配 | 3.2 全量烧 `@flash_args` |
| `bash: ./tools/idf_env.sh: No such file or directory`，可 `ls ./tools/idf_env.sh` 偏偏能看到 | 命令行里混进了**不可见字符**（BOM `U+FEFF` / 零宽空格 `U+200B`），从网页或聊天窗口复制时带进来的；文件本身没问题 | 重打这行，别粘贴。自查：`history \| tail -3 \| cat -A`，行首出现 `M-oM-;M-?`（BOM）或 `M-bM-^@M-^K`（ZWSP）就是它 |
| 屏幕上的界面和改过的 XML 不一致 | 没重跑 `gen_uires.py` | 见 2.4，然后重编重烧 |
| `fps` 掉到 14 左右 | 有东西整屏重绘（窗口 `cache="1"`、整屏 compose） | 查 `--strip-window-cache`、看 README 10.x |

---

## 6. 一键脚本

存成 `build_flash.sh` 放在 `esp32s3/` 下，`chmod +x` 后直接跑：

```bash
#!/usr/bin/env bash
# 编译 + 烧录 + 抓 12 秒日志。失败立刻退出。
set -e
cd "$(dirname "$0")"

echo "== 编译 =="
export CCACHE_DISABLE=1
./tools/idf_env.sh ninja -C build -j2 1> b.out 2> b.err || { tail -30 b.err; exit 1; }
tail -2 b.err

echo "== 烧录 =="
cd build
../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 -b 921600 write_flash "@flash_args"
cd ..

echo "== 日志 =="
python tools/serial_capture.py COM4 12
```

---

## 7. 换个 shell：Git Bash / PowerShell / cmd

**不需要 MinGW 的编译器。** `xtensa-esp32s3-elf-gcc.exe`、`cmake.exe`、`ninja.exe`、
`python.exe`、`esptool` 全是**原生 Windows 程序**，跟 MinGW 工具链无关。Git Bash
在这里的作用只是"能跑 `.sh` 的壳"，因为 `tools/idf_env.sh` 是 bash 脚本。所以：

| Shell | 编译 | 从零配置 | 烧录 | 抓日志 |
|---|---|---|---|---|
| **Git Bash** | ✅ 实测 | ✅ 实测，**必须** `-DPYTHON_DEPS_CHECKED=1` | ✅ 实测 | ✅ 实测 |
| **PowerShell** | ✅ 实测 | ✅ 实测，**不需要**那个 define | ✅ 实测，**`@flash_args` 必须加引号** | ✅ 实测，**python 必须指 venv** |
| **cmd** | 预期 ✅（未直接实测，见下） | 预期 ✅ | 预期 ✅ | 预期 ✅ |

三种 shell 的差别只有"怎么把环境变量摆好"，编译器和产物完全一样。

### 7.1 为什么 cmd 里反而少一个坑

`idf_tools.py` 的 MSys 检查是 `if 'MSYSTEM' in os.environ`。`MSYSTEM=MINGW64`
**只有 Git Bash 会设**，PowerShell / cmd 里没有这个变量 → 依赖检查直接通过：

```
PS> python .../tools/idf_tools.py check-python-dependencies
Python requirements are satisfied.
```

所以那个 `-DPYTHON_DEPS_CHECKED=1` 纯粹是给 Git Bash 用的补丁，别在 PowerShell 里
无条件照抄（加了也无害，只是没必要）。

### 7.2 PowerShell：可直接粘贴

`tools/idf_env.sh` 是 bash 脚本，PowerShell 跑不了，得把这四个变量摆好：

```powershell
$ROOT  = "D:\work\xiaozi\2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0"
$TOOLS = "$ROOT\.espressif"
$IDF   = "$ROOT\esp-idf-v5.4.1"

$env:IDF_PATH         = $IDF
$env:IDF_TOOLS_PATH   = $TOOLS
$env:ESP_ROM_ELF_DIR  = "$TOOLS\tools\esp-rom-elfs\20241011\"
$env:CCACHE_DISABLE   = "1"
$env:PATH = "$TOOLS\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;" +
            "$TOOLS\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64;" +
            "$TOOLS\tools\ninja\1.12.1\ninja;" +
            "$TOOLS\tools\cmake\3.30.2\bin;" +
            "$TOOLS\python_env\idf5.4_py3.13_env\Scripts;" +
            "$IDF\tools;" + $env:PATH

Set-Location D:\work\soui4\soui-freertos\esp32s3
ninja -C build -j2                                    # 编译
Set-Location build
python -m esptool --chip esp32s3 -p COM4 -b 921600 write_flash "@flash_args"
Set-Location ..
python tools\serial_capture.py COM4 12                 # 抓日志
```

三个 PowerShell 专属坑，都是实测撞出来的：

1. **`@flash_args` 必须加引号写成 `"@flash_args"`。** PowerShell 把行首的 `@`
   当 splatting 运算符（`@varname` 展开成参数数组），`@flash_args` 被静默吞掉，
   esptool 收到的是空参数列表：

   ```
   esptool write_flash: error: the following arguments are required: <address> <filename>
   ```

   加引号后 PowerShell 原样传递，esptool 自己去展开这个响应文件，正常工作。
   （Git Bash 里加不加引号都行，**加引号是三种 shell 都安全的写法**，建议一律加。）

2. **`python` 可能不是 IDF 那个。** 全新的 PowerShell 里 `python` 常解析到
   `C:\Users\<you>\AppData\Local\Programs\Python\Python313\python.exe`，它**没装
   pyserial**，`serial_capture.py` 会 `ModuleNotFoundError: No module named 'serial'`。
   确认一下：

   ```powershell
   (Get-Command python).Source
   # 要看到 ...\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe
   ```

   不对就说明 PATH 里 venv 的 `Scripts` 没排在最前面。应急写法是直接用全路径：

   ```powershell
   & "$TOOLS\python_env\idf5.4_py3.13_env\Scripts\python.exe" tools\serial_capture.py COM4 12
   ```

3. **看退出码要用 `$LASTEXITCODE`，不是 `$?`。** `ninja` / `esptool` 是原生程序，
   `$?` 只能告诉你"上一条语句没抛异常"；判断成败要 `if ($LASTEXITCODE -ne 0)`。
   另外 PowerShell 5.1 对原生命令的 stderr 会包成 `NativeCommandError` 打印一堆红字，
   但**退出码仍是 0 就是成功**，别被红色输出吓到。

### 7.3 cmd：批处理写法

PowerShell 的限制不适用于 cmd（`@flash_args` 在 cmd 里原样传给程序，esptool 正常展开）：

```bat
set ROOT=D:\work\xiaozi\2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0
set IDF_PATH=%ROOT%\esp-idf-v5.4.1
set IDF_TOOLS_PATH=%ROOT%\.espressif
set ESP_ROM_ELF_DIR=%ROOT%\.espressif\tools\esp-rom-elfs\20241011\
set CCACHE_DISABLE=1
set PATH=%ROOT%\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%ROOT%\.espressif\python_env\idf5.4_py3.13_env\Scripts;%ROOT%\.espressif\tools\ninja\1.12.1\ninja;%ROOT%\.espressif\tools\cmake\3.30.2\bin;%IDF_PATH%\tools;%PATH%

cd /d D:\work\soui4\soui-freertos\esp32s3
ninja -C build -j2
cd build
python -m esptool --chip esp32s3 -p COM4 -b 921600 write_flash @flash_args
```

> **这一小节没有直接跑过**——当前会话的工具链不允许我调起 `cmd.exe`，所以上面是
> 按 PowerShell 实测结果推的。依据有两条：① `build.ninja` 里的每条规则本身就是
> `C:\Windows\system32\cmd.exe /C "...python.exe ..."`，也就是 ninja 内部一直在用
> cmd，对外层 shell 没有要求；② cmd 不把行首 `@` 当特殊字符，且 `flash_args` 里
> 没有 `%`、`!` 这类会被 cmd 展开的东西。如果真跑出问题，十有八九是 `set PATH`
> 少了一段，用 `echo %PATH%` 对照 7.2 的六个目录。

### 7.4 该选哪个

- **日常改代码 → 编译 → 烧录**：用 **Git Bash**，`tools/idf_env.sh` 一行搞定，
  不用记那六个路径。本文 2~4 节全部是这个写法。
- **要在 VS Code / CLion 里点按钮编译**：用 **PowerShell**（7.2 那段塞进 profile
  或 `.vscode/settings.json` 的 `terminal.integrated.env.windows`）。
- **写给别人的自动化脚本**：用 **cmd** 批处理，最不容易被 PowerShell 的
  splatting / 引号规则坑到。

---

## 8. 相关文档

- `README.md`：整体设计、11 个移植期修复的缺陷、性能优化的完整数据（第 10 节）。
- `README.md` 第 3~5 节：本文的详细背景（为什么不用 export.sh / idf.py）。
- `tools/idf_env.sh` 头部注释：`PATH` 装配逻辑与两个变量的来历。
