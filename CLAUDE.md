# CLAUDE.md

本文件为 Claude Code（claude.ai/code）在本仓库中工作时提供指引。

## 项目概览

MiniCAD 是用现代 C++（C++20）编写的跨平台二维 CAD 编辑器。一个仓库里包含三个库和两个应用：
平台无关的核心库 MiniCADLib、保留模式界面库 MiniGUI、DWG/DXF 读写库 MiniDWG；
Windows 桌面版（D3D11）与网页版（Emscripten + WebGL2）用同一份代码（`apps/Common`）绘制同一套 MiniGUI 界面。
仓库于 2026-10 由原 MiniCADLib、MiniGUI、MiniDWG 三个仓库合并而成（未保留历史）。当前版本 v0.1.0。

### 目录

- `src/`：核心库（目标 `MiniCADLib`，命名空间 `MiniCAD`）
- `src/UI/`：MiniGUI（目标 `MiniGUI`，定义在 `cmake/MiniGUI.cmake`，命名空间 `MiniGUI`）。平台层 `Platform/Win32`（`MiniGUI_Win32`）与 `Platform/Web`（`MiniGUI_Web`：DOM 输入、经隐藏 textarea 接入输入法、剪贴板、打包的界面字体）。开发计划：`docs/MiniGUI/PLAN.md`
- `src/Serialization/Codec|Database|Dwg|Dxf/`：MiniDWG（移植自 ACadSharp，MIT；目标 `MiniDWG`，定义在 `cmake/MiniDWG.cmake`，命名空间 `MiniDWG`）。`PRIVATE` 链接进 MiniCADLib（网页版也包含），核心库的公开头文件不能暴露 MiniDWG 类型；它不依赖 MiniCADLib，`CadDatabase` ↔ `Scene` 的映射放在 `src/Import/`。开发计划：`docs/MiniDWG/PLAN.md`；`*.g.cpp` 由已归档的 MiniDWG 仓库 `tools/` 里的脚本生成，不要手改
- `apps/Common/`：主窗口的界面与逻辑，桌面版和网页版共用（目标 `MiniCADApp`：`MainFrame`、命令、面板、命令行、对话框）。平台服务经 `AppPlatform` 接口（`apps/Common/src/GUI/AppPlatform.h`）提供
- `apps/Win32/`：桌面版宿主 `Win32Window`（无边框窗口、D3D11、消息循环，实现 `AppPlatform`），目标 `MiniCADWin`；自测 `Host/SelfTest.cpp`。`src/Render/` 下是 CAD 渲染器 `D3D11Renderer` 与 MiniGUI 后端（`D3D11/D3D11Backend` → `MiniGUI_D3D11`，`Software/SoftwareBackend` → `MiniGUI_Software`）
- `apps/Web/`：网页版宿主 `WebApp`（经 `WebHost` 管理画布与 WebGL2，浏览器文件选择与下载，实现 `AppPlatform`），目标 `MiniCADWeb`。`src/Render/` 下是 CAD 渲染器 `GLES3Renderer` + `GLRenderTarget` 与 MiniGUI 的 `WebGL2/WebGL2Backend`（`MiniGUI_WebGL2`）。`MiniGUIWebGallery` 是浏览器里的 MiniGUI 控件展示
- `wasm/`：旧版 C 接口 WASM（给仓库外的 HTML/JS 前端用）。只在 `-DMINICAD_BUILD_LEGACY_WASM=ON` 时编译，此时核心库定义 `MINICAD_WEB`（文字输入交给 JS、块名自动命名、文字走纹理图集），并且不编译 `apps/Web`
- `tests/`：`Core/`（MiniCADLib）、`UI/` + `UI/Golden/`（MiniGUI）、`Serialization/`（MiniDWG）、`Data/`（DWG/DXF 样例、截图测试用图标）
- `tools/make_web_font.py`：重新生成 `assets/fonts/NotoSansSC-UI.ttf`（网页版界面字体，Noto Sans SC Regular 子集，OFL）
- `assets/`：字体、图标、填充图案、界面描述文件 `ui/minicad_ui.json`；桌面版构建后复制到可执行文件旁，网页版打包进虚拟文件系统根目录（`/fonts`、`/icons`、`/patterns`、`/ui`）
- `3rd/`：stb_image、stb_truetype（MiniCADLib 与 MiniGUI 共用）
- `docs/`：早期 MiniCAD 项目的教程（描述的是旧架构）、各库开发计划、发布说明 `release-notes/`

