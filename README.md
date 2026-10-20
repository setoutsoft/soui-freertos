# freertos_env — SOUI FreeRTOS 工作区

SOUI4/SOUI5 移植 FreeRTOS 的全部实验工程。上游源码在 `D:/work/soui4`（SOUI core + swinx 平台层），本仓库只放各目标环境的工程胶水与测试程序。

> 本仓库已作为 **soui4 的 git 子模块**（`soui4/freertos_env`）。子模块场景下各 CMakeLists 自动以 `../..` 定位 SOUI 源码；独立 checkout 时回退 `D:/work/soui4`。

## 工程一览

| 目录 | 目标 | 说明 |
|------|------|------|
| `cmake/soui.cmake` | — | **唯一构建入口**：`include` + `soui_fr_project()` 即完成整环境搭建 |
| `sim/` | Windows 宿主 | SDL2 宿主运行时（出图/注入/脚本/截图），被 `SIM_HOST` 工程复用 |
| `templates/app/` | Windows 宿主 | 新建工程模板：复制即用，构建脚本 2 行 |
| `demo/` | QEMU mps2-an385 (Cortex-M3) | 裸机 FreeRTOS 冒烟：UART 输出 + 调度器跑通，移植 earliest 基线 |
| `swinx_test/` | QEMU mps2-an385 | swinx freertos 平台层单测（fb/input/syncobj/render 等 main_*.cpp 变体） |
| `soui_fr/` | QEMU mps2-an385 | 完整 SOUI SApp 冒烟：XML 布局 → render-gdi(cairo) → LCD fb（截图由程序运行时落盘） |
| `soui_sim/` | Windows 宿主 (MinGW) | **FreeRTOS Windows 模拟器**：FreeRTOS-Kernel MSVC-MingW port + swinx freertos 层 + SDL2 出图/鼠标注入，与 QEMU 目标 95% 同构，用于快速迭代交互测试 |
| `FreeRTOS-Kernel/` | — | vendored 内核（demo / soui_fr / swinx_test / soui_sim 共用，含 `portable/MSVC-MingW`） |

仓库只保留源码与构建脚本：构建目录、截图帧、一次性探针脚本都不入库（`.gitignore` 已覆盖 `build*/`、`*.log`、`*.ppm`）。

## 外部依赖（不在本仓库）

- `D:/work/soui4` — SOUI/swinx 源码（`SOUI_FR_ROOT` 引用）
- MSYS2 mingw64（gcc 15.2.0、SDL2、cmake+ninja）；QEMU（qemu-system-arm，mps2-an385）+ arm-none-eabi 工具链（QEMU 三工程）

FreeRTOS 内核默认用本仓库 vendored 的 `FreeRTOS-Kernel/`（含 `portable/MSVC-MingW`，soui_sim 直接复用），不再需要外部 `lvgl_simulator` 那份。

## 新建项目：一个 include + 一个函数

```bash
cp -r freertos_env/templates/app  my_app
cmake -S my_app -B my_app/build -G Ninja
ninja -C my_app/build && ./my_app/build/hello_freertos.exe
```

`my_app/CMakeLists.txt` 全文（除 `project()`/编译标准外）就这两行：

```cmake
include(<freertos_env>/cmake/soui.cmake)
soui_fr_project(hello_freertos SOURCES main.cpp SIM_HOST)
```

`soui_fr_project()` 一次做完：

