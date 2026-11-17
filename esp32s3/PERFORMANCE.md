# ESP32-S3 性能实测与优化记录（2026-11-14 → 11-17）

> 本文从 [`README.md`](README.md) §10–§11 拆出，收录 ESP32-S3 真机上所有"帧率、裁剪、绘制预算"相关的
> 实测与优化过程。README 只保留当前状态 / 编译 / 性能参数 / 已知限制；
> 想知道"为什么是这个帧率、每一微秒花在哪"，看这里。
>
> **小节编号沿用原 README**（§10.1–10.4、§11.1–§11.10、§11.15–§11.18、§11.20），
> 便于与历史日志、评估报告以及本文内部的交叉引用对照；README 引用这些小节时会写成
> "PERFORMANCE.md §10.3" 这样的形式。只想跑起来看 [BUILD_FLASH.md](BUILD_FLASH.md)。

## 当前帧率口径（2026-11-17 更新）

| 场景 | 帧率 | 口径 |
|---|---|---|
| 单动画（只有 `anim_box` 扫动，`fade_box visible="0"`） | **56~58 fps** | 探针关，frame 17.3 ms / paint 14.2 ms |
| **双动画（当前设备版本：`fade_box visible="1"`，扫动 + alpha）** | **约 40 fps** | 2026-11-17 板上实测，默认构建（探针关）；这就是设备上正在跑的版本 |
| 双动画（11-15 优化前口径） | 20~22 fps | paint 42~45 ms，历史数字，不再代表当前固件 |
| 单动画（11-15 优化前口径） | ~35 fps | paint 28 ms，历史对照点 |

两个数字的差异来自 `uires/xml/dlg_main.xml` 里 `fade_box` 的 `visible` 属性：
`visible="0"`（HEAD `ba4cd99e`）时蓝块不参与绘制，测的是单动画 56 fps；
`visible="1"`（当前工作区，设备上跑的版本）时蓝块的 layered alpha 动画每帧多一次离屏合成，
稳态约 40 fps。layered 合成仍是结构性成本，但它不再把帧率"腰斩"到 20 fps。

> 注：40 fps 是用户 2026-11-17 在板上读到的稳态值（`fps_label` 口径 = 完成的 `WM_PAINT` 次数 / 3 s），
> 本次没有再逐项记录 paint / 分窗口账；下文 §10.3 / §11.17 的拆解是 20 fps 时代的账本，
> 结论（固定开销 × 调用次数，而非像素）仍然成立，但绝对值已不适用。

---

## 10. 显示链路的性能实测（原 README §10.1–10.4）

### 10.1 帧率从 ~8fps 到 ~20fps：四个真正的瓶颈

上面那句"大头是 PSRAM 访存"是**错的**。加了 `SOUI_PORT_PROFILE=1` 的分阶段
计时后（`[prof]` 行，见下）才看清：一次 `WM_PAINT` 只脏 6k 像素，却要 133ms，
时间根本不是花在像素上。

复现方法：`components/soui/CMakeLists.txt` 里把 `SOUI_PORT_PROFILE=0` 改成 1，
串口每 20 帧输出（**这个开关已在优化收尾时移除**，要复现得按 git 历史把宏和
`shostwnd.cpp` 里的计时段加回去）

```
[prof] layout=366 paint=26904 present=1215 [begin=409 clip+clr=1880 tree=23873 tail=93] updRgn=5747px dirty=5747px clip=40 calls/42 rects
  [w] 4 windows touched
  [w]              total=21871 own=1369 pre=500 child=19841 post=154 lay=4 n=20
  [w] fade_box     total=6406  own=829  pre=1111 child=1835 post=1838 lay=792 n=20
```

| # | 瓶颈 | 现象 | 修法 | 收益 |
|---|---|---|---|---|
| 1 | `SWindow::IsDrawToCache()` 把"有动画"也算作要缓存 | 每帧 ClearRect 整窗 + 整窗重画进离屏 + 整幅 AlphaBlend，只为 6k 像素脏区 | 用一个临时编译开关（让该函数只认 `m_bCacheDraw`）量出这一项，确认它是主因；**这个开关没有留在代码里**，最终的解落在资源层：根窗口 `cache="0"`（坑 16） | 162→133ms（临时开关）/ 162→126ms（`cache="0"`） |
| 2 | `BeginPaint()` 没把 DC 裁剪到 update region | `GetClipBox()` 报整窗 → `SHostWnd::OnPrint()` 把 `rcInvalid` 并成整窗 → present 变成 307KB 整幅 BitBlt | `wnd.cpp` 的 `BeginPaint` 用 `wndObj->invalid.hRgn` 设 `RGN_AND` 裁剪（Win32 语义，`ReleaseDC` 的 `RestoreDC(-1)` 撤销） | present 15.7→1.2ms |
| 3 | `DispatchPaint()` 对**每个**可见窗口**无条件**付 2 次 `SaveClip` + 2 次 `PushClip` | 一屏 25 个窗口就是上百次裁剪运算，而真正相交的只有 3 个 | ① 整窗不在脏区 → 函数开头直接 return；② 只在真有 `m_clipRgn/m_clipPath` 时 `SaveClip`；③ `rcClient == rcWnd` 时跳过 `PushClipRect` | 85→54ms，窗口 25→4 |
| 4 | layered 窗口（alpha≠255，如 `fade_box`）每帧新建/释放离屏 RT | `CreateRenderTarget` + `cairo_create` + `Release` ≈ 6ms/帧，窗口只有 40×16 | 新增 `m_layeredRT` 成员跨帧复用，尺寸变化时在 `OnSize` 里丢弃重建 | 16.2→6.4ms |

第 1 项之所以不在框架层留开关：`IsDrawToCache()` 现在（以及当时提交后）
仍然是 `m_bCacheDraw || m_isAnimating || !m_pAnimatorHandler->IsEmpty()`
（`SOUI/src/core/Swnd.cpp:3985`），改它会同时改变 Windows/移动端"动画窗口
自带离屏缓冲"的行为。同一份收益用资源层就能拿到——让根窗口不写 `cache="1"`
（坑 16，162→126ms），代价只有"根窗口每帧重画自己的背景"，而那点面积本来就
落在脏区里。`components/soui/CMakeLists.txt` 里曾经的
`SOUI_DISABLE_AUTO_CACHE` 宏从来没有对应的 `#ifdef` 分支，已删除。

顺带把 `ExtSelectClipRgn(RGN_AND)` 改成快路径（`ApplyRegionAnd`）：cairo 的裁剪
天生就是"与当前裁剪求交"，不需要先 `cairo_copy_clip_rectangle_list` 导出、再做
GDI 区域布尔运算、再 `cairo_reset_clip` 重新应用。

**编译/缓存配置**也补上了（之前漏的）：

- `soui` 组件的 `target_compile_options` 里原来写的是 `-Os`。它排在
  `CMAKE_CXX_FLAGS` 之后，**静默覆盖了全局 `-O2`** —— 整个 cairo/pixman/SOUI
  其实是按 `-Os` 编的。改成 `-O2` 后单这一项就让 paint 从 31.6ms 掉到 26.9ms
  （固件 4.01MB → 4.40MB，6MB 分区还剩 30%）。
- **同一个坑在 `components/soui_cairo/CMakeLists.txt` 里还有一份**（`-w -Os`），
  漏掉了 169 个文件：cairo 107、pixman 32、png、zlib 30 —— 也就是**真正碰像素的
  那部分代码**。改 `-O2` 后固件 4.40MB → 4.49MB（+94KB），但 `paint` 只从
  48.6ms 降到 46.3ms。**这个结果本身很有信息量**：把渲染代码全开到 -O2 只买回
  2ms，说明剩下的 46ms 不是指令数，而是别的东西（见下面"还剩的空间"）。
  核对方法：
  ```bash
  python -c "import json;from collections import Counter;
  db=json.load(open('build/compile_commands.json'));
  print(Counter(tuple(o for o in e['command'].split() if o.startswith('-O') and len(o)<=4) for e in db))"
  # 期望 {('-O2',): 1421}；出现 ('-O2','-Os') 就说明某个组件还在偷偷降级
  ```
- `CONFIG_ESPTOOLPY_FLASHMODE_QIO`（原来是 DIO，代码 XIP 取指带宽砍半）。
  注意镜像头里的 `spi_mode` 字节仍然是 `0x02`(DIO)，**这是正常的**：
  `CONFIG_ESPTOOLPY_FLASHMODE="dio"` 只是给 esptool 用的烧录模式，
  bootloader 起来后会自己升到 QIO。运行时看启动日志的
  `I spi_flash: flash io: qio` 才是真的。
- `CONFIG_ESP32S3_DATA_CACHE_64KB`（原来是 32KB，S3 的最大值）。

**最终实测**（COM4 @115200，`SOUI_PORT_PROFILE=0`）：

```
I ( 7345) presents=110 (+110/3s), pixels=912056,  paint=49.2ms, flush=3.9ms, 304 kpix/s
I (10521) presents=230 (+120/3s), pixels=1619096, paint=46.3ms, flush=2.7ms, 236 kpix/s
I (13586) presents=342 (+112/3s), pixels=2390648, paint=48.1ms, flush=3.2ms, 257 kpix/s
```

**先分清两个数，别被 `presents` 骗了**：

| 指标 | 值 | 说明 |
|---|---|---|
| `presents` | **37~40 /秒** | 脏矩形 blit 次数。**一帧会发 1.85 次**（窗口提交 + 重叠窗口重合成），不是帧率 |
| 真实帧率 | **~20 fps** | `paint` 46ms + `flush` 3ms。demo 的 `fps_label` 数的就是这个，屏上显示 18.5~20 |

两者互相对得上：`40 presents/s ÷ 21.6 frames/s = 1.85 presents/帧`。优化前
`presents` 是 14.7/秒，折合帧率 **~8fps**，所以仍是 **2.5×**。

（本文档早先把 `presents` 当成帧率写成"37fps"，是错的，已按上表更正。）

**和 LVGL 那个 100fps 不可直接比**：320×240×2B = 153600 B，40MHz SPI 全屏刷一次
就要 30.7ms（上限 33fps），所以 LVGL 的 100fps 一定是"只刷几十行"的局部刷新
数字，而不是整屏。SOUI 现在的脏区也只有 6k 像素，量级对得上。

**还剩的空间**：把 cairo/pixman 开到 `-O2` 只买回 2ms，说明 46ms 不是指令数问题。
`[prof]` 行里的 `clip=40 calls/42 rects` 是最可疑的线索——**一帧 40 次裁剪操作**，
而 `cairo_clip` 在这个后端单次 1~2ms（每次都要重建 pixman region），40 × 1.1ms
大约就是 45ms。也就是说每帧画的像素只有 6k，**时间全花在"反复告诉 cairo 要裁到
哪里"上**。下一步按收益排序：

1. `PushClipRect` / `PushClipRegion` 加**包含性快路径**：新矩形若已包含当前
   `GetClipBox()`，`RGN_AND` 是恒等操作，直接跳过（SOUI 每个窗口都推一次
   `rcClient`，而裁剪往往已经更小）。
2. 给 `SRenderTarget_GDI` 加一层 clip 状态缓存，同一组裁剪矩形不重复下发。
3. 渲染目标 ARGB32 → RGB565，内存和带宽都减半（改动面最大）。
4. SPI 提到 80MHz，把全屏刷新的上限从 33fps 抬到 66fps——只在脏区变大
   （整屏滑动）时才有意义，现在 `flush` 只占 3ms，不是瓶颈。

### 10.2 裁剪到底有多贵：直接量出来，然后修掉

10.1 的结论是"每帧 40 次裁剪 ≈ 45ms"，但这是推算。为了不靠推算，做了一次
**消融实验**，结论还差点把人带偏：

- 把 RT 层的 `PushClipRect / PushClipRegion / PopClip / SaveClip / RestoreClip`
  全部改成空实现（当时用一个临时编译开关做开关，量完即拆，代码里没有留下
  `SOUI_ABLATE_CLIP` 这个名字），板子上的帧率**反而从 19fps
  掉到 14fps**。原因是那些空实现把裁剪一起删掉了，绘制不再被限制在 6k 像素的
  脏区里，填充面积成倍放大——**"删掉裁剪"和"裁剪很贵"两件事被混在一起了**。

所以改成直接测量。在开机时对一块和绘制目标同尺寸（320×240 ARGB32）的 HDC
跑固定次数的操作（临时基准，现已删除）：

| 操作 | 单次成本 |
|---|---|
| `SelectClipRgn(NULL)`（只 `cairo_reset_clip`） | **0.5 µs** |
| `GetClipBox` | 5 µs |
| `SaveDC`+`RestoreDC`（无裁剪） | 58 µs |
| `CreateRectRgnIndirect`+`DeleteObject` | **193 µs** |
| `AND clip`，4×4 / 40×30 / 320×240 | **500 µs（三者相同）** |
| `COPY clip`，40×30 | 545 µs |
| `SaveDC`+`RestoreDC`（有裁剪） | 878 µs（含上一个 clip） |
| `PushClipRect` 1 矩形 + `PopClip` | **931 µs** |
| `PushClipRect` 4 矩形 + `PopClip` | **3359 µs** |

三点结论：

1. **裁剪成本与面积无关**（4×4 和 320×240 一样是 500µs），是纯固定开销，
   所以"减少脏区面积"对这个数字没用。
2. **每多一个矩形多 ~800µs**：4 矩形的区域是 1 矩形的 3.6 倍。SOUI 每次推的是
   多矩形的不规则区域，这才是最贵的一档。
