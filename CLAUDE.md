# CLAUDE.md

本文件为 Claude Code（claude.ai/code）在本仓库中工作时提供指引。

## 项目概览

MiniCAD 是用现代 C++（C++20）编写的跨平台二维 CAD 编辑器。一个仓库里包含三个库和两个应用：
平台无关的核心库 MiniCAD、保留模式界面库 MiniGUI、DWG/DXF 读写库 MiniDWG；
Windows 桌面版（D3D11）与网页版（Emscripten + WebGL2）用同一份代码（`apps/Common`）绘制同一套 MiniGUI 界面。
仓库于 2026-10 由原 MiniCADLib、MiniGUI、MiniDWG 三个仓库合并而成（未保留历史）。当前版本 v0.2.2.0。

### 目录

- `src/`：核心库（目标 `MiniCAD`，定义在 `cmake/MiniCAD.cmake`，命名空间 `MiniCAD`）
- `src/UI/`：MiniGUI（目标 `MiniGUI`，定义在 `cmake/MiniGUI.cmake`，命名空间 `MiniGUI`）。平台层 `Platform/Win32`（`MiniGUI_Win32`）与 `Platform/Web`（`MiniGUI_Web`：DOM 输入、经隐藏 textarea 接入输入法、剪贴板、打包的界面字体）。开发计划：`docs/MiniGUI/PLAN.md`
- `src/Serialization/Codec|Database|Dwg|Dxf/`：MiniDWG（移植自 ACadSharp，MIT；目标 `MiniDWG`，定义在 `cmake/MiniDWG.cmake`，命名空间 `MiniDWG`）。`PRIVATE` 链接进 MiniCAD（网页版也包含），核心库的公开头文件不能暴露 MiniDWG 类型；它不依赖 MiniCAD，`CadDatabase` ↔ `Scene` 的映射放在 `src/Import/`。开发计划：`docs/MiniDWG/PLAN.md`；`*.g.cpp` 由已归档的 MiniDWG 仓库 `tools/` 里的脚本生成，不要手改
- `src/NodeGraph/`：节点式参数化建模（核心库，命名空间 `MiniCAD::Nodes`）：值与类型、节点类型注册表、节点图（依赖调度求值、读写）、撤销快照、预览与烘焙、内置节点（`Builtin*Nodes.cpp`，几何经 MiniCAD 曲线实体与 `ISolidKernel`；网格运算与网格求交在 `MeshOps` / `MeshIntersect.cpp`，不依赖内核，也是内核求交失败时的退路）、示例图（节点图文件 `assets/nodes/examples/*.mcnode`，索引 `examples.json` 定顺序、名称与是否需要内核；`Examples.cpp` 只负责读取，编辑器工具行的“示例”菜单）、组节点（`Group.h`：组库属于顶层的图，组实例类型 `group:<编号>`，每个实例持有自己的组内图）。节点图随文档保存（`Document::GetNodeGraph`，`.mcad` / JSON 的 `nodeGraph`），也可经节点面板的“文件”菜单单独存成 / 打开 `.mcnode`。编辑器是 `apps/Common/src/GUI/NodeEditor`（停靠面板 `nodes`，`NodePanel.cpp` 负责求值、预览、烘焙；桌面版把节点图副本交给单个工作线程求值，按脏戳合并回来，改动正在算的节点会取消重来，见 `docs/NodeGraph/PLAN.md` 第 3.4 节）。开发计划：`docs/NodeGraph/PLAN.md`
- `apps/Common/`：主窗口的界面与逻辑，桌面版和网页版共用（目标 `MiniCADApp`：`MainFrame`、命令、面板、命令行、对话框）。平台服务经 `AppPlatform` 接口（`apps/Common/src/GUI/AppPlatform.h`）提供
- 注意：根目录 `.gitignore` 的 `tools` 规则也会命中 `src/Editor/Tools/`，在那里新增文件要 `git add -f`
- `apps/Win32/`：桌面版宿主 `Win32Window`（无边框窗口、D3D11、消息循环，实现 `AppPlatform`），目标 `MiniCADWin`；自测 `Host/SelfTest.cpp`。`src/Render/` 下是 CAD 渲染器 `D3D11Renderer` 与 MiniGUI 后端（`D3D11/D3D11Backend` → `MiniGUI_D3D11`，`Software/SoftwareBackend` → `MiniGUI_Software`）
- `apps/Web/`：网页版宿主 `WebApp`（经 `WebHost` 管理画布与 WebGL2，浏览器文件选择与下载，实现 `AppPlatform`），目标 `MiniCADWeb`。`src/Render/` 下是 CAD 渲染器 `GLES3Renderer` + `GLRenderTarget` 与 MiniGUI 的 `WebGL2/WebGL2Backend`（`MiniGUI_WebGL2`）。`MiniGUIWebGallery` 是浏览器里的 MiniGUI 控件展示
- `wasm/`：旧版 C 接口 WASM（给仓库外的 HTML/JS 前端用）。只在 `-DMINICAD_BUILD_LEGACY_WASM=ON` 时编译，此时核心库定义 `MINICAD_WEB`（文字输入交给 JS、块名自动命名、文字走纹理图集），并且不编译 `apps/Web`
- `tests/`：`Core/`（MiniCAD）、`UI/` + `UI/Golden/`（MiniGUI）、`Serialization/`（MiniDWG）、`Data/`（DWG/DXF 样例、截图测试用图标）
- `tools/make_web_font.py`：重新生成 `assets/fonts/NotoSansSC-UI.ttf`（网页版界面字体，Noto Sans SC Regular 子集，OFL）
- `assets/`：字体、图标、填充图案、界面描述文件 `ui/minicad_ui.json`、示例节点图 `nodes/examples/`；桌面版构建后复制到可执行文件旁，网页版打包进虚拟文件系统根目录（`/fonts`、`/icons`、`/patterns`、`/ui`）
- `3rd/`：stb_image、stb_truetype（MiniCAD 与 MiniGUI 共用）；`3rd/SMLib`：NURBS BRep 实体建模内核（NVIDIA usd-brep 1.0.1 的 SMLib + SM_API，Apache-2.0），目标 `SMLib` / `SM_API`，定义在 `cmake/SMLib.cmake`，开关 `MINICAD_WITH_SOLID`（默认开；网页版要小体积可 `-DMINICAD_WITH_SOLID=OFF`）；静态库、`SM_STATIC`，本地修改与使用注意（静态库不能并发使用，`SMLibKernel` 内部加锁让多个线程轮流调用；为 C++20 做的修改）见 `3rd/SMLib/README.md`。MiniCAD 只经 `src/Solid/ISolidKernel.h`（核心库里的接口，只用 MiniCAD 类型）使用实体内核，SMLib 实现在 `src/Solid/SMLib/`（目标 `MiniCADSolid`，工厂 `CreateSMLibKernel()`，唯一包含 SMLib 头文件的地方）。开发计划：`docs/Solid/PLAN.md`
- `docs/`：早期 MiniCAD 项目的教程（描述的是旧架构）、各库开发计划、发布说明 `release-notes/`

