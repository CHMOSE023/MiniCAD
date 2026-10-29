# AGENTS.md

本文件为 AI 编程助手在本仓库中工作时提供指引。构建命令见 `README.md`；各库的开发计划在 `docs/*/PLAN.md`，发布说明在 `docs/release-notes/`。

## 按需阅读（`.agents/`）

下面的文件不会自动加载，做到相应的事时再读：

- **验证改动** → skill [`.agents/skills/test`](.agents/skills/test/SKILL.md)：按改动编译并运行相关测试，只返回汇总
- **排查图纸导入 / 显示问题**（打开后缺东西、位置或线型不对、天正对象、布局页、外部参照）→ skill [`.agents/skills/drawing-debug`](.agents/skills/drawing-debug/SKILL.md)
- **改某一块功能之前** → [`.agents/features/INDEX.md`](.agents/features/INDEX.md)：按功能（DWG/DXF 交换、外部参照、布局视口、天正对象、三维视图、主窗口界面、显示性能）分的实现说明
- **编译、运行或改文件时报错** → [`.agents/docs/troubleshooting.md`](.agents/docs/troubleshooting.md)：按报错现象查原因和做法；遇到不在表里的新问题，解决后补一条

### 文档跟着代码走

- 本文件和 `.agents/` 里的说法与代码冲突时，**以代码为准**，并在同一次改动里改正文档（不要另开一次）
- 改了行为、接口、约束或文件位置时，同时更新覆盖它的文档：每份功能指南与 skill 开头的 `covers` 列出了它覆盖的代码
- 核对：`pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/check-docs.ps1`（测试 skill 每次跑完会自动运行）。它报出文档里已经找不到的路径、`类::成员`、`MINICAD_*` 环境变量，以及"改了 covers 里的代码而文档没改"的提醒；`-Paths <文件>` 查改某个文件要看哪些文档
- 写文档时引用代码用反引号，写完整路径或 `类::成员`，脚本才查得到；占位写成 `<名称>`

## 项目概览

C++20 2D/3D CAD 编辑器，几何内核 `3rd/SMLib`，Win32、Web 与 Linux 用同一套界面（`apps/Common`）。

### 目录

