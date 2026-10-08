# MiniGUI 开发计划

> 状态：进行中（2026-10-01，M0～M10 已完成，MiniCAD 已不再依赖 ImGui；下一步 M11 Vulkan 后端）
> 定位：一个**保留模式**的 C++ GUI 库，首要目标是作为 [MiniCADLib](D:/MiniCADLib) 的界面层，逐步替换现有的 Dear ImGui。

---

## 1. 目标与非目标

### 目标
- **保留模式**：界面是一棵长期存在的节点树，只在数据变化时修改；可查询、可增删、可由数据描述。
- **按需重绘**：没有变化时不布局、不绘制、不 Present，空闲时 CPU/GPU 占用接近零。
- **GPU 优先**：绘制指令按 GPU 友好的方式组织，第一个后端为 D3D11，可与 MiniCAD 共用 `ID3D11Device`。
- **中文友好**：UTF-8、动态字形图集（支持数千个汉字）、输入法候选窗定位、组合串内嵌显示。
- **可嵌入**：CAD 视口作为一个普通节点嵌入界面；宿主程序掌控主循环。
- **跨平台预留**：平台层、渲染层可替换，后续支持 Emscripten/WebGL2（MiniCAD 已有 WASM 版）。
- **可测试**：软件光栅后端输出确定性像素，用于截图对比测试。

### 非目标（至少在 1.0 之前）
- 不做通用框架的全部功能：无障碍接口、从右往左书写的文字、复杂字形组合，暂不考虑。
- 不做原生外观，界面统一由自己绘制。
- 不做多线程 UI，只有一个 UI 线程。
- 不做动画系统（保留节点结构，后续可以加）。

---

## 2. 技术约束（与 MiniCAD 保持一致）

| 项 | 选择 |
|---|---|
| 语言 | C++20 |
| 构建 | CMake 3.20+、Ninja、`CMakePresets.json`（debug / release） |
| 编译器 | MSVC（`/utf-8`），后续 Emscripten |
| 图形 | D3D11（主力）、Vulkan（第二个 GPU 后端）、软件光栅（测试用）、WebGL2（后续） |
| 依赖 | 尽量少：stb_truetype（MiniCAD 已有），其余按需引入 |
| 命名风格 | 与 MiniCAD 一致：命名空间 `MiniGUI`，类名和方法名 PascalCase，成员变量 `m_` 前缀，注释用中文 |

**构建环境注意（沿用 MiniCAD 的经验）**：在普通 PowerShell 中直接 `cmake --build` 会找不到 MSVC 标准库头文件，需要先调用 `vcvars64.bat`，或者在 VS 开发者命令行中构建。

---

## 3. 总体架构

### 3.1 每帧流程

```
平台事件 ──► 分发事件（命中测试 / 焦点 / 捕获 / 冒泡）
          ──► 修改节点树，标记脏（需要重新布局 NeedsLayout / 需要重绘 NeedsPaint）
          ──► 【没有脏标记则跳过后续全部步骤】
          ──► 计算样式 ──► 布局（Measure / Arrange）
          ──► 绘制：遍历节点，生成 DrawList
          ──► 宿主渲染视口节点（CAD 画面）
          ──► 渲染后端执行 DrawList ──► Present
```

**从 MiniCAD 学到的教训**：视口输入必须在**同一帧内**采样和消费，宿主的视口渲染要放在"事件分发和布局之后、提交界面之前"，避免晚一帧以及随之而来的各种补丁（`kSettleFrames`、挂起尺寸变化、切换文档后补渲染）。

### 3.2 分层

```
┌──────────────────────────────────────────────┐
│ 控件层 Widgets：Button / Label / TextBox /     │
│   ScrollView / Menu / ComboBox / TabView ...   │
├──────────────────────────────────────────────┤
│ 核心层 Core（与平台无关）                        │
│   节点树 · 事件系统 · 焦点 · 脏标记 · 布局 · 样式 │
├───────────────┬──────────────────────────────┤
│ 文字 Text     │ 绘制 Paint                     │
│  字体 / 图集 /  │  DrawList（三角形 + 纹理 +      │
│  排版 / 编辑    │  裁剪矩形），路径、圆角、描边     │
├───────────────┴──────────────────────────────┤
│ 渲染后端 IRenderBackend                         │
│   D3D11 │ Vulkan │ Software │ WebGL2（后续）    │
├──────────────────────────────────────────────┤
│ 平台层 IPlatform                               │
│   Win32（窗口/输入/输入法/剪贴板/DPI/光标）      │
│   Emscripten（后续）                            │
└──────────────────────────────────────────────┘
```

依赖方向严格自上而下；核心层不引用任何平台头文件（包括 `windows.h`、`d3d11.h`、`DirectXMath.h`）。这一点也是在修正 MiniCAD 目前的问题：核心库反向依赖了 imgui 的头文件。

### 3.3 目录结构（计划）

```
D:\MiniGUI
├── CMakeLists.txt
├── CMakePresets.json
├── PLAN.md                  ← 本文件
├── 3rd/                     第三方（stb 等）
├── src/                     头文件与源码放在一起（与 MiniCAD 一致），按 "模块/文件.h" 引用
│   ├── Core/                Node、事件、焦点、脏标记、UIContext
│   ├── Layout/              布局引擎
│   ├── Style/               样式、主题
│   ├── Paint/               DrawList、几何生成（圆角/抗锯齿/描边）
│   ├── Text/                字体、字形图集、排版、文本编辑
│   ├── Widgets/             各类控件
│   └── Platform/Win32/      Win32 平台实现
├── backends/
│   ├── D3D11/               D3D11 渲染后端
│   ├── Vulkan/              Vulkan 渲染后端
│   └── Software/            软件光栅后端（测试用）
├── samples/
│   ├── HelloGUI/            最小示例
│   └── Gallery/             控件展示
└── tests/
    ├── unit/                单元测试（布局、事件、文本编辑）
    └── golden/              截图对比测试（基准图片）
```

---

## 4. 关键设计决策

### 4.1 绘制指令 DrawList（仿照 ImGui，已被大量验证）
- 顶点 `{ float2 pos; float2 uv; uint32 color; }`，索引 `uint32`。
- 命令 `{ clipRect, textureId, indexOffset, indexCount }`；纹理或裁剪矩形变化时才切分命令，尽量合批。
- 纯色填充采样图集里的一个白色像素，**整个界面只用一套着色器**（纹理 × 顶点色）。
- 抗锯齿采用"边缘羽化几何"：在边缘多生成一圈 alpha 渐变为 0 的三角形，不依赖 MSAA 或着色器，软件光栅也能画出同样的效果。
- **禁止**在接口里出现逐像素操作或读回像素，保证任何 GPU 后端都能高效实现。

### 4.2 渲染后端接口

```cpp
class IRenderBackend {
public:
    virtual TextureId CreateTexture(int w, int h, TextureFormat fmt) = 0;
    virtual void      UpdateTexture(TextureId id, const Rect& region, const void* pixels, int pitch) = 0;
    virtual void      DestroyTexture(TextureId id) = 0;
    virtual void      Render(const DrawData& data) = 0;  // 不负责 Present，由宿主决定
};
```
- D3D11 后端**可以接收外部传入的 Device/Context**，这样 MiniCAD 可以把自己的设备交给它，双方共用。
- 支持"外部纹理"：把宿主的 SRV（例如 CAD 视口的渲染结果）登记成 `TextureId`，控件就能直接显示它。
- **接口要同时适配 D3D11 和 Vulkan**：`Render` 只接收 DrawData，不暴露任何图形 API 类型；Vulkan 特有的东西（命令缓冲、描述符集、多帧并行时的顶点缓冲轮换、纹理上传的暂存缓冲与屏障）全部封装在后端内部。宿主通过后端专属的初始化结构传入 `VkDevice` / `VkQueue` / `VkRenderPass`（或 dynamic rendering 的格式），以及每帧的 `VkCommandBuffer`，这和 D3D11 传入 Device/Context 的方式对应。
- `UpdateTexture` 在 Vulkan 下是延迟执行的：先记录下来，在下一次 `Render` 录制命令时统一上传，所以调用方不需要关心同步问题。

### 4.3 节点树
- `Node` 由父节点通过 `std::unique_ptr` 持有；向上的引用（父节点指针）用裸指针。
- 每个节点保存：样式、布局结果（相对父节点的矩形）、脏标记、事件处理函数、可见/启用/可获得焦点等标志。
- 脏标记向上传播：子节点需要重新布局时，一路标记到根节点。
- 控件（Widget）是 Node 的子类，或者由多个 Node 组合而成。

### 4.4 事件系统
- 指针事件：按下、抬起、移动、滚轮、进入、离开；支持**指针捕获**（拖拽时即使移出控件也继续收到事件）。
- 键盘事件：按键按下/抬起（KeyDown/KeyUp）、字符输入（Char）、输入法组合（Composition）；只发给**焦点节点**，然后逐级冒泡。
- 分发分两个阶段：先从根往下的捕获阶段，再从目标往上的冒泡阶段；任一节点都可以标记"已处理"，终止传递。
- 全局快捷键表：Ctrl+S 等组合键在分发前统一匹配。当文本输入框持有焦点时，按规则让位给输入框（MiniCAD 在 `HandleShortcuts` 里处理过同样的问题）。
- 悬停状态由框架维护，控件只需响应"进入/离开"。