### include 根目录（重要）

每个库有自己的 include 根：MiniCAD → `src`，MiniGUI → `src/UI`，MiniDWG → `src/Serialization`，共用应用 → `apps/Common/src`，桌面版 → `apps/Win32/src` 与 `apps/Win32/src/Render`，网页版 → `apps/Web/src` 与 `apps/Web/src/Render`。
`src` 和 `src/UI` 下都有 `Core/`、`Render/`、`Text/`；目前没有任何相对路径（如 `Core/Foo.h`）同时存在于两个根下，要保持这样——重名时编译器会按搜索顺序悄悄选中其中一个，不会报错。
命名空间保持 `MiniCAD` / `MiniGUI` / `MiniDWG`。

## 架构

### 核心系统

**实体**（`src/Core/Entity/`）
- 可绘制对象继承 `Entity`（基类 `Object` 负责类型信息与 ID）；30 种左右实体：`LineEntity`、`CircleEntity`、`ArcEntity`、`TextEntity`、`DimensionEntity`、`InsertEntity` 等；三维实体 `Solid3DEntity`（内核数据 + 变换矩阵，编辑只改矩阵；导出 DWG / DXF 时写成多面网格，见 `docs/Solid/PLAN.md`；`SolidEntity` 是二维填充）；多面网格 `MeshEntity` 继承 `Solid3DEntity`（只有局部网格，没有内核数据）
- 支持克隆、包围盒、绘制到 draw sink、序列化；`ICurveEntity` 提供几何操作接口（捕捉、求交、编辑）
- 颜色 `EntityColor` 同时保存 DXF 语义（ByLayer / ByBlock / ACI / 真彩）和解析后的 RGBA