- `src/`：核心库（目标 `MiniCAD`，定义在 `cmake/MiniCAD.cmake`，命名空间 `MiniCAD`）
- `src/UI/`：MiniGUI（目标 `MiniGUI`，定义在 `cmake/MiniGUI.cmake`，命名空间 `MiniGUI`）。平台层 `Platform/Win32`（`MiniGUI_Win32`）、`Platform/Web`（`MiniGUI_Web`）与 `Platform/SDL`（`MiniGUI_SDL`，Linux，需要 SDL3）。开发计划：`docs/MiniGUI/PLAN.md`
- `src/Serialization/Codec|Database|Dwg|Dxf/`：MiniDWG（移植自 ACadSharp，MIT；目标 `MiniDWG`，定义在 `cmake/MiniDWG.cmake`，命名空间 `MiniDWG`）。`PRIVATE` 链接进 MiniCAD，核心库的公开头文件不能暴露 MiniDWG 类型；它不依赖 MiniCAD，`CadDatabase` ↔ `Scene` 的映射放在 `src/Import/`。`*.g.cpp` 由已归档的 MiniDWG 仓库里的脚本生成，不要手改。开发计划：`docs/MiniDWG/PLAN.md`
- `src/NodeGraph/`：节点式参数化建模（命名空间 `MiniCAD::Nodes`），编辑器在 `apps/Common/src/GUI/NodeEditor`。开发计划：`docs/NodeGraph/PLAN.md`
- `src/Solid/`：三维实体内核接口 `ISolidKernel.h`（只用 MiniCAD 类型）；SMLib 实现在 `src/Solid/SMLib/`（目标 `MiniCADSolid`，唯一包含 SMLib 头文件的地方）。开发计划：`docs/Solid/PLAN.md`
- `apps/Common/`：主窗口的界面与逻辑，各宿主共用（目标 `MiniCADApp`）。平台服务经 `AppPlatform`（`apps/Common/src/GUI/AppPlatform.h`）
- `apps/Render/`：渲染后端，定义在 `cmake/Render.cmake`，每个平台只编译自己的图形接口。宿主经 `IGraphicsDevice`（`apps/Render/IGraphicsDevice.h`，`CreateGraphicsDevice` 按平台的默认顺序创建，桌面版可用 `--gpu` / `MINICAD_GPU` 指定）使用：Windows 为 `D3D12GraphicsDevice`（渲染器 `D3D12Renderer`、界面后端 `D3D12Backend`，共用 `MiniGUI::D3D12Context`）与 `D3D11GraphicsDevice`（`D3D11Renderer`、`D3D11Backend`），两者的 HLSL 共用 `apps/Render/Hlsl/`；网页默认 `WebGPUGraphicsDevice`（`WebGPURenderer`、`WebGPUBackend`，浏览器的 WebGPU 经 Emscripten 的 emdawnwebgpu 绑定），不支持或网址带 `?webgl` 时 `GLES3GraphicsDevice`（`GLES3Renderer`、`GLES3Backend`，WebGL2）；Linux 默认 `VulkanGraphicsDevice`（`VulkanRenderer`、`VulkanBackend`，共用 `MiniGUI::VulkanContext`，要 Vulkan 1.3；GLSL 着色器构建时编成 SPIR-V），不行时 `GLES3GraphicsDevice`（OpenGL ES 3），窗口表面与 GL 上下文都由宿主经 SDL3 创建；`SoftwareBackend` 是截图对比测试用的软件光栅界面后端。开发计划：`docs/Render/PLAN.md`
- `apps/Win32/`：桌面版宿主 `Win32Window`（目标 `MiniCADWin`）；自测 `Host/SelfTest.cpp`
- `apps/Web/`：网页版宿主 `WebApp`（目标 `MiniCADWeb`）；`MiniGUIWebGallery` 是浏览器里的控件展示
- `apps/Linux/`：Linux 宿主 `LinuxApp`（目标 `MiniCADLinux`）：SDL3 窗口与事件 + Vulkan，不行时 EGL / OpenGL ES 3；缺 SDL3 或 GLES3 开发文件时不编译，核心库与测试照常（Vulkan 开发文件与 glslangValidator 可选）
- `wasm/`：旧版 C 接口 WASM，只在 `-DMINICAD_BUILD_LEGACY_WASM=ON` 时编译（此时核心库定义 `MINICAD_WEB`，不编译 `apps/Web`）
- `tests/`：`Core/`、`UI/` + `UI/Golden/`、`Serialization/`、`NodeGraph/`、`Solid/`、`Web/`（Node 运行的 JS 测试）、`Data/`（样例图纸）
- `tools/`：`make_icons.py`（功能区图标）、`make_web_font.py`（网页版界面字体子集）、`inspect_dwg.cpp`（`MiniCADDwgInspect`，检查 DWG 内容）
- `assets/`：字体、图标、填充图案、界面描述文件 `ui/minicad_ui.json`（菜单、工具栏、功能区、停靠布局）、示例节点图；桌面版构建后复制到可执行文件旁，网页版打包进虚拟文件系统根目录
- `3rd/`：stb_image、stb_truetype（本地修改见 `3rd/stb/README.md`）；`3rd/SMLib`：NURBS BRep 实体内核（Apache-2.0），开关 `MINICAD_WITH_SOLID`（默认开），静态库不能并发使用（`SMLibKernel` 内部加锁），本地修改见 `3rd/SMLib/README.md`

### include 根目录（重要）

每个库有自己的 include 根：MiniCAD → `src`，MiniGUI → `src/UI`，MiniDWG → `src/Serialization`，共用应用 → `apps/Common/src`，渲染后端 → `apps/Render`，桌面版 → `apps/Win32/src`，网页版 → `apps/Web/src`，Linux 版 → `apps/Linux/src`。
`src` 和 `src/UI` 下都有 `Core/`、`Render/`、`Text/`；不要让同一个相对路径（如 `Core/<文件>.h`）同时存在于两个根下——重名时编译器按搜索顺序悄悄选中其中一个，不会报错。

### 源文件登记