### 4.5 布局
- 先自研一个 Flexbox 子集：方向（行/列）、内边距、间距、固定/最小/最大尺寸、`grow`、`shrink`、主轴和交叉轴对齐。
- 两阶段：Measure（自下而上算期望尺寸）→ Arrange（自上而下分配最终矩形）。
- 布局引擎放在 `ILayoutEngine` 接口后面；如果自研版本满足不了需求，可以替换为 Yoga。
- 另有**绝对定位**，给弹层、悬停提示、下拉菜单使用。

### 4.6 坐标与 DPI
- 界面使用**逻辑像素**（DIP），平台层提供缩放系数，后端换算成物理像素。
- 支持每个显示器单独缩放（Per-Monitor DPI v2）；字形图集按"字号 × 缩放系数"缓存，保证文字清晰。

### 4.7 文字
- 第一版：stb_truetype 光栅化 + **动态字形图集**（按需加载字形，图集满了就扩容或淘汰），能容纳大量汉字。
- 排版：UTF-8 解码、字距、自动换行、省略号截断；测量结果按字符串缓存。
- 文本编辑：光标、选区、撤销/重做、剪贴板、双击选词。
- 输入法（Win32）：处理 `WM_IME_STARTCOMPOSITION`、`WM_IME_COMPOSITION`、`WM_IME_ENDCOMPOSITION`；用 `ImmSetCompositionWindow` / `ImmSetCandidateWindow` 把候选窗定位到光标位置；组合串在输入框内嵌显示并加下划线。
- 后续可选：FreeType + HarfBuzz（需要复杂字形组合或更好的字体微调时再引入）。

### 4.8 样式与主题
- 样式属性：背景色、边框（颜色/宽度/圆角）、文字颜色、字号、内边距，以及各种状态（悬停/按下/禁用/获得焦点）下的变体。
- 主题 = 一组命名的颜色和尺寸变量；控件引用变量，不直接写死颜色值；支持运行时切换深色/浅色。
- 第一版用 C++ 代码描述样式；样式文件（JSON）放到后续里程碑。

### 4.9 弹层
- 根节点下设若干独立层：主界面层、弹层（菜单/下拉框/对话框）、悬停提示层、拖拽层。
- 命中测试从最上层开始；有模态弹层时，下层不再接收输入。

### 4.10 嵌入 CAD 视口
- 提供 `ViewportHost` 节点：布局给出它的矩形；宿主通过回调拿到尺寸，在本帧内完成 CAD 渲染，把结果登记为外部纹理，节点再把这张纹理画出来。
- 视口的指针和键盘事件由 MiniGUI 分发，再原样转发给宿主（MiniCAD 的 `Editor`），坐标换算成视口内的局部坐标。
- 后续优化：CAD 画面直接画在交换链上，MiniGUI 只在视口区域"留空"（对应此前讨论的方案 A）。

### 4.11 主循环由宿主掌控
- MiniGUI 不自带消息循环，只提供 `UIContext::ProcessEvent(...)`、`UIContext::Update()`（返回是否有内容需要重绘）和 `UIContext::Render(backend)`。
- 宿主根据返回值决定是否 Present，没有变化时就调用 `WaitMessage`。

---

## 5. 里程碑

每个里程碑都有可运行的成果和验收标准。

### M0 · D3D11 后端 + DrawList　✅ 已完成（2026-09-30）
- 实际产物：`src/Render/IRenderBackend.h`、`src/Render/DrawData.hpp`、`src/Paint/DrawList.*`、`backends/D3D11/D3D11Backend.*`、`samples/HelloGUI`。
- 与原计划的差异：不设 `include/MiniGUI/` 目录，头文件和 MiniCAD 一样放在 `src/` 下，用 `#include "Paint/DrawList.h"` 这种相对 `src/` 的路径引用。
- CMake 工程、预设、`samples/HelloGUI`。
- Win32 窗口 + D3D11 设备/交换链；实现 `IRenderBackend` 的 D3D11 版本（一套着色器）。
- DrawList：矩形、圆角矩形、线条、带羽化抗锯齿的多边形，以及裁剪矩形。
- **验收**：窗口中显示若干抗锯齿圆角矩形和斜线；拖动改变窗口大小时画面正常。

### M1 · 节点树 + 事件 + 按钮　✅ 已完成（2026-09-30）
- 实际产物：`src/Core/{Event.hpp, Node.*, UIContext.*}`、`src/Widgets/{Panel.*, Button.*}`、`src/Platform/Win32/Win32Input.*`（独立静态库 `MiniGUI_Win32`，核心库不包含 `windows.h`）。
- 实测：空闲 3 秒 CPU 时间增加 0 ms、渲染 0 帧；在没有悬停变化的区域移动鼠标不重绘；按下后拖出再松开不触发；禁用的按钮不响应。
- 布局暂时由节点重写 `OnLayout()` 手工设置子节点 `bounds`，脏标记（`m_needsLayout` / `m_subtreeNeedsLayout`）已按向上传播实现，M2 在此基础上加 Measure/Arrange。
- 事件处理函数里删除节点是安全的：`UIContext` 用树版本号检测，发现节点被移除就停止本次分发。
- `Node`、`UIContext`、绘制遍历、命中测试、指针事件（含捕获、进入/离开）。
- 第一个控件 `Button`：常态/悬停/按下三种状态，点击回调。
- 按需重绘：没有事件时不渲染。
- **验收**：点击按钮触发回调；鼠标静止时 CPU 占用接近 0。

### M2 · 布局　✅ 已完成（2026-09-30）
- 实际产物：`src/Layout/{LayoutStyle.hpp, ILayoutEngine.h, FlexLayout.*}`；`Node` 新增 `LayoutStyle`、`Measure`（带缓存）、默认 `OnLayout` 调用布局引擎；`tests/unit`（31 个用例：布局 21 个、事件 10 个）。
- Flexbox 子集与 CSS 的差异：单行不换行；没有 `flex-basis`（用 width/height 或内容尺寸）；min 默认 0；隐藏节点不占位。
- 子节点边缘按 DPI 缩放对齐到物理像素，相邻项之间不留缝；缩放系数变化时整棵树重新布局。
- `InvalidateLayout` 一路标记到根（同时让各级测量缓存失效），不做提前结束的优化——层级浅，正确性优先。
- 单元测试框架：自己写的最小框架（`tests/unit/TestFramework.h`），不引入第三方依赖；`ctest` 或直接运行 `MiniGUITests.exe`。
- Flexbox 子集 + 绝对定位；Measure/Arrange；布局脏标记。
- 单元测试：各种排列组合的布局结果。
- **验收**：一个"工具栏 + 侧栏 + 主区域 + 状态栏"的框架，改变窗口大小时自动重排。

### M3 · 软件光栅后端 + 截图对比测试　✅ 已完成（2026-09-30）
- 实际产物：`backends/Software/SoftwareBackend.*`（静态库 `MiniGUI_Software`，与平台无关）、`tests/golden/`（`MiniGUIGolden`，6 个场景，基准图片在 `tests/golden/baseline/`）、`samples/Common/`（示例与测试共用的演示节点）、根目录 `run_tests.bat`。
- 光栅化规则与 D3D11 对齐：像素中心 +0.5、顶点吸附到 1/256 像素、左上填充规则、双线性采样 + 截断寻址、相同的混合公式、写回四舍五入。
- 实测：D3D11（GTX 1060 与 WARP）与软件光栅逐像素比较，所有场景最大差值都只有 1 级；Debug 与 Release 构建的软件光栅输出完全相同。
- 容差：软件光栅 vs 基准允许 1 级舍入差、0 个超阈值像素；D3D11 vs 软件光栅阈值 3 级、超阈值像素不超过 0.05%。
- PNG：编码器自己写（LZ77 + 固定 Huffman，基准图 85～170 KB），解码用 `3rd/stb/stb_image.h`（从 MiniCAD 复制，公有领域）。
- 基准图片更新流程：`MiniGUIGolden --update` → 人工检查图片 → 提交；失败时在 `out/<preset>/golden_out/` 输出实际图片和差异图（差异像素标红）。
- `SoftwareBackend`：把同一份 DrawList 光栅化到内存（三角形、纹理采样、alpha 混合、裁剪）。
- 截图对比测试框架：渲染出 PNG，与基准图片逐像素比较，允许少量误差。
- **验收**：M0～M2 的示例在两个后端下画面一致（差异在容差内）；CI 或本地脚本一键运行测试。