**场景**（`src/Scene/`）
- `Scene`：实体容器，管理图层、线型、块、文字样式、标注样式、当前属性
- `LayerManager`、`BlockTable`（块定义，经 `InsertEntity` 引用）、`LineTypeTable`、`DimStyleTable`、`TextStyleTable`
- 脏标记 + 几何版本号，用于让空间索引失效

**文档与撤销**（`src/Document/`）
- `Document`：包装场景，负责保存 / 打开、撤销 / 重做、名称与路径、DWG/DXF 写出版本
- `CommandStack`：命令模式的撤销栈；所有编辑都必须经 `ICommand` 子类（`src/Document/Command/`）
- `DocumentManager`：多文档、共用一个 `Viewport` + `Editor`（切换文档时换相机状态）、剪贴板

**编辑器**（`src/Editor/`）
- 输入管线：`InputResolver` → `Editor` → 工具 / 约束
- 工具（`src/Editor/Tools/`）：`ITool`；绘图工具 `LineTool`、`PolylineTool`、`CircleTool` 等，修改工具在 `Tools/Modify/`
- 拾取（带空间索引）、`SnapEngine` 捕捉、`GripEditor` 夹点、正交 / 极轴约束
- 需要界面配合的操作通过"请求"交给宿主（`GetTextInputRequest`、`GetArrayRequest`、`GetHatchRequest` ……）

**渲染**（`src/Viewport/` + 平台渲染器）
- `Viewport`：相机、收集顶点、分两层绘制（不常变的场景画进缓存层，每帧只叠加光标、夹点等动态内容）
- 渲染器实现 `IRenderer`：`D3D11Renderer`（桌面）、`GLES3Renderer`（网页）。GL 的纹理 / 帧缓冲名按值放在 `void*` 里传递（见 `GLRenderTarget`）
- 三维视图（M6，见 `docs/Solid/PLAN.md` 第 6 节）：相机有视线方向（`CameraState::Azimuth` / `Elevation`），俯视（`Camera::IsPlanView`）时与二维完全相同；三维视图按场景范围设置深度，着色面写深度、线只测试（渲染目标都带深度缓冲）。着色面与轮廓线由 `Editor` 在 CPU 上随视线方向生成（`ViewState::Shaded` / `Silhouettes`），明暗写在顶点颜色里，不需要新着色器。标准视图命令 `ViewTop` / `ViewSWIso` 等、Shift + 中键环绕、视觉样式 `Shade` / `Wireframe`；绘图区右上角的视图方块 `GUI/ViewCube`（点面 / 棱 / 角或东南西北切换视图、拖动环绕；右键菜单或命令 `NAVVCUBE` 隐藏 / 显示，开关存进 `layout.json`）
- 背景深浅（`IRenderer::SetLightBackground`，7 号色语义）：浅色背景时纯白顶点画成纯黑、深色背景时纯黑画成纯白，在两个渲染器的顶点着色器里处理（光栅图像不变）。`MainFrame::RenderFrame` 每帧按界面主题设置 `Viewport::SetLightBackground`；图层颜色块在 `LayerPanels.cpp` 的 `SwatchColor` 里做同样的反转

**序列化**（`src/Serialization/`）
- `ISerializer` 抽象接口，`JsonSerializer` / `BinarySerializer` 实现，`EntityIO` 负责实体与数据的映射
- 按扩展名选格式：`.mcad`（不区分大小写）→ 二进制，其余（`.json` 等）→ JSON 文本，两者内容等价