| 步骤 | 内容 |
|---|---|
| 内核 | `FreeRTOS-Kernel` + 按宿主/裸机自动选 port（`MSVC-MingW` / `GCC/ARM_CM3`）、heap_3 |
| 三方库 | cairo/pixman/libpng/zlib，宿主默认用系统包（pkg-config 能证明时），交叉编译强制 vendored 源码 |
| 平台层 | swinx freertos 层（`src/*.cpp` + `platform/freertos/*` + cairo gdi 后端/字体桥） |
| SOUI | utilities + core + render-gdi + imgdecoder-stb（whole-archive 保留 COM 注册对象） |
| 资源 | `uires/uires.idx` → uiresbuilder → windres COFF → `.incbin` 数据段（`.balign 4`，Cortex-M 非对齐会 fault） |
| 启动 | 和 wizard 工程一样自己写：`SApplication` + `SAppCfg`（`SetRender(Render_Gdi)` / `SetImgDecoder(ImgDecoder_Stb)` / `SetSysResPeHandle` / `SetAppResPeHandle`）→ `cfg.DoConfig(&app)`；`SAppCfg` 内部把 render-gdi 工厂与 imgdecoder-stb 一起注册好（缺后者会在 `LoadSystemNamedResource` 里解码系统皮肤时静默段错误），不再是框架提供的"启动胶水" |
| 宿主出图 | `SIM_HOST` 时链 `sim/soui_sim_hal.cpp`（SDL2 窗口/ARGB 纹理/事件注入/脚本/截图） |
| Windows 运行 | system-deps 构建自动把 20 个运行时 DLL 收拢到 exe 同级 |

参数（全部可选）：`SOURCES`（必填）、`RESOURCES`（默认 `./uires`）、`RESOURCE_SCRIPT`（默认自动生成）、`NO_RESOURCES`、`FREERTOS_CONFIG`（默认 `./config`）、`SIM_HOST`、`INCLUDES`、`LIBRARIES`、`OUTPUT_NAME`。裸机交叉编译只需换 toolchain file 并去掉 `SIM_HOST`。

需要更细的控制时，底层 API 仍然可用：

```cmake
include(<freertos_env>/cmake/soui.cmake)
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
| `sim/soui_sim_hal.*` | 模拟器专有的宿主壳：SDL2 窗口 / ARGB 纹理 / 鼠标键盘注入 / 脚本与截图 |

两处当前仍与 wizard 不同（受平台层能力限制）：

- swinx freertos 的 `LoadImageW/A`、`CreateIconFromResource` 还是 NULL 桩，所以 `<icon src="ICON_LOGO">` / `bigIcon` 用不了 —— 标题栏 logo 改成 `<img skin="app_logo">`（PNG 皮肤）。
- 该工具链不定义 `_UNICODE`（`TCHAR == char`），字符串字面量一律 `_T(...)`，`L"..."` 不能直接喂给 `LPCTSTR` 接口。

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

`soui_sim/screenshots/` 存了三张定妆图：`00_idle.png`（PNG 态）、`02_pressed.png`（按钮按下）、`i1.png`（点击后切到 SVG）。

## 平台宏（swinx）

swinx 里判断"是不是 FreeRTOS 平台 / 有没有宿主 OS / 内核原语从哪来"原来散落 4 个宏、还要在调用点写 `A || B`。现在只保留**单一配置头** `swinx/include/sfr_config.h`：

| 构建参数 | 派生开关 |
|---|---|
| `SOUI_PLATFORM_FREERTOS` | `SFR_PLATFORM = 1` |
| `SOUI_PLATFORM_FREERTOS_HOST` | `SFR_PLATFORM = 1`、`SFR_HOSTED = 1` |
| `SOUI_FREERTOS_REAL` | `SFR_KERNEL_REAL = 1`（真内核 API） |
| `SOUI_FREERTOS_HOST_EMU` | `SFR_KERNEL_EMU = 1`（POSIX 模拟） |

调用点一律写 `#if SFR_HOSTED` / `#if !SFR_PLATFORM`，两个开关永远有定义（0/1），不再有"未定义即假"的隐式语义；`SOUI_*` 原宏只在该头里出现，其它平台（linux/cocoa/ios/mobile）拿到的是全 0，行为不变。

注意：各工程 `gen/config.h` 由 CMake 生成（`gen/soui_fr` 与 `gen/cairo` 两个目录，同名文件不能同目录），`build*/` 与日志不入库。