3. 20 次/帧 × 0.9~3.4ms = **18~68ms**，和 `tree` 的 45ms 完全对得上。
   同时 `SelectClipRgn(NULL)` 只要 0.5µs，说明 cairo 本身不慢——贵的是
   "重建裁剪区"这一整套（建区域对象 + `SaveDC` 复制 gstate + `cairo_clip` 求交）。

修法（`SRenderTarget_GDI::PushClipRect / PushClipRegion / PopClip`）：

- **包含性快路径**：`RGN_AND` 时先 `GetClipBox`（5µs），若新矩形已完全包含当前
  裁剪框，则该裁剪是恒等操作，直接返回。SOUI 每个可见窗口都会推一次
  `rcClient`，而帧级裁剪通常已经更小，所以绝大多数推送都能跳过。
  跳过时必须在 `PopClip` 里也跳过对应的 `RestoreDC`，否则 DC 状态栈会失衡、
  裁剪会跨帧累积——用一个按嵌套层记录的位图 + 一个小 mark 栈实现
  （`SaveClip`/`RestoreClip` 也要带上它，因为 `SWindow::DispatchPaint`
  是用 `RestoreClip` 而不是 `PopClip` 撤销裁剪的）。
- **复用区域对象**：`PushClipRect` 原来每次 `CreateRectRgnIndirect` +
  `DeleteObject`（193µs），改成构造时建一个 `m_hClipTmp`，每次 `SetRectRgn`。

当时为了把"动画本身的帧率"单独量出来，把 demo 临时精简成**只有一个动画**
（删掉 `fade_box`）。这一刀后来按用户要求撤回了，两种动画混跑的真实代价见 10.3。

**结果**（COM4 @115200）：

| | 优化前 | 现在 |
|---|---|---|
| `paint` | 46.3 ms | **28.0 ms** |
| `flush` | 2.7 ms | 0.5 ms |
| `presents` | 40/s | **62/s** |
| 帧率 | ~20 fps | **~35 fps** |

（这是"只有一个动画"的对照点。两个动画都开时的最新数字是 20.3fps，见 10.4。）

固件 4,492,976 字节，6MB 分区剩 29%。

**下一步**：`paint` 仍是 28ms，而每帧只脏约 1000 像素——还是固定开销主导，
不是像素。剩下的候选：`SaveClip`/`RestoreClip` 每窗口一对（58µs×2）、
`ClearRect` 的 cairo 填充、以及每帧 4 次 `cairo_create`（`cairo_new=4`）。

### 10.3 两个动画混跑的真实代价：35fps → 18fps（+20ms/帧）

> LVGL 是专门为嵌入式写的，SOUI 是全平台框架——两者目标不同，绝对数字不该直接
> 横向比。这一节只是把"多一个 alpha 动画要付多少钱"量出来，供取舍用。

`fade_box`（40×16 的 layered 窗口，`SByteAnimator` 驱动 alpha 40↔255，
900ms 往返）恢复后重新实测，并加了一个中间点 B 来把"layered 窗口存在"和
"alpha 每帧在变"分开：

| 点 | 场景 | fps | `paint` | 其中 `tree` | clip 次数/帧 | `cairo_create`/帧 | 脏区 bbox |
|---|---|---|---|---|---|---|---|
| A | 只有 `anim_box` 扫动 | **35** | 28.0 ms | ~19 ms | 20 | 1~2 | 46×16 |
| B | `anim_box` + `fade_box`（静态不驱动 alpha） | **30.7** | 29.6 ms | 19.4 ms | 20~22 | 1~2 | 46×16 |
| C | `anim_box` + `fade_box` alpha 动画 | **18** | 50 ms | 30.4 ms | **40~44** | **4** | **292×36** |

所以那 20ms 的构成是：

1. **+11ms 在 `tree`**：alpha 每变一次，整个 layered 窗口就要重走一遍
   "离屏表面 → 绘制 → `AlphaBlend` 提交"，裁剪操作数**翻倍**（20 → 42 次）。
   每次裁剪 ~0.5ms 的固定开销又回来了，只是这次不是 SOUI 乱推，而是 layered
   路径本身要求的两段：画离屏一次、合成一次。
2. **+4ms 在 flush**：两次提交的 SPI 时间是 3.2ms/次（A 点是 1.3ms/次）。
3. 剩下是 `present` 与窗口遍历。

第 2 条背后有个容易被忽略的坑：`SConnection::commitCanvas()` 用的是
**`GetRgnBox()` 的外接矩形**，不是区域里的矩形列表。两个 40×16 的小方块
（y 204..220 和 224..240）合并后，送去屏上的是 **292×36 = 10512 像素**的一条
横带（串口日志里能直接看到 `flush #200 rect (14,204)-(306,240)`），而实际脏
像素只有 1280。**面积被放大了 8 倍**。单个动画时两行不并存，所以这个问题一直
没暴露——它是"加第二个动画"才浮出来的。

`paint` 里剩下的固定开销（20 次裁剪 × ~0.5ms）依然是最大的一块，但再往下压就
不是"修 bug"而是改架构了（渲染目标换 RGB565、cairo clip 状态缓存）。

### 10.4 逐矩形提交（18 → 20.3fps），以及"能不能靠 SIMD 救 AlphaBlend"

#### (1) `commitCanvas` 改成按矩形列表提交

10.3 里发现提交用的是脏区**外接矩形**。改成遍历 `invalid.hRgn` 的矩形逐个
blit + flush（`kMaxPresentRects = 8`，超了退回外接框；`GetRegionData` 失败也退回）：

```c
flush #200 rect (14,224)-(54,240)      // 之前是 (14,204)-(306,240) 一条 292x36 的横带
```

| | 外接矩形 | 逐矩形 |
|---|---|---|
| 送屏面积/帧 | 10512 px | **约 4200 px** |
| 首帧后 `flush` | 2.9 ms/次 | 0.4~0.9 ms/次 |
| `paint` | 50 ms | **43 ms** |
| 帧率 | 18.0 | **20.3** |

代价是每帧多付几次 SPI 事务的固定开销（实测每次 flush ≈ 1.1ms 固定 + 0.27µs/px），
所以矩形数超过 8 个就不划算了，这也是那个上限的来由。

#### (2) BitBlt 替掉 AlphaBlend：只快 1ms

把 `SWindow::OnCommitSurface()` 里的 `AlphaBlend` 换成 `BitBlt`（等于完全不做
混合、丢失半透明）：

| | `paint` | 帧率 |
|---|---|---|
| `AlphaBlend` | 42.5 ms | 20.7 |
| `BitBlt` | 41.5 ms | **21.7** |

**混合本身只值 1ms。** 所以"cairo 没有 SIMD 导致 AlphaBlend 慢"这个方向可以排除，
这也和下面（3）的微观数据一致。既然只差 1ms 还要丢半透明，就保留了 `AlphaBlend`。

#### (3) 微观基准：混合是内存带宽，不是算力

开机时对一块 ARGB32 表面做固定次数的操作（临时基准，现已删除）：

| 操作 | 40×16（640px） | 320×240（76800px） |
|---|---|---|
| cairo `save`+`clip`+`restore` | 21 µs | 24 µs |
| **cairo `paint`（`AlphaBlend` 的内部路径）** | **709 µs**（1.11 µs/px） | **24063 µs**（0.31 µs/px） |
| 手写标量 ARGB 混合 | 75 µs（0.12 µs/px） | 16030 µs（0.21 µs/px） |

三点：

1. cairo 的**状态开销极低**（21µs），之前在 10.2 里量到的 `PushClipRect` 931µs
   属于 GDI 那层（建区域 + 导出矩形 + 求交），不是 cairo 本身。
2. `cairo_paint` 对小面积的**单价**反而高 3.6 倍（1.11 vs 0.31 µs/px），说明它每次
   调用有一笔几百微秒的固定开销，与面积无关。
3. 手写标量循环是 **0.21 µs/px ≈ 50 周期/像素**，而它每像素只有十来个整数操作
   —— 也就是**卡在 PSRAM 带宽**（614KB / 16ms ≈ 38MB/s），不是 ALU。
   加 SIMD 换不来带宽，所以对 640px 的 alpha 方块，SIMD 的理论收益是"最多 1ms"。
   只有"全屏级半透明叠加"才值得考虑，而且上限也就 24ms → 16ms 这个量级。

#### (4) 真正的钱花在哪：每帧的绘制操作次数

同一份插桩（`SOUI_PORT_PROFILE=1`）按窗口拆出来的每帧微秒数：

| 窗口 | `total` | `own`(自绘) | `pre` | `child` | `post` | `lay`(离屏提交) |
|---|---|---|---|---|---|---|
| root | 30459 | 6948 | 400 | 22968 | 8 | 0 |
| **fade_box**（40×16 layered） | **8125** | 3046 | 1164 | 77 | ~~3726~~ | 1540 |
| anim_box（40×16 普通） | 5807 | 2923 | 425 | 74 | ~~2232~~ | 0 |
| fps_label（每 7 帧才动一次） | 5010/3 次 | 4594 | 56 | 19 | 319 | 0 |

> **`post` 这一列作废**（划掉的两格）。`post` 的定义是 `pw4 - pw3`，而这一段
> 只剩 `_PaintNonClient` 和 `RestoreClip(nSave1)`：前者在 `rcWnd == rcClient`
> 时立刻 `return`（`Swnd.cpp:1854`），后者只在 app 层真的调过 `SetWindowRgn`
> （即 `m_clipRgn`/`m_clipPath` 非空）时才有 `nSave1 != -1`。demo 里两个都不
> 成立，所以 `post` 只能是 0 —— 实测却报了 2.2ms，说明我把串口里
> `child`/`post` 两列读串了。**下面只依赖 `own` 和 `lay` 两列**，这两列的位置
> 没有歧义（`lay` 还和 10.4(2) 的 BitBlt 实验互相印证）。

- `lay`（`EndDraw` + `AlphaBlend`）只有 **1.54ms**，和 (2) 的结论一致。
- 一个 40×16 的普通窗口每帧要 **5.8ms**，其中**自绘 2.9ms 是画 640 个像素**——
  折合 4.5µs/px，而全屏填充实测是 0.13µs/px，差 35 倍，所以这 2.9ms 也不是像素。
- 结论仍然是 (3) 第 2 条的固定开销：一个窗口每帧有 4~6 次绘制调用，每次 ~0.7ms，
  跟画了多少像素无关。**每帧的绘制调用次数**才是要压的东西。


## 11. clip-free 极简窗口：把窗口树的代价单独量出来

> 本轮是路线 ①/② 验证的收尾实验。结论是**此前把瓶颈归给"20~44 次裁剪状态切换"
> 的说法已经过时**（那是 10.1 优化之前的实测）；10.1 之后每帧只剩约 1 次
> `PushClipRect`，所以路线 ② 的 clip 缓存几乎没有可优化空间。

### 11.1 做法

> **注**：11.1~11.4 记录的是第一轮实现（同一矩形重复 FillRect，档位 1/2/4/8/16）。
> 当前代码已改成 **11.5 的"独立元素数"扫描版**（1/2/4/8/16/32/64，每个元素
> 是不同位置、不同颜色的矩形）。开关、统计口径、脏区大小都没变。

`main/soui_demo.h` 里的 `SOUI_FPS_ONLY`（默认 1）切换两套负载：

- `0`：原来的 soui_sim demo（caption / skin / combobox / 两个动画）。
- `1`：`CFpsWnd`（`main/soui_demo.cpp`），一个 `SHostWnd` 派生窗口**自己接管
  `WM_PAINT`**，只做 `FillRect` + 一次 `DrawTextA` 显示 fps 和帧计数，然后
  `EndPaint`。它**不调用** `SHostWnd::OnPrint()`，因而完全不进
  `SWindow::DispatchPaint()`、不建子窗口 render target、**0 次 `PushClipRect`**。
  `BeginPaint/EndPaint` 保留，所以 canvas → framebuffer → present → SPI 路径与原
  demo 完全相同。

每轮消息泵调用 `soui_fps_tick()` 只 invalidate 一个 **200×28 = 5600 px** 的矩形
（与原 demo 每帧 4~6 kpx 同量级），`postMsg()` 会合并 WM_PAINT，因此帧率只受
"画一帧要多久"限制，没有定时器天花板。为量化"一次 GDI 绘制调用"的代价，每 9 秒
把每帧 `FillRect()` 次数按 **1/2/4/8/16** 轮转；所有调用都填同一个矩形、用同一把
刷子，屏上始终只有一行 fps，改变的只是 cairo `save/rectangle/fill/restore` 的次数。
3 秒报告里额外打一行 `[scan] calls=N` 标档位。

### 11.2 实测（同一块板、同一统计口径：完成的 WM_PAINT / 墙钟秒）

| 负载 | 同口径 fps | paint/帧 | flush/帧 | 像素吞吐 | clip push/帧 |
|---|---|---|---|---|---|
| 原版 demo（基线） | 26.4 | 34.04 ms | 2.23 ms | 148 kpix/s | ~1 |
| 路线 ② clip 状态缓存 | 27.5 | 33.06 ms | 2.20 ms | 153 kpix/s | ~1 |
| 路线 ①+② RGB565 + DMA | 27.3 | 33.01 ms | 2.20 ms | 152 kpix/s | ~1 |
| **clip-free 窗口，1 次调用** | **83.3** | **9.70 ms** | 2.60 ms | 467 kpix/s | **0** |
| clip-free，2 次调用 | 83.3 | 10.10 ms | 2.60 ms | 467 kpix/s | 0 |
| clip-free，4 次调用 | 77.0 | 10.80 ms | 2.60 ms | 431 kpix/s | 0 |
| clip-free，8 次调用 | 71.7 | 12.25 ms | 2.60 ms | 401 kpix/s | 0 |
| clip-free，16 次调用 | 58.3 | 15.10 ms | 2.60 ms | 327 kpix/s | 0 |