**DWG/DXF 交换**（`src/Import/CadExchange.*`）
- `ImportCad` / `ExportCad` 经 MiniDWG 的 `CadDatabase` 与 `Scene` 互转；`Document::LoadFromFile` / `SaveToFile` 按扩展名 `.dwg` / `.dxf` 分派，DXF 写 ASCII
- 写出版本 `CadSaveVersion`：2018（AC1032）、2013（AC1027，AutoCAD 2013～2017 共用，界面上标为"2013/2014"）、2010、2007、2004、2000；新建文档默认 2004（`kDefaultCadSaveVersion`）。2007 只能写 DXF，DWG 2007 改写为 2010（MiniDWG 没有实现 AC1021 的写出）；2007 以前的版本按 `ANSI_936` 代码页写中文。打开文件时记住其版本（`Document::GetCadSaveVersion`），另存为时由用户选择，格式列表 `kSaveFormats`（`AppPlatform.h`）桌面版与网页版共用
- 已映射：图层、线型、文字样式、块、Line / Circle / Arc / Ellipse / Point / Polyline / Text / MText / Insert / Solid / 3DFace；三维实体只导出（多面网格）；多面网格（PolyfaceMesh）与 `MeshEntity` 互转；标注导入时用其匿名块还原为 Insert；其余实体（Hatch、Spline、Leader 等）跳过并 `LOG_WARN`
- 以原图为底另存：打开时 `Document` 留下原图 `CadSource`（原始字节、场景实体 ↔ 原图句柄、导入时的签名）。另存为同格式同版本时 `ExportCad` 重新读出原图，只改写变化的实体与表项，未修改的实体（包括天正 `TCH_*` 等自定义对象）、跳过的实体、图纸空间原样写回；格式或版本不同时重新生成整张图纸。天正对象的显示适配在 `src/Import/Tch*.h`

**文字与字体**（`src/Text/`）
- `FontSystem` / `FontEngine`：SHX（形字体）与 TTF；`TextLayoutEngine`：测量与排版；支持 GBK 编码的中文

**几何内核**（`src/Core/GeomKernel/`）：`Point3`、`Line`、`Circle`、`Arc`、`AABB`，`ICurve` 曲线抽象，`CurveIntersect` 求交，向量 / 矩阵。`Mat4` 为行向量约定（P' = P · M）

### 主窗口与平台（桌面版、网页版）

整个界面由 MiniGUI 绘制。界面由 MiniGUI 后端渲染（`D3D11Backend` / `WebGL2Backend`），CAD 视口由 CAD 渲染器渲染（`D3D11Renderer` / `GLES3Renderer`），两者不是一回事。
`apps/Common/src/GUI` 必须保持平台无关，平台相关的一律经 `AppPlatform`：图形对象、重绘调度、窗口标题与按钮、文件选择（可能是异步的）、另存为路径与 DWG/DXF 版本、`OnDocumentSaved`（网页版据此下载）、资源目录与用户数据目录、三维实体内核（`GetSolidKernel`，桌面版与网页版都是 SMLib，网页版可 `-DMINICAD_WITH_SOLID=OFF` 去掉）、后台任务（`RunInBackground`：桌面版用线程读图纸、求值节点图，网页版先画一帧再同步执行；`HasBackgroundThread` 区分两者）与 `Post`（当前消息与绘制处理完后再执行）。打开图纸经 `MainFrame::OpenDrawings` 在后台读取（`DocumentManager::Load`，可在其他线程调用），读完后在界面线程 `DocumentManager::Add`，状态栏显示进度。