### M4 · 文字　✅ 已完成（2026-09-30）
- 实际产物：`src/Text/{Utf8.hpp, Font.*, GlyphAtlas.*, TextSystem.*}`、`src/Widgets/Label.*`、`Button::SetText`、`src/Platform/Win32/Win32Fonts.*`；`3rd/stb/stb_truetype.h`（v1.26，从 MiniCAD 的 `imstb_truetype.h` 复制，公有领域）。
- 字形图集：单张 RGBA8 纹理（白色 RGB + 覆盖率 alpha），行式打包，512 起、翻倍到 2048，满了清空重建；**纯色图元的白色像素就在图集里**，文字和图形共用纹理、合并批次。帧内图集扩容/清空时 `UIContext::Render` 自动重新生成本帧绘制指令。
- 排版在物理像素下进行，基线和字形原点对齐物理像素，150% 缩放下文字清晰；英文按空格换行，中文任意两字之间可换行；单行省略号；测量结果按（文字、字号、宽度、缩放）缓存。
- 字体：核心库只接受文件路径/内存数据；Win32 示例用系统的微软雅黑 UI（`msyh.ttc` 第 2 个字体）+ Segoe UI Symbol 后备。MiniCAD 自带的 `GB2312.ttf` 是仿宋_GB2312（中易，商业字体），不适合界面，也不放进仓库。
- 截图测试：文字场景依赖系统字体，`baseline/fonts.txt` 记录字体文件指纹，指纹不一致的机器上跳过文字场景（不报失败）。新增 `atlas_grow` 场景（1200 个汉字，帧内扩容）。
- 实测：单元测试 43 个全部通过（新增 UTF-8、换行、省略号、Label 布局、图集扩容 12 个）；截图测试 7 个场景，D3D11 与软件光栅最大差值仍为 1；示例中每添加一个新名称的侧栏项目，图集字形数随之增加（209 → 249）。
- 已知不足（后续改进）：无字体微调（hinting），12 号字偏细；图集用 RGBA8 存单通道数据，显存是 R8 的 4 倍；没有字形级别的 LRU 淘汰；暂不支持粗体/斜体（可以用 `AddFont` 加载另一个字体文件实现）。
- 字体加载、动态字形图集、UTF-8、`Label`；DPI 缩放下文字清晰。
- **验收**：中英文混排标签；把 Windows 缩放设为 150% 时文字依然清晰；新增的汉字能按需加载。

### M5 · 焦点 + 键盘 + TextBox + 输入法　✅ 已完成（2026-09-30，微软拼音实机验证通过）
- 实际产物：`src/Core/{Event.hpp（Key/KeyEvent/TextInputEvent/CompositionEvent）, ShortcutTable.*, Clipboard.h}`；`Node` 焦点接口；`UIContext` 焦点/键盘/输入法/双击计数/剪贴板；`src/Widgets/TextBox.*`；`TextSystem::BuildLayout`（带字节偏移的排版信息）；`src/Platform/Win32/{Win32Input.*（键盘、IME）, Win32Clipboard.*}`。
- 焦点：点击聚焦最近的可聚焦祖先，点空白处清除；Tab / Shift+Tab 按树顺序循环，跳过隐藏和禁用的节点；只有键盘导航时显示焦点框；节点被移除、隐藏、禁用时自动失去焦点。
- 按键分发顺序：全局快捷键 → 焦点节点（捕获/目标/冒泡）→ 未处理的 Tab 切换焦点。快捷键沿用 MiniCAD 的规则：只响应按下沿、文本输入时让位（功能键可设 `allowInTextInput`）、消费后不再分发；窗口失去焦点时结束组合、清除修饰键状态。
- TextBox：单行（横向滚动、Enter 提交）/ 多行（自动换行、上下移动保持列）；按词移动与删除（中文连续汉字算一个词）、双击选词、三击选行、拖拽选择；撤销/重做（连续输入合并、空格分组）；剪贴板（单行粘贴时换行变空格）；只读；占位文字；Tab 进入单行框时全选。
- 输入法：组合串内嵌显示并加下划线，不进入正文；确认的文字作为一次输入（可一步撤销）；开始组合时先删除选区；只在文本输入控件持有焦点时开启输入法（其余时候字母键直接作为 CAD 命令键）；每次渲染后把候选窗定位到光标下方（`CFS_EXCLUDE` 排除光标行）；组合中切换焦点会取消输入法里的组合串。
- 测试：单元测试 67 个（新增 24 个：焦点、按键分发、快捷键、TextBox 编辑/剪贴板/撤销/输入法事件/鼠标选择）；截图测试 9 个场景（新增输入框状态、焦点框），D3D11（GTX 1060 与 WARP）与软件光栅最大差值仍为 1。
- **实机验证**（系统缩放 150%）：微软拼音输入 `nihao`+空格、`shijie`+空格、回车，命令回调收到"你好世界"；组合串内嵌显示并带下划线，候选窗位于光标正下方。
- 已知不足：自动换行处的光标总显示在下一行行首；多行输入框还没有滚动条（可以放进 ScrollView，但光标跟随尚未与外层滚动联动）。光标闪烁、I 形光标已在 M6 补上。
- 焦点管理（Tab 切换、点击获得焦点）、键盘事件冒泡、全局快捷键表。
- `TextBox`：光标、选区、剪贴板、撤销、单行/多行。
- Win32 输入法：候选窗定位、组合串内嵌显示。
- **验收**：用微软拼音在输入框里连续输入中文，候选窗跟随光标；Ctrl+C/V/Z 正常。

### M6 · 控件补全（原 M8，按用户要求提前：控件齐全后再进入下一阶段）　✅ 已完成（2026-09-30）
- 需求来源：用户列出的 7 项（菜单栏、下拉框、对话框、树形列表、下拉列表、水平/竖直滚动、命令行补全、悬浮提示、拖动移动、单选/多选），加上按 MiniCAD 现有 ImGui 界面补充的控件（统计了 `apps/Win32/src/UI` 用到的 ImGui 函数）。
- 基础设施：
  - **弹层**（`Core/Popup.*`）：树根下分主界面层 → 弹层 → 悬浮提示层；轻触关闭（点在弹层和所有者之外关闭并消费这次点击）、模态遮罩（挡住下方输入，Tab 只在模态弹层内循环，全局快捷键暂停）、Esc 关闭、关闭时还原焦点、翻转/平移保证完整可见；关闭的节点推迟到下一次 `Render` 销毁，在自己的事件处理函数里关闭是安全的。
  - **定时器**：`UIContext::StartTimer/StopTimer/Tick`，时间与唤醒由平台层提供（Win32 用 `SetTimer`，没有定时器时不设，空闲仍为零 CPU）。
  - **悬浮提示**：`Node::SetTooltip`，停留 0.5 秒显示，相邻控件间移动立即切换，点击后隐藏直到离开。
  - **光标形状**：`Node::GetCursor`（输入框 I 形、分隔条左右/上下箭头），Win32 在 `WM_SETCURSOR` 设置。
  - `Node::InsertChild`、`OnPaintOverlay`（子节点之后绘制，例如滚动条）、`InterceptsHit`（滚动条区域不交给内容）。
  - 图片：`Paint/Image.*`（stb_image，`STB_IMAGE_STATIC` 避免与宿主冲突）、`UIContext::LoadTexture/CreateTexture`（上下文持有、按路径缓存）。
- 控件（`src/Widgets/`）：
  - `MenuBar` / `MenuPopup` / `ShowContextMenu`（子菜单、快捷键提示、勾选、禁用、分隔线、打开时求值的 `enabledIf/checkedIf`、键盘导航、按下与抬起同一行才执行）
  - `ComboBox`（下拉列表放不下时向上翻转、键盘与滚轮）
  - `Dialog` / `ShowMessageBox`（模态或浮动；拖动标题栏移动；Enter 默认按钮、Esc/× 取消按钮；打开后聚焦第一个输入控件）
  - `ScrollView`（竖直 + 水平滚动条、拖动滑块、点击轨道翻页、滚轮、Shift+滚轮、到头后交给外层）
  - `ListView`（单选/多选、Ctrl/Shift、键盘、双击激活；只绘制可见行，10 万行测试通过）、`TreeView`（展开/折叠、← → 导航）
  - `TextBox::EnableAutoComplete` → `AutoComplete`（命令行补全：焦点留在输入框，↑↓ 选择、Enter/Tab 接受、Esc 关闭）
  - `CheckBox`（三态）、`RadioButton` + `RadioGroup`（方向键在组内移动）、`Slider`、`ProgressBar`、`Separator`
  - `NumberBox`（对应 `InputDouble`：微调按钮、↑↓、Shift×10、滚轮；**可输入算术表达式**如 `1200/3+50`，支持全角数字；非法输入恢复原值）
  - `TabView`（可关闭标签、中键关闭、`OnCloseRequested` 由调用方决定、Ctrl+Tab 切换、标签过多时等比缩窄）
  - `Splitter`（拖动调整相邻面板宽/高，带限制）、`ColorButton`（AutoCAD 1～9 号色 + 色板 + 十六进制输入）、`ImageView` / `Button::SetIcon`
  - 文字排版补充**避头避尾**规则：句号、逗号、右括号不出现在行首，左括号不出现在行尾。
  - `TextBox` 补充：光标闪烁（只在持有焦点时运行定时器）、I 形光标。