日志：`C:/Users/setou/AppData/Local/Temp/soui_fpsonly_scan.log`（17 个 3 秒窗口，
`clip=0/0` 全程成立）。档位边际成本：**(15.10−9.70)/15 = 0.36 ms / 次调用**。

### 11.3 三条结论

1. **路线 ② 的前提已失效，路线 ① 也救不了帧率**。clip 计数显示 `clip=0/77`
   （≈1 次/帧），`SOUI_FAST_CLIP` 的包含性快路径一次都没命中；RGB565 直出 +
   DMA 异步把转换缓冲和上屏等待去掉了，但 `paint` 仍是 33.0 ms、fps 仍是 27.3
   —— 因为 `flush` 本来只占 2.2 ms。**这两条路线都不该继续投入**。
2. ~~**真正的大头是"窗口树 × 每帧几十次 GDI 绘制调用"**~~ —— **此归因已被
   11.5 推翻，勿沿用**。把整个 `DispatchPaint` 拿掉、只留一次填充 + 一次文本，
   帧率确实从 26.4 → 83.3（3.2 倍），但那主要是**画的元素从几十个降到了 1 个**，
   不是窗口树结构变简单了。11.5 在同样 0 clip、同样 5600 px/帧的窗口里把元素数
   补回 64 个，`paint` 立刻 10.3 → 31.9 ms、fps 83.3 → 29.7，几乎复现原 demo
   的 34.0 ms / 26.4 fps。**窗口树遍历 + 那 1 次 clip + skin 合计只值约 2 ms**；
   主导项是每帧 GDI 绘制调用次数 × 约 0.34 ms/次。
3. **GDI/cairo 像素路径本身还有一层固有成本**。0 clip、1 次调用时 `paint`
   仍有 9.70 ms，其中 SPI 2.60 ms，剩 7.1 ms 是 cairo 填充 5600 px + 字形 +
   `commitCanvas` 合成，折合约 **1.27 µs/px**，与 10.2 微观基准 `cairo_paint`
   的 1.11 µs/px 吻合。对照 LVGL 151 fps / 6.56 ms（5064 px/帧），差距约 3 ms
   来自 ARGB32 全屏画布 + Win32 DC 抽象，而不是 SPI。

**所以下一轮该做的不是换渲染器、也不是调 clip**，而是**减少每帧的 GDI 绘制
调用次数**：合并 invalidate 减少参与绘制的窗口、缓存不变内容的离屏 RT（alpha
变化只重新 AlphaBlend）、把同一窗口的多次绘制调用合成一次。11.5 的线性曲线
说明每少一次调用就省 0.34 ms，这是唯一被实测定量过的杠杆。

### 11.4 复现

```powershell
# main/soui_demo.h: SOUI_FPS_ONLY = 1 (极简窗口) / 0 (原 demo)
$env:IDF_PATH = 'D:\work\soui4\soui-freertos\esp32s3\tools\esp-idf-v5.4.1'
$env:IDF_TOOLS_PATH = 'D:\work\soui4\soui-freertos\esp32s3\tools\.espressif'
. "$env:IDF_PATH\export.ps1"
ninja -C 'D:\work\soui4\soui-freertos\esp32s3\build' -j8 app
python -m esptool --chip esp32s3 -p COM4 -b 460800 write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build\soui_esp32s3.bin
```

板上当前跑的是 `SOUI_FPS_ONLY=1` 的**元素数扫描版**（每 9 秒换一档，屏上读数
在 29.7~83.3 之间跳，屏上同时显示当前 `e=N`）。改回 `SOUI_FPS_ONLY=0`
增量编译重烧即恢复原 demo。

### 11.5 补充实验：83fps 是不是"画得太少"换来的？（是）

第一轮的 83.3 fps 有一个明显的替代解释：极简窗口只画 1 个矩形 + 1 行文本，
而原 demo 有十几个控件。为把这个变量单独拎出来，把 `CFpsWnd` 改成每帧画
**N 个独立的可见元素**——每个都是不同的子矩形、不同的预创建刷子，即一次
真实的额外 `FillRect()` 调用；N 按 **1/2/4/8/16/32/64** 每 9 秒轮转。所有元素
都落在同一个固定的 `200×28 = 5600 px` 脏区内，背景填充 1 次、fps 文本 1 次
保持不变，所以**像素量、SPI flush 全程恒定（2.60 ms）**，唯一变量是绘制调用
次数。日志用 `[scan] elements=N` 标档位。

| 独立元素数 | 同口径 fps | paint/帧 | 帧周期 | flush/帧 | 像素吞吐 |
|---|---|---|---|---|---|
| 1 | **83.3** | 10.27 ms | 12.00 ms | 2.60 ms | 467 kpix/s |
| 2 | 78.2 | 10.60 ms | 12.79 ms | 2.60 ms | 438 kpix/s |
| 4 | 75.0 | 11.20 ms | 13.33 ms | 2.60 ms | 420 kpix/s |
| 8 | 67.3 | 12.65 ms | 14.86 ms | 2.60 ms | 377 kpix/s |
| 16 | 57.9 | 15.40 ms | 17.29 ms | 2.60 ms | 324 kpix/s |
| 32 | 43.7 | 20.80 ms | 22.88 ms | 2.60 ms | 245 kpix/s |
| 64 | **29.7** | **31.90 ms** | 33.67 ms | 2.60 ms | 166 kpix/s |

日志：`C:/Users/setou/AppData/Local/Temp/soui_elements_scan.log`（24 个 3 秒窗口，
`clip=0/0` 全程成立）。曲线严格线性：**(31.90−10.27)/63 = 0.343 ms/元素**，
与第一轮"同矩形重复填充"测得的 0.36 ms/次一致。

**结论**：用户的质疑成立。83.3 fps 主要就是"画得少"换来的，不是去掉窗口树的
功劳。三组数据的**每帧像素量其实完全相同**：

- 原 demo：148 kpix/s ÷ 26.4 fps ≈ 5606 px/帧
- 极简窗口 1 元素：467 kpix/s ÷ 83.3 fps ≈ 5606 px/帧
- 极简窗口 64 元素：166 kpix/s ÷ 29.7 fps ≈ 5589 px/帧

像素一样、SPI 一样、clip 都是 0（原 demo 约 1 次/帧），帧率却差 2.8 倍，
差别只能来自**每帧发起了多少次 GDI 绘制调用**。64 个元素（共 66 次调用）时
`paint` 31.90 ms 已逼近原 demo 的 34.04 ms，剩下约 2.1 ms 才是窗口树遍历、
那 1 次 clip、skin 与多窗口 render target 的合计成本。

这也重新解释了 LVGL 为什么能到 151 fps：不是它的像素循环神奇，而是它**一次
刷新只发起很少几次绘制调用**（一个 `lv_area_t` 脏区、立即模式、无 DC 状态机、
无逐控件 GDI 调用）。SOUI 要提速，最有效的方向是把每帧几十次调用压到几次，
而不是换光栅化器、调裁剪或提 SPI 时钟。

> **11.6 补正**：这句只说对了一半。LVGL 高帧率的另一个同等重要的前提是它
> **从不全屏推屏**（部分缓冲 + 脏矩形，SPI 带宽峰值只用了 32%）。11.6 的
> 空绘制实验证明，一旦要求每帧把 320×240 全送上屏，40MHz SPI 的 31.4ms
> 就是所有方案共同的硬天花板；此时"画多少"反而是次要矛盾。

### 11.6 空 WM_PAINT + 每帧全屏刷新：21.0 fps，天花板是 SPI

**问题**：`OnPaint` 里一次 GDI 都不画，但每帧仍把整块 320×240 送到屏幕，
fps 能到多少？

**做法**：`main/soui_demo.h` 新增 `SOUI_FPS_SCAN_MODE`（默认 1）。`CFpsWnd`
一次烧录自动轮转 4 档 × 9s，`[scan] blank mode=N` 标档位：

| 档 | invalidate | OnPaint | 上屏路径 |
|---|---|---|---|
| 0 `empty_full` | 320×240 | **0 次 GDI** | BeginPaint/EndPaint → commitCanvas → blitCanvas → SPI |
| 1 `empty_small` | 200×28 | **0 次 GDI** | 同上，只提交 5600 px |
| 2 `fill_full` | 320×240 | 1× FillRect(全屏) + 1× DrawText | 同上 |
| 3 `direct_full` | 无 | 无 WM_PAINT | `swinxFbPresent(NULL)` 直推 framebuffer |

档 3 没有 WM_PAINT，用 `soui_board_frame_tick()` 把「一次推屏」记为一个完成
的刷新回合；`app_main.cpp` 的 3s 报告因此改用 `soui_board_frame_count()`
差分算 fps（其余档与原来的 `nPaintCount` 完全相等），并新增 `frame=xx.xms`
帧周期。首帧画一次可见背景，之后档 0/1 严格 **0 次 GDI 调用**（屏上画面
静止，但 SPI 全速推）。另外接管 `WM_ERASEBKGND` 返回 TRUE，排除默认擦背景
偷偷绘制。

**为什么空绘制仍会全速推屏**：`wnd.cpp:3552 ReleaseDCInternal()` 在 FreeRTOS
分支只做 `GetClipBox()` 就无条件 `commitCanvas()`，提交范围来自
`wndObj->invalid.hRgn`，与「有没有调用过 GDI」无关；`hasDirty` 短路只存在于
`__OHOS__`。所以只要保留 `BeginPaint/EndPaint`，中间一个像素不画，整屏照样
blit + 推 153600 字节。（若连 `BeginPaint/EndPaint` 都不调，则一帧都不会
present——`wnd.cpp:1444` 还会清掉脏区。）

**实测**（`C:/Users/setou/AppData/Local/Temp/soui_blank_scan.log`，取每档中间的
稳定窗口，剔除跨档的 3s 混合窗口；两轮 stage 0 都复现 21.0）：

| 档 | 同口径 fps | 帧周期 | paint/帧 | flush/帧 | presents/帧 | 像素/帧 |
|---|---|---|---|---|---|---|
| 0 空 + 全屏 | **21.0** | 47.6 ms | 46.4 ms | 31.4 ms | 1.00 | 76800 |
| 1 空 + 小脏区 | **111.3** | 9.0 ms | 6.7 ms | 2.6 ms | 1.00 | 5600 |
| 2 2 次 GDI + 全屏 | **16.7** | 60.0 ms | 58.5 ms | 31.4 ms | 1.00 | 76800 |
| 3 直推 fb（无 WM_PAINT） | **30.3** | 33.0 ms | 0.0 ms | 31.4 ms | 1.00 | 76800 |

`clip=0/0` 全程，`presents/frame=1.00`（无 composeAbove 干扰）。档 3 的
2330 kpix/s ≈ 4.66 MB/s，已达到 40MHz SPI 理论 5 MB/s 的 93%。

**47.6 ms 花在哪**：

```
空绘制全屏 47.6ms ─┬─ 31.4ms  SPI 推 153600 B（物理上限，不可绕过）
                   ├─ ~15.0ms  canvas(ARGB32) → fb(RGB565) 整屏 BlendOverFb565
                   │           + WM_PAINT 派发 / GetDC / HRGN / cairo_surface_flush
                   └─ ~1.2ms   消息泵其余开销
```

加 2 次 GDI 调用（档 2）再多 12.4ms，主要是一次 **全屏** `FillRect` 的 cairo
软件光栅化；把脏区缩到 5600 px（档 1）则掉到 9.0ms——同一套框架、同样 0
次 GDI，只因像素量少 13.7 倍，帧率从 21.0 涨到 111.3。

**结论**：

1. 直接回答：**21.0 fps**，比原 demo 的 26.4 fps 还低。「不画东西」并不等于
   「免费」，只要要求每帧全屏刷新，就必须付整屏合成 + 整屏 SPI 的钱。
2. **全屏刷新的硬天花板是 SPI，不是 CPU**：40MHz 推满一屏 31.4ms，对应
   31.8 fps；档 3 实测 30.3 fps。任何「每帧全屏」的方案都不可能超过这个值，
   除非提 SPI 时钟 / 换并行接口 / 只推脏区。
3. 补正 11.5 末尾关于 LVGL 的说法：LVGL 能到 151 fps 不只因为绘制调用少，
   **还因为它用部分缓冲 + 脏矩形、从不全屏推屏**（峰值 1.58 MB/s，约 SPI
   带宽的 32%）。让 LVGL 每帧全屏刷新，同样会掉到 ~30 fps。SOUI 的优化顺序
   应是：① 永远只提交脏矩形（本项目已做到，原 demo 每帧仅 5600 px）；
   ② 把每帧几十次 GDI 调用压到几次（11.5 实测 0.343 ms/次）；③ 最后才是
   换光栅化器 / 提 SPI 时钟。
4. 档 1 的 111.3 fps 是这套 swinx/GDI 框架在「零绘制调用 + 最小脏区」下的
   上限：其中 SPI 2.6ms，消息泵 + BeginPaint/EndPaint + commitCanvas 的固定
   开销仅约 4.1ms。框架本身构不成瓶颈。