- `GUI/MainFrame`：文档标签、视口输入 → `Editor`、逐帧渲染（`RenderFrame`）、尺寸变化（`OnResize`），由宿主调用；`RunSelfTest` 只在桌面版里定义
- `GUI/MainCommands.cpp`：所有操作注册为 MiniGUI 命令；菜单、工具栏、功能区、停靠布局来自 `assets/ui/minicad_ui.json`（保存后自动重新加载）。界面布局参照 SolidCraft（`docs/MiniGUI/UI-REFRESH-PLAN.md`）：绘图区（面板 `canvas`）铺满标题栏以下，功能区、停靠面板（浮动停靠区 `"floating": true`，文档区 `documents` 是透明的“洞”，里面放视图方块、竖排导航条和底部居中的命令行）、状态栏（左侧模型 / 布局标签）都是浮在上面的圆角卡片；“全部显示”按没被挡住的区域留边（`Camera::SetFitInsets`，`MainFrame::UpdateFitInsets`）。标题栏：MiniCAD、首页、“文件”菜单、保存 / 撤销 / 重做、文档标签（面板 `doctabs`）、右侧 AI / 设置 / 帮助 / 版本（面板 `appbar`）；其余命令在功能区（`MiniGUI::Ribbon`，描述文件顶层 `ribbons`；选项卡行右端是收起按钮和命令搜索框，Ctrl+K）；功能区图标由 `tools/make_icons.py` 生成
- `GUI/StartPage.cpp`（起始页，面板 `start`，显示时隐藏标题栏以下的其余界面；首页键 `view.start`）、`GUI/ViewNavBar.cpp`（视图方块下面竖排的导航条、标题栏右侧的 AI / 设置 / 帮助 / 版本）
- `GUI/MainPanels.cpp`、`LayerPanels.cpp`、`CommandLine.cpp`、`DynamicInput.cpp`、`EditorDialogs.cpp`、`StatusBarView`、`ViewCube`：特性 / 图层面板、命令行、动态输入、各种对话框、状态栏、视图方块
- `Host/Win32Window`（桌面）：无边框窗口（`MiniGUI::Win32Frame`）、D3D11 设备与交换链、消息循环、监视界面描述文件；另存为用系统对话框，"保存类型"里区分 2018 与 2013/2014
- `Host/WebApp`（网页）：`WebHost` 负责画布、WebGL2、DPI（`ResizeObserver` + devicePixelRatio 监听）、`requestAnimationFrame` 按需刷新；打开文件写进 `/upload`，保存后下载；`beforeunload` 提示未保存；没有窗口按钮（不注册 `window.*` 命令，标题栏就不显示）；`HasSaveDialog()` 为 false，另存为由 `MainFrame::OpenSaveAsDialog` 询问文件名、格式与版本

输入流程：Win32 消息 / DOM 事件 → `MiniGUI::Win32Input` / `MiniGUI::WebInput` → `UIContext` → `ViewportHost` → `MainFrame::OnViewportPointer/Key` → `Editor::OnInput`，在同一帧内完成。
网页版键盘按 `KeyboardEvent.keyCode` 映射（与 Windows 虚拟键码相同）；文本框获得焦点时，文字与输入法从跟随光标的隐藏 textarea 送来；Ctrl+V 等 `paste` 事件带着文字到达后再交给界面。

## 构建与运行

### 桌面版

使用 `CMakePresets.json` 的预设：

```powershell
cmake --preset debug              # 首次配置
cmake --build --preset debug      # 编译
```

- `debug`：Ninja，输出到 `out/debug/`
- `release`：Ninja，输出到 `out/release/`，`USE_WIN32=ON`（窗口程序，没有控制台）

**构建环境的坑**：在普通 PowerShell / cmd 里直接 `cmake --build` 会报 `fatal error C1083: Cannot open include file: "cmath"`。缓存的 `cl.exe` 路径没问题，但 MSVC 标准库的 `INCLUDE` / `LIB` 来自开发者环境变量，普通命令行里没有。用 `vcvars64` 包一层：

```powershell
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cmake --build out/debug"
```

VS 安装路径不同时自行修改；在"Developer PowerShell / Command Prompt for VS"或 Visual Studio 里构建则不需要。

**头文件依赖的坑（已处理）**：Ninja 靠 MSVC `/showIncludes` 的输出记录头文件依赖。中文版 cl 的前缀"注意: 包含文件:"按构建时控制台代码页（GBK / UTF-8）输出，与配置时写进 `rules.ninja` 的字节对不上时依赖记录为空，改头文件不重编，新旧对象文件混用、运行时崩溃。现在根 `CMakeLists.txt` 让 cl 经启动器 `cmake/ClShowIncludes.cpp`（配置时 `try_compile` 成 `out/<preset>/ClShowIncludes.exe`）运行，它把各种编码的前缀改写成 ASCII 的 `Note: including file:`，`rules.ninja` 也写这个，与代码页无关，不再需要 `chcp`。检查：`ninja -C out/debug -t deps` 里除 SMLib（预编译头，本来就没有）和 `.rc.res` 外不应有 `#deps 0`。

