# templates/app — 新建「SOUI on FreeRTOS」工程

复制整个目录即可开工：

```bash
cp -r soui-freertos/templates/app  my_app
cmake -S my_app -B my_app/build -G Ninja
ninja -C my_app/build && ./my_app/build/hello_freertos.exe   # 关窗口退出
```

## 目录

| 路径 | 作用 |
|---|---|
| `CMakeLists.txt` | 全部构建配置：`include(soui.cmake)` + `soui_fr_project()` |
| `main.cpp` | FreeRTOS 任务 → SOUI 启动 → SDL/消息泵主循环 |
| `config/FreeRTOSConfig.h` | 内核配置（栈、heap_3、tick、hook 开关） |
| `uires/uires.idx` | 资源索引，`LAYOUT:MAIN` 对应 `xml/main.xml` |
| `uires/uidef/init.xml` | 全局 UIDEF（`uidef:xml_init`） |
| `uires/xml/main.xml` | 界面布局 |

## 常见改动

- **改界面**：编辑 `uires/xml/main.xml`；新增资源（图片/SVG/skin）就在 `uires.idx` 里登记对应节点。
- **加源文件**：`soui_fr_project(hello_freertos SOURCES main.cpp my_page.cpp SIM_HOST)`。
- **加库**：`LIBRARIES foo`；只影响本工程。
- **换 FreeRTOSConfig**：`FREERTOS_CONFIG config_stm32`（默认识别 `./config`）。
- **裸机**：去掉 `SIM_HOST`，加 `-DCMAKE_TOOLCHAIN_FILE=<arm-none-eabi.cmake>`。此时 `soui.cmake` 自动切到 vendored cairo + `GCC/ARM_CM3` port，并跳过 SDL 宿主层与 DLL 收拢；`main()` 换成你的板级启动即可（`app_task` 里的 SOUI 流程不变）。
- **不需要 PE 资源**：传 `NO_RESOURCES` 并自行提供 `SResProvider`（比如从文件系统读 `uires`）。

## 已替你处理掉的坑

- `SApplication::LoadSystemNamedResource()` 要解码系统皮肤，**没有 img decoder 会在工作线程里静默段错误** —— `main.cpp` 里 `SAppCfg` 的 `SetRender(Render_Gdi).SetImgDecoder(ImgDecoder_Stb)` 已经把 render-gdi 工厂和 imgdecoder-stb 一起注册好（和 `wizard/CmakeApp` 的写法一致）。
- swinx freertos 的 `LoadImageW/A` / `CreateIconFromResource` 仍是 NULL 桩 ⇒ XML 里别用 `<icon src="ICON_...">` 或 `bigIcon`，图片资源请走 PNG 皮肤（`<imglist src="PNG:xxx">`）。
- 资源 blob 的 `.incbin` 前必须 `.balign 4`：COFF 里有 uint16/uint32 读，Cortex-M3 上非对齐直接 fault（x86 不会报）。
- system-deps 构建会把运行时 DLL 收拢到 exe 同级：PATH 里若有另一套 MinGW（PortableGit / Android platform-tools）会影子掉 `libwinpthread-1.dll` 等，进程在 `main()` 之前就以 `0xc0000139` 退出。
