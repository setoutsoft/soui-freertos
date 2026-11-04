# soui-freertos — SOUI FreeRTOS 工作区

SOUI4/SOUI5 移植 FreeRTOS 的全部实验工程。上游源码在 `D:/work/soui4`（SOUI core + swinx 平台层），本仓库只放各目标环境的工程胶水与测试程序。

> 本仓库已作为 **soui4 的 git 子模块**（`soui4/soui-freertos`）。子模块场景下各 CMakeLists 自动以 `../..` 定位 SOUI 源码；独立 checkout 时回退 `D:/work/soui4`。

## 工程一览

| 目录 | 目标 | 说明 |
|------|------|------|
| `cmake/soui.cmake` | — | **唯一构建入口**：`include` + `soui_fr_project()` 即完成整环境搭建 |
| `sim/` | Windows 宿主 | SDL2 宿主运行时（窗口/出图/鼠标键盘注入），被 `SIM_HOST` 工程复用 |
| `templates/app/` | Windows 宿主 | 新建工程模板：复制即用，构建脚本 2 行 |
| `demo/` | QEMU mps2-an385 (Cortex-M3) | 裸机 FreeRTOS 冒烟：UART 输出 + 调度器跑通，移植 earliest 基线 |
| `swinx_test/` | QEMU mps2-an385 | swinx freertos 平台层单测（fb/input/syncobj/render 等 main_*.cpp 变体） |
| `soui_fr/` | QEMU mps2-an385 | 完整 SOUI SApp 冒烟：XML 布局 → render-gdi(cairo) → LCD fb（截图由程序运行时落盘） |
| `soui_sim/` | Windows 宿主 (MinGW) | **FreeRTOS Windows 模拟器**：FreeRTOS-Kernel MSVC-MingW port + swinx freertos 层 + SDL2 出图/鼠标注入，与 QEMU 目标 95% 同构，用于快速迭代交互测试 |
| `FreeRTOS-Kernel/` | — | vendored 内核（demo / soui_fr / swinx_test / soui_sim 共用，含 `portable/MSVC-MingW`） |

仓库只保留源码与构建脚本：构建目录、截图帧、一次性探针脚本都不入库（`.gitignore` 已覆盖 `build*/`、`*.log`、`*.ppm`）。

## 外部依赖（不在本仓库）

- soui4 工作目录 — SOUI/swinx 源码。本仓库是 soui4 的子模块，固定检出在 `<soui4>/soui-freertos`，
  `SOUI_FR_ROOT` 由本文件位置推得（`cmake/` 往上两级），不用环境变量、也不写死盘符
- MSYS2 mingw64（gcc 15.2.0、SDL2、cmake+ninja）；QEMU（qemu-system-arm，mps2-an385）+ arm-none-eabi 工具链（QEMU 三工程）

FreeRTOS 内核默认用本仓库 vendored 的 `FreeRTOS-Kernel/`（含 `portable/MSVC-MingW`，soui_sim 直接复用），不再需要外部 `lvgl_simulator` 那份。

## 新建项目：一个 include + 一个函数

```bash
cp -r soui-freertos/templates/app  my_app
cmake -S my_app -B my_app/build -G Ninja
ninja -C my_app/build && ./my_app/build/hello_freertos.exe
```

`my_app/CMakeLists.txt` 全文（除 `project()`/编译标准外）就这两行：

```cmake
include(<soui-freertos>/cmake/soui.cmake)
soui_fr_project(hello_freertos SOURCES main.cpp SIM_HOST)
```

`soui_fr_project()` 一次做完：