运行：`out/debug/MiniCADWin/MiniCADWin.exe`（Release 在 `out/release/...`）。

需要：CMake 3.20+、Ninja、MSVC（C++20）、Windows SDK（D3D11）。

### 网页版

```powershell
build_wasm.bat              # 增量构建（首次自动配置）
build_wasm.bat configure    # 清空 out\build\wasm 后重新配置
build_wasm.bat serve        # 构建后在 http://localhost:8080 启动本地服务器
```

Release 构建 `MiniCADWeb` 与 `MiniGUIWebGallery`，输出 `out/build/wasm/MiniCADWeb/index.html`（必须经 HTTP 打开，`file://` 不行）。脚本默认 emsdk 在 `D:\dev\emsdk`，并在 Visual Studio 安装目录里找 Ninja，工具链位置变了就改脚本顶部。本地验证过的 Emscripten 版本为 5.0.7（CI 固定同一版本）。网页版默认带三维实体内核（SMLib，`.wasm` 约 9.9 MB，另加 8 MB 栈）；要小体积构建在 `emcmake cmake` 加 `-DMINICAD_WITH_SOLID=OFF`（`.wasm` 约 3.6 MB）。已有的 `out\build\wasm` 要先 `build_wasm.bat configure` 一次才会改用新默认。

### 验证改动

编译、运行测试（`run_tests.bat`，见下），再实际运行桌面版；涉及网页版时用 `build_wasm.bat serve` 在浏览器里检查。

## 测试

- `run_tests.bat [debug|release]`：编译并经 ctest 运行全部测试。测试程序输出到 `out/<preset>/`，定义在 `tests/CMakeLists.txt`
- `MiniCADTests.exe`：核心库测试（`tests/Core/`：空间索引、实体、命令、DWG/DXF 往返等），退出码 = 失败项数
- `MiniGUITests.exe`：MiniGUI 单元测试（`tests/UI/`）
- `MiniGUIGolden.exe`：MiniGUI 截图对比（软件光栅 vs `tests/UI/Golden/baseline`，D3D11 vs 软件光栅）。更新基准：`MiniGUIGolden.exe --update`，检查图片后再提交。2026-10-25 按 v0.3.0 的界面样式（圆角、标题栏高 34、命令行“命令: + 输入框 + 展开按钮”）更新过基准，47 个场景全部通过
- `MiniDWGTests.exe`：MiniDWG 测试（`tests/Serialization/`，样例在 `tests/Data/Dwg/`）
- `NodeGraphTests.exe`、`NodeSolidTests.exe`：节点图核心（调度、最长列表、读写、撤销、曲线 / 引用节点、预览与烘焙、后台求值、组节点）与三维实体节点（`tests/NodeGraph/`，后者只在 `MINICAD_WITH_SOLID` 时编译）。按功能分文件，每个文件开头写明测哪些节点、实现文件与计划章节：入口 `NodeGraphTests.cpp` / `NodeSolidTests.cpp`，公共部分与各组测试的声明在 `NodeGraphTestCommon.h` / `NodeSolidCommon.h`（头文件里写了新测试该放进哪个文件），测试在 `NodeGraph{Core,Data,Document,Group}Tests.cpp` 与 `NodeSolid{Basic,Form,Curve,Surface,Mesh}Tests.cpp`；单个文件超过约 600 行时再拆。`NodeSolidTests` 只能是一个程序（SMLib 一个进程只能有一个内核对象），debug 版运行约 7 分钟
- `SMLibSmokeTests.exe`、`SolidKernelTests.exe`、`Solid3DEntityTests.exe`、`SolidToolTests.exe`、`SolidDrawingTests.exe`、`SolidViewTests.exe`：SMLib 冒烟测试、`ISolidKernel` 接口测试、三维实体 `Solid3DEntity` 测试、建模命令测试（含旋转、扫掠、选边圆角 / 倒角）、截面 / 投影出图与网格导出测试、三维视图测试（`tests/Solid/`），只在 `MINICAD_WITH_SOLID` 时编译
- `MiniCADWin.exe --bench 图纸`：打开图纸、全部显示后注入滚轮放大 / 缩小，打印每帧耗时、是否重建与内存（Release 版需重定向输出）
- `MiniCADWin.exe --selftest`：桌面版端到端自测（注入窗口消息）以及帧耗时、大图纸测量；控制台打印每一项的名字与用时，最后列出最慢的 5 项。20 万条线的耗时测量（只打印、debug 版约 24 秒）默认跳过，加 `--full` 才跑。2026-10-23 起应全部通过（之前的失败是测试本身的问题：视口未显示、细线显示默认值已改）
- CI（`.github/workflows/build.yml`）运行除 `MiniGUIGolden` 外的全部 ctest；只在推送 `v*` 标签时自动运行，推送分支、PR 不运行（可在 Actions 页面手动运行）