- 示例：`samples/Gallery`（菜单栏 + 4 个标签页 + 状态栏，覆盖全部控件，使用 MiniCAD 的 16 个工具图标）；`samples/Common/SampleWindow` 为示例共用的 Win32 + D3D11 宿主。
- 测试：单元测试 97 个（新增 30 个）；截图测试 18 个场景（新增 Gallery 9 个：四个页面、150% 缩放、菜单栏 + 子菜单、模态对话框、右键菜单 + 子菜单、展开的下拉框、命令补全、悬浮提示），D3D11 与软件光栅最大差值 2（图标缩小时双线性采样的舍入差异）。
- 实机（150% 缩放）：菜单悬停打开子菜单、对话框居中与遮罩、空闲 CPU 为 0。
- 第二轮补充（用户反馈：缺右键菜单、树/列表在位编辑、多文档标签新建关闭、线型不显示，并自查遗漏）：
  - **右键菜单**：`Node::SetContextMenuHandler` / `OnContextMenu`、`AttachContextMenu(node, builder)`；右键抬起时打开（按下时关闭了已有菜单则不再打开新的）、菜单键、Shift+F10（在光标或焦点控件处打开）。`TextBox` 内置编辑菜单（撤销、重做、剪切、复制、粘贴、删除、全选，按状态禁用）；列表、树、标签页右键先选中所在项。
  - **在位编辑**（`InlineEditor`）：`ListView` / `TreeView` 按 F2、慢速单击（550 毫秒）或 `BeginEdit` 进入；Enter 提交、Esc 取消、点到别处提交；回调返回 false 拒绝（例如图层重名），Enter 时继续编辑、失焦时取消。`TreeItem::SetEditable` 单独禁止某项。
  - **多列表格**：`ListView::SetColumns`（表头、拖动列宽、点击表头回调 + 排序箭头、水平滚动）、`SetCellPainter` 自绘单元格、`SetOnCellClicked`、`SetRowContextMenu`；列表按首字母跳转。
  - **多文档标签**：`TabView` 新建按钮（`OnNewTabRequested`）、未保存标记 ●、拖动排序、右键菜单（关闭、关闭其他、关闭右侧、全部关闭 + 调用方追加项）、标签过多时标签条滚动 + ⌄ 列出全部、Ctrl+W / Ctrl+F4 关闭。
  - **CAD 预览**（`CadPreview`）：标准线型定义（Continuous、Dashed、Hidden、Center、Phantom、Dot、DashDot、Border、Divide）与线型/线宽样例绘制、图层开关/冻结/锁定图标；`ComboBox::SetItemPainter` 自绘下拉项，用于线型、线宽、图层下拉框。
  - **特性面板**：`Expander`（可折叠分组）、`PropertyGrid`（名称 | 编辑控件，名称列宽可拖动）。
  - **菜单栏键盘操作**：助记符 `文件(&F)`（`ParseMnemonic` + 下划线）、单独按 Alt 或 F10 进入键盘模式（← → 切换、↓/Enter 打开、Esc 退出并把焦点还给原来的控件）、Alt+字母直接打开、菜单内按字母执行。
  - 其他：图标按显示尺寸重采样（`ResizeImage` 预乘面积平均，`Button::SetIcon(path)`，解决图标缩小锯齿）；多行 `TextBox` 滚轮滚动 + 位置指示条；`ScrollView::SetHeaderHeight`；`Splitter::SetTarget`。
- 示例 Gallery 改为 6 页：基础控件、列表与树、**图层与特性**（图层表 + 特性面板）、**多文档**、弹层与对话框、命令行；Ctrl+1～6 切换。
- 测试：单元测试 108 个（新增 11 个：右键菜单三种打开方式、输入框编辑菜单、列表在位编辑提交/取消/拒绝、慢速单击、表头拖动与点击、行右键菜单与按字母跳转、树重命名、多文档标签、菜单栏助记符/Alt/F10、特性面板、图片缩放）；截图测试 27 个场景（新增 9 个：树在位编辑、菜单栏键盘模式、输入框右键菜单、线型下拉框、图层表 100%/150%、图层名在位编辑、多文档右键菜单、标签溢出）。Debug / Release / WARP 全部通过。
- 已知不足：通用拖放（drag & drop，例如把图层拖到另一个列表）尚未实现；右键菜单打开时输入框的选区不显示（失去焦点时不画选区）；工具栏溢出折叠、属性面板的"多种"值已在 M9 完成。

### M7 · 嵌入 MiniCAD 验证（原 M6）　✅ 已完成（2026-10-01）
- **集成方式**：MiniCAD 的 `apps/Win32/CMakeLists.txt` 用 `add_subdirectory` 引入 MiniGUI 源码（`MINIGUI_DIR`，默认 `../MiniGUI`，关闭示例和测试），链接 `MiniGUI_Win32` + `MiniGUI_D3D11`。第 9 节"静态库还是子模块"的问题暂按此方式处理。
- **状态栏**（MiniCAD `src/UI/MiniGUI/{MiniGUILayer, StatusBarView}`，替换原 ImGui 版 `Widgets/StatusBar.cpp`）：工具 | 坐标 | 捕捉(F3) 正交(F8) 悬停 | 文档名 ● 未保存 …… 共 N 个文档；开关按钮不可聚焦（点击后功能键和命令仍交给绘图区）；右键"捕捉"弹出对象捕捉设置（复选框 + 全选/清除 + 捕捉孔径滑块）。窗口变窄时只压缩文档名（省略号），其余项保持自然宽度。
- **与 ImGui 共用窗口的输入分配**（`MiniGUILayer::PreTranslateMessage`，在 ImGui 之前调用）：
  - 新接口 `UIContext::IsPointerOverUI`（指针在可命中的节点上、正在捕获、或有弹层打开）与 `WantsKeyboard`（有焦点或有弹层）。透明容器设 `SetHitTestVisible(false)`，否则整块区域都算 MiniGUI 的。
  - 鼠标按下时决定归属，拖动期间不变；移动总是交给 MiniGUI 维护悬停，只有不在 MiniGUI 上时才同时交给 ImGui；指针移到 MiniGUI 上时给 ImGui 补一个 `WM_MOUSELEAVE` 清除悬停。点到 ImGui 区域时 MiniGUI 交出焦点。
  - `Win32Input::SetManageIme(false)`：与 ImGui 共存时不按焦点开关整个窗口的输入法，也不接管组合窗口，命令行的中文输入仍由 ImGui 处理。
  - MiniGUI 在 ImGui 之后绘制，弹层盖在 ImGui 之上。
- **`ViewportHost`**（`src/Widgets/ViewportHost.*`）：布局给出矩形；宿主按 `GetPixelSize()` 的物理像素渲染，把 SRV 用 `RegisterExternalTexture` 登记后 `SetTexture`，纹理与视口像素一一对应；指针事件转发时带逻辑坐标和物理像素坐标，按下自动捕获、拖出视口继续收到；消费全部指针事件（滚轮不交给外层、右键不弹 MiniGUI 菜单）；点击获得焦点后转发按键和文字，宿主不处理的按键（如 Tab）继续冒泡。`RenderContent()` 只在请求渲染或尺寸变化时回调宿主。新增 `CursorShape::Hidden`（视口自己画十字光标）。
- **帧顺序**（解决 MiniCAD 现有的晚一帧问题）：输入到达时宿主立即交给 Editor 并 `RequestRender` → 消息队列清空后 `WM_PAINT`：`UIContext::Update()` 布局 → `ViewportHost::RenderContent()` 按最新尺寸和相机渲染 CAD → `UIContext::Render()` → Present。不再需要 `kSettleFrames`、挂起尺寸变化、切换文档后补渲染。
- **示例** `MiniCADViewportDemo`（放在 MiniCAD 仓库 `apps/Win32/src/ViewportDemo/`，与 `MiniCADWin` 输出到同一目录共用资产，不含 ImGui）：工具栏（直线/圆/矩形/圆弧/多段线）+ 说明侧栏 + 视口 + 状态栏（坐标、界面帧数、视口帧数、输入→呈现耗时）。MiniGUI 的按键/鼠标掩码在示例中换算成 MiniCAD 的 `InputEvent`。
- 测试：单元测试 118 个（新增 10 个：`IsPointerOverUI` 透明容器/捕获/弹层/悬浮提示、`WantsKeyboard` 随焦点变化，`ViewportHost` 坐标换算（150% 缩放）、拖出捕获、右键不弹菜单、按键转发、按需渲染）；截图测试 27 个场景不变，Debug / Release 全部通过。
- 示例自测 `MiniCADViewportDemo --selftest`（Debug / Release 均通过）：向窗口注入真实的鼠标消息，验证十字光标、中键平移（位移等于鼠标位移）、拖到侧栏继续平移、以光标为中心的滚轮缩放都在**注入后的第一帧**生效，空闲时视口不重新渲染。
- 实测：MiniCAD Release 与示例空闲 3 秒 CPU 时间不增加。
- 本轮顺带：`src/Core/Math/` 改名为 `src/Core/Types/`（Vec2 / Rect / Color 是基础类型，避免与 MiniCAD 的 `Core/Math/` 同名冲突）。
- 实机确认：MiniCAD 命令行（ImGui）用微软拼音输入中文正常（用户 2026-10-01 验证）。
- 已知不足：MiniCAD 的视口仍通过 ImGui 采样输入（下一帧消费），换成 `ViewportHost` 要等主界面迁移到 MiniGUI（M10）；示例没有命令行，字母键只作为 Editor 的快捷键（如 L 直线）。

