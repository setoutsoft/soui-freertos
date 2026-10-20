# SOUI 核心 FreeRTOS 链接尝试 — 评估报告

日期：2026-10-04 ｜ 工程：`/d/work/freertos_env/soui_fr/` ｜ 工具链：arm-none-eabi-gcc 13.3 (cortex-m3, -Os, -fno-rtti, -fexceptions)

## 结论

**SOUI 核心（含全部控件源码）在 FreeRTOS 工具链上 100% 可编译**；配合 49 个平台桩即可**全量链接通过**（0 未定义符号）。缺口全部集中在 swinx 的 FreeRTOS 平台桥尚未覆盖的 Win32 API 面，不在 SOUI 代码本身。

## 编译范围（全部通过）

| 模块 | 文件数 | 说明 |
|---|---|---|
| utilities4_fr | 16 | string/xml(SXml+pugixml)/helper(SAutoBuf/SCriticalSection/SRwLock/SSemaphore/STls/slog) |
| SOUI core | 27 | SApp/SouiFactory/core/*（仅排除 SwndAccessible.cpp msaa） |
| res.mgr | 9 | 排除 AndroidAsset/OhosRawFile 两个 NDK provider |
| helper/layout/matrix/event/animation/valueAnimator | 40 | 全过 |
| control + activex | 40 | **全量通过**（含 SRichEdit/SCalendar/SActiveX 等上游 OHOS 同款全集） |

编译侧仅需 3 个探针垫片（`soui_fr/shim/`，正式移植应收进 swinx `platform/freertos/`）：
- `sys/mman.h`（SOUI POSIX 分支遗留 include，mmap 无实际使用）
- `dirent.h`（newlib 自带版 `#error not supported`；SResProvider.cpp 目录枚举）
- `soui_fr_compat.h`（force-include：newlib 缺 `<climits>` 传递包含；`SSingleton2.h` 缺 `sdef.h`；`SAnimation.h` 缺 `SAttrCracker.h`；`SViewBase.cpp` 上游唯一不包含 souistd.h 的源文件，单独 `-include souistd.h`）

## 链接 census（`-Wl,--whole-archive` + 无 gc-sections，全量符号解析）

**51 个未定义符号**（加桩前）→ 49 桩后 **0**，ELF text 5.62MB + bss 844KB（`-DSOUI_FR_CENSUS=ON`）。
> ⚠️ >4MB 的镜像在 QEMU mps2-an385 上不可运行：0x00400000(ZBT2) 与 0x20000000(RAM) 互为别名，text 段越过 4MB 会与 .data/.bss 物理重叠导致复位即锁死。census ELF 仅作链接闭环证明；可运行版用 `--gc-sections`（87KB，QEMU 实跑 SStringA 冒烟 OK）。

### 51 符号分类（→ freertos 移植归位）

| 类别 | 符号 | Linux 侧定义处 | freertos 归位建议 |
|---|---|---|---|
| 模块加载 | GetModuleHandleA/LoadLibraryA/FreeLibrary/GetProcAddress/GetModuleFileNameA/dlsym/dlerror | sysapi.cpp（dl） | sysbridge 补桩（dlfcn.h 垫片已有声明，无实现） |
| 堆 | HeapCreate/Destroy/Size/Validate、GlobalAlloc/Free/Lock/Unlock、LocalAlloc/LocalReAlloc | memory.cpp | sysbridge |
| 文件 | FindFirstFileA/Next/Close、GetFileAttributesA/GetFullPathNameA、_stat/_open/_unlink/_link | fileapi.cpp | sysbridge 或显式不支持 |
| 时间 | GetTickCount64/GetSystemTime/GetLocalTime(winobjs 已有)/GetCurrentProcessId、_time64/_mktime64/_mkgmtime | sysapi.cpp | sysbridge（GetTickCount64 可用 xTaskGetTickCount 实作） |
| 字符串/CRT | StrToIntExA/W、qsort_s、IsDBCSLeadByte、OutputDebugStringA、_getentropy | sysapi.cpp | sysbridge |
| IMM 输入法 | ImmCreateContext/Destroy/Get/Associate/Release | platform/<os>/imm | 无 IME 环境纯桩 |
| 图标/光标 | DestroyCursor/LoadBitmapA/CreateIconFromResource | cursoricon.cpp | 桩（帧缓冲环境无图标资源） |
| 对话框/Shell | SGetOpenFileNameA/SChooseColor（cmmmdlg.cpp 平台钩子）、ShellExecuteA、RegisterDragDrop | platform/<os>/dlghelper | platform/freertos/dlghelper.cpp 桩 |
| 其它 | PostThreadMessageA、pthread_self（utilities SRwLock/STls 内部引用） | sysapi.cpp / newlib | PostThreadMessageA 桩；pthread_self 由 STL 垫片线程身份接 |

**最终 49 桩**见 `gapstubs.cpp`（含语义：全部优雅失败，时间类给出真实值）。

## 运行验证

- runnable 版（gc-sections，87KB）：QEMU mps2-an385 启动 → `[smoke] SStringA len=43`（SOUI SStringA 在 swinx FreeRTOS 堆上构造/析构成功）→ 调度器空转正常。
- swinx_test 回归：33 项 OK + PASS（新增 dump 测试后）。

## 复现

```bash
cd /d/work/freertos_env/soui_fr/build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../demo/cmake/toolchain-arm-freertos.cmake -DBOARD=mps2 -DSOUI_FR_CENSUS=ON ..   # census
cmake --build .
# runnable: -DSOUI_FR_CENSUS=OFF，qemu-system-arm -M mps2-an385 -nographic -kernel soui_fr.elf
```

## 下一步建议

1. 把 49 桩按上表归位进 swinx `platform/freertos/`（sysbridge 扩容 + 新增 dlghelper.cpp），替换探针桩。
2. 3 个 shim 头与 compat force-include 逐一收编（swinx 侧 `platform/freertos/sys/mman.h`、`dirent.h`；SOUI 侧建议上游补显式 include）。
3. 真正跑 SApp：初始化 SResProvider（内存包 provider）+ SApplication + 一个 SHostWnd，走完整 XML 布局渲染到 fb（与 dump PPM 串联出图目检）。