## 发布

推送 `v*` 标签后 CI 自动：编译桌面版与网页版、运行测试、打包 `MiniCAD-<标签>-windows-x64.zip` 与 `MiniCAD-<标签>-web.zip`、创建 GitHub Release，网页版部署到 Cloudflare Pages。版本号只改根 `CMakeLists.txt` 的 `MINICAD_VERSION`（三段或四段，四段时第四段作为 exe 产品版本的第四段）。发布说明取自 `docs/release-notes/<标签>.md`，打标签前先写好它。

## 约定

### 对象标识与所有权
- 实体有唯一的 `ObjectID`（uint64_t），0 保留（`InvalidID`）；实体归 `Scene` 所有（`m_entities`）
- 撤销 / 重做通过移除 / 恢复所有权实现（`RemoveEntity()` 返回 unique_ptr）；不存在循环所有权，反向引用用裸指针（由 Scene 保证生命周期）

### 编辑必须走命令
1. 实现 `bool Execute(Scene&)` 与 `void Undo(Scene&)`；`Execute()` 返回 false 表示失败，不入撤销栈
2. 执行后推入 `Document::GetCommandStack()`
3. 直接修改实体会绕过撤销

```cpp
auto cmd = std::make_unique<EntityTranslate>(selected_id, delta);
cmd->Execute(scene);
doc.GetCommandStack().Push(std::move(cmd));
```

### 实体属性
`EntityAttr`（颜色、图层、线型、线宽）默认随层（`ByLayer`）。`Scene::SetCurrentLineType()` 等只影响之后新建的实体。

### 几何版本与脏标记
- `Scene::m_geomVersion` 在拓扑 / 几何变化时递增，拾取的空间索引据此失效；增删、移动实体时要更新
- 单个实体改动用 `Scene::MarkEntityDirty(id)`；只影响显示的全局变化（图层颜色、可见性、线型）用 `Scene::MarkDisplayDirty()`；`Scene::MarkDirty()` 只用于几何的全局变化——它会整体重建拾取索引

### 序列化
每种实体经 `DECLARE_RUNTIME_TYPE` 注册。反序列化时先解析块与线型（依赖项），再调用 `ResolveAllInserts()` 解析块引用；映射见 `src/Serialization/EntityIO.cpp`。保存 / 打开用 `Document::SaveAs()` / `LoadFromFile()`。

### 日志
`Core/Log.h` 提供 `LOG_INFO`、`LOG_WARN`、`LOG_ERROR`（桌面版输出到控制台，网页版输出到浏览器控制台）。

## 常见任务

### 新增实体类型
1. 在 `src/Core/Entity/NewEntity.hpp` 创建头文件（继承 `Entity`，需要时实现 `ICurveEntity`）
2. 定义几何成员与访问函数，实现 `Clone()`、`GetBoundingBox()`、`Draw()`
3. 加 `DECLARE_RUNTIME_TYPE`，在 `EntityIO.cpp` 注册序列化
4. 需要 DWG/DXF 支持时在 `src/Import/CadExchange.cpp` 加映射
5. （可选）在 `src/Editor/Tools/` 加创建工具