| 步骤 | 内容 |
|---|---|
| 内核 | `FreeRTOS-Kernel` + 按宿主/裸机自动选 port（`MSVC-MingW` / `GCC/ARM_CM3`）、heap_3 |
| 三方库 | cairo/pixman/libpng/zlib，宿主默认用系统包（pkg-config 能证明时），交叉编译强制 vendored 源码 |
| 平台层 | swinx freertos 层（`src/*.cpp` + `platform/freertos/*` + cairo gdi 后端/字体桥） |
| SOUI | utilities + core + render-gdi + imgdecoder-stb（whole-archive 保留 COM 注册对象） |
| 资源 | `uires/uires.idx` → uiresbuilder → windres COFF → `.incbin` 数据段（`.balign 4`，Cortex-M 非对齐会 fault）。只有 uiresbuilder 那一步是 Windows 独占（`tools/uiresbuilder.exe`）；windres/incbin 任何装了 mingw 的宿主都能跑。产物 `soui_res.rc2` + `resource.h` 输出到源码目录 `<项目>/<name>_res/` 并**入库**，所以换平台/换机器直接复用，不必先在 Windows 上过一遍 |
| 启动 | 和 wizard 工程一样自己写：`SApplication` + `SAppCfg`（`SetRender(Render_Gdi)` / `SetImgDecoder(ImgDecoder_Stb)` / `SetSysResPeHandle` / `SetAppResPeHandle`）→ `cfg.DoConfig(&app)`；`SAppCfg` 内部把 render-gdi 工厂与 imgdecoder-stb 一起注册好（缺后者会在 `LoadSystemNamedResource` 里解码系统皮肤时静默段错误），不再是框架提供的"启动胶水" |
| 宿主出图 | `SIM_HOST` 时链 `sim/soui_sim_hal.cpp`（SDL2 窗口 / ARGB 纹理 / 鼠标键盘注入） |
| Windows 运行 | system-deps 构建自动把 20 个运行时 DLL 收拢到 exe 同级 |

参数（全部可选）：`SOURCES`（必填）、`RESOURCES`（默认 `./uires`）、`RESOURCE_SCRIPT`（默认自动生成）、`NO_RESOURCES`、`FREERTOS_CONFIG`（默认 `./config`）、`SIM_HOST`、`INCLUDES`、`LIBRARIES`、`OUTPUT_NAME`。裸机交叉编译只需换 toolchain file 并去掉 `SIM_HOST`。

需要更细的控制时，底层 API 仍然可用：

```cmake
include(<soui-freertos>/cmake/soui.cmake)
soui_fr_add_all()
add_executable(app main.cpp)
soui_fr_link_app(app)                 # 等价于 target_link_libraries(app PRIVATE soui_fr::app)
```

各项依赖（`soui_fr::thirdparty` / `::swinx` / `::soui` / `::freertos` / `::sim_host`）都是 INTERFACE target，可以直接链。

## `soui_sim` 的目录结构（对齐 `wizard/CmakeApp`）

`soui_sim` 现在就是一个标准 SOUI wizard 工程，只是跑在 FreeRTOS 任务 + SDL 宿主壳里：

| 文件 | 与 wizard 的对应关系 |
|---|---|
| `CMakeLists.txt` | 同 wizard：`file(GLOB SOURCES/HEADERS)` + `source_group` + `target_precompile_headers(stdafx.h)`，构建细节收进 `soui_fr_project()` |
| `stdafx.h` / `stdafx.cpp` | 同 wizard：`#define SCOM_MASK (scom_mask_render_gdi\|scom_mask_imgdecoder_stb)` + `souistd.h` + 控件头 + `resource.h` |
| `MainDlg.h` / `MainDlg.cpp` | 同 wizard：`SHostWnd` + `SDpiHandler<CMainDlg>`，`EVENT_MAP_BEGIN()` / `EVENT_NAME_COMMAND` / `EVENT_ID_COMMAND(R.id.xxx, …)` / `EVENT_MAP_END2(SHostWnd)` + `BEGIN_MSG_MAP_EX` |
| `main.cpp` | 同 wizard：`SApplication` + `SAppCfg::DoConfig()` + `InitXmlNamedID(R.name, R.id, …)`；差别只是 `app.Run()` 换成宿主消息泵（FreeRTOS 任务里 pump + 事件投递） |
| `main.rc` | 同 wizard：`#include "soui_res.rc2"` + `theme_sys_res.rc2` |
| `uires/uires.idx` | 同 wizard：`XML_INIT` / `XML_MAINWND` / `values`（string\|color\|skin）/ `PNG` / `SVG` |
| `uires/xml/dlg_main.xml` | 同 wizard：`_skin.sys.wnd.bkgnd` 背景 + `<caption>` 标题栏（`_skin.sys.btn.minimize` / `.close`）+ `class="normalbtn"` 按钮 |
| `uires/uidef/init.xml` | 同 wizard：`<unit>` / `<deffont>` / 样式类；文案与颜色走 `@string/*`、`@color/*` |
| `config/FreeRTOSConfig.h` | FreeRTOS 专有（栈、heap_3、hook 开关） |
| `sim/soui_sim_hal.*` | 模拟器专有的宿主壳：SDL2 窗口 / ARGB 纹理 / 鼠标键盘注入 |