源文件逐个列出（不用通配），新增 `.cpp` 要加到对应位置：核心库 `cmake/MiniCAD.cmake`，MiniGUI `cmake/MiniGUI.cmake`，MiniDWG `cmake/MiniDWG.cmake`，共用应用 `apps/Common/CMakeLists.txt`，渲染后端 `cmake/Render.cmake`，宿主 `apps/Win32/CMakeLists.txt` / `apps/Web/CMakeLists.txt` / `apps/Linux/CMakeLists.txt`，测试 `tests/CMakeLists.txt`。例外：`3rd/SMLib` 在 `cmake/SMLib.cmake` 里按目录通配。实体是纯头文件，一般不用改 CMake。

## 架构

- **实体**（`src/Core/Entity/`）：继承 `Entity`（基类 `Object` 负责类型信息与 ID），纯头文件；支持克隆、包围盒、绘制到 `IDrawSink`、序列化；`ICurveEntity` 提供捕捉、求交、编辑用的几何接口。颜色 `EntityColor` 同时保存 DXF 语义（ByLayer / ByBlock / ACI / 真彩）与解析后的 RGBA
- **场景**（`src/Scene/`）：`Scene` 是实体容器，管理图层、线型、块、文字样式、标注样式与当前属性；脏标记 + 几何版本号
- **文档与撤销**（`src/Document/`）：`Document` 包装场景，负责保存 / 打开、撤销 / 重做；`CommandStack` + `ICommand`（`src/Document/Command/`）；`DocumentManager` 管理多文档，共用一个 `Viewport`，每个文档一个 `Editor`
- **编辑器**（`src/Editor/`）：输入管线 `InputResolver` → `Editor` → 工具 / 约束（对象捕捉优先：捕捉到对象上的点时不再套正交 / 极轴，同 AutoCAD）；工具 `ITool`（`src/Editor/Tools/`，修改工具在 `src/Editor/Tools/Modify/`）；拾取（空间索引）、`SnapEngine`、`GripEditor`；需要界面配合的操作通过"请求"交给宿主（`GetTextInputRequest` 等）
- **渲染**：`Viewport`（相机、分两层绘制：缓存层 + 每帧的动态内容）；渲染器实现 `IRenderer`。CAD 视口由 CAD 渲染器绘制，界面由 MiniGUI 后端绘制，两者不是一回事。GL 的纹理 / 帧缓冲名按值放在 `void*` 里传递（见 `GLRenderTarget`）
- **序列化**（`src/Serialization/`）：`ISerializer`，`JsonSerializer` / `BinarySerializer`（键值树，缺少的键取默认值，可以加字段而不破坏旧文件），`EntityIO` 负责实体映射。`.mcad`（不区分大小写）→ 二进制，其余 → JSON
- **DWG/DXF 交换**（`src/Import/CadExchange.*`）：`ImportCad` / `ExportCad` 经 MiniDWG 的 `CadDatabase` 与 `Scene` 互转，`Document` 按扩展名分派。打开时保留原图（`CadSource`），另存为同格式同版本时只改写变化的部分；天正对象的适配在 `src/Import/Tch*.h`
- **文字**（`src/Text/`）：`FontSystem` / `FontEngine`（SHX 与 TTF），`TextLayoutEngine`；字体先在资源目录（`AppPlatform::GetResourceDir`）的 `fonts/` 找（`MainFrame::Initialize` 设定，与启动时的当前目录无关），再到 `AppPlatform::GetSystemFontDirs()`
- **几何内核**（`src/Core/GeomKernel/`）：`Point3`、`ICurve`、`CurveIntersect`、向量 / 矩阵。`Mat4` 为行向量约定（P' = P · M）

### 主窗口与平台

`apps/Common/src/GUI` 必须保持平台无关，平台相关的一律经 `AppPlatform`（图形对象、重绘调度、文件选择（可能是异步的）、另存为路径、资源与用户数据目录、三维内核、后台任务 `RunInBackground` / `Post`）。不要在 `apps/Common` 里写 `#ifdef`：需要新的平台能力就在 `AppPlatform` 加虚函数，在 `Win32Window`、`WebApp`、`LinuxApp` 里各实现一份。