### include 根目录（重要）

每个库有自己的 include 根：MiniCADLib → `src`，MiniGUI → `src/UI`，MiniDWG → `src/Serialization`，共用应用 → `apps/Common/src`，桌面版 → `apps/Win32/src` 与 `apps/Win32/src/Render`，网页版 → `apps/Web/src` 与 `apps/Web/src/Render`。
`src` 和 `src/UI` 下都有 `Core/`、`Render/`、`Text/`；目前没有任何相对路径（如 `Core/Foo.h`）同时存在于两个根下，要保持这样——重名时编译器会按搜索顺序悄悄选中其中一个，不会报错。
命名空间保持 `MiniCAD` / `MiniGUI` / `MiniDWG`。

## 架构

### 核心系统

**实体**（`src/Core/Entity/`）
- 可绘制对象继承 `Entity`（基类 `Object` 负责类型信息与 ID）；30 种左右实体：`LineEntity`、`CircleEntity`、`ArcEntity`、`TextEntity`、`DimensionEntity`、`InsertEntity` 等
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
- 背景深浅（`IRenderer::SetLightBackground`，7 号色语义）：浅色背景时纯白顶点画成纯黑、深色背景时纯黑画成纯白，在两个渲染器的顶点着色器里处理（光栅图像不变）。`MainFrame::RenderFrame` 每帧按界面主题设置 `Viewport::SetLightBackground`；图层颜色块在 `LayerPanels.cpp` 的 `SwatchColor` 里做同样的反转

**序列化**（`src/Serialization/`）
- `ISerializer` 抽象接口，`JsonSerializer` / `BinarySerializer` 实现，`EntityIO` 负责实体与数据的映射
- 按扩展名选格式：`.mcad`（不区分大小写）→ 二进制，其余（`.json` 等）→ JSON 文本，两者内容等价

**DWG/DXF 交换**（`src/Import/CadExchange.*`）
- `ImportCad` / `ExportCad` 经 MiniDWG 的 `CadDatabase` 与 `Scene` 互转；`Document::LoadFromFile` / `SaveToFile` 按扩展名 `.dwg` / `.dxf` 分派，DXF 写 ASCII
- 写出版本 `CadSaveVersion`：`R2018`（AC1032）或 `R2013`（AC1027，AutoCAD 2013～2017 共用，界面上标为"2013/2014"）。打开文件时记住其版本（`Document::GetCadSaveVersion`），另存为时由用户选择
- 已映射：图层、线型、文字样式、块、Line / Circle / Arc / Ellipse / Point / Polyline / Text / MText / Insert / Solid / 3DFace；标注导入时用其匿名块还原为 Insert；其余实体（Hatch、Spline、Leader 等）跳过并 `LOG_WARN`
- 以原图为底另存：打开时 `Document` 留下原图 `CadSource`（原始字节、场景实体 ↔ 原图句柄、导入时的签名）。另存为同格式同版本时 `ExportCad` 重新读出原图，只改写变化的实体与表项，未修改的实体（包括天正 `TCH_*` 等自定义对象）、跳过的实体、图纸空间原样写回；格式或版本不同时重新生成整张图纸。天正对象的显示适配在 `src/Import/Tch*.h`

**文字与字体**（`src/Text/`）
- `FontSystem` / `FontEngine`：SHX（形字体）与 TTF；`TextLayoutEngine`：测量与排版；支持 GBK 编码的中文

**几何内核**（`src/Core/GeomKernel/`）：`Point3`、`Line`、`Circle`、`Arc`、`AABB`，`ICurve` 曲线抽象，`CurveIntersect` 求交，向量 / 矩阵。`Mat4` 为行向量约定（P' = P · M）

### 主窗口与平台（桌面版、网页版）

整个界面由 MiniGUI 绘制。界面由 MiniGUI 后端渲染（`D3D11Backend` / `WebGL2Backend`），CAD 视口由 CAD 渲染器渲染（`D3D11Renderer` / `GLES3Renderer`），两者不是一回事。
`apps/Common/src/GUI` 必须保持平台无关，平台相关的一律经 `AppPlatform`：图形对象、重绘调度、窗口标题与按钮、文件选择（可能是异步的）、另存为路径与 DWG/DXF 版本、`OnDocumentSaved`（网页版据此下载）、资源目录与用户数据目录。