两处当前仍与 wizard 不同（受平台层能力限制）：

- swinx freertos 的 `LoadImageW/A`、`CreateIconFromResource` 还是 NULL 桩，所以 `<icon src="ICON_LOGO">` / `bigIcon` 用不了 —— 标题栏 logo 改成 `<img skin="app_logo">`（PNG 皮肤）。
- 该工具链不定义 `_UNICODE`（`TCHAR == char`），字符串字面量一律 `_T(...)`，`L"..."` 不能直接喂给 `LPCTSTR` 接口。

## 工作原理（soui_sim）

`soui_sim` 不是"又一个 SDL demo"，而是一份跑在**真实 FreeRTOS 任务**里的标准 SOUI 应用：

- 入口在原生 `main()` 线程上 `xTaskCreate(app_task, …)` + `vTaskStartScheduler()`；`app_task` 里 `SApplication` + `SAppCfg::DoConfig()` 起 SOUI，然后在 FreeRTOS 任务里跑宿主消息泵——`SimHalPumpEvents()` 把 SDL 事件转成 swinx 输入、`SimHalRenderIfDirty()` 把 swinx 帧缓冲刷到 SDL 纹理，循环条件 `while(IsWindow(hWnd))`。
- 出图与输入由 `sim/soui_sim_hal.cpp`（SDL2 窗口 / ARGB 纹理 / 鼠标键盘注入）承担，被 `SIM_HOST` 工程复用；与真实 FreeRTOS 目标唯一的差别只是"窗口系统后端"换成了 SDL。
- **退出路径是确定性的**：窗口 `DestroyWindow` 后 `IsWindow` 落到 `WndMgr` 注册表（窗口已摘除即返回 FALSE），循环退出 → `app_task` 调 `vTaskEndScheduler()` 让内核辅助线程 inert，再以 `ExitThread(0)` 结束自身任务线程；`vTaskStartScheduler()` 返回原生 `main()` 线程，`main()` 用 `_exit(s_appRc)` 直接终止（不走 `return`）。早期版本里 `SConnection::IsWindow` 恒真（循环永不退出）、`vTaskEndScheduler` 后走 `vTaskDelete` 自删（触发内核 `port.c` 的 `configASSERT(xPortRunning)`）、以及 C++ 静态析构在调度器停转后删除 FreeRTOS 信号量再进 `vPortExitCritical`（同样触发该断言）——都已修复，且修复**全部在 soui-freertos 侧**：`ExitThread(0)` 绕开 `vTaskDelete`、`_exit()` 跳过静态析构（`SDL_QUIT` 分支同样走 `_exit(0)`）。`FreeRTOS-Kernel` 是 vendored 的内核，保持原样不改动。

它与 QEMU 裸机目标**同源同代码**：swinx freertos 平台层、SOUI 应用、XML 布局完全一致，只是出图后端（SDL vs LCD 帧缓冲）和是否在真实内核上跑不同。

## 构建