### M8 · 样式与主题（原 M7）　✅ 已完成（2026-10-01）
- **核心思路：控件保存颜色引用，绘制时才解析**。新模块 `src/Style/`：
  - `ColorRef`（`Style/ColorRef.hpp`）：固定颜色，或者主题槽位 × 透明度系数；`Color32` 和 `ThemeColor` 都能隐式转换成它。`ColorScaleAlpha` 对主题颜色记录系数，解析时再乘。
  - `ThemeColor`：30 个语义槽位（底色、面板、控件表面及悬停/按下、边框三级、强调色及悬停/按下、强调色上的文字、选中/悬停行、网格线、文字四级、危险色、弹层边框、悬浮提示、滚动条、模态遮罩、阴影、反差描边 `Outline`）。
  - `ThemeColors`（`Style/ThemeColors.*`）：一套颜色表，预设 `Dark()`（默认，数值与之前写死的配色完全相同）和 `Light()`；可以在预设基础上 `Set` 个别颜色；槽位名称可以互相转换（`SlotName` / `FindSlot`），为 M9 的主题文件准备。
  - `Style/Theme.hpp`（由 `Widgets/Theme.hpp` 迁来，核心层的弹层和悬浮提示也能用）：`Theme::Text` 等简写从固定颜色改为主题引用，原有调用基本不用改；尺寸（字号、控件高度、行高、圆角）暂不随主题切换。