- `GUI/MainFrame`：文档标签、视口输入 → `Editor`、逐帧渲染（`RenderFrame`）、尺寸变化（`OnResize`），由宿主调用；`RunSelfTest` 只在桌面版里定义
- `GUI/MainCommands.cpp`：所有操作注册为 MiniGUI 命令；菜单、工具栏、停靠布局来自 `assets/ui/minicad_ui.json`（保存后自动重新加载）
- `GUI/MainPanels.cpp`、`LayerPanels.cpp`、`CommandLine.cpp`、`DynamicInput.cpp`、`EditorDialogs.cpp`、`StatusBarView`：特性 / 图层面板、命令行、动态输入、各种对话框、状态栏
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

运行：`out/debug/MiniCADWin/MiniCADWin.exe`（Release 在 `out/release/...`）。

需要：CMake 3.20+、Ninja、MSVC（C++20）、Windows SDK（D3D11）。

### 网页版

```powershell
build_wasm.bat              # 增量构建（首次自动配置）
build_wasm.bat configure    # 清空 out\build\wasm 后重新配置
build_wasm.bat serve        # 构建后在 http://localhost:8080 启动本地服务器
```

Release 构建 `MiniCADWeb` 与 `MiniGUIWebGallery`，输出 `out/build/wasm/MiniCADWeb/index.html`（必须经 HTTP 打开，`file://` 不行）。脚本默认 emsdk 在 `D:\dev\emsdk`，并在 Visual Studio 安装目录里找 Ninja，工具链位置变了就改脚本顶部。本地验证过的 Emscripten 版本为 5.0.7（CI 固定同一版本）。

### 验证改动

编译、运行测试（`run_tests.bat`，见下），再实际运行桌面版；涉及网页版时用 `build_wasm.bat serve` 在浏览器里检查。

## 测试

- `run_tests.bat [debug|release]`：编译并经 ctest 运行全部测试。测试程序输出到 `out/<preset>/`，定义在 `tests/CMakeLists.txt`
- `MiniCADTests.exe`：核心库测试（`tests/Core/`：空间索引、实体、命令、DWG/DXF 往返等），退出码 = 失败项数
- `MiniGUITests.exe`：MiniGUI 单元测试（`tests/UI/`）
- `MiniGUIGolden.exe`：MiniGUI 截图对比（软件光栅 vs `tests/UI/Golden/baseline`，D3D11 vs 软件光栅）。更新基准：`MiniGUIGolden.exe --update`，检查图片后再提交。截至 2026-10-08 有 10 个场景（标题栏、停靠、文档标签）在原 MiniGUI 仓库里就已不一致
- `MiniDWGTests.exe`：MiniDWG 测试（`tests/Serialization/`，样例在 `tests/Data/Dwg/`）
- `MiniCADWin.exe --selftest`：桌面版端到端自测（注入窗口消息）以及帧耗时、大图纸测量。截至 v0.1.0 有 2 项已知失败（阵列对话框默认 3×4、改颜色后重新细分），整合前就存在
- CI（`.github/workflows/build.yml`）运行除 `MiniGUIGolden` 外的全部 ctest

## 发布

推送 `v*` 标签后 CI 自动：编译桌面版与网页版、运行测试、打包 `MiniCAD-<标签>-windows-x64.zip` 与 `MiniCAD-<标签>-web.zip`、创建 GitHub Release。发布说明取自 `docs/release-notes/<标签>.md`，打标签前先写好它。

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
- 源文件逐个列出（不用通配）：MiniCADLib 在顶层 `CMakeLists.txt`，MiniGUI 在 `cmake/MiniGUI.cmake`，MiniDWG 在 `cmake/MiniDWG.cmake`，共用应用在 `apps/Common/CMakeLists.txt`，桌面 / 网页宿主在 `apps/Win32/CMakeLists.txt` / `apps/Web/CMakeLists.txt`，测试在 `tests/CMakeLists.txt`；新增 `.cpp` 要加到对应位置。实体是纯头文件（`.hpp`），一般不用改 CMake
- `src/Core/Object/Object.hpp`：所有场景对象的基类（类型信息、ID）
- `src/Render/VertexTypes.hpp`：平台无关的顶点格式

## 性能

- 拾取的空间索引按 `Scene::GeometryVersion()` 缓存与失效
- 视口缓存层：场景版本、相机、尺寸、显示开关、背景深浅都没变时，每帧只复制缓存层再画动态内容
- 渲染器按 (slot, version) 缓存顶点缓冲，版本没变时不重新上传
- `SnapEngine` 每帧 O(n)，大图纸时可考虑接入空间索引