```bash
# 宿主模拟器：默认优先链接系统 cairo/pixman/libpng/zlib（pkg-config 能证明存在时）
cmake -S soui_sim -B soui_sim/build_sys -G Ninja
ninja -C soui_sim/build_sys
./soui_sim/build_sys/soui_sim.exe

# 想用 vendored 源码（可复现 / 交叉编译一定是这条路）
cmake -S soui_sim -B soui_sim/build -G Ninja -DSOUI_FR_USE_SYSTEM_DEPS=OFF

# QEMU 工程（demo / swinx_test / soui_fr）保持各自 CMake
cmake -S <proj> -B <proj>/build -G Ninja && ninja -C <proj>/build
```

`SOUI_FR_USE_SYSTEM_DEPS` 是一次写入 cache 的普通布尔选项（`-D` 之后不会在下次 reconfigure 被自动探测覆盖）；交叉编译/裸机永远走 vendored 源码。

`SOUI_FR_USE_SYSTEM_DEPS=ON` 时会在链接后把运行时 DLL 收拢到 exe 同级目录（`cmake/CopyRuntimeDlls.cmake`）——PATH 里若存在另一套 MinGW（PortableGit / Android platform-tools / xampp）会影子掉 `libwinpthread-1.dll`、`zlib1.dll`、`libiconv-2.dll`，进程在 `main()` 之前就以 `0xc0000139` 退出；exe 目录优先级最高，收拢后与 PATH 无关。

### 怎么上手体验

```bash
./soui_sim/build_sys/soui_sim.exe
```

窗口起来后直接**用鼠标点**：悬停 `btn_test` / `btn_anim` 看 hover 与 `animate="1"` 的淡入，按住看 push 态，点 `btn_test` 在 PNG / SVG 之间换图（走 `EVENT_ID_COMMAND(R.id.btn_test)`），点 `btn_anim` 冻结时间轴动画；标题栏右侧的最小化/关闭是 `_skin.sys.*` 系统皮肤。这就是全部——输入走 SDL → swinx → 真实 SOUI 窗口代码，没有任何脚本注入或像素比对环节。

模拟器不再内置自动化测试：交互就是人点、眼睛看。要做回归时，验证点本身是"点一下，界面有没有对"；需要留证据就系统截图（Win+Shift+S）。

`soui_sim/screenshots/` 存定妆图（如 `00_idle.png`）；需要更多证据用系统截图（Win+Shift+S）。

## 调试与排障

- **无窗口 / 秒退（`0xc0000139`，进不了 `main()`）**：PATH 里另一套 MinGW（PortableGit / Android platform-tools / xampp）的 `libwinpthread-1.dll` / `zlib1.dll` / `libiconv-2.dll` 影子掉了 exe 旁边的同名 DLL。`SOUI_FR_USE_SYSTEM_DEPS=ON` 构建会自动把运行时 DLL 收拢到 exe 同级（exe 目录优先级最高），或把冲突目录从 PATH 移除。
- **内核断言不再自旋**：`fr_sim_assert`（FreeRTOS `configASSERT` 落点）已从 `for(;;)` 自旋改为 `RtlCaptureStackBackTrace` 打印原始返回地址后 `_exit(3)`。复现时抓到地址后用 `addr2line -e soui_sim.exe -f -C <addr>` 离线定位（构建链了 `--disable-dynamicbase`，地址可对照；该选项是生成文件，重生成即失）。
- **无显示环境下触发关闭 / 退出**：设 `SOUI_SIM_AUTOEXIT=1`，消息泵迭代约 60 次后自动注入关闭按钮点击，可 headless 跑通"关闭 → 退出"整条路径（**不要**在泵里直接 `DestroyWindow`，无真实事件泵时会同步死锁）。
- **gdb 注入鼠标事件**：`SwinxInjectMouse((unsigned long long)hwnd, msg, x, y, down)` 可向目标窗口投递 `WM_MOUSEMOVE`(0x0200) / `WM_LBUTTONDOWN`(0x0201) / `WM_LBUTTONUP`(0x0202)，用于在无头环境复现点击交互与 teardown。

## 裸机目标与 QEMU 实跑