**复现**：

```powershell
# main/soui_demo.h: SOUI_FPS_ONLY=1, SOUI_FPS_SCAN_MODE=1
$env:IDF_PATH = 'D:\work\soui4\soui-freertos\esp32s3\tools\esp-idf-v5.4.1'
$env:IDF_TOOLS_PATH = 'D:\work\soui4\soui-freertos\esp32s3\tools\.espressif'
$env:ESP_IDF_SHELL = 'powershell.exe'   # 必须：否则 export.ps1 识别成
                                        # "powershell.EXE" 并失败，落到系统 python
. "$env:IDF_PATH\export.ps1"
ninja -C 'D:\work\soui4\soui-freertos\esp32s3\build' -j8 app
python -m esptool --chip esp32s3 -p COM4 -b 460800 write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x10000 'D:\work\soui4\soui-freertos\esp32s3\build\soui_esp32s3.bin'
python tools/serial_capture.py COM4 48   # 4 档 × 9s = 36s，加启动余量
```

`SOUI_FPS_SCAN_MODE=0` 可切回 11.5 的元素数扫描（同一份 `CFpsWnd` 内按宏
分支，不需要改 CMake）。板上当前跑的是 11.6 的四档扫描版。

### 11.7 同一个动画矩形：SOUI 111 fps vs LVGL 151 fps，差在框架税

11.5/11.6 的对照都不够公平：11.5 是「固定脏区里叠 N 个静态元素」，11.6 是
「全屏刷新」。而 LVGL 的 150+ fps 测的是**一个 60×60 方块每帧移动 2px**。
这里把那个负载原样搬到 SOUI（`SOUI_FPS_SCAN_MODE=2`）：

| 项 | LVGL (`main.c`) | 本实验 (`soui_demo.cpp`) |
|---|---|---|
| 对象 | 60×60 方块，y=90 | 同 |
| 移动 | x 在 0..180 往返，每帧 ±2px | 同 |
| 不透明度 | `LV_OPA_70`（178/255） | `AlphaBlend(SourceConstantAlpha=178)` |
| 每帧脏区 | old ∪ new = 62×60 = 3720 px | 同（合并后的外接框） |
| 每帧绘制 | 脏区内背景 + 方块 | `FillRect(old,bg)` + 方块，被 invalid 区裁剪 |
| 调度 | `vTaskDelay(0)` | 档 0/1 为 1 tick，档 2 为 0 |

三档：0 不透明方块（2 次 FillRect）；1 opa70（FillRect + AlphaBlend）；
2 同 1 但 `vTaskDelay(0)`。每档 9s，`presents/frame=1.00`、
`+1242480/3s ÷ 334 帧 = 3720 px/帧`，与 LVGL 的脏区完全一致。

**第一轮**（`OnPaint` 里按 LVGL 的做法分别 `InvalidateRect(old)` +
`InvalidateRect(new)`）：

| 档 | fps | frame | paint | flush |
|---|---|---|---|---|
| 0 不透明 | 100.3 | 10.0 ms | 6.9 ms | 1.9 ms |
| 1 opa70 | 86.0 | 11.6 ms | 8.8 ms | 1.8 ms |
| 2 opa70 + nodelay | 94.7 | 10.6 ms | 8.8 ms | 1.8 ms |

比预期低。于是给 `InvalidateRect()` 加了计时，发现**一次就要 0.75 ms**
（`CreateRectRgnIndirect` + `CombineRgn` + `postMsg`），两次正好 1.5 ms。
既然 `RGN_OR` 本来就会把两个 60×60 合并成同一个 62×60 外接框，改成一次
`InvalidateRect(union)` 不改变任何提交像素。

**第二轮**（合并成一次 invalidate，最终数据）：

| 档 | fps | frame | paint | 其中 inval | flush | 像素/帧 |
|---|---|---|---|---|---|---|
| 0 不透明，2 GDI | **111.3** | 9.0 ms | 6.9 ms | 0.75 ms | 1.8 ms | 3720 |
| 1 opa70，2 GDI | **91.0** | 11.0 ms | 8.8 ms | 0.75 ms | 1.8 ms | 3720 |
| 2 opa70，nodelay | **102.0** | 9.8 ms | 8.7 ms | 0.74 ms | 1.8 ms | 3720 |
| LVGL 同负载（历史） | 151 | 6.7 ms | 6.56 ms(CPU) | ~0 | ~1.8 ms | ~5230 |

日志：`C:/Users/setou/AppData/Local/Temp/soui_anim_scan2.log`（第一轮为
`soui_anim_scan.log`）。

**差距在哪**（档 0，9.0 ms vs LVGL 6.7 ms）：

```
SOUI 9.0ms ─┬─ 6.9ms  WM_PAINT：2 次 FillRect + canvas→fb 合成 + 1.8ms SPI
            │          ← 与 LVGL 的 6.56ms CPU 基本持平
            ├─ 0.75ms InvalidateRect（region 运算 + postMsg）
            └─ 1.35ms 消息泵：PeekMessage ×2、poll_touch、vTaskDelay(1)≈1ms
```

1. **渲染已经追平**：SOUI 的 `paint=6.9ms`（含 1.8ms SPI）对 LVGL 的
   `CPU=6.56ms`。11.5 说的「每次 GDI 调用 0.343ms」在 2 次调用上只值
   0.7ms，不再是主导项。
2. **真正的差距是每帧固定框架税 2.1ms**：`InvalidateRect` 0.75ms（LVGL 的
   `lv_obj_set_x` 只是往 `lv_area_t` 里做一次内存并集，代价≈0）+ 泵 1.35ms
   （其中 1ms 是 `vTaskDelay(1)`）。档 2 把 delay 去掉后 11.0→9.8ms、
   91→102 fps，正好验证了这 1ms。
3. **AlphaBlend 比 FillRect 贵 1.9ms**（6.9→8.8ms）：cairo 的
   `cairo_paint_with_alpha` 要建临时图案+裁剪；LVGL 的 opa70 混合是它在
   部分缓冲里顺手做的，所以 151 fps 那一档本来就是带 alpha 的。
4. 结论：**SOUI 在「一个动画矩形」这种真实负载下是 111 fps（不透明）/
   91 fps（70% alpha），不是 26 fps，也不再是「画得多」的问题**；剩下的
   40~60 fps 差距全部落在 `InvalidateRect` + 消息泵 + AlphaBlend 这三处，
   都能针对性修，而且都不涉及光栅化器或 SPI。

**可继续做的三件事**（按收益排序）：

1. `InvalidateRect()` 快路径：`invalid.hRgn` 为空时直接 `SetRectRgn`，
   跳过 `CreateRectRgnIndirect` + `CombineRgn`（预期省 ~0.7ms/帧）。
2. 泵内合并：一次 pump 处理多条待发帧 / 去掉 1ms delay 改为条件让出
   （预期省 ~1ms/帧，档 2 已证明）。
3. `AlphaBlend`：60×60 常量 alpha 的场景走一次 `cairo_paint_with_alpha`
   之外的手写路径，或把方块预混合进一张 62×60 位图（预期省 ~1ms/帧）。

三者都做，档 0 理论上 9.0 → ~7.3ms ≈ 137 fps，档 1 约 9.2ms ≈ 108 fps。

> 前两件已经做完并实测，见 **11.8**：实际 9.0 → 7.4 ms / 111.3 → 135.7 fps。
> 第 1 件的收益来源与当时的猜测略有出入——真正贵的是那个临时 HRGN 句柄，
> 而不是 region 合并本身。

**复现**：`main/soui_demo.h` 里 `SOUI_FPS_SCAN_MODE=2`，构建/烧录/抓日志
同 11.6；`SOUI_FPS_SCAN_MODE` 设 0/1 可切回 11.5/11.6 的扫描。

### 11.8 把两个框架税削掉：111.3 → 135.7 fps

11.7 结尾列的三件事里，前两件（`InvalidateRect` 快路径、去掉泵内固定 1ms）
已经实现并实测（第三件 AlphaBlend 未做）。负载与 11.7 档 0 完全相同：
60×60 方块每帧 ±2px、不透明、两次 `FillRect`、每帧一次 62×60 的
`InvalidateRect`、`presents/frame=1.00`、3720 px/帧。

**改了什么**

| 记法 | 改动 | 位置 |
|---|---|---|
| A | `InvalidateRect()` 不再造临时 HRGN 句柄：`CreateRectRgnIndirect` + `CombineRgn` + `DeleteObject` → 一个栈上 region + 三种精确快路径（空区域 / 已被覆盖 / 单矩形 x 或 y 方向扫掠）；`fp=0` 可恢复旧序列做对照 | `swinx/src/region.cpp` `add_rect_to_region_fast()`、`swinx/src/wnd.cpp` `InvalidateRect()` |
| B | 泵尾不再固定 `vTaskDelay(1)`：改成 `vTaskDelay(0)`（纯让出），每 250 ms 才喂一次 1 tick 给 IDLE1 | `main/app_main.cpp` 泵尾 |

B 之所以要「每 250 ms 喂一次」：`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` 且
`CHECK_IDLE_TASK_CPU1=y`，纯 `vTaskDelay(0)` 只让给同/更高优先级任务，
IDLE1 永远轮不到，5 秒后任务看门狗就报 `soui_ui`。按 9 ms 一帧算，
250 ms 喂一次 = 0.4% 的墙钟，而不是原来的 11%（1ms/帧）。两轮实测
（80 s 日志）无任何 `task_wdt` 输出。

**实测（4 档 × 9 s，跑了两整轮，两轮同值）**

| 档 | fps | frame | paint | 其中 inval | flush |
|---|---|---|---|---|---|
| A=off B=off（= 11.7 档 0） | 111.3 / 111.3 | 9.0 ms | 6.8 ms | 0.79 ms | 1.8 ms |
| A=on  B=off | **125.0** / 125.0 | 8.0 ms | 6.8 ms | **0.18 ms** | 1.8 ms |
| A=on  B=on | **135.7** / 135.7 | **7.4 ms** | 6.8 ms | 0.18 ms | 1.8 ms |
| A=off B=on | 125.0 / 125.0 | 8.0 ms | 6.9 ms | 0.77 ms | 1.9 ms |
| LVGL 同负载 | 151 | 6.7 ms | 6.56 ms(CPU) | ~0 | ~1.8 ms |

两项收益**完全可加**，两轮都复现：

```
9.00 ms ──A──> 8.00 ms ──B──> 7.40 ms      9.00 ──B──> 8.00 ──A──> 7.40
111.3 fps      125.0 fps      135.7 fps    111.3      125.0      135.7
```

- **A：`InvalidateRect` 0.79 → 0.18 ms（−0.61 ms/帧，+12.3% fps）**。
  与 11.7 预测的 −0.7 ms 基本吻合。注意收益的来源是**去掉了那个临时 HRGN
  句柄**（GDI 句柄表 alloc + free + 三次句柄查找），而不是「空区域判断」
  本身：把快路径的快分支关掉、只保留「栈 region + 泛型 union」时，
  inval 就已经从 0.79 掉到 0.20 ms，再加空/扫掠分支只再省 0.02 ms。
  （`fp=0` 之所以要恢复整段旧序列，就是为了让 A/B 只差这一个变量。）
- **B：−1.0 ms/帧（+12.3% fps）**，与 11.7 档 2 的 91→102 一致。
  `frame 8.0 → 7.4` 里剩下的 0.6 ms 是 `vTaskDelay(0)` 的让出 + 两次
  `PeekMessage` + `poll_touch` + 3 s 上报的分摊。
- **合起来 111.3 → 135.7 fps，帧周期 9.0 → 7.4 ms（−18%）**，与 LVGL 的
  151 fps / 6.7 ms 之间只剩 0.7 ms，而 `paint=6.8 ms` 已经和 LVGL 的
  `CPU=6.56 ms` 基本一致——**渲染侧已经没有差距，剩下的全在泵**。

**现在的 7.4 ms 构成**

```
7.4ms ─┬─ 6.8ms WM_PAINT（2 次 FillRect + 62×60 canvas→fb 合成 + 1.8ms SPI）
       ├─ 0.18ms InvalidateRect
       └─ 0.4ms  泵：vTaskDelay(0) 让出、PeekMessage ×2、poll_touch
```

**默认值**：A 在 swinx 里默认开启（`s_bRegionFastPath = 1`），对真实 demo
的每一次 `Invalidate()` 都生效，不只是这个实验窗口；B 只在
`SOUI_FPS_ONLY` 的扫描里启用，`SOUI_FPS_ONLY=0` 的真实 demo 仍用
`vTaskDelay(1)`（它的帧率由 16 ms 动画器节流，1 ms 不再是瓶颈，而且固定
1 ms 对输入延迟更友好）。要全局启用 B，把 `app_main.cpp` 泵尾那个
`#else vTaskDelay(1)` 分支也换成同一套 pacing 即可。

**复现**：`main/soui_demo.h` 里 `SOUI_FPS_SCAN_MODE=3`（当前默认），
构建/烧录/抓 80 s 日志；4 档每 9 s 轮换，3 s 上报里 `fp=` 是当场生效的
region 快路径开关。日志：`C:/Users/setou/AppData/Local/Temp/soui_opt_scan2.log`。

> **补正（见 11.9）**：A 项的收益与 `InvalidateRect()` 的**调用频率**成正比。
> 这个实验窗口是 100+ 次/秒，值 0.61 ms；真实 demo UI 只有 ~5 次/帧，
> 实测只省 ~0.02 ms/帧，帧率差不出噪声。别把这里的数字外推到真实负载。