- `DrawList` 的所有颜色参数、`TextParams::color` 改为 `ColorRef`，按 `DrawListSharedData::palette`（当前主题）解析；没有设置主题时按深色解析。
- `UIContext::SetTheme / GetTheme / ResolveColor`：切换后对整棵树调用 `Node::OnThemeChanged`（给缓存了颜色相关资源的控件用，普通控件不需要）并请求重绘；**不重建节点、不重新布局**。
- 写死的颜色全部改为主题引用：弹层、悬浮提示、模态遮罩、阴影、滚动条、输入框、数值框、特性面板网格线、复选框勾选标记、颜色面板描边等。CAD 领域颜色（灯泡黄、雪花蓝、锁橙）保持固定。
- **样式与状态变体**：`ButtonStyle`（常态/悬停/按下/选中/禁用/边框/焦点框/**文字**）字段改为 `ColorRef`，新增预设 `ButtonStyle::Primary()`（强调色底 + `TextOnAccent` 文字，对话框默认按钮使用）和 `ButtonStyle::Flat()`（透明无边框，工具栏图标按钮）；`SetStyle` 会同步按钮文字颜色。`PopupStyle`、`TextBoxStyle`、`Label`、`Panel`、`ImageView` 的着色、`Separator` 都接收 `ColorRef`。
- 新增 `DrawColorSwatch`（`Widgets/CadPreview.h`）：图层颜色色块 + 随主题的淡描边，白色图层在浅色底上也能看清；列表行首色块和示例图层表都用它。
- Gallery：视图 > 主题（深色/浅色，勾选当前主题）、`Ctrl+T` 一键切换；根节点自己铺主题底色（宿主清屏的颜色不随主题变化）。
- 测试：单元测试 123 个（新增 5 个：`ColorRef` 解析与透明度、槽位名称往返、切换主题后顶点颜色更新且不重建、自定义颜色覆盖、按钮样式预设与文字颜色）；截图测试 35 个场景（新增 8 个浅色场景：基础控件、列表与树、图层与特性、菜单 + 主题子菜单、模态对话框、多文档、命令补全，以及**先按深色构建再切换**的 `gallery_basic_switched`，其基准图与 `gallery_basic_light` 逐字节相同）。D3D11 与软件光栅最大差值仍为 2，Debug / Release 全部通过。
- 重构的回归验证：在修改示例之前，27 个深色场景中 26 个与旧基准图逐像素相同，唯一差异是对话框默认按钮文字从 `Text` 改为 `TextOnAccent`（预期）。之后因为示例本身变化（状态栏提示多了 Ctrl+T、视图菜单多了"主题"、色块加了描边）更新了基准图。
- MiniCAD 不需要改代码即可编译（状态栏仍是深色，与 ImGui 一致）；MiniCAD 主界面换到 MiniGUI 之前不提供浅色切换，否则 ImGui 与状态栏配色不一致。
- 已知不足：尺寸类主题变量（紧凑/宽松）未做；没有通用的样式表或选择器，样式仍由各控件的 Style 结构体 + 预设描述；主题文件（JSON）放到 M9；MiniCAD 的工具图标为深色界面设计，浅色下可用但对比度一般。

### M9 · 数据驱动　✅ 已完成（2026-10-01）
- **JSON**（`src/Data/Json.*`，零依赖）：对象保留书写顺序；支持 `//`、`/* */` 注释和末尾多余的逗号（手写界面文件更方便）；`\u` 转义与代理对；错误给出"第 N 行第 M 列"；`Dump` 序列化；`ReadTextFile` 按 UTF-8 路径读文件。
- **命令注册表**（`src/Data/CommandRegistry.*`）：`Command { id, label（含助记符）, tooltip, icon, shortcut, allowInTextInput, execute, canExecute, isChecked }`。
  - `Execute` 先检查可用，执行后自动 `NotifyStateChanged`；宿主数据变化（选择集、当前工具…）时也调用它，订阅者（工具栏、数据绑定）据此刷新。
  - 快捷键文本 `ParseKeyChord / FormatKeyChord`（"ctrl+shift+s" → "Ctrl+Shift+S"，F1～F12、Del、PgUp…）；`BindShortcuts` 注册到 `ShortcutTable`，快捷键变化后自动重新绑定；`FindShortcutConflicts` 报告冲突。
  - `ApplyOverrides`：界面描述文件的 `commands` 覆盖名称、快捷键、图标、提示。
  - 生命周期：注册表和 `ShortcutTable` 都提供生命周期令牌，宿主按任意顺序销毁 UIContext 和注册表都安全（示例在 Debug 下发现过退出时崩溃，由此修复）。
- **工具栏**（`Widgets/CommandUI.*`）：`ToolBar` 横向/竖向，按钮的图标、提示（"名称 (快捷键)" + 说明）、可用、选中都取自命令并自动刷新；按钮不参与焦点；**空间不够时末尾的按钮折叠到 » 菜单**（紧挨折叠处的分隔线一起折叠）；测量和判断都按物理像素取整，分数缩放下不会误折叠。`CommandMenuItem` 由命令生成菜单项（打开菜单时求值）。
- **界面描述**（`Widgets/UiLayout.*`）：JSON 的 `commands` / `menus` / `toolbars` / `layout` 四部分；布局节点 row、column、menubar、toolbar、panel（宿主提供的命名面板）、splitter、separator、spacer、label，通用属性 width/height/min/max/grow/shrink/gap/padding/align/justify/background/visible。
  - 宿主面板在重建时被回收再放回，**不会销毁**（视口、特性面板保持状态），布局属性恢复为注册时的值；描述里删掉的命令覆盖恢复为代码里的默认值。
  - 旧界面推迟销毁，在菜单或快捷键的回调里重新加载也安全；`MenuBar` 析构时从 UIContext 注销键盘导航。
  - 错误不中断：未知的命令、面板、类型、属性和快捷键冲突记入警告，界面照常生成；JSON 语法错误时返回 false，**保留当前界面**。
- **数据绑定**（`Widgets/Binding.*`）：`BindingSet` 把 CheckBox / NumberBox / TextBox / ComboBox / Label 绑定到 getter / setter；getter 返回 `std::nullopt` 表示**"多种"**（复选框显示不确定，数值框/输入框清空并显示 *多种*，下拉框不选中并显示 *多种*）；setter 为空即只读；正在编辑的输入框不被 Refresh 覆盖；写回后回调宿主并重新读取。`NumberBox::SetMixed`："多种"状态下直接回车不改模型，输入数字后对所有对象生效。绑定集合先销毁时控件回调自动失效。
- **验收示例** `MiniCADViewportDemo`（MiniCAD 仓库）：
  - MiniCAD 的操作注册为 35 个命令（文件、编辑、绘图、修改、视图开关、捕捉/正交/悬停、主题、重新加载、关于）；绘图/修改工具进行中时按钮显示选中，撤销/重做、剪切/复制按文档状态启用。依赖 ImGui 弹窗的文字、多行文字、阵列暂不注册。
  - 菜单栏、三个工具栏、面板布局全部来自 `assets/ui/minicad_ui.json`；开发时直接读源码目录里的文件（`--ui` 可指定其他文件）。**修改并保存后自动重新加载**（`FindFirstChangeNotification` + `MsgWaitForMultipleObjectsEx`，空闲仍为零 CPU），F5 手动重新加载；状态栏显示加载结果、警告或错误位置。
  - 实测：运行中修改 JSON，去掉一个工具栏、把特性面板移到左侧、移除说明面板，界面在 2 秒内更新，视口保持原状态；写入语法错误时界面不变，状态栏显示"第 13 行第 13 列：键之后缺少 ':'"。
  - 特性面板（数据绑定）：当前图层/线型/线宽（写回 MiniCAD 场景）、对象捕捉/正交/悬停/捕捉孔径（与工具栏、F3/F8 是同一份数据）、选择集数量与公共图层/线宽（取值不同时显示 *多种*）。
  - 视口自测仍然通过（Debug / Release）。顺带修复：`ViewportHost` 的物理像素坐标吸附到 1/256 像素，视口原点不在整数逻辑坐标时不再差 1 像素。
- 测试：单元测试 139 个（新增 16 个：JSON 解析与错误位置、快捷键文本、命令执行/状态/通知、快捷键绑定与覆盖与冲突、命令菜单项、工具栏状态刷新与折叠、分数缩放不误折叠、界面描述生成/重建/保留面板/恢复默认/语法错误、分隔条、数据绑定与"多种"、绑定集合先销毁、注册表比 UIContext 活得久、视口像素坐标无浮点误差）；截图测试 38 个场景（新增 3 个：JSON 生成的菜单栏 + 横向/竖向工具栏 100%/150%、» 折叠菜单）。Debug / Release 全部通过。
- 已知不足：界面描述还不能描述特性面板本身（字段仍在代码里绑定）；工具栏不能拖动停靠（M10）；MiniCAD 主程序（ImGui 版）的菜单和工具栏要等 M10 迁移后才由 JSON 驱动；主题颜色还不能写在界面描述文件里。

### M10 · 停靠布局 + 全面替换 ImGui　✅ 已完成（2026-10-01）
- 可拖拽、拆分的停靠布局，以及标签页式的多文档视口。
- 按面板逐个迁移 MiniCAD：状态栏 → 工具栏 → 菜单栏 → 属性面板 → 命令行 → 动态输入框 → 各类弹窗 → 文档标签页。
- **验收**：MiniCAD 移除 ImGui 依赖，功能和体验不低于迁移前。
- 分阶段（2026-10-01 确定；迁移期间旧的 ImGui 版 `MiniCADWin` 保持可用，新界面在单独的程序里开发，M10.5 再替换）：
  - **M10.1 停靠布局**：`DockSpace`（标签组停靠、拖动标签到上下左右或合并为标签、分隔条、关闭与重新显示、布局保存为 JSON / 从 JSON 恢复），`UiLayout` 支持 `"dock"` 节点。
    - ✅ 已完成（2026-10-01）。`Widgets/DockSpace.*`：布局树 = 分割节点（row / column，子节点固定尺寸或占剩余空间）+ 标签组；每个标签组是一个 `DockGroup` 节点（标题栏标签 + 当前面板），相邻节点之间是 `DockSplitter`。树变化时重建这些节点，**面板内容节点原样搬移不重建**；旧节点推迟销毁，在标签自己的回调里关闭或移动面板也安全。
    - 拖动标签：落点按光标位置判断——标题栏合并为标签（按位置插入，同组内即重排）、内容区靠近哪条边（30%）就拆到哪一侧、中间合并为标签、贴近停靠区边缘停靠到整体一侧；标题栏优先于边缘判断。拖动时半透明预览落点 + 光标旁显示面板名称，Esc 取消（拖动期间临时注册 Esc 快捷键，不抢键盘焦点）。拖回原处没有落点。
    - 分隔条拖动调整尺寸（两侧不小于 `kMinPanel`）；窗口变窄时固定尺寸按比例缩小。面板可设为无标题栏、不可关闭（文档区），这样的组不接受合并标签。
    - 关闭面板时记住旁边的面板和方位，`ShowPanel` 回到原处；`TogglePanel` 供"视图 > 面板"菜单使用。`SaveLayout / LoadLayout` 使用与界面描述文件相同的 JSON 格式；未知面板、重复引用记入警告。
    - `UiLayout` 的 `"dock"` 节点：`root` 为布局树，`panels` 列出布局里没有但可随时显示的面板，`GetDock(id)` 取得停靠区；重新加载界面描述时从停靠区取回全部宿主面板（包括隐藏的）。`RegisterPanel` 增加标签标题。
    - MiniCAD 示例的布局改为停靠区（说明 | 视口 | 特性），"视图 > 面板"可显示/隐藏；实测在程序里把"特性"拖到视口左侧，松开后正确停靠、视口按新尺寸重新渲染。
    - 测试：单元测试 148 个（新增 9 个：布局尺寸与无标题栏组、保存/加载往返、停靠/关闭/重新显示回到原处、拖动到组的一侧、拖到整体边缘/合并标签/Esc 取消/拖回原处、点击标签与关闭按钮、分隔条与最小尺寸、标签重排、界面描述中的停靠区跨重新加载保留面板）；截图测试 42 个场景（新增 4 个：停靠布局 100%/150%、拖动预览深色/浅色）。
    - 已知不足：面板不能浮动成独立窗口；用户拖动后的布局还没有自动保存到配置文件（`SetOnLayoutChanged` + `SaveLayout` 已具备，M10.2 接入）。
  - **M10.2 新主窗口**：在 `MiniCADViewportDemo` 基础上做 MiniGUI 版主窗口：无边框标题栏（菜单栏在标题栏内 + 最小化/最大化/关闭）、多文档标签（每个文档一个视口）、工具栏里的图层/线型/线宽下拉框、状态栏、停靠的特性与图层面板。
    - ✅ 已完成（2026-10-01）。MiniGUI 新增：
      - `Widgets/TitleBar.*`：自绘标题栏 = 图标 + 内容区（放菜单栏）+ 居中标题 + 最小化 / 最大化（还原）/ 关闭。按钮执行命令 `window.minimize / window.maximize / window.close`（命令不存在则不显示；`window.maximize` 的 isChecked 表示已最大化，显示还原图标），关闭按钮悬停红底白叉；标题优先在整条标题栏居中，与菜单或按钮重叠时改在中间空白处居中，再放不下截断；窗口未激活时变暗。`IsCaptionAt`：标题栏本身和 Label / Panel / Separator 等静态子节点算标题区域，按钮、菜单栏不算；有弹层打开或正在捕获时整条都不算（点击要用来关闭菜单）。
      - `Platform/Win32/Win32Frame.*`：无边框窗口（`WS_OVERLAPPEDWINDOW` + `WM_NCCALCSIZE` 返回 0 + DWM 扩展 1 像素边框保留阴影），保留贴靠、Win11 圆角、最大化 / 最小化动画、双击标题最大化、右键系统菜单；`WM_NCHITTEST` 处理四边四角缩放（按窗口 DPI）和标题区域（回调给 `TitleBar::IsCaptionAt`）；最大化时收回伸出屏幕的边框；`WM_NCACTIVATE` 不重画系统标题栏。构造后由宿主保存好再调用 `Apply`（它立即触发 `WM_NCCALCSIZE`，必须能交给 `HandleMessage`——实测发现过构造函数里直接刷新边框时系统标题栏没有去掉）。成员函数不叫 `IsMaximized`（windowsx.h 同名宏）。
      - `UiLayout` 的 `"titlebar"` 节点（`children` 放在图标右侧，`icon`、`title`），`GetTitleBar()`。
      - `TabView` 的内容节点可以为空（只用标签条，多个文档共用一个视口），`SetTabData / GetTabData / FindTabData` 让宿主数据随标签移动。
      - `ShowColorPalette`：调色板弹层独立成函数（`ColorButton` 改用它），表格里的颜色单元格也能弹出。
    - MiniCAD（MiniCADLib 仓库）：`MiniCADViewportDemo` 改为 **`MiniCADGUI`**（`apps/Win32/src/GUI/`，类 `MainFrame`），迁移期间与 ImGui 版 `MiniCADWin` 并存：
      - 标题栏里的菜单栏（文件 / 编辑 / 视图 / 格式 / 绘图 / 修改 / 窗口 / 帮助），标题为"文档名 * - MiniCAD"（同时设为窗口标题，任务栏可见）。
      - **多文档标签**：MiniCAD 的 `DocumentManager` 只有一个 `Viewport + Editor`，切换文档时换相机状态，所以所有文档共用一个 `ViewportHost`，标签条只负责切换（实测切回时相机恢复）。"+" 新建、× / Ctrl+W 关闭、Ctrl+Tab / Ctrl+Shift+Tab 切换、拖动排序、右键加"保存 / 另存为"；关闭当前文档时切到标签上相邻的文档；未保存时询问"保存 / 不保存 / 取消"，保存时取消另存为则不关闭；退出时有未保存的文档同样询问。没有文档时显示提示和"新建 / 打开"按钮。
      - 工具栏右侧的图层 / 线型 / 线宽下拉框（宿主面板 `layerbar`，数据绑定）：图层行显示开 / 锁定图标 + 颜色色块，线型、线宽带预览。
      - 停靠区：文档区 | 右侧上下排列"特性"与"图层"。**图层面板**（`ListView` 多列）：当前标记、名称（F2 / 慢速点击改名，0 层和重名拒绝）、开、锁、颜色（点击弹出调色板）、线型、线宽（点击弹出菜单），双击置为当前，右键新建 / 置为当前 / 重命名 / 删除（对象移到 0 层）。
      - 状态栏复用 `StatusBarView`（新增不依赖 ImGui 输入结构的 `Refresh` 重载与开关回调）+ 界面描述文件的加载结果。
      - **用户布局**：停靠区变化时保存到 `%LOCALAPPDATA%\MiniCAD\layout.json`，启动时恢复（文件损坏或缺少文档区时回到默认），"视图 > 面板 > 重置面板布局"删除它；之后修改界面描述文件里的 dock，重新加载时立即生效。实测：关闭图层面板后重启，布局保持。
      - 命令执行后、宿主数据变化时统一通过 `NotifyStateChanged` 刷新：文档标签、图层面板、标题、数据绑定，并重绘视口（撤销、粘贴、图层可见性等改了图形）。
    - 测试：单元测试 155 个（新增 5 个：标题栏命中测试与菜单打开时、按钮执行窗口命令与拖出不执行、命令缺失时隐藏按钮、只有标签条的 TabView 与标签数据、界面描述中的标题栏 + 菜单栏）；截图测试 45 个场景（新增 3 个：标题栏 + 多文档标签条，悬停关闭 / 150% 最大化 / 浅色）。Debug / Release 全部通过。
    - `MiniCADGUI --selftest`（Debug / Release 均通过）：原有视口输入 6 项，加上标题栏命中（标题处 HTCAPTION、菜单和关闭按钮 HTCLIENT、上边缘 HTTOP）、最大化后客户区正好铺满工作区且按钮为还原、新建 / 点击标签切换（相机恢复）/ Ctrl+Tab / 关闭时询问与取消 / 关闭后回到相邻文档、新建图层后面板与当前图层同步。
    - 已知不足：Win11 悬停最大化按钮弹出的贴靠布局不支持（按钮由界面绘制）；图层下拉框里不能直接点灯泡 / 锁切换（在图层面板里切换），MiniCAD 的图层没有冻结属性；文字、多行文字、阵列、块等依赖 ImGui 弹窗的命令还没注册（M10.4），命令行在 M10.3；窗口位置和大小不保存；"关闭其他"遇到多个未保存文档时连续弹出多个询问框。
  - **M10.3 命令行**：回显历史、输入补全、↑↓ 历史、空格等于回车、Esc 清空、绘图区直接打字进入命令行、当前工具提示。
    - ✅ 已完成（2026-10-01）。MiniGUI 新增 `Widgets/CommandConsole.*`（与 CAD 无关的通用命令行）：上方回显（`ListView`，只绘制可见行，内容变化时滚到最后一行，不抢焦点），下方"提示 + 输入框"。
      - 补全：`SetCompletion` 提供候选，默认选中第一项（`AutoComplete::SetSelectFirst`），Enter / 空格 / Tab / 点击直接执行；候选放不下时翻到输入框上方。
      - 空格等于回车（输入法确认的整段文字里的空格照常输入）；没有输入时提交空串，宿主据此重复上一条命令。
      - ↑↓：有候选时选候选，没有候选时翻历史（不区分大小写去重，只保留最近一次）。Esc：先关候选，再清空输入并回调 `OnEscape`。
    - MiniCAD（`MiniCADGUI`）：命令行是停靠面板（默认在文档区下方，"视图 > 面板 > 命令行"、Ctrl+9 显示 / 隐藏）。
      - 回显来自 `Editor::GetCmdLine().Lines()`；提示是当前工具的提示（Editor 在渲染时更新，所以在视口渲染之后同步，有变化时补一次布局），没有工具时为"命令:"。
      - 候选：工具全名前缀匹配，输入恰好是别名时把它指向的命令排第一（L → Line），右侧显示中文名称（直线、圆…）。
      - **绘图区直接打字**：没有进行中的工具时，在绘图区按字母只把焦点交给命令行，随后的 `WM_CHAR` 自然落入输入框（大小写、输入法都由输入框处理）；工具进行中时字母仍交给工具；命令行面板被关掉时交给 Editor 自己的命令缓存。执行后焦点回到绘图区；命令行里 Esc 等于在绘图区按 Esc（取消工具），焦点回到绘图区。
      - 依赖 ImGui 弹窗的 Text / MText / Insert / Array 不出现在候选里，输入时回显"对话框尚未迁移（M10.4）"。
      - `Editor::GetLastCommand()`（MiniCADLib 核心库新增）：工具栏按钮的选中状态和状态栏的工具名改按 Editor 当前工具判断，无论从工具栏、菜单还是命令行启动都一致（去掉了宿主自己记录的"最近启动的工具"）。
      - 用户布局文件记录保存时已有的面板；界面描述新增了面板（例如这次的命令行）时改用默认布局，免得新面板被旧布局藏起来。
    - 顺带：删掉 M10.1 遗留在 `DockSpace.cpp` 里的调试输出（拖动标签时往 stderr 打印）。
    - 测试：单元测试 160 个（新增 5 个：默认选中第一项与回车执行、空格提交与空串重复、历史去重与上下翻、Esc 先关候选再清空、回显滚到最后一行）；截图测试 47 个场景（新增 2 个：命令行回显 + 工具提示 + 候选，深色 100% / 浅色 150%）。Debug / Release 全部通过。
    - `MiniCADGUI --selftest` 新增 9 项（Debug / Release 均通过）：绘图区按 L 后焦点和字母进入命令行、Line 排第一并选中、空格执行、回显同步、执行后焦点回到绘图区且工具栏按钮选中、提示为工具提示、Esc 后提示恢复、空回车重复、命令行里 Esc 取消工具，以及对话框命令的提示。
    - 已知不足：工具进行中在命令行里输入坐标（如 `100,50`）、距离仍按命令名处理（AutoCAD 会交给当前工具），放到 M10.4 的动态输入一起做；回显不能选中复制。
  - **M10.4 动态输入与弹窗**：光标旁的长度/角度输入、文字/多行文字输入、块定义/插入、阵列对话框、图层管理器（可停靠面板）、关于。
    - ✅ 已完成（2026-10-01）。全部在 MiniCAD 侧用现有的 MiniGUI 控件实现，MiniGUI 库本身没有改动。界面通过 Editor 的"请求"驱动：命令状态变化时 `SyncEditorRequests` 检查 `GetTextInputRequest / GetMTextInputRequest / GetBlockNameRequest / GetBlockInsertRequest / GetArrayRequest`，打开或关闭对应的界面（请求被撤销时界面跟着关闭）。
      - **动态输入**（`GUI/DynamicInput.cpp`）：工具有锚点时，光标右下角显示"长度 / 角度"输入框和正交 / 极轴 / 捕捉信息（文档区里的绝对定位节点，背景不挡指针，放不下时翻到光标另一侧）。焦点平时留在绘图区；工具进行中按数字键时，焦点交给长度框，随后的字符自然落入（与命令行相同的思路）。未键入时实时显示当前值，第一个字符替换它；Tab 切换；Enter / 空格提交（只填长度沿光标方向、只填角度取当前长度、都填按极坐标；含 `, < @` 时按坐标，默认相对上一点，`#` 开头为绝对坐标）；字母交给工具作为选项键；Esc 先清空键入，再取消工具；什么都没键入时回车交给工具。
      - **命令行坐标输入**（解决 M10.3 遗留）：工具进行中输入 `x,y`、`@dx,dy`、`@距离<角度`、单个数字（直接距离）交给工具；单个字母是工具的选项键；空回车交给工具（例如结束多段线），没有工具时仍是重复上一条命令；没有锚点时（例如第一点）在绘图区按数字直接进入命令行。
      - **文字 / 多行文字原位编辑**（`GUI/EditorDialogs.cpp`）：编辑框（无边框弹层）放在插入点的屏幕位置，字号跟随视口缩放（11～72 逻辑像素），单行宽度随内容增长；Enter（多行为 Ctrl+Enter）或点击框外提交，Esc 取消；编辑已有文字时带出原文。中文输入法候选窗跟随光标。
      - **定义块**：块名对话框（默认唯一块名，空名 / 重名时提示并禁用"确定"）。**插入块**：块表列表（名称 + 对象数，双击直接插入），确定后进入放置工具。**阵列**：矩形（行、列、行距、列距、角度）/ 环形（中心点 + "拾取 <"、项目总数、填充角度、旋转项目），参数变化时实时幽灵预览并显示项目总数；拾取中心时对话框暂时关闭，拾取完成或取消后重新打开（参数保存在请求里）。**关于**：独立对话框。图层管理器即 M10.2 的停靠图层面板。
      - 新命令：单行文字、多行文字（工具栏也有）、创建块、插入块、阵列；菜单"绘图 > 文字 / 块"、"修改 > 阵列"。命令行不再拦截这些命令。
    - MiniCADLib 核心库新增 `Editor::SubmitPoint`（把精确点交给当前工具，不经捕捉 / 约束）和 `Editor::SubmitCoordinateText`（解析上述坐标格式，接受全角逗号 / 小于号 / @，成功时回显"提示 输入"）；`ApplyDynamicInput` 改为调用 `SubmitPoint`。ImGui 版 `MiniCADWin` 照常编译。
    - `MiniCADGUI --selftest` 共 55 项（Debug / Release 均通过），新增：命令行 `0,0 → @10,0 → @5<90` 画两段线且锚点正确、动态输入出现时焦点仍在绘图区、按数字进入长度框、回车按长度 7 定点、提交后焦点回到绘图区、Esc 结束工具并隐藏动态输入、单行文字原位编辑提交、多行文字 Esc 取消、点选对象后定义块（默认块名）、插入块进入放置工具、阵列按默认 3×4 生成 11 个副本、关于对话框。自测输出改为不缓冲，中途崩溃也能看到执行到哪一项。
    - 实机截图确认：画直线时动态输入框跟随光标，键入 25 回车后从新点继续；单行文字编辑框位于插入点，输入法候选窗跟随。
    - 已知不足：从绘图区开始输入时只有数字键会转交（`.`、`-` 开头的数要先按数字或在命令行里输入）；动态输入不支持第一点的绝对坐标提示（第一点请在命令行输入）；原位编辑期间缩放视口，编辑框不跟着移动。
  - **M10.5 移除 ImGui**：删除 ImGui 界面代码与库，核心库去掉 `imgui.h` 与 ImGui 自带的 stb_truetype；新主窗口改名为 `MiniCADWin`；逐项核对功能清单。
    - ✅ 已完成（2026-10-01）。MiniCADLib 仓库：
      - 删除 ImGui 界面：`MainWindow`、`Main.cpp`、`UI/`（`UIManager`、`ImGuiLayer`、`MiniGUILayer` 与 `Widgets/` 下全部 ImGui 控件）、`Input/`（Win32 消息责任链、ImGui 输入采样），以及只给它们用的核心库头文件 `Editor/Input/ViewportInput.h`、`ViewportInputAdapter.h`、`InputSystem.h`、`KeyCodeUtils.h`、`IInputHandler.h`。删除 `3rd/imgui`。
      - 核心库：`Editor.cpp` 去掉多余的 `#include <imgui.h>`；`TTFFont.cpp` 改用 `3rd/stb/stb_truetype.h`（从 MiniGUI 复制，注意它与 ImGui 带的是同一份 1.26 改版，功能与官方 1.26 相同）；注释里的 ImGui 字样改为实际含义（纹理字形只有 Web 端使用）。
      - `MiniCADGUI` 改名为 **`MiniCADWin`**（唯一的桌面程序）：带上 `MiniCAD.rc`、`USE_WIN32`（无控制台）选项、资产复制；`StatusBarView` 移到 `GUI/` 并去掉 ImGui 时代的 `Tool` / `ViewportInput` 接口；线宽表从 ImGui 的 `WidgetCommon.h` 移到 `GUI/Lineweights.h`。CMake 不再构建 ImGui 库。
      - 核对中发现并补上：F10 极轴追踪（旧版由 Editor 处理；MiniGUI 里 F10 会激活菜单栏），改为命令（输入框有焦点时也生效），加到"视图 > 绘图辅助"。
      - 验证：Debug / Release 编译无警告（除 pch 的宏重定义），`MiniCADWin.exe` 里没有任何 ImGui 字样（Release 2.2 MB）；`MiniCADWin --selftest` 56 项全部通过；实机启动截图正常。WASM 版没有改动构建脚本（它本来就不用 ImGui），核心库的改动在 `MINICAD_WEB` 下不参与编译的部分，但本机没有 Emscripten 环境，未实际构建。
    - **功能核对（旧 ImGui 版 → MiniCADWin）**：

      | 旧版功能 | MiniCADWin | 说明 |
      |---|---|---|
      | 无边框窗口：拖动、缩放、双击最大化、最小化 / 最大化 / 关闭、Logo、居中文档名 | ✅ | 另有贴靠、阴影、Win11 圆角、窗口未激活时标题变暗 |
      | 菜单：文件 / 编辑 / 修改 / 绘图 / 视图 / 帮助 | ✅ | 菜单来自界面描述文件；另有格式、窗口菜单，关闭文档、文字、块、阵列等 |
      | 快捷键 Ctrl+N/O/S、Ctrl+Shift+S、Ctrl+Alt+S、Ctrl+C/X/V、Ctrl+Shift+C、Ctrl+Z/Y、Delete、F3/F8/F10 | ✅ | 全部是命令，可在界面描述文件里改；F10 见上 |
      | 工具栏 15 个按钮（绘图、文字、修改、撤销重做），当前工具高亮 | ✅ | 无论从哪里启动工具都高亮；空间不够时折叠到 » |
      | 工具栏图层 / 线型 / 线宽下拉框（带预览） | ✅ | 差异：旧版可在下拉列表里直接点灯泡 / 锁 / 颜色，现在在图层面板里操作；"图层管理器…"入口改为"视图 > 面板 > 图层" |
      | 文档标签：切换、关闭、未保存标记 | ✅ | 另有关闭 / 退出时询问保存、拖动排序、右键菜单、"+" 新建；切换时相机恢复 |
      | 视口：平移、缩放、选择、十字光标 | ✅ | 输入当帧生效，不再需要 kSettleFrames、挂起尺寸变化、切换文档后补渲染 |
      | 命令行：回显、补全、↑↓ 历史、空格等于回车、Esc、绘图区直接打字、拖动调整高度 | ✅ | 停靠面板，可移动、关闭（Ctrl+9）；另有坐标输入 |
      | 动态输入（长度 / 角度、捕捉信息、字母转工具） | ✅ | |
      | 文字 / 多行文字原位编辑 | ✅ | |
      | 定义块（块名）、插入块（块表）、阵列（矩形 / 环形、拾取中心、预览） | ✅ | |
      | 图层管理器：新建、置为当前、改名、开 / 锁、颜色、线型、线宽、删除 | ✅ | 可停靠的图层面板；差异：双击行是"置为当前"，改名用 F2 或慢速点击（旧版双击改名） |
      | 状态栏：工具、坐标、捕捉 / 正交 / 悬停开关、对象捕捉设置、文档信息 | ✅ | 同一个 `StatusBarView` |
      | 关于 | ✅ | |
      | ImGui 字体图集作为视口纹理字形 | — | 桌面端没有设置纹理字形回调，这条路径本来就没有内容，去掉无影响 |