`soui_fr` / `swinx_test` / `demo` 三个工程交叉编译到 **QEMU mps2-an385（Cortex-M3）**，共用 vendored 内核与 `demo/cmake/toolchain-arm-freertos.cmake`。典型流程：

```bash
cd soui-freertos/soui_fr            # 或 swinx_test / demo
cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../demo/cmake/toolchain-arm-freertos.cmake \
    -DSOUI_FR_CENSUS=OFF -DCMAKE_BUILD_TYPE=MinSizeRel   # -Os：否则 text 5.0MB 越 4MB FLASH
cmake --build build
qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel build/soui_fr.elf
#   -semihosting 是截图（soui_fb.ppm）落盘所必需；缺了它 dump_fb_ppm 会卡住
# 需要完整跑完读日志：
qemu-system-arm -M mps2-an385 -kernel build/swinx_test.elf \
    -serial file:C:/temp/qemu.log -display none
```

完整构建 / 运行 / 排障（含 `soui_fr` 的 `-semihosting` 出图、`demo` 的小板、串口 stdio 背压假 hang、`-serial file:` 必须写 Windows 绝对路径）见《SOUI5-FreeRTOS 开发指南》。

> 🚨 **FLASH 4MB 物理上限**：mps2-an385 的 ZBT SSRAM1 只有 4MB，text 越过 `0x00400000` 即与 RAM 物理重叠，复位即 HardFault。可运行镜像必须 `--gc-sections`（census 模式 `--whole-archive` 只链不跑，用于暴露符号缺口）。

## 平台宏（swinx）

swinx 里判断"是不是 FreeRTOS 平台 / 有没有宿主 OS / 内核原语从哪来"原来散落 4 个宏、还要在调用点写 `A || B`。现在只保留**单一配置头** `swinx/include/sfr_config.h`：

| 构建参数 | 派生开关 |
|---|---|
| `SOUI_PLATFORM_FREERTOS` | `SFR_PLATFORM = 1` |
| `FREERTOS_HOST` | `SFR_PLATFORM = 1`、`SFR_HOSTED = 1`（宿主 OS + CRT 在下面） |
| `SOUI_FREERTOS_REAL` | `SFR_KERNEL_REAL = 1`（真内核 API） |
| `SOUI_FREERTOS_HOST_EMU` | `SFR_KERNEL_EMU = 1`（POSIX 模拟） |

调用点一律写 `#if SFR_HOSTED` / `#if !SFR_PLATFORM`，两个开关永远有定义（0/1），不再有"未定义即假"的隐式语义；`SOUI_*` 原宏只在该头里出现，其它平台（linux/cocoa/ios/mobile）拿到的是全 0，行为不变。

注意：各工程 `gen/config.h` 由 CMake 生成（`gen/soui_fr` 与 `gen/cairo` 两个目录，同名文件不能同目录），`build*/` 与日志不入库。

## 已知限制

- **模拟器 ≠ 裸机**：`soui_sim` 验证的是逻辑层（消息泵、渲染管线、XML 布局、控件行为、输入路由），不能替代 QEMU 验证裸机启动链（startup / 链接脚本 / newlib 堆锁）、内存布局（`.bss` 段 / FLASH 4MB）、真实中断时序——交付前裸机目标必过。
- **交互验证仍靠人工**：`soui_sim` 里的点击/目检没有上界，留证用系统截图（详见上文"怎么上手体验"）；
  无头取证可用 `SOUI_SIM_DUMP_FRAME=<n>` 让模拟器把第 n 帧写进 `sim_frame.bmp`（默认不写）。
  裸机侧已有一条机器门禁（`.github/workflows/soui-freertos-gate.yml`）：census 链接（0 未定义符号）
  → `-Os` 可运行镜像（`text` 必须 < 4MB）→ QEMU 实跑断言 `SOUISAPP FREERTOS DONE`
  → 校验收到的 PPM 是 320x240 且不是空帧。它证明"启动链 + 渲染落笔"每天都能复现，
  不证明画面好不好看。