### 11.9 真实 UI 28 fps；以及一次"画面错乱"的完整归因（SPI DMA 分块同步）

11.8 的 135.7 fps 是极简实验窗口的成绩。这一节把 `SOUI_FPS_ONLY` 关掉、
恢复原版 demo UI（状态栏 + 控件列表 + 两个动画窗口），量真实场景，然后
处理一个真正的显示 bug。

**真实场景的 fps：28.3 / 29.7（交替，均值 28.5）**

| fps | frame | paint | flush | presents/frame | pixels | clip | region |
|---|---|---|---|---|---|---|---|
| 28.3 | 35.3 ms | 32.5 ms | 2.1 ms | 1.15 | 151 kpix/s | 0/85 | 170/176 |
| 29.7 | 33.7 ms | 30.7 ms | 2.1 ms | 1.17 | 157 kpix/s | 0/89 | 177/184 |

- 两个值交替出现只是 3 s 统计窗把同一个 ~34.5 ms 周期量化成 85 或 89 帧，
  不是两档性能。
- `paint` 32.5 ms 里，SPI 只占 2.1 ms，**其余全是 SOUI 的绘制调用开销**
  （10.3 / 10.4 的 0.343 ms/次乘上调用次数）。
- **region 快路径在真实窗口树里命中率 96.6%**（`fallback=6`/176），
  但帧率与关闭时（27.0~28.3）差不出噪声以外——因为真实 UI 每秒只有
  ~60 次 `InvalidateRect`（约 5 次/帧），而 11.7 的动画窗口是 100+ 次/秒。
  11.8 的 A 项收益与 invalidate 次数成正比，**真实 UI 上它省的是
  ~0.02 ms/帧，不是 0.61 ms**。这条修正很重要：别把实验窗口的数字直接
  外推到真实负载。

**症状**：真实 UI 上屏后，动画窗口**上方**那块区域显示的不是预期界面，
"绘制位置错乱"；关掉 region 快路径后依然，所以和 11.8 的 A 项无关。

**排查路线（四步，每步都在做减法，其中第 2 步是弯路）**

1. **dump framebuffer**：`soui_board_dump_fb()` 把 fb 的 RGB565 逐行 hex
   打回串口（先在 PSRAM 里快照再打印，每 4 行 `vTaskDelay(1)` 喂 IDLE1，
   否则全屏 hex 会触发 `task_wdt: IDLE1`）。重建出图像后发现
   **fb 内容是完好的**——文字清晰、位置正确。所以问题不在 SOUI 渲染，
   而在"fb → 面板"这一段。
   （中间踩过一个假象：2× 降采样 dump 让文字出现锯齿，一度被当成"文字画错"。改成
   1:1 局部裁剪后文字完全正常，才把渲染层彻底排除。降采样 dump 只适合看大色块。）
2. **四象限映射测试图**（`soui_board_test_pattern()`，直接写 fb 并推屏，
   完全绕开 `SWindow`）。报告是"左上绿 右上红"，而 fb 是"左上红 右上绿"
   → 当时判定为**面板左右镜像**，于是把 MADCTL 从 `0x28` 改成 `0xA8`
   （`MV|MY|BGR`）。**这一步是错的**，理由见第 4 步——那次判断是在 DMA bug
   还没修、画面本身就是环绕伪影的情况下做的：损坏的帧把 fb 右边缘的绿色带
   环绕到了最左列，看起来正好像"左上绿 右上红"。真实的 UI 又刚好帮不上忙：
   极简 fps 窗口是整块深色背景 + 一个移动方块，**镜像后看着完全一样**，
   所以这个"镜像"从 10.x 一直没人碰。
3. 改 `0xA8` 后仍不对：黑方块跑到了别的色块、"四个色块没铺满、每块中间有
   细横线"。这一轮把测试图改成**三段式**，一次分清三种可能——
   先整屏刷**品红**（"canary"：任何残留品红 = 全屏那一推没覆盖满，
   看到的是旧像素），再刷四象限，最后推两个**颜色独一无二**的局部方块
   （青色在红区 (100,100)、黑色在黄区 (250,180)）。
   照片结论：**有品红细线残留**（全屏推送边界丢数据）+ 两个方块位置全错。
   → 问题在 SPI 推屏的数据完整性和地址窗口，不在映射，于是去查 DMA 分块（下节）。
   修完 DMA 后测试图完全正确。
4. 修完 DMA 后**真实 UI 仍然左右镜像**（文字反向、`✕/−` 跑到左侧、垂直顺序
   却正常）。这时唯一可靠的判据是**交叉验证 fb 和玻璃**：
   把 fb 的标题条 (0,0)-(320,24) 以 `step=1` dump 回串口、在 PC 上重建成图像——
   里面是正常的 `SOUI5 on FreeRTOS`，`✕`/`−` 在**右**边；而屏上照片是镜像的、
   按钮在**左**边。**fb 正常 + 屏镜像 + 当前 MADCTL=0xA8 ⇒ 镜子是 `0xA8`
   自己加的**（`MV=1` 时 MX/MY 的屏幕语义互换，MY 管的就是水平那一位）。
   改回厂商原值 `0x28`（`MV|BGR`）后，真实 UI 完全正常。
   所以 10.x 一开始抄的厂商值就是对的，第 2 步「修」的是一个不存在的镜像。

**根因：`soui_board_esp32.cpp` 的 DMA 双缓冲同步有两处错**

```c
// 错法一：get_trans_result() 返回的是"队列里最先完成的那笔"，不指定 buffer
void wait_chunk(int idx) {
    if (!s_inflight[idx]) return;
    spi_device_get_trans_result(s_spi, &done, portMAX_DELAY);  // 可能弹回另一个 buffer 的
    s_inflight[idx] = false;                                   // 却标记 idx 已完成
}
```

- **错法一**：`spi_device_get_trans_result()` 无法按 buffer 取结果，只会返回
  最先完成的那笔。拿 A 的结果去清 B 的标志，两个 buffer 的「在飞」状态就
  逐渐漂移：某个 buffer 实际还在被 DMA 读，`s_inflight` 却是 false。
- **错法二**：填下一块之前没等它空闲。`wait_chunk()` 只在 `queue_chunk()`
  里被调用，而调用顺序是「queue(idx) → idx^=1 → 立刻开始写新 idx」，
  等到真正 wait 时**缓冲已经被覆写一轮**：

  ```c
  queue_chunk(idx, used);   // 等的是 idx
  idx ^= 1;
  p = ensure_chunk_buf(idx); // ← 这一块可能还在 DMA 读，立刻被写
  ```

两处叠加的后果分两个层次：
- CPU 改写 DMA 正在读的缓冲 → **每条 chunk 边界（8192 B ≈ 12.8 行）附近
  一小段颜色错误**，就是照片里的细横线/品红残留；
- `wait_all_chunks()` 可能提前返回 → 下一次 flush 的 `0x2A` **在像素流还
  没发完时就拉低了 DC 线**，面板把剩下的像素字节当命令吃掉，
  **地址窗口被写坏** → 整幅图平移、边界外环绕（照片左侧那条绿/黄竖带、
  底部那条黄/蓝横带就是环绕出来的）。

修法（`soui_board_esp32.cpp`）：

```c
// 按身份回收：一直弹到目标 buffer 的那笔回来为止
void wait_chunk(int idx) {
    for (int guard = 0; guard < kNumChunks && s_inflight[idx]; ++guard)
        retire_one_blocking();     // 弹出的那笔属于谁，就清谁的 flag
    s_inflight[idx] = false;
}
// 填之前先等：这个 wait 必须在第一次写入之前
queue_chunk(idx, used);
idx ^= 1;
wait_chunk(idx);
p = ensure_chunk_buf(idx);
```

**代价为零**：修复前后 `flush` 都是 2.1 ms、真实 UI 帧率都是 28.3~29.7。
之前省掉的等待不是优化，是省掉了正确性——异步双缓冲能重叠的只有
「CPU 做字节交换」和「上一块的 DMA」，而这两者本来就是并行的。

**教训**

- `spi_device_get_trans_result()` 的语义是"取任意一笔已完成的结果"，
  ESP-IDF 的 `spi_master` 没有 "wait for transaction X" 的 API。要么用一个
  `SemaphoreHandle_t` 逐笔带回调，要么像这里一样**按指针身份回收**。另外，
  「两个 buffer 轮流用」这个模式里，**等待必须紧挨在写入之前**，
  而不是紧挨在提交之后。
- **画面本身不可信的时候，不能拿画面当判据**。第 2 步把 DMA 损坏造成的
  环绕伪影读成了"左右镜像"，接着"修"出一个真实镜像，白烧了两版固件。
  可靠的判据是**走不同通路的两个观测**：fb 是软件侧的可证状态（dump 走串口，
  和 SPI 显示通路完全无关），照片是玻璃上的实际结果，两者一比就知道镜子
  在链路的哪一段。这类"哪一段出错"的问题，先找一条**不经过可疑环节**的
  观测通路，比在可疑环节上加更多测试图有效得多。

**留下的诊断工具**（`main/soui_board.h`，都是手动调用，不自动触发）：

| 函数 | 用途 |
|---|---|
| `soui_board_dump_fb(step)` / `soui_board_dump_rect(x0,y0,x1,y1,step)` | 把 fb 的 RGB565 逐行 hex 打回串口，off-device 重建；`step=1` 看文字，`step>=4` 看大色块 |
| `soui_board_test_pattern()` | 品红 canary + 四象限 + 两个唯一色局部方块，一次分辨「映射错 / 推屏不完整 / 局部 present 错」 |

### 11.10 28 fps 的真正构成：不是"画得多"，是每帧的框架固定成本

11.9 收尾时真实 UI 是 28 fps。用户指出：只有一个方块在滚，其它地方不该刷新，
理论上应该接近空载（11.5 的 100+ fps）——**28 fps 说明根本原因还没找到**。
这一节把它量到底。

先说结论：**重绘请求本身是对的**。每帧只有 1 次 `WM_PAINT`、只有 1 个窗口
（320x240 的主窗口）、dirty region 只有 1 个矩形（包围盒 ~5.8 kpx ≈ 100x58）。
问题在于这次重绘要走完 SOUI 主窗口的整条 paint 管线，而那条管线的成本与
"脏了多大面积"无关。

**加了三个剖析器**（都是累计值 + 定期清零）。*这些探针已在优化收尾时全部
移除，下表记录的是它们当时的位置与口径*：

| 剖析器 | 位置 | 回答什么 |
|---|---|---|
| `SwinxPaintProf*` | `swinx/src/wnd.cpp` | 每次 WM_PAINT 是哪个 hwnd、窗口多大、dirty region 几个矩形/多少 px、耗时，并拆成 pre / proc |
| `SwinxDrawProf*` | `swinx/src/gdi/cairo/gdi.cpp` | fill / blt / stretch / alpha / text / sel(SelectObject) / measure 各自的次数与累计耗时 |
| `SouiCullProf*` `SouiClipProf*` `SouiHostProf*` `SouiSvgProf*` | SOUI 与 render-gdi | 裁剪判断次数、clip 保存/恢复、`OnPrint` 的 layout/redraw/present、SVG 栅格化 |

时钟统一走 **`QueryPerformanceCounter()`** —— `sysapi.h` 本来就声明了它，各端口
各自实现（POSIX 用 `gettimeofday`、10 MHz；freertos 用 `esp_timer`、1 MHz，见
`swinx/src/platform/freertos/winobjs.cpp`，和 `GetTickCount()` 放在一起）。
各探针在**自己的 TU 里**有一个 `static inline int64_t profNowUs()`，负责
`tick → 微秒`（除以 `frequency/1e6`，频率是常量，读一次就缓存）。

> 早先的版本为此导出了一个 `SwinxProfNowUs()` C 符号，6 个模块各自写一句
> `__attribute__((weak))` 声明去引用它 —— 一个纯粹为调试存在的跨模块 ABI，
> 而且弱引用在静态库里有"`.o` 没被拉进来就静默变 0"的坑。改用 QPC 后
> **没有新增任何对外符号**：探针是文件内 `static`，时钟是框架本来就有的 API。

**第一刀：每窗口每帧 3 次 region 裁剪判断（已修，+37%）**

`SWindow::DispatchPaint()` 对每个子窗口都问"这个矩形和脏区域相交吗"，而且
一问就是 3 次（入口、client、nonclient）。原来的实现只要矩形相交就
`CreateRegion` ×2 + `CombineRgn` ×2 做精确判断——在这个 port 里 region 是
GDI 句柄，一次这样的判断 **0.8 ms**，每帧 14.5 次 ≈ **952 ms/3s**。

修法（`SOUI/src/core/Swnd.cpp`）：单位矩阵下窗口矩形在设备空间里轴对齐，
**包围盒相交就是正确答案**，直接返回，不建任何 region。非单位矩阵
（缩放/旋转）才走原来的多边形精确判断。偏保守（多画一个窗口）是允许的，
像素仍被裁掉。

```
27.0 fps / frame 37.0ms / paint 33.6ms   →   36.0~38.0 fps / frame 26.5ms / paint 23.1ms
cull 952ms/3s (deep=603)                 →   cull 130ms/3s (deep=0)
```

**完整账目**（35.7 fps，paint 24.5 ms/帧，剖析器自身的开销约 5%）