- `GUI/MainFrame`：文档标签、视口输入 → `Editor`、逐帧渲染（`RenderFrame`）；打开图纸经 `OpenDrawings` 在后台读取（`DocumentManager::Load` 可在其他线程调用），读完在界面线程 `DocumentManager::Add`
- `GUI/MainCommands.cpp`：所有操作注册为 MiniGUI 命令；菜单、功能区、停靠布局来自 `assets/ui/minicad_ui.json`（保存后自动重新加载）。命令行输入经 `GUI/CommandLine.cpp`（界面层命令）再交给 `Editor::RunCommand`
- 网页版：没有后台线程（`RunInBackground` 先画一帧再同步执行）、没有窗口按钮、没有另存为对话框（`HasSaveDialog()` 为 false）；打开的文件写进内存文件系统 `/upload`，保存后下载；图形设备异步就绪（先试 WebGPU，再退 WebGL2）之后才初始化界面
- Linux 版：系统标题栏（没有窗口按钮）；文件对话框用 SDL3 的接口（xdg-desktop-portal，没有时 zenity），打不开时提示改用拖放或命令行；没有自测 / `--bench`（`RunSelfTest` 只在桌面版里）

输入流程：Win32 消息 / DOM 事件 / SDL 事件 → `MiniGUI::Win32Input` / `WebInput` / `SDLInput` → `UIContext` → `ViewportHost` → `MainFrame::OnViewportPointer/Key` → `Editor::OnInput`，在同一帧内完成。

## 验证改动

用 `.agents/skills/test` 只编译、运行与改动相关的测试，不要每次跑 `run_tests.bat` 全量；全量测试交给 CI（到 main 的 PR 自动跑 Windows、网页、Linux 三个任务，要求 `Windows (D3D11)` 与 `Web (WebAssembly)` 两项通过才能合并，`Linux (Vulkan / GLES3)` 不是必需的；CI 不跑 `MiniGUIGolden`）。改了界面要实际运行桌面版看；涉及网页版时用 `build_wasm.bat serve` 在浏览器里看。

## 发布

推送 `v*` 标签后 CI 自动编译桌面版、网页版与 Linux 版、运行测试、打包 `MiniCAD-<标签>-windows-x64.zip` / `MiniCAD-<标签>-web.zip` / `MiniCAD-<标签>-linux-x64.tar.gz`、创建 GitHub Release，网页版部署到 Cloudflare Pages。Linux 版在 Ubuntu 24.04 上编译，SDL3 由 CI 从源码编成静态库链入（版本与校验和在 `.github/workflows/build.yml` 的 `SDL3_VERSION` / `SDL3_SHA256`）。版本号只改根 `CMakeLists.txt` 的 `MINICAD_VERSION`（三段或四段）。发布说明取自 `docs/release-notes/<标签>.md`，打标签前先写好。

## 约定

### 对象标识与所有权
- 实体有唯一的 `ObjectID`（uint64_t），0 保留（`InvalidID`）；新 ID 用 `Scene::NextObjectID()`；实体归 `Scene` 所有
- 撤销 / 重做通过移除 / 恢复所有权实现（`RemoveEntity()` 返回 unique_ptr）；反向引用用裸指针（由 Scene 保证生命周期）

### 编辑必须走命令
1. 命令继承 `ICommand`（`src/Document/Command/`），实现 `bool Execute(Scene&)` 与 `void Undo(Scene&)`；`Execute()` 返回 false 表示失败，不入撤销栈
2. 用 `CommandStack::Execute` 执行并入栈；`Push` 只入栈不执行，用于已经生效的改动（如拖动结束时）
3. 直接修改实体会绕过撤销；每条命令生效后 `CommandStack` 的钩子会处理附属改动（如关联标注跟随），与原命令合为一步撤销

```cpp
auto cmd = std::make_unique<MoveCommand>(ids, Math::Vec3{ dx, dy, 0.0 }, scene);
doc.GetCommandStack().Execute(std::move(cmd), scene);   // 工具里是 m_ctx->cmdStack
```

### 实体属性
`EntityAttr`（颜色、图层、线型、线宽）默认随层（`ByLayer`）。`Scene::SetCurrentLineType()` 等只影响之后新建的实体。

### 几何版本与脏标记
- 拓扑 / 几何变化时 `Scene::m_geomVersion` 递增，拾取的空间索引据此失效
- 单个实体改动用 `Scene::MarkEntityDirty(id)`；只影响显示的全局变化（图层颜色、可见性、线型）用 `Scene::MarkDisplayDirty()`；`Scene::MarkDirty()` 只用于几何的全局变化——它会整体重建拾取索引