- **可运行镜像必须开优化**：同一份源码 `-O0` 的 `text` 是 5.0MB，越过 mps2-an385 的 4MB FLASH，
  QEMU 上表现为复位即 HardFault（"can't escalate 3 to HardFault"）；`-DCMAKE_BUILD_TYPE=MinSizeRel`
  后是 2.4MB。构建脚本/CI 一律用 `MinSizeRel`，不要拿调试构建去跑 QEMU。
- **`soui_sim` 的宿主构建依赖 swinx 里 2026-10-29 的头部修复**：swinx 1.8 的 WKE 适配
  （`aee0bc8`）把 `HWND` 等句柄改成 `struct XXX__ *` 以对齐 Windows SDK，同时新增了
  `swinx/include/io.h` 与 `strapi.h` 的 `struct _timeb`。这三处在宿主构建（`SFR_HOSTED=1`）
  上会和 MinGW 自带的 CRT 相撞：`io.h` 遮蔽真头文件导致 `<unistd.h>` 内联 `ftruncate()`
  找不到 `_chsize`；`_timeb` 在 MinGW 是宏（`__timeb64`）已由 `<sys/timeb.h>` 定义过；
  `sim/soui_sim_hal.cpp` 里手写的 `unsigned long long` 与指针型 `HWND` 符号不匹配。
  现修法：`io.h` 在宿主上 `#include_next` 交给真实头文件，`struct _timeb`/`_ftime` 与
  `ftruncate` 桩只在 `!SFR_HOSTED` 下定义，注入接口按 `struct HWND__ *` 声明。
  验证（gcc 15.2.0 / cmake 4.x / ninja，洁净构建）：`-j1` 与 `-j8` 均 0 error 0 warning，
  进程启动到标题为 `SOUI5 on FreeRTOS (Windows simulator)` 的窗口，并渲染出完整主界面。
  **这跟并行编译无关**：串行构建同样失败，问题全在上面三处头文件/签名。`soui_sim` 仍**不在 CI 内**（要桌面会话）。
- **宿主构建里 `stricmp` 家族宏会自递归成死循环（已修，2026-10-29）**：构建成功、窗口能开，但客户区
  一直空白、CPU 占满一核。根因是宏循环：MinGW 的 `<string.h>` 把 `strcasecmp` 实现成
  `__CRT_INLINE … { return _stricmp(…); }`，而 `swinx/include/strapi.h` 又定义
  `_stricmp(s1,s2) → stricmp(s1,s2) → strcasecmp(s1,s2)`，于是那个内联函数的函数体被改写成了
  "调用自己"。gcc `-O3` 把这条自递归尾调用折叠成 `jmp $`，任何大小写无关比较都变成原地死转
  （首个中招的是 `ClassMgr::find_class` → `SApplication` 构造阶段的窗口类注册，所以连
  `soui_app.cpp` 里的第一句日志都出不来）。修法：`strapi.h` 把整个大小写无关比较族
  （`stricmp/strnicmp/_stricmp/_strnicmp` 与 `_wcsicmp/_wcsnicmp`）收进**一张统一的宏表**，
  由 `SFR_HOSTED && _WIN32` 一个开关决定：宿主 + Windows 时六个别名全部定义成恒等宏
  （`#define _stricmp(s1,s2) _stricmp(s1,s2)`），让 CRT 的内联包装继续调真实符号；其余目标
  （POSIX 宿主、裸机、WASM）映射到 `strcasecmp/wcscasecmp`。原先 `_wcsicmp` 与 `stricmp`
  各用一个开关、条件还不一致，属于同一判定的重复表达，已合并。定位手法：`gdb --pid` 附加后
  反汇编落点，看到 `jmp <自身>` 即可确认是宏循环而非锁竞争（锁竞争在 Win32 端口上是阻塞，
  CPU 不会满）。
- **平台层两处与 wizard 的差异**（无 `ICON:` 桩接口、无 `_UNICODE`）见上文"soui_sim 的目录结构"。