| 项 | ms/帧 | 说明 |
|---|---|---|
| pre | 1.3 | `GetDC` + `SelectClipRgn` + `WM_ERASEBKGND` + 状态型 `ReleaseDC` |
| layout | **0.5** | `GetRoot()->UpdateLayout()`，**不是瓶颈** |
| redraw | **16.2** | `_RedrawRegion`：BeginDraw → PushClip+ClearRect → 窗口树 → EndDraw |
| 　└ cull | 1.4 | 14 次/帧 |
| 　└ fill | 0.67 | 1 次/帧 |
| 　└ text | 0.7~0.9 | 0.5 次/帧 |
| 　└ SelectObject | 0.06 | 13 次/帧，每次 4.6 µs |
| 　└ clip save/restore | 0.7 | 3.5 次/帧 |
| 　└ **未归因** | **~12.7** | 见下 |
| present | 0.1 | `UpdatePresenter` 把 memRT 贴到窗口 DC |
| commit | 3.8 | 其中 SPI 2.1，合成 1.7 |

**已被数据排除的假设**：

- **布局**：0.5 ms。动画只改位置，没有触发整树重排。
- **SVG 栅格化**：稳态 `svg=0c`，只有开机首帧出现 5 次（102 ms，正是
  9-patch 的 9 次全量栅格化）。动画期间确实不触发——用户的判断正确。
  （这条值得记：`DrawSVG` 每次调用都遍历整张 SVG 的所有 shape，而
  `DrawSVG9Patch` 一次要调 9 遍，所以**首帧**会卡 100 ms，稳态无影响。）
- **文字/填充**：合计 1.5 ms/帧。
- **clip 保存/恢复**：0.7 ms/帧。
- **提交/上屏**：3.8 ms/帧，其中 2.1 是 SPI 物理时间。

**剩下的 12.7 ms 在哪**：在 `_RedrawRegion` 内、上述叶子之外，也就是
`BeginDraw`/`BeforePaint`、`EndDraw`/`AfterPaint`，以及 `DispatchPaint`
每窗口那些**没有图元、也没被上面任何探针覆盖**的 state 操作——
`pRgn->IsEmpty()`（2~3 次/窗口）、`pRgn->GetRgnBox()`（1~2 次/窗口）、
`_ApplyMatrix()` 里的 `GetTransform`/`SetTransform`。按已实测的量级
（一次 `IRegionS` 调用约 0.1 ms，每帧 ~40 次），这部分大约能解释 4 ms，
还有约 8 ms 未定位——下一步就是把 `_RedrawRegion` 切成 r0..r4 四段。
（那段计时骨架已随探针一起移除，需要时按 git 历史恢复。）

**顺手修掉的一个真 bug：UI 任务栈贴着上限（并纠正一条错结论）**

往 `_RedrawRegion` 里加几个局部变量后，同一个固件两次运行一次报
`A stack overflow in task soui_ui`、一次正常。当时把它归因到
`SWINX_FR_DEFAULT_STACK_WORDS`（`components/soui/CMakeLists.txt`）并改成 32KB，
**这个归因是错的**：

- `SWINX_FR_DEFAULT_STACK_WORDS` 只是 swinx **内部辅助任务**的默认栈；
- UI 任务在 `main/app_main.cpp` 里用 `kUiTaskStackBytes` 显式创建，
  一直是 **48KB**，上面那个宏怎么改都碰不到它；
- 32KB 那次"好了"纯属运气——它只是恰好熬过了被观察的那十几秒。

真正的位置和峰值：`DispatchPaint()` → `_PaintChildren()` → `DispatchPaint()`
递归下降整棵控件树，每层带 `CRect`/`SMatrix`/`SPainter`/region 引用，而**最深的
一次是首帧建窗，不是稳态**。探针给每层多吃几十字节后，48KB 在首帧就崩了
（`overflow=11`，日志停在 `named ids registered` 之后，backtrace 已 CORRUPTED）。

已改成 **96KB**（`main/app_main.cpp` 的 `kUiTaskStackBytes`），首帧 + 稳态都不再溢出。

教训：**改之前先确认这个值到底管谁**。`SWINX_FR_DEFAULT_STACK_WORDS` 这个名字
太像一个全局栈设置，实际上和 UI 任务毫无关系；"改完不崩了"也就成了假阳性。

**方法论**：这一轮能推进，靠的是**每次只加一个度量、并且度量的对象必须能
证伪一个假设**。`paint=32ms` 而 `pixels=5.8k` 这个矛盾点，用图元计数器一次性
排掉了"画得贵"，用 paintprof 排掉了"画得多"，用 host 三段排掉了"布局"，
最后收敛到"每窗口的固定 state 成本"。中间唯一一次走弯路（11.9 的 MADCTL）
恰恰是**在没有可靠度量的情况下用画面猜原因**。

> §11.11–§11.14（平台层结构收敛 / 码表合并 / 下沉 / 补测试）与 §11.19（z-order 回归）、
> §11.21（framebuffer 运行时化）属于工程与正确性问题，已从 README 移除；
> 这里只收录性能实测，因此编号有跳号。

### 11.15 背景皮肤 + `cache="1"`：40fps 掉到 18fps 的完整实测分解（2026-11-16）

10.x 里有一条结论"`cache="1"` 更慢（162→126ms，坑 16）"，但一直没量清楚**慢在哪**。
这一轮把 root 的背景（纯色 / `_skin.sys.wnd.bkgnd` 九宫格）与 `cache` 属性做
2×2 实测，每次都只改 XML 里那一个属性，其它（可见控件、动画、编译选项）完全相同。

**四个格子（320×240，同一块板，每格 5 个 3s 窗口的稳定值）**

| 背景 | `cache` | fps | `frame` | `paint` | 其中 `client` | 其中 `children` |
| --- | --- | --- | --- | --- | --- | --- |
| 纯色 `#cccccc` | 0 | **39~40** | 25.0 ms | 21.4 ms | 1.7 ms | 8.3 ms |
| 纯色 `#cccccc` | 1 | 21.7 | 46.2 ms | 43.9 ms | 22.7 ms | 8.6 ms |
| 九宫格皮肤 | 0 | 36~38 | 26.5 ms | 23.6 ms | 4.5 ms | 7.2 ms |
| 九宫格皮肤 | 1 | **18.0** | 55.6 ms | 55.3 ms | 33.4 ms | 9.0 ms |

读法：`cache` 本身就要 **+18~21ms**（纯色 21.4→43.9），皮肤再叠 **+11ms**
（纯色 43.9→55.3）；而**没有 cache 时皮肤只贵 2ms**（21.4→23.6）。
也就是说"皮肤慢"这个从 10.x 传下来的说法只在 cache 打开时才成立——
**真正的乘数是 `cache`，皮肤只是把其中一次整窗填充的单价抬了 2.1 倍**。

**慢在哪：cache 分支的四段（新增 `cacheprof`）**

| 段 | 纯色 | 皮肤 | 说明 |
| --- | --- | --- | --- |
| `clear` = `ClearRect(整窗, 0)` | 9.40 ms | 9.40 ms | 320×240×4B = 307 KB 写一遍 |
| `erase` = `WM_ERASEBKGND`（背景） | 9.85 ms | **20.63 ms** | 纯色=再写一遍；九宫格=读源+写目标（非 1:1 拉伸） |
| `paint` = 子窗口画进 cache RT | 0.57 ms | 0.59 ms | 几乎为 0：**子窗口不画进 cache**，cache 只缓存窗口自己的背景 |
| `blend` = 整窗 `AlphaBlend` 回宿主 RT | 2.30 ms | 2.30 ms | 被宿主 RT 的脏区裁过，只贴 ~5.7k px |

关键的反直觉点：**cache 只缓存 root 自己的 client 绘制（背景），子窗口仍然由
`_PaintChildren()` 直接画到宿主 RT 上**（`paint=0.57ms` 说明它进 cache 时几乎没干活）。
所以打开 cache 得到的不是"背景只画一次"，而是"每帧多写两遍整窗、再贴回来一次"。

**为什么一次整窗 pass 就是 9ms：PSRAM 的写带宽**

`clear` 9.40 ms / 307 KB = **32.7 MB/s**，这个数字太整，于是直接标定内存带宽
（一次性 memset/memcpy，40 次平均）：

| 区域 | memset | 307 KB 耗时 | memcpy |
| --- | --- | --- | --- |
| PSRAM（`MALLOC_CAP_SPIRAM`） | **34.5 MB/s** | **8.89 ms** | 22.1 MB/s（13.9 ms） |
| 内部 RAM（`MALLOC_CAP_INTERNAL`） | 760 MB/s | — | — |

所以 `clear` 的 9.4ms 就是 PSRAM 的物理下限，**不是 cairo 的算法问题**。
验证过这一点：把 `ClearRect`/`FillRect` 从 `cairo_rectangle()+cairo_fill()`
（pixman general path）改成 `cairo_rectangle()+cairo_clip()+cairo_paint()`
（填色更快的一条路），上板实测 `clear` 仍是 9.41ms、`erase` 仍是 9.84ms，**零收益**，
改动已回退。内部 RAM 快 22 倍（760 vs 34.5 MB/s），但 320×240×4 的整窗表面要 307 KB，
S3 的内部 RAM 装不下——这也解释了 10.3 里"渲染目标换 RGB565"为什么是个方向：
它能把一次整窗 pass 从 8.9ms 压到 4.4ms。

**每帧时间的去向（新增 `stageprof`，覆盖 `DispatchPaint` 顶层）**

| 阶段 | 纯色/无cache | 皮肤/cache |
| --- | --- | --- |
| `mtx`（`_ApplyMatrix`） | 0.55 | 0.52 |
| `clip`（SaveClip/PushClipRect 等） | 0.42 | 0.39 |
| `client`（`_PaintClient`） | 1.70 | 33.41 |
| `children`（`BeforePaint`+`_PaintChildren`+`AfterPaint`） | 8.25 | 9.04 |
| `nc`/`restore` | 0.01 | 0.02 |
| **小计** | **10.9** | **43.4** |
| 余下（`BeginPaint`/`ReleaseDC`/commit/flush/LCD） | ~10.5 | ~12 |

两个此前完全没被度量到的固定成本，正好就是 40fps 的天花板：

- `children` 8.3ms：整棵树的 `DispatchPaint` 递归（每个窗口 ~0.7ms 的固定 state 成本，10.3 的老结论）；
- 余下 ~10.5ms：`commit` 3.8ms + LCD `flush` 2.1ms + 约 4.5ms 未归属（`paint` 记到
  `proc` 之后到 `AfterProcMsg` 之间，下一轮再拆）。

**结论与取舍**

1. **root 上不要写 `cache="1"`**。动画子控件每帧 `InvalidateRect` 会把祖先 cache
   标脏（`SWindow::InvalidateRect` 里 `MarkCacheDirty(true)` 逐级冒泡），于是
   cache 每帧都失效，缓存命中率为 0，只剩整窗擦写的代价。
2. **要皮肤又要帧率：去掉 cache 就够了**（36~38fps，与纯色的 39~40fps 只差 2ms）。
   不需要"背景改用兄弟窗口"这类改造——非 cache 路径本来就按脏区裁剪，背景只画
   ~6k px（`client` 4.5ms）。
3. 真想再往上（>40fps），目标不是皮肤，而是那 ~10.5ms 的固定成本和 `children` 8.3ms。
   ~~以及把 ARGB32 表面换 RGB565（整窗 pass 带宽减半）~~ —— 见 11.16 第 5 条，这个方向是错的。
4. ~~诊断工具已经留在树里~~ —— 11.15/11.16 里的 `cacheprof` / `stageprof` / `cull=` 三个
   探针后来在清理 `Swnd.cpp` 时被一起删掉了（连同 `_WndRectInRgn` 里那段计时），
   `app_main.cpp` 的调用也已同步移除，所以现在 3s 日志里不再有这两行。
   **下面所有分段数字来自带探针的那一版固件**，要复现就按 11.16 的描述把那几个
   计时段加回去。swinx 侧的 `paintprof` / `drawprof` / `commit` 和 11.10 的
   `SouiHostProf*` / `SouiClipProf*` / `SouiSvgProf*` 也在同一次收尾里清理干净了：
   现在 3s 日志只剩一行 `fps=`，所有剖析器与它们的调用点都不在树里。

### 11.16 `cache="1"` 到底错在哪：clear 是白写的，脏标记也是打错的（2026-11-16）

11.15 把 18fps 拆到了段，但留下两个疑问：**(a)** `clear` 为什么要单独占 9.4ms，
清屏和后面的绘制难道不能合成一遍？**(b)** 既然 `cache` 只缓存背景，为什么它每帧都被
判脏？这一节把这两条都落地成代码改动。

**先把"合并"这件事说清楚：cairo 没有命令缓冲，合并不存在**

`ClearRect` → `cairo_paint` → pixman 立即把 307KB 写进 surface；下一句
`FillSolidRect` 再写一遍同一批像素。中间没有显示列表可以"优化掉前一次写"，
cairo 也不会追踪"这个 surface 上哪些像素被写过"（`cairo_surface_mark_dirty` 是给
外部直接改像素用的，不是绘制区追踪）。所以"clear 和 render 合并成一次上屏"在
这个绘制模型里做不到——**唯一的办法是根本不发这个 clear，或者把它缩到脏区**。

另外要澄清一个语义：**上屏只有一次**，就是最后的 `AlphaBlend` 回宿主 RT + LCD flush。
`clear` 不属于上屏链路，它只是给 cache 表面打底，所以它和上屏之间没有"可以合并"的关系。

**clear 在语义上是 100% 冗余的（背景不透明满铺时）**