### 序列化
每种实体经 `DECLARE_RUNTIME_TYPE` 注册，映射在 `src/Serialization/EntityIO.cpp`。反序列化时先解析块与线型（依赖项），再 `ResolveAllInserts()`。保存 / 打开用 `Document::SaveAs()` / `LoadFromFile()`。

### 路径与编码
路径在接口间一律传 UTF-8 `std::string`；转成 `std::filesystem::path` 用 `std::u8string`，转回用 `u8string()`。不要用 `path::string()` 或窄字符串构造 path（按系统代码页转换，英文系统遇到中文会抛异常）。

### 日志
`Core/Log.h` 提供 `LOG_INFO`、`LOG_WARN`、`LOG_ERROR`（桌面版输出到控制台，网页版输出到浏览器控制台）。

### 文本格式
仓库里文本统一存为 LF（`.gitattributes`），`.bat` 为 CRLF，`.dwg` / `.dxf` 按二进制。代码注释与提交信息用简体中文。

## 常见任务

### 新增实体类型
1. 在 `src/Core/Entity/<名称>Entity.hpp` 创建头文件（继承 `Entity`，需要时实现 `ICurveEntity`）
2. 实现 `Clone()`、`GetBoundingBox()`、`Draw()`
3. 加 `DECLARE_RUNTIME_TYPE`，在 `EntityIO.cpp` 注册序列化
4. 需要 DWG/DXF 支持时在 `src/Import/CadExchange.cpp` 加映射
5. 夹点（`src/Editor/Grip/`）、拾取 / 捕捉、特性面板（`src/Editor/Properties/PropertyTable.cpp`）、创建工具按需补上

### 新增工具
1. 在 `src/Editor/Tools/` 创建继承 `ITool`（`src/Editor/Tools/ITool.h`）的类：必须实现 `OnInput(const EditorContext&)`，按需实现 `Cancel()`、`GetPrompt()`（命令行提示）、`OnNumberInput()`（键入数值）等。参考 `LineTool.h`
2. 预览图形画进上下文的 `overlay`（`AddLine`、`Clear`）；坐标捕捉用 `InputResolver`，捕捉标记用 `SnapEngine`；修改经 `m_ctx->cmdStack.Execute` 入撤销栈
3. 在 `Editor` 里 `RegisterTool`，在 `apps/Common/src/GUI/MainCommands.cpp` 注册命令，再放进 `assets/ui/minicad_ui.json` 的菜单 / 功能区

### 图层
图层的新建 / 删除 / 修改走命令（`src/Document/Command/LayerCommands.h`：`AddLayerCommand`、`DeleteLayerCommand`、`ChangeLayerCommand`），可撤销；切换当前图层不算文档编辑，直接设：

```cpp
doc.GetCommandStack().Execute(std::make_unique<AddLayerCommand>("NewLayer", /*makeCurrent=*/true), scene);
scene.GetLayerManager().SetActiveLayerID(id);   // 之后新建的实体在当前图层上
```

`LayerManager::AddLayer` 直接加图层、不进撤销栈，只用于导入和测试。

## 性能

- 拾取的空间索引按 `Scene::GeometryVersion()` 缓存；渲染器按 (slot, version) 缓存顶点缓冲，版本没变不重新上传
- 视口缓存层：场景版本、相机、尺寸、显示开关都没变时，每帧只复制缓存层再画动态内容
- 场景顶点分静态流与动态流：几何按当前缩放烘焙的实体（按像素的线宽、点样式、文字大小）绘制时要调用 `IDrawSink::MarkViewDependent()`，进动态流，滚轮缩放只重建动态流（`Editor::RebuildDynamicStream`）；其余进静态流，不随缩放重建
- 逐实体缓存细分结果，切换文档、追加实体、选中高亮都从缓存拼接，不重新细分；矢量文字按字形实例化（每个字形的网格只存一份）
- 成批细分时同一块定义只细分一次（`BlockTessCache`），之后的插入经平移、旋转接上；块参照绘制时经 `IDrawSink::BeginBlockInstance` 询问，新写的转发型 sink 要么转发它、要么保持默认的不缓存
- 大图纸注意 O(n) 的每帧操作（如 `SnapEngine`）；有性能改动时用 `--bench` 前后对比