### M11 · Vulkan 后端
- 实现 `IRenderBackend` 的 Vulkan 版本：同一套"纹理 × 顶点色"着色器（预编译为 SPIR-V 并嵌入代码），支持多帧并行，也支持外部传入的设备和命令缓冲。
- 着色器源码保持 HLSL 一份，用 DXC 同时编译出 DXBC/DXIL 和 SPIR-V，避免维护两份着色器。
- **验收**：Gallery 在 D3D11 和 Vulkan 下画面一致（与软件光栅的基准图片比对通过）；开启 Vulkan 验证层时没有报错。
- 说明：**先把 D3D11 路线全部完成，再做 Vulkan 等其他后端**（2026-09-30 确定）。所有绘制都必须经过 `IRenderBackend`，核心层不直接调用任何图形 API，这样之后加后端时不用返工。

### 后续
- Emscripten 平台层 + WebGL2 后端（MiniCAD 的 WASM 版）。
- FreeType / HarfBuzz、样式文件、简单动画。

---

## 6. 测试策略
- **单元测试**：布局计算、事件分发顺序、焦点移动、文本编辑（光标/选区/撤销）、UTF-8 处理。这些都与平台无关，可以直接测。
- **截图对比测试**：软件光栅后端输出 PNG，与基准图片比对；基准图片更新需要人工确认。
- **示例程序**：每个里程碑配一个可运行的示例，人工验证交互手感。
- **MiniCAD 实测**：从 M6 起，每个里程碑都在 MiniCAD 里验证一次。