`ClearRect(&rcWnd, 0)` 的唯一作用是"把缓存表面清成透明"，好让本帧没画到的像素
在回贴时不盖住宿主。但紧接着的 `WM_ERASEBKGND` 会把整个客户区用不透明背景重写一遍
（`OnEraseBkgnd` 里 `FillSolidRect(&rcClient, crBg)`，`#cccccc` 解析出来 alpha=255）。
覆盖之后 clear 写下的东西一个字节都不剩。

所以加了一个判据 `SWindow::CanSkipCacheClear()`：无背景皮肤 + 客户区==窗口区 +
背景色 alpha==255 时跳过 clear。有皮肤时不跳（皮肤位图可能带 alpha），客户区和
窗口区不等时不跳（非客户区那一圈只有 clear 会碰），背景色是 `rgba(...)` 时不跳。

**真正的逻辑错误：子窗口失效把祖先 cache 打脏了**

`SWindow::InvalidateRect` 里，子窗口失效会冒泡到父窗口：
`GetParent()->InvalidateRect(rcIntersect, FALSE, bClip)`，而接收方无条件
`MarkCacheDirty(true)`。问题是 **cache 表面里装的只有"本窗口自己画的那层"**：
`DispatchPaint` 是 `_PaintClient(pRT)` 之后 `_PaintChildren(pRT, ...)`，
子窗口一直画在宿主 RT 上，从来没进过 cache RT（11.15 里 `paint=0.57ms` 就是证据）。
子窗口动一下，父窗口缓存的那层像素根本没变，却被判脏 → 全窗重建。

于是改成只在"自己失效"时才标脏：

```cpp
// bFromThis == FALSE 只出现在子窗口冒泡那一行
if (!bClip && bFromThis)
    MarkCacheDirty(true);
```

**实测（320×240，同一块板，每格 3 个 3s 窗口）**

| 配置 | fps（改前 → 改后） | `paint`（改前 → 改后） | 改后 `cacheprof` |
| --- | --- | --- | --- |
| 九宫格皮肤 + `cache="1"` | 18.0 → **37.7~39.0** | 55.3 → **22.1 ms** | `dirty=0c blend=1.49ms` |
| 纯色 + `cache="1"` | 21.7 → **37.3~39.0** | 43.9 → **22.1 ms** | `dirty=0c blend=1.47ms` |
| 纯色 + 无 cache（用户基线） | 40.2 → 39.7~40.3 | 21.4 → 21.2 ms | `(no cache)` |

`dirty=0c` 是关键读数：113 帧里 cache 一次都没重建，每帧只剩 `blend=1.49ms`
（把背景位图贴到宿主，且被脏区裁过），九宫格那 20.6ms 的 `erase` 整段消失。
补上的 `skipclr` 计数也验证了第一处改动：启动首帧 `dirty=1c skipclr=1c clear=0.00`。

**关于 ARGB32 → RGB565：这个方向是错的**

之前把它当作"整窗 pass 带宽减半"的备选，但实际上：pixman 对 32bpp 有特化的
fill/blend 循环，16bpp 会掉到 general path；320×4=1280 字节的行 stride 换成
640 字节后和 cache line 对齐的关系更差；而且 SOUI 的 alpha 混合、字体抗锯齿、
SVG 渲染全部假定 ARGB32。**真正的问题是"整窗写"这个行为，不是"每像素几字节"**，
11.16 的两处改动已经把那两次整窗写去掉了。

**结论**

1. `cache="1"` 现在对容器是**中性偏赚**：皮肤场景 22.1ms vs 无 cache 的 23.6ms，
   纯色场景 22.1 vs 21.4（几乎相等）。不再出现"开了 cache 反而掉一半帧"。
2. 但**默认仍然别给 root 加 cache**：省下的那 1.5ms 换来的是"背景必须是静态的"
   这个隐含约束，而 root 背景本来就只有 1.7~4.5ms。
3. 真正的性能预算现在是 `children` 8.3ms + 固定成本 ~12ms（`paint` 21.4 里除
   children 之外的部分），和 cache / 皮肤都无关了——回到 11.10 那条主线。

### 11.17 一帧 29ms 的完整账：真画像素只占 36%，其余是每帧固定开销（2026-11-17）

11.15/11.16 把 40fps 拆到了 `children` / `client` / commit，这一节回答三个更基础的问题：
**脏区合并后到底画了几遍？ARGB32 画布的 blit 贵不贵？剩下的时间在哪？**

**探针**：`SOUI/include/core/SPaintProbe.h`（header-only，函数内静态共享，`log=NULL` 即零成本）。
挂点：`SHostWnd::OnPaint`（BeginPaint/EndPaint 两段）、`SHostWnd::OnPrint`（layout、zorder、present）、
`SHostWnd::_RedrawRegion`（脏区与阶段）、`SWindow::DispatchPaint`（逐窗口自身耗时）、
`SWindow::_PaintClient`（谁画了、多大）。配合 `app_main.cpp` 的泵/消息计数。
（blit 会计 `SwinxFbBlitPixels()` 已从 swinx 移除。）

| 字段 | 含义 |
| --- | --- |
| `cycle` | 两次 `OnPaint` 入口的间隔 = **真实帧周期** |
| `wmpaint: begin/print/end` | `BeginPaint` / `OnPrint` / `EndPaint` 三段 |
| `pre: layout / zorder / zc` | `UpdateLayout` / `BuildWndTreeZorder` 耗时，`zc=调用次数/帧数` |
| `walk / v / cli` | 进 `DispatchPaint` 的次数 / 通过区域测试 / 真正画客户区 |
| `px` | 所有画了的窗口 `area(窗口 ∩ 外接框)` 之和 = 逻辑落笔像素 |
| `blit: N px/frame, X% blend` | 每帧真正写进 framebuffer 的像素，其中非不透明的比例 |
| `pump / msgs` | 泵空转、`WM_TIMER`（动画 tick）、其它消息 |

#### 一、先纠正上一版的两个错误结论

上一版把 `BuildWndTreeZorder()` 和"每帧重排文本"列成了主要成本。实测都不对：

1. **`BuildWndTreeZorder()` 稳态确实每帧都调**（`zc=49/49`、`79/79`…，因为每帧的
   `OnRedraw()` 都会把 `m_bNeedRepaint` 置回 TRUE），**但它只要 3~5 µs**。
   "zorder 只在增删时变"这个推测不成立，"它可省 1~2ms"更是错的 —— 省了等于没省。
2. **文本那 4.9ms 不是通病**。它只在 fps 标签恰好与动画脏区相交时发生：83 个采样帧里
   只有 6 帧（7%）画到了文本。**93% 的帧里文本根本不在重绘范围内**，与"子窗口不在 dirty rect 里"一致。

#### 二、blit 不贵，也没有整窗重绘

```
blit: 5213 px/frame, 0% need blend (0 blend px/frame)
```

脏区 85×60≈5100px，blit 5213px —— **一像素不多**，没有整窗 76800px 的打包，
**也没有一像素走 alpha 混合慢路径**（全部命中 `sa==0xFF` 的 `ToRGB565` 快分支）。
所以"ARGB32 → RGB565 转换"这条线可以彻底关掉：它连 0.3ms 都不到。

#### 三、29ms 的完整账（无文本帧，`n=77`，中位数）

```
cycle 29.0ms = wmpaint 24.2 + WM_TIMER 3.20 + pump 0.30 + 其它 ~1.3
wmpaint 24.2 = begin 0.82 + print 19.55 + end 3.84
print 19.55  = layout 0.55 + dispatch 16.84 + 其它 2.16
dispatch 16.84 = client 10.38 + setup 2.08 + 遍历未归类 2.17 + excl 0.87 + tail/bef/aft 1.09
```

| 段 | 耗时 | 占帧 | 说明 |
| --- | --- | --- | --- |
| **`client` 真画像素** | **10.38 ms** | **36%** | 9975px；root 铺满脏区 5160 + 动画窗 4800 |
| `setup` | 2.08 | 7% | 13 个窗口的区域测试 + 两次 clip 压栈，每窗口 ~160µs |
| `_PaintChildren` 遍历未归类 | 2.17 | 7% | |
| `EndPaint` | 3.84 | 13% | `commitCanvas`（每帧 2 次 `GetRegionData` + `malloc`）+ blit + **SPI 2.1** |
| `BeginPaint` | 0.82 | 3% | `GetDC` + `ExtSelectClipRgn` + `GetClipBox` |
| `WM_TIMER` 动画 tick | 3.20 | 11% | `OnNextFrame` → `Move()`（内含一次 `BuildWndTreeZorder`）+ 2 次 `InvalidateRect` |
| `OnPrint` 其它 | 2.16 | 7% | `CreateRegion` / `GetClipBox` / present 调用 |
| `layout` | 0.55 | 2% | 每帧 `UpdateLayout()`，布局其实没变 |
| `excl` | 0.87 | 3% | `SaveClip` + `_ExcludeVideoCanvasFromPaint` |
| pump 空转 | 0.30 | 1% | `vTaskDelay(1)` 只占 1% |
| `BuildWndTreeZorder` | **0.004** | 0% | 每帧都调，但基本免费 |

**账是闭合的**：19.55 + 3.84 + 0.82 + 3.20 + 0.30 + 1.3 = 29.0ms，与 3s 报告的
`frame=29.7ms / paint=25.9ms` 一致（`paint` 比 `wmpaint` 多约 1.7ms，是
`SMessageLoop::OnMsg()` 的 `ExecutePendingTask()`/`PreTranslateMessage`）。

#### 四、和 LVGL 对比：每像素更便宜，有效率差 3 倍

| | 帧周期 | 真正画像素 | 每像素 | 有效率 |
| --- | --- | --- | --- | --- |
| LVGL 16bpp（11.7 同负载） | 6.6 ms | 6.6 ms / 5062 px | 1.30 µs/px | ~100% |
| SOUI（本 demo，无文本帧） | 29.0 ms | 10.4 ms / 9975 px | **1.04 µs/px** | 36% |

**SOUI 每像素比 LVGL 还便宜 20%**；差的 22ms 里，18.6ms 是框架固定开销
（setup / 遍历 / BeginPaint / EndPaint / 定时器 / layout），与"画了多少像素"无关。

这正好解释两个现象：
- 把界面上其它元素全隐藏，fps 不变 —— 它们的像素本来就少，省的是 `client`，
  而 `setup`+遍历照付；
- 脏区从 258px 缩到 45px，文本耗时几乎不变 —— 文本的成本与像素无关。

#### 五、优化优先级（按实测收益）

1. **`WM_TIMER` 3.20ms/帧**：`Move()` 里又做一次 `BuildWndTreeZorder`，且分开两次
   `InvalidateRect`。改成"一次并集 invalidate + 位置直改"可省 ~2ms。
2. **`EndPaint` 3.84ms**：`commitCanvas` 每帧 `GetRegionData` 两次 + `malloc/free`；
   SPI 2.1ms 是物理下限，可省 ~1.5ms（缓存 region 数据、避免堆分配）。
3. **`setup` 2.08 + 遍历 2.17ms**：13 个窗口只有 2 个画。用 `RectInRegion` 精确否决、
   或把 clip 压栈延后到确认要画之后，可省 ~2ms。
4. **`layout` 0.55ms + `OnPrint` 其它 2.16ms**：布局没脏就别 `UpdateLayout()`。
5. `BeginPaint` 0.82ms：`ExtSelectClipRgn` 每帧重建一次 cairo clip，可复用。

**但要有心理预期**：即使把上面 5 项全部清零（省 ~9ms），帧周期仍有 20ms
（≈50fps），因为"真画 9975px"就要 10.4ms。**要突破 100fps，只能减少脏区面积
（让动画窗口更小 / 减少与背景的重叠重绘），而不是继续压框架开销。**

**复现**：`app_main.cpp` 里三行接线（`now_us` / `log` / `nEvery`），日志以 `sp:` 开头；
把 `log` 置 NULL 即完全关闭（每次进入只多一次指针判空）。`nEveryWin=30` 才输出逐窗口明细，
把 UART 占用压在 2% 以内。原始日志 `%TEMP%\sp9.log`（探针）与 `sp10/sp11.log`（泵与消息类型）。

### 11.18 按 11.17 的账逐项优化：33.7 → 45.7 fps（探针开），56.7 fps（探针关）

11.17 列了 5 项优先级。这一节是逐项落地后的实测。**先纠正其中一条**：

> 11.17 第 4 条写"布局没脏就别 `UpdateLayout()`"。这个说法不准确 ——
> `SRootWindow::UpdateLayout()` 第一行就是 `if (!IsLayoutDirty()) return;`，
> 设计上本来就是脏了才布局。真正的问题是**脏标记发出去了没有收回**：
> `SWindow::UpdateLayout()` 里只有 `dirty_child` 会被清回 `dirty_clean`，
> `dirty_self` 不清。于是 root 一旦被 `RequestRelayout()` 置成 `dirty_self`
> 就再也没干净过，每帧都白跑一遍 `GetLayout()->LayoutChildren()`。
> 探针加 `ld=`（root 进 `UpdateLayout` 时是否 dirty 的累计计数）后一目了然：
> 修复后 `ld=1/1219` —— 1219 帧里只有第一帧是脏的。

#### 一、改了六处，每一处都对应 11.17 的一个数字

