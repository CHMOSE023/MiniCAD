---
covers: assets/ui/minicad_ui.json, apps/Common/src/GUI/MainFrame.*, apps/Common/src/GUI/MainCommands.cpp, apps/Common/src/GUI/CommandLine.cpp, apps/Common/src/GUI/MainPanels.cpp, apps/Common/src/GUI/LayerPanels.cpp, apps/Common/src/GUI/StartPage.cpp, apps/Common/src/GUI/ViewNavBar.cpp, apps/Common/src/GUI/DocumentWindows.cpp, apps/Common/src/GUI/FloatingPanelHost.*, apps/Common/src/GUI/AppPlatform.h, apps/Win32/src/Host/Win32Window.*, apps/Win32/src/Host/Win32FloatingWindow.*, apps/Web/src/Host/*, apps/Linux/src/Host/*, src/UI/Platform/SDL/*
---

# 主窗口界面

## 概述

整个界面由 MiniGUI 绘制，代码在 `apps/Common/src/GUI`，桌面版、网页版与 Linux 版共用；平台相关的一律经 `AppPlatform`。界面描述文件 `assets/ui/minicad_ui.json` 定义菜单、功能区、停靠布局，保存后自动重新加载。

## 布局

参照 SolidCraft（`docs/MiniGUI/UI-REFRESH-PLAN.md`）：

- 绘图区（面板 `canvas`）铺满标题栏以下；功能区、停靠面板、状态栏都是浮在上面的圆角卡片
- 停靠：浮动停靠区 `"floating": true`；文档区 `documents` 是透明的"洞"，里面放视图方块、竖排导航条（`GUI/ViewNavBar.cpp`）和底部居中的命令行
- 浮动面板：特性 / 图层 / 节点等面板的标签拖到停靠区外松开或双击标签即浮出（`MiniGUI::DockSpace::FloatPanel`），拖标题栏回停靠区停靠（落点预览画在主窗口里）、双击标题栏回原处、× 关闭后"视图"菜单再显示时回到原处；位置随用户布局保存（`layout.json` 里 `dock.floatingPanels`，主窗口客户区的逻辑坐标）。
  浮动窗口由 `GUI/FloatingPanelHost.cpp`（`MiniGUI::IDockFloatHost`）向平台要：`AppPlatform::CreateFloatingWindow` 返回带自己 `UIContext` 的 `IFloatingWindow`，里面放无边框的 `MiniGUI::FloatingPanel`；主题切换、字体加载对每个浮动窗口各做一次；关闭的窗口推迟到下一帧开头销毁（`FloatingPanelHost::Purge`，关闭常常发生在它自己的事件里）。平台给不出窗口时（网页版、Linux 版）是窗口内的浮动面板（`MiniGUI::FloatingPanel` 弹层）
- "全部显示"按没被挡住的区域留边（`Camera::SetFitInsets`，`MainFrame::UpdateFitInsets`，主窗口与文档窗口各按自己的那一套算）
- 标题栏：MiniCAD、首页、"文件"菜单、保存 / 撤销 / 重做、文档标签（面板 `doctabs`）、右侧 AI / 设置 / 帮助 / 版本（面板 `appbar`）
- 文档窗口（桌面版，`GUI/DocumentWindows.cpp`）：像 Visual Studio 那样把文档标签拖到标签条之外（`MiniGUI::TabView::SetOnTabDragOut`）或标签右键"在独立窗口中打开"，这个文档就移到自己的系统窗口里（可以放到第二台显示器），主窗口继续显示其余文档。标签一拖出标签条就建窗口（先不激活），鼠标没松开前主窗口持有捕获，标签条把移动 / 松开经 `TabView::SetOnTornTabDrag` 转给 `MainFrame::OnDocTabTornDrag`，窗口跟着光标走；同一次拖动里松开在标签条上即合并回去，松开在别处才激活窗口；拖窗口的标题栏到主窗口的标签条上松开（经过时标签条画插入标记，`TabView::DropIndexAt` / `TabView::SetDropIndicator`）或双击标题栏即合并回去；× / 系统关闭 = 关闭文档（未保存时照常询问）。
  - 窗口与停靠面板的浮动窗口同一套：`AppPlatform::CreateFloatingWindow` 给的 `IFloatingWindow`，里面放无边框的 `MiniGUI::FloatingPanel`（标题栏显示文档名）+ 铺满的 `MiniGUI::ViewportHost`，视口上浮着与主窗口相同的一层（透明的"洞"里是视图方块、导航条、底部居中的命令行，下面是状态栏含模型 / 布局标签）；每帧经 `IFloatingWindow::SetOnBeforeRender` 在那个窗口布局之后、绘制之前渲染它的视口（`MainFrame::RenderDocWindow`，与主窗口的 `MainFrame::RenderViewport` 相同）。视口是文档自己的（`DocumentManager::DetachDocument`：独立的 `Viewport`，相机从文档保存的视图恢复），渲染目标另建一个（`AppPlatform::CreateViewportTarget`），渲染器共用，见 [display-performance](display-performance.md) 的"多视口"。CAD 渲染器把视口纹理清成透明，`MiniGUI::ViewportHost` 先铺窗口底色（`Theme::Background`）再画纹理，所以文档窗口的绘图区与主窗口的同色
  - 文档窗口建成与主窗口平级的窗口（`CreateFloatingWindow` 的 `document`：不归主窗口所有、任务栏里有按钮），因为界面的对话框都画在主窗口里：主窗口弹出对话框时置前（`MainFrame::RaiseMainForModal`，每帧检查 `UIContext::HasModal`，`AppPlatform::ActivateMainWindow`）；对话框开着时文档窗口不接受编辑，按键与字符转给主窗口（Esc / 回车给对话框），点它或关它只把主窗口置前（`MainFrame::BlockedByModal`）
  - 每个窗口一套绘图区界面（`MainFrame::ViewChrome`：视图方块、导航条、命令行、状态栏与布局标签）：主窗口的 `MainFrame::m_main` 绑定主视口里的文档，文档窗口的 `DocWindow::chrome` 绑定它的文档（`MainFrame::DocOf`），由同一组函数建出来（`MainFrame::CreateDocumentArea`、`MainFrame::CreateStatusBar`、`MainFrame::CreateCommandLine`、`MainFrame::CreateLayoutTabs`、`MainFrame::CreateViewNavBar`，参数是那一套）；显示随各自的文档同步（主窗口在 `MainFrame::RenderFrame` 里，文档窗口在它自己的帧里 `MainFrame::SyncDocWindowChrome`；布局标签 `MainFrame::SyncLayoutTabs` 逐套同步）；命令行 / 视图方块的显示开关各窗口共用
  - "当前文档"（`DocumentManager::GetActive`：特性 / 图层面板、功能区与菜单命令操作的对象）随用户最后操作的窗口切换：在视口里按下 / 滚动 / 敲键，或在某一套界面上按下（主窗口经捕获阶段的 `PressHook`，文档窗口经 `MiniGUI::FloatingPanel::SetOnActivate`），那个窗口的文档就成为当前文档（`MainFrame::UseChrome`、`MainFrame::ActivateDocumentOf`），导航条等按钮的命令随后就作用于它。主视口显示的文档（`DocumentManager::GetMainDocument`）是标签条上选中的那个，当前文档在独立窗口里时它不变。视口的指针事件按视口交给那个文档的编辑器（`MainFrame::RouteViewportPointer`，每个视口自己的鼠标状态 `MainFrame::ViewInput`）；按键交给当前文档的编辑器（`MainFrame::OnViewportKey`），在绘图区敲字母进入当前文档所在窗口的命令行（`MainFrame::ChromeOf`）；主窗口的快捷键（Ctrl+S、Ctrl+Z …）在文档窗口里一样触发（`MainFrame::OnDocWindowKey`）；文档窗口里控件都不处理的按键在内容节点上收尾，不转给主窗口（免得落进主窗口有焦点的命令行）；动态输入框只在主视口里显示，文档窗口里的数字进它自己的命令行
  - 后续改成对等的多窗口（每个窗口都有标题栏、功能区、可放多个文档，标签在任意窗口之间拖动），计划见 `docs/MultiWindow/PLAN.md`
  - 关闭的窗口推迟到下一帧开头销毁（`MainFrame::PurgeDocWindows`）；用户布局文件不保存文档窗口；平台给不出窗口或渲染目标（网页版、Linux 版）时拖出不做什么。自测第 29 节覆盖拖出（拖动中窗口已出现并跟随光标、同一次拖动拖回标签条）、文档窗口自己的一套界面（命令行与提示、布局标签各自绑定）、平移 / 缩放 / 画线 / 选择、绘图区底色、拖回、关闭（对话框开着时不接受编辑、Esc 给对话框）
- 功能区（`MiniGUI::Ribbon`，描述文件顶层 `ribbons`）：选项卡行右端是收起按钮和命令搜索框（Ctrl+K）；图标由 `tools/make_icons.py` 生成
- 起始页 `GUI/StartPage.cpp`（面板 `start`，显示时隐藏标题栏以下的其余界面；首页键 `view.start`）
- 状态栏左侧是模型 / 布局标签

## 主要文件

- `MainFrame`：文档标签、视口输入 → `Editor`、逐帧渲染 `RenderFrame`（主视口只画主视口里的文档，`DocumentManager::GetMainEditor`；文档改了要重绘所有视口用 `MainFrame::RequestRenderAll`）、尺寸变化 `OnResize`（由宿主调用）；后台打开图纸 `OpenDrawings` → `DocumentManager::Load`（其他线程）→ 界面线程 `DocumentManager::Add`，状态栏显示进度；`RunSelfTest` 只在桌面版里定义
- `MainCommands.cpp`：所有操作注册为 MiniGUI 命令（id、标签、图标、快捷键、执行、可用条件）
- `CommandLine.cpp`：命令行；界面层命令（`NODES`、`NAVVCUBE`、`XREF` 等）在这里处理，其余交给 `Editor::RunCommand`
- `MainPanels.cpp`、`LayerPanels.cpp`、`DynamicInput.cpp`、`EditorDialogs.cpp`、`StatusBarView`、`ViewCube`：特性 / 图层面板、动态输入、对话框、状态栏、视图方块
- 图层排序：面板、特性面板、工具栏下拉框都按名称的自然顺序（`MainFrame::SortedLayerIds`，0 层最前，按 "ID:名称" 缓存，增删改名才重排）；图层面板有搜索框（不分大小写，Esc 清空），点"名称"表头切换升降序
- 停靠卡片：激活的标签与面板内容同色连成一片（`DockGroup::BodyColor` 读内容 `Panel` 的背景色，标题条深一级）

## 宿主

- 图形：各宿主都只经 `IGraphicsDevice`（`apps/Render/IGraphicsDevice.h`，由 `CreateGraphicsDevice` 按平台创建）使用图形接口：渲染器、界面后端、视口渲染目标、开始界面通道、呈现、等 GPU、读回像素（自测截图）都从它来；`AppPlatform` 的图形函数直接转给它
- 桌面版 `apps/Win32/src/Host/Win32Window`：无边框窗口（`MiniGUI::Win32Frame`）、图形设备、消息循环、监视界面描述文件；另存为用系统对话框（"保存类型"里区分 2018 与 2013/2014）
- 桌面版的浮动窗口 `apps/Win32/src/Host/Win32FloatingWindow`（停靠面板与文档窗口共用）：无边框（保留系统的缩放边框）；停靠面板是主窗口拥有的工具窗口（总在主窗口之上、随它最小化、不占任务栏），文档窗口与主窗口平级（任务栏里有按钮，图标取主窗口的），每个有自己的 `UIContext`、`Win32Input` 和窗口表面（`IGraphicsDevice::CreateWindowSurface`，D3D11 / D3D12 各自再建一个交换链，共用设备与界面后端），`WM_PAINT` 里自己布局、回调 `IFloatingWindow::SetOnBeforeRender`（文档窗口渲染视口）、绘制、呈现；里面没处理的按键转给主窗口（`Win32Window::ForwardKeyMessage`，Ctrl+S 等全局快捷键仍有效）；焦点在浮动窗口里时主标题栏不变灰（`Win32Window::IsOurWindow`）；创建时位置不在任何显示器上就挪到主窗口旁边
- 桌面版的图形设备：`D3D12GraphicsDevice` 或 `D3D11GraphicsDevice`（都可用 WARP）。默认按 `apps/Render/GraphicsDevice.cpp` 里的表的顺序（D3D12 → D3D11 → WARP）创建，失败就试下一个；命令行 `--gpu d3d12|d3d11|warp` 或环境变量 `MINICAD_GPU` 指定（命令行优先，指定的不可用时回退默认顺序）。关于对话框显示实际用的（`AppPlatform::GetGraphicsName`）
- 网页版 `apps/Web/src/Host/WebApp`：图形接口先异步试 WebGPU（`CreateWebGPUGraphicsDeviceAsync`），取不到适配器 / 设备或网址带 `?webgl` 时用 WebGL2（`WebHost::CreateWebGLContext` 建上下文，`GLES3GraphicsDevice` 只使用当前上下文）；同一个画布只能有一种上下文，所以 WebGPU 取到设备后才建画布表面，失败时画布还是干净的；设备就绪（`WebApp::OnGraphicsReady`）之前不建界面、不画帧。`WebHost` 负责画布、DPI（`ResizeObserver` + devicePixelRatio）、`requestAnimationFrame` 按需刷新；打开的文件写进 `/upload`，保存后下载；`beforeunload` 提示未保存；没有窗口按钮（不注册 `window.*` 命令，标题栏就不显示）；`HasSaveDialog()` 为 false，另存为由 `MainFrame::OpenSaveAsDialog` 询问文件名、格式与版本；浏览器交互（文件选择、目录授权、下载）写在 `EM_JS` 里
- Linux 版 `apps/Linux/src/Host/LinuxApp`：SDL3 窗口（系统标题栏，`HasWindowControls()` 为 false）+ 图形设备：默认先建带 `SDL_WINDOW_VULKAN` 的窗口试 `VulkanGraphicsDevice`（表面经 `LinuxApp::VulkanWindow` 给出的回调由 SDL3 建），失败就重建带 `SDL_WINDOW_OPENGL` 的窗口和 EGL / GLES3 上下文用 `GLES3GraphicsDevice`（`EndUiPass` 里 `SDL_GL_SwapWindow`）；`--gpu vulkan|gl` / `MINICAD_GPU` 指定；`SDL_WaitEvent` 事件循环，重绘请求、后台任务完成、`Post` 都经 SDL 自定义事件唤醒；输入 / 剪贴板 / 界面字体在 `src/UI/Platform/SDL`（`SDLInput`、`SDLClipboard`、`LoadSDLUIFonts`）；文件对话框用 SDL3（portal / zenity），另存为在界面线程上等结果；切回窗口时检查界面描述文件是否改过
- 网页版输入：键盘按 `KeyboardEvent.keyCode` 映射（与 Windows 虚拟键码相同）；文本框获得焦点时，文字与输入法从跟随光标的隐藏 textarea 送来；Ctrl+V 等 `paste` 事件带着文字到达后再交给界面

## 测试与排查

- `MiniCADWin.exe --selftest`（注入窗口消息的端到端自测，应全部通过；`--full` 加跑 20 万条线的耗时测量）；改了渲染后端时 `--gpu d3d12`、`--gpu d3d11` 各跑一遍
- D3D12 的调试版自动打开调试层（系统装有 Graphics Tools 时），警告与错误以 `[D3D12]` 开头打到 stderr：调试版跑一遍自测，stderr 里不应有这样的行
- MiniGUI 控件：`MiniGUITests`、`MiniGUIGolden`（截图对比：软件光栅 vs 基准，D3D11 / D3D12 vs 软件光栅；CI 不跑）
- 网页宿主的 JS：`tests/Web/*.cjs`；整体行为在浏览器里看（`build_wasm.bat serve`），两个图形后端都要看：默认 WebGPU，网址加 `?webgl` 是 WebGL2，关于对话框显示实际用的是哪个。浏览器里没法操作系统的文件对话框，验证时可以在页面里替换 `HTMLInputElement.prototype.click` / `showDirectoryPicker`，从本地服务器取样例文件