---

## 7. 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| 输入法和文本编辑比预期难 | M5 拖期 | 尽早做（排在 M5），先把单行输入框做扎实；参考 Chromium、SDL 的输入法处理 |
| 自研布局满足不了复杂界面 | 返工 | 布局放在 `ILayoutEngine` 接口后面，必要时换成 Yoga |
| 和 MiniCAD 集成时才暴露问题 | 返工 | M6 就开始集成验证，不等到最后 |
| 范围蔓延，变成通用框架 | 进度失控 | 以"MiniCAD 需要什么"为准，非目标清单严格执行 |
| 两个后端画面不一致 | 测试失真 | 着色器保持最简单（纹理 × 顶点色），抗锯齿放在几何生成阶段，两个后端逻辑对齐 |

---

## 8. 参考资料
- **绘制指令**：Dear ImGui 的 `ImDrawList` / `ImDrawData` 及各渲染后端实现。
- **架构**：Flutter 的三棵树（Widget / Element / RenderObject）；WPF 的 Measure/Arrange；Chromium 的渲染流水线（样式 → 布局 → 绘制 → 合成）。
- **小型保留模式框架**：GuiLite（`E:\Code\GuiLite`，已研究过）、LVGL。
- **布局**：Yoga、Clay。
- **文章**：Raph Levien 关于 GUI 架构的系列博客（Druid / Xilem）。
- **MiniCAD 现状**：`D:\MiniCADLib\apps\Win32\src\UI`（现有 ImGui 界面，约 3700 行，是迁移的需求清单）、`src/Render/IRenderer.h`。

---

## 9. 待定问题
- [ ] MiniGUI 以静态库形式给 MiniCAD 用。
- [x] 单元测试框架：自己写一个最小的测试框架（M2 已实现，零依赖）。
- [ ] 图标：沿用 MiniCAD 的 PNG 图标（需要图片解码，已有 stb_image），需要支持 矢量图标和图标字体。
- [x] 界面描述文件用 JSON：M9 在 MiniGUI 里自带了零依赖的解析器（`Data/Json`），不依赖 MiniCAD 的 `JsonValue`。