| # | 改动 | 文件 | 对应 11.17 的账 | 实测 |
| --- | --- | --- | --- | --- |
| 1 | `SRegion_GDI::CombineRect()` 复用一个 `m_hTmp` 区域，`SetRectRgn` 改写代替 `CreateRectRgnIndirect`+`DeleteObject` | `components/render-gdi/linux/render-gdi.{h,cpp}` | `WM_TIMER` 3.20ms（每次 invalidate 都建/删一个区域） | `msgs: timer` **3.20 → 2.77ms** |
| 2 | `SRegion_GDI::GetRgnBox()` 缓存外接框（`GetRegion`/所有 mutator 失效） | 同上 | `setup` 2.08ms（每窗 2~3 次区域查询） | `setup` **1393~2121 → 390~580µs** |
| 3 | `SWindow::UpdateLayout()` 把 `dirty_self` 也清回 clean | `SOUI/src/core/Swnd.cpp` | `layout` 0.55ms | `layout` **550 → 5µs** |
| 4 | cairo `ApplyRegion`/`ApplyRegionAnd` 先试栈上 `RGNDATA`（24 个矩形），过大才 malloc | `swinx/src/gdi/cairo/gdi.cpp` | 每窗 clip 一次的 malloc/free | 并入下面 `pre` 的下降 |
| 5 | `commitCanvas()` 的 `GetRegionData` 尺寸查询 + `malloc` 换成栈上缓冲 | `swinx/src/platform/freertos/SConnection.cpp` | `EndPaint` 里的 `GetRegionData`×2 + malloc | 小（EndPaint 主要是 SPI） |
| 6 | `_RedrawRegion` 的脏区对象在 `m_rgnInvalidate`/`m_rgnSpare` 之间乒乓，不再每帧 `CreateRegion` | `SOUI/src/core/{shostwnd,SHostWnd}.{cpp,h}` | `OnPrint` 其它 2.16ms 的一部分 | `pre` 的 `rgn` 段 0.65ms |

外加一处 **`cache="1"`**（不是框架改动，是 demo 的 `dlg_main.xml`）：`fps_label` 每帧被
`anim_box` 的扫动脏区带上（扫动下沿 y=225，标签上沿 y=222，只重叠 3 行），而不缓存时
**为了这 3 行要把 7 个字形重新栅格化一遍**：`text 196,222,110x18 5221us 141px`。
加 `cache="1"` 后变成一次 AlphaBlend：**5221 → 1006µs**。

> 注意别和 11.15/11.16 的结论打架：那两节说的是"整窗背景用 `cache="1"` 反而更慢"，
> 因为那个窗口**每帧都被 invalidate**，缓存等于白清一遍再画一遍再多贴一遍。
> 这里的标签只在文本变化（0.5s 一次）时才脏，正是缓存的适用场景。判据是
> `MarkCacheDirty` 的频率，不是窗口大小。

#### 二、前后对比（同一块板，同一统计口径）

| | 帧率 | `frame` | `paint` | blit px/帧 |
| --- | --- | --- | --- | --- |
| 优化前（探针开） | 33.7~36.3 | 29.7 ms | 24.2~27.0 ms | 5213 |
| 优化后（探针开） | **45.0~46.0** | **21.9 ms** | 18.8 ms | 5106 |
| 优化后（探针关） | **56.0~57.7** | **17.3 ms** | 14.2 ms | 5108 |

**探针本身要 4.5ms/帧**（每 10 帧 8 行日志走 115200 串口，是真实开销）。
所以横向对比要同口径：`33.7 → 45.7` 是探针开的数字，`-26%` 帧周期；
而正常固件（`SOUI_PROBE=0`，现在是默认）是 **56.7fps / 17.4ms**，
对应 11.17 之前那个 40fps/25ms 的基线，即 **+42% 帧率**。

#### 三、现在的账（探针开，`px=9978`，`v=3` 的代表帧）

```
cycle 18ms = wmpaint 14.8 + WM_TIMER 2.8 + pump/其它 ~0.4
wmpaint 14.8 = begin 0.80 + pre 1.83 + (region 6.0 + 尾 0.3) + end 3.7   [print 8.4]
pre    1.83 = rgn 0.65 + BeforePaint 0.35 + PushClipRegion 0.84
region 6.0  = clear 0.68 + root 5.96
root   5.96 = client 3.55 + setup 0.58 + save 0.11 + excl 0.09 + tail 0.11 + bef 0.47 + 未归类 0.9
client 3.55 = root 背景 1.40 + 动画块 1.05 + 文本 1.00
end    3.70 = commitCanvas + blit(5106px) + SPI(~2.2)
present 0.015  ← memRT → 窗口画布那一步；实测 15µs，不用动
```

逐窗口（`nEveryWin` 输出）现在长这样：

```
root   0,0,320x240     1409us 5040px     ← 动画窗扫过的背景
window 125,165,80x60   1051us 4800px     ← 动画块自己
text   196,222,110x18  1006us 252px      ← 缓存后的 AlphaBlend
```

对照 11.17 的 29ms 账本：`setup` 2.08→0.58、`layout` 0.55→0.005、`WM_TIMER` 3.20→2.80、
`client` 10.38→3.55（这一项一半是文本缓存、一半是这版帧里少画了 1 个窗口）。
**每像素成本从 3.0µs 降到 1.75µs（探针关）**，LVGL 同负载是 0.92µs。

#### 四、还剩下什么（按当前实测）

1. **`WM_TIMER` 2.80ms/帧**：还剩两次独立的 `InvalidateRect`（各自走一遍
   `CombineRect` + swinx 的 `add_rect_to_region_fast` + `SendExposeEvent`）。
   合并成一次"old ∪ new 外接框"可以省一半，代价是脏区从"两个矩形"退化成"一个外接框"
   （本 demo 里两者面积只差 1%，但大跨度跳跃时不一样）。
2. **`PushClipRegion` 0.84 + `BeginPaint` 0.80 = 1.6ms 的 cairo clip 安装**。
   `BeginPaint` 那个 clip 在这个 port 里只被 `OnPrint` 的 `GetClipBox` 读一次，
   而 `rcInvalid` 本来就能从 `pRgnUpdate->GetRgnBox()` 拿到 —— 可以给 HDC 加一个
   "裁剪框"字段、跳过 `ExtSelectClipRgn`，省掉一次 `cairo_clip`。
   属于 swinx 层改动，影响所有平台，需要单独验证。
3. **`end` 3.7ms 已经是总线时间**：5106px × 2B = 10.2KB，SPI 40MHz 约 2.2ms，
   加 5 次命令事务的固定开销。**只能靠减少像素或提高 SPI 时钟**。
4. `clear` 0.68ms：`ClearRect(rcInvalid, 0)` 清的是脏区，本来必要；但如果
   root 背景是不透明皮肤，这次 clear 是白写的（11.16 的老问题）。
5. `setup` 0.58ms：13 个窗口只画 3 个，区域否决已经比表面积相交快很多了。

**天花板**：把 1/2/4 三项全部清零（约 -4ms），探针关的帧周期约 13.4ms ≈ **75fps**。
再往上必须减少脏区面积（动画块与背景的重叠、或让背景不动），而不是继续压框架开销 ——
这条结论与 11.17 一致。


### 11.20 `GetRgnBox` 缓存为什么值 4.5ms/帧：省的不是算法，是环境（2026-11-17）

**现象**：删掉 `SRegion_GDI` 里的 `m_rcBox` 缓存（`GetRgnBox` 透传给 swinx），
fps 从 57 掉到 43。直觉上这不成立：swinx 的 `::GetRgnBox` 是 Wine 移植，
读 `WINEREGION->extents` 是 O(1)，`GDI_GetObjPtr` 只是两次指针解引用，
`get_region_type` 是个 switch —— 理论成本 <100ns，而它每帧只被调 ~36 次。
**"和外部维护一个 box 理论上没什么区别"完全正确 —— 在算法层面。**

**实测矩阵**（全部同一块板、同一负载）：

| 配置 | fps | 单次成本 |
| --- | --- | --- |
| boot 微基准（系统安静） | — | **48ns/call** 热，50ns 冷（memset 64KB 冲 cache 后） |
| 帧内背靠背 2000 次（UI 活跃） | — | **54.7µs/call** |
| 帧内分散调用（间隔 ~4ms 绘制工作） | 44.3 | **~117µs/call** × 36 = 4.2ms/帧 |
| LOOP100（窗口内 100 次背靠背） | 4.3（预期崩塌） | 57µs/call —— **时间真实流逝，非测量伪影** |
| `GetRgnBox` 本体 `IRAM_ATTR` | 44.0 | 128µs（仅省 9µs，取指排除） |
| `GetObjectType`/`GetGdiObjPtr` 也 IRAM | 44.7 | 127µs（调用链接管，取指彻底排除） |
| 对象位置 | — | `_Handle`=0x3fcde200、`WINEREGION`=0x3fcd26e8 —— **都在内部 SRAM**，DRAM 直访段 |

**结论**：同一条指令流、同一块 SRAM 数据、无锁、代码在 IRAM —— boot 48ns，
帧内 54~117µs，**差 3 个数量级的成本完全由"调用时刻的系统状态"决定**。
帧内每次这条跨模块调用链（`GetRgnBox` → `GetObjectType` → `GetGdiObjPtr` → 读 extents）
都要付出 ~100µs 的环境代价；证据排除了取指（IRAM）、数据位置（SRAM）、锁，
剩余解释指向 **MSPI/cache 层的仲裁与同步**：UI 帧里每 3.5µs 的调用间隔中，
PSRAM 画布流量、LCD SPI DMA 与 cairo/pixman 光栅化交替占用总线，
跨函数调用的 cache/TLB 状态建立与核间同步被放大成几十 µs 的固定开销。
最后一层（为何恰好 100µs 量级）需要 JTAG 性能计数器才能定量，工程上不再深挖。

**缓存生效的原因**：`m_rcBox` 把"跨模块 hop"变成"本对象字段的读"——
它省的不是 48ns 的算法时间，而是帧内 ~100µs 的环境成本 × 36 次 ≈ 4.5ms/帧。
在 ESP32 这类 cache 层级复杂、多主设备争用总线的 SoC 上，
**"远端对象上的 O(1) 读"的真实成本由 cache 状态决定，不由算法复杂度决定**。
热路径上凡是"函数调用 + 间接寻址"能换成"调用方本地字段"的，都值得实测。

**顺带的正确性验证**：探针账（nEvery=100）下缓存版与透传版的
`walk=13 v=3 cli=3 nc=2` 完全一致 —— 缓存版没有少画窗口，`IsEmpty()`
语义也未变，57fps 不是漏画换来的。px 的 ~300px 差异来自 fps_label
每 500ms 的文本帧落在不同采样点，与缓存无关。

**遗留的实验设施**（默认零开销，留作后续分析）：
- `swinx/src/region.cpp`：`GetRgnBox` 标了 `SWINX_RGNBOX_IRAM`（= ESP-IDF 的
  `IRAM_ATTR`，其余平台空宏）—— 省 ~9µs/次，且免 flash cache。
- `swinx/src/gdi/cairo/gdi.cpp`：`GetObjectType`/`GetGdiObjPtr`/`SetGdiObjPtr`
  同样 IRAM —— 它们在所有 GDI 热路径背后，IRAM 化是普适收益。
- `render-gdi.cpp`：`SwinxRgnBoxCallCount()` 计数器保留（3s 报告的
  `rgnbox:` 行）；`SOUI_RGNBOX_TIMING`（QPC 计时）与 `SOUI_RGNBOX_NOCACHE`
  （透传 A/B）编译开关已移除定义，代码保留。
- `app_main.cpp`：boot 微基准（`rgnbox: hot=...`）+ 帧内一次性微基准
  （`rgnbox in-frame: ...`，只跑一次 ~110ms）。

**最终状态**：缓存版 + 零计时开销 = **56.3~57.7fps / 17.3~17.8ms**，
与 z-order 回归修复后的基线一致。



## 12. 双动画场景：从 ~20 fps 到 40 fps（2026-11-17）

**结论**：当前设备跑的是双动画版本（`dlg_main.xml` 里 `fade_box visible="1"`），
稳态约 **40 fps**。这与 `README.md` / 评估报告长期沿用的"双动画 ~20 fps"不是同一份构建：

| | 旧口径（11-15，优化前） | 当前版本（11-17） |
|---|---|---|
| 单动画 | ~35 fps | **56~58 fps** |
| 双动画（扫动 + layered alpha） | 18~22 fps | **约 40 fps** |

工作区里与 HEAD 的差异只有一行（尚未提交）：

```diff
-    <window name="fade_box" pos="14,224,54,240" colorBkgnd="@color/blue" visible="0"/>
+    <window name="fade_box" pos="14,224,54,240" colorBkgnd="@color/blue" visible="1"/>
```

**为什么值得单独记一笔**：20 fps 时代曾把"双动画比单动画慢 15~17 fps"归因为 layered 窗口的
离屏合成（§10.3 的 A/B/C 三点实测：35 → 30.7 → 18 fps）。经过 §11.17 / §11.18 那六处
逐项优化（`GetRgnBox` 缓存、`dirty_self` 清回 clean、区域对象复用、`cache="1"` 等）后，
同样两个动画现在跑 40 fps——相对单动画 56 fps 仍差约 16 fps，说明 layered 合成的
结构性成本还在，但"每帧多付 20 ms"的旧结论已经不适用于当前固件。

**复现**：在 `soui-freertos/esp32s3` 下确认 `uires/xml/dlg_main.xml` 的 `fade_box` 为
`visible="1"`，按 `BUILD_FLASH.md` 重新编译烧录，串口读 `fps=` 行或看屏上 `fps_label`。