### 新增工具
1. 在 `src/Editor/Tools/` 创建继承 `ITool` 的类，实现 `OnEnter()`、`OnInput()`、`OnExit()`、`Render()`
2. 坐标捕捉用 `InputResolver`，捕捉标记用 `SnapEngine`；修改经命令推入撤销栈
3. 在 `Editor` 里 `RegisterTool`，在 `apps/Common/src/GUI/MainCommands.cpp` 的工具表里注册命令，再放进 `assets/ui/minicad_ui.json` 的菜单 / 工具栏

### 新增需要平台配合的功能
先看 `AppPlatform` 里有没有合适的接口；没有就加一个虚函数，在 `Win32Window` 和 `WebApp` 里各实现一份，不要在 `apps/Common` 里写 `#ifdef`。

### 图层
```cpp
auto& layers = doc.GetLayerManager();
layers.CreateLayer("NewLayer");
layers.SetCurrentLayer("NewLayer");     // 之后新建的实体在当前图层上
```

## 文件组织

- `src/*/` 下按模块组织，`#include "Module/File.h"` 相对于 `src/`
- 源文件逐个列出（不用通配）：MiniCAD 在 `cmake/MiniCAD.cmake`，MiniGUI 在 `cmake/MiniGUI.cmake`，MiniDWG 在 `cmake/MiniDWG.cmake`，共用应用在 `apps/Common/CMakeLists.txt`，桌面 / 网页宿主在 `apps/Win32/CMakeLists.txt` / `apps/Web/CMakeLists.txt`，测试在 `tests/CMakeLists.txt`；新增 `.cpp` 要加到对应位置。例外：第三方 `3rd/SMLib` 在 `cmake/SMLib.cmake` 里按目录通配收集。实体是纯头文件（`.hpp`），一般不用改 CMake
- `src/Core/Object/Object.hpp`：所有场景对象的基类（类型信息、ID）
- `src/Render/VertexTypes.hpp`：平台无关的顶点格式

## 性能

- 拾取的空间索引按 `Scene::GeometryVersion()` 缓存与失效
- 视口缓存层：场景版本、相机、尺寸、显示开关、背景深浅都没变时，每帧只复制缓存层再画动态内容
- 场景顶点分两路：随视图变化的实体（虚线、按像素等宽的线宽）的线与填充进动态流，其余进静态流；滚轮缩放只重建动态流（`Editor::RebuildDynamicStream`），静态流不重新拼接、不重新上传。D3D11 的大缓冲（≥ 4 MB）用 DEFAULT 用途按实际大小创建，避免 WRITE_DISCARD 重命名造成的内存抖动
- 渲染器按 (slot, version) 缓存顶点缓冲，版本没变时不重新上传
- 顶点颜色为 RGBA8（`VertexColor`，`Vertex_P3_C4` 16 字节）。拼接后的场景静态流上传 GPU 后即释放（`Viewport::SceneUploaded` → `Editor::ReleaseUploadedSceneStreams`），CPU 端只留逐实体缓存；需要时（追加实体、渲染器 `ReleaseCachedResources` 后 `RequestSceneRestitch`）从缓存重新拼接，不重新细分。注意 NVIDIA 驱动会为上传的顶点缓冲在进程里留一份同样大小的系统内存，GPU 数据量直接计入私有内存
- 矢量文字按字形实例化：`DrawContext` 把每个字输出为一条 `GlyphInstance`（36 字节，局部 → 世界的仿射变换 + 颜色），字形网格在 `GlyphMeshLibrary` 里每个字形只存一份；实例流按网格排序分批，用 `DrawInstanced` / `drawArraysInstanced` 绘制（字形三角形画在填充之后、线之前，轮廓线画在线之后）。被区域覆盖盖到的实例由 `WipeClip::Batch::ClipGlyphs` 展开成顶点再裁剪；选中流仍按顶点展开
- `SnapEngine` 每帧 O(n)，大图纸时可考虑接入空间索引
