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
- 浮动面板：特性 / 图层 / 节点等面板的标签拖出停靠区（鼠标没松开就浮出，窗口跟着光标走，`MiniGUI::DockSpace::IsTornDragging`）或双击标签即浮出（`MiniGUI::DockSpace::FloatPanel`）。
  每个浮动窗口有自己的一棵停靠布局（`DockSpace` 里的 `Float`，内容节点 `DockFloatView`），停靠规则与停靠区相同：别的面板的标签或整个浮动窗口（拖标题栏）拖到它上面就合并为标签或拆分（标题栏、标签条上合并，内容区按位置），它里面的标签也能拖出去（拖出窗口即浮出成新窗口）、拖回停靠区或进别的浮动窗口；只有一个面板时不画标签条。找落点时浮动窗口在停靠区之上，按最近按下的顺序（`MiniGUI::DockSpace::RaiseFloat`）；落点预览画在落点所在的窗口里。
  标题栏：双击 → 里面的面板各回停靠区原处（`MiniGUI::DockSpace::RedockFloat`；双击浮动窗口里的标签只让那个面板回去，`MiniGUI::DockSpace::RedockPanel`）；× 左边的最大化按钮最大化到窗口所在的显示器（窗口内浮动时铺满窗口），最大化时拖标题栏先还原；× 关闭里面所有的面板（`MiniGUI::DockSpace::CloseFloat`），"视图"菜单再显示时回到原来的浮动窗口（还开着就合并进去）或在原处重新浮出。
  位置、标签与是否最大化随用户布局保存（`layout.json` 里 `dock.floatingPanels`：一个面板写 `panel`，几个面板写那个窗口的布局树；主窗口客户区的逻辑坐标，最大化时为还原后的位置）。
  停靠区重建标签组时结构没变的浮动窗口不动：以前每次重建都把所有浮动面板的内容摘下，别的面板浮出后，先浮出的窗口（如节点编辑器）就空了。
  浮动窗口由 `GUI/FloatingPanelHost.cpp`（`MiniGUI::IDockFloatHost`）向平台要：`AppPlatform::CreateFloatingWindow` 返回带自己 `UIContext` 的 `IFloatingWindow`，里面放无边框的 `MiniGUI::FloatingPanel`（标题栏 + 停靠区给的 `DockFloatView`）；窗口里的坐标与停靠区的坐标经 `FloatingPanelHost::ToDock` / `FloatingPanelHost::FromDock` 互换（浮动窗口里的标签拖动、在浮动窗口里画落点），最大化经 `IFloatingWindow::ToggleMaximize`，保存的是 `IFloatingWindow::GetRestoreRect`；主题切换、字体加载对每个浮动窗口各做一次；关闭的窗口推迟到下一帧开头销毁（`FloatingPanelHost::Purge`，关闭常常发生在它自己的事件里）。文档窗口的停靠区各有一个 `FloatingPanelHost`：位置按那个窗口的坐标换算（构造时给停靠区的 `UIContext` 与窗口左上角的主窗口坐标），浮动窗口归文档窗口所有（`AppPlatform::CreateFloatingWindow` 的 `owner`），在里面按下时那个窗口的文档成为当前文档（`FloatingPanelHost::SetOnActivate`）；文档窗口销毁前先把浮出的面板收回（`MiniGUI::DockSpace::TakeAllPanels`）。平台给不出窗口时（网页版、Linux 版）是窗口内的浮动面板（`MiniGUI::FloatingPanel` 弹层）
- "全部显示"按没被挡住的区域留边（`Camera::SetFitInsets`，`MainFrame::UpdateFitInsets`，主窗口与文档窗口各按自己的那一套算）
- 标题栏：MiniCAD、首页、"文件"菜单、保存 / 撤销 / 重做、文档标签（面板 `doctabs`）、右侧 AI / 设置 / 帮助 / 版本（面板 `appbar`）
- 文档窗口（桌面版，`GUI/DocumentWindows.cpp`）：像 Chrome / Visual Studio 那样，文档标签拖到标签条之外（`MiniGUI::TabView::SetOnTabDragOut`）或标签右键"在新窗口中打开"，这个文档就移到新的系统窗口里（可以放到第二台显示器）；一个文档窗口可以放几个文档，标签在任意窗口之间拖动合并。每个窗口一个窗格（`DocumentManager::CreatePane`，主窗口是 `DocumentManager::kMainPane`；窗格里的文档轮流用窗格的视口，切换时保存 / 恢复各自的相机，`DocumentManager::MoveDocument` 移动文档，窗格空了自动删掉，对应的窗口在 `MainFrame::SyncDocuments` 里关掉 `MainFrame::PruneDocWindows`）。拖动规则：
  - 任何窗口的标签拖出标签条（`MainFrame::OnDocTabDragOut`）：建新窗口放这个文档（先不激活）；窗口里只有这一个标签时不建新窗口，拖的就是这个窗口。鼠标没松开前原窗口持有捕获，标签条把移动 / 松开经 `TabView::SetOnTornTabDrag` 转给 `MainFrame::OnDocTabTornDrag`（那个窗口的坐标，`MainFrame::PaneToMain` 换算），窗口跟着光标走
  - 拖动中落在任何窗口的标题栏那一行（`MainFrame::FindDropTarget`：在标签上按中线定位置，标签左边插到最前、右边的空白处接到最后）就画插入标记（`TabView::SetDropIndicator`），被拖的窗口让到那条标签条下面免得挡住它；松开就把文档移进那个窗口（`MainFrame::MoveDocumentsTo`），原窗口空了就关掉。拖文档窗口的标题栏是系统的移动循环（`MainFrame::OnDocWindowMoving`），同上，松开时它的文档全部移过去
  - 标签右键：保存、另存为、在新窗口中打开（已独占一个窗口时不可用）、移到主窗口（文档窗口里）。文档窗口的 × / 系统关闭 = 关闭它里面的文档，未保存的逐个询问（`MainFrame::CloseDocWindow` → `MainFrame::CloseDocuments`）
  - 新建 / 打开的文档进用户最后操作的窗口（`DocumentManager::GetNewDocumentPane`）：激活文档时 `DocumentManager::SetActive` 顺带设，在某个窗口里按下 / 激活窗口时 `MainFrame::UseChrome` 也设（当前文档没变时也要设：主窗口空着时点过它、再点回原来的窗口）。打开图纸在后台读取，发起时记下目标窗口，读完加回那里（`MainFrame::OpenDrawings`、`MainFrame::AddOpened`），读取期间点了别的窗口也不跑偏。加入后分帧生成显示（`Editor::BeginProgressiveBuild`，见 [display-performance](display-performance.md) 的"打开图纸：分帧细分"）：状态栏显示"正在生成显示"与进度（`MainFrame::SyncOpenProgress`，那张图纸不在主窗口里显示时不显示），细分期间每帧画完再请求下一帧（主窗口在 `MainFrame::RenderFrame` 帧尾，文档窗口在 `MainFrame::RenderDocWindow`）；`MainFrame::IsOpeningDrawings` 也包括还在生成显示（自测的 `waitOpen` 等到画全）。主窗口里没有文档时显示起始页（见下面"面板每个窗口一套"一条）
  - 窗口与停靠面板的浮动窗口同一套：`AppPlatform::CreateFloatingWindow` 给的 `IFloatingWindow`。界面由窗口自己的 `MiniGUI::UiLayout` 按界面描述文件的 `layouts.document` 一段生成（`MiniGUI::UiLayout::SetLayoutName`，`MainFrame::ApplyDocWindowUi`；命令表与主窗口共用，命令覆盖只由主窗口的 `UiLayout` 应用；读不出或没有这一段时用代码里的最小布局）：`MiniGUI::TitleBar`（"文件"菜单、快速访问、文档标签条——行为与主窗口的相同 `MainFrame::SetupDocTabs`、最小化 / 最大化 / 关闭；按钮经 `MiniGUI::TitleBar::SetWindowActions` 作用于这个窗口，不走主窗口的 `window.*` 命令）、功能区（与主窗口同一份定义；图层 / 线型 / 线宽下拉框 `MainFrame::CreateLayerBar` 每个窗口一份，显示、修改那个窗口显示的文档，绑定在窗口自己的 `BindingSet`）、铺满其余部分的 `MiniGUI::ViewportHost`、停靠区（与主窗口相同：透明的文档区 + 右侧的特性、图层，节点编辑器可打开；浮出的面板在窗口内浮动，布局不保存）、状态栏。界面描述文件改了、切换主题时所有窗口一起（`MainFrame::ReloadUi`、`MainFrame::SetDocWindowsTheme`）。标题区域（含标签条右边的空白）交给系统（`IFloatingWindow::SetCaptionTest` → `MiniGUI::TitleBar::IsCaptionAt`）：系统拖动、双击最大化、贴边分屏；系统移动窗口时经 `IFloatingWindow::SetOnMoving` 上报光标与窗口位置（宿主可以把窗口挪开）。窗口被激活（`IFloatingWindow::SetOnActiveChanged`）它显示的文档就成为当前文档，标题栏按活动 / 非活动样式。视口上浮着与主窗口相同的一层（透明的"洞"里是视图方块、导航条、底部居中的命令行，下面是状态栏含模型 / 布局标签）；每帧经 `IFloatingWindow::SetOnBeforeRender` 在那个窗口布局之后、绘制之前渲染它的视口（`MainFrame::RenderDocWindow`，与主窗口的 `MainFrame::RenderViewport` 相同）。视口是窗格的（`DocumentManager::GetPaneViewport`，相机随显示的文档换），渲染目标另建一个（`AppPlatform::CreateViewportTarget`），渲染器共用，见 [display-performance](display-performance.md) 的"多视口"。CAD 渲染器把视口纹理清成透明，`MiniGUI::ViewportHost` 先铺窗口底色（`Theme::Background`）再画纹理，所以文档窗口的绘图区与主窗口的同色
  - 文档窗口建成与主窗口平级的窗口（`CreateFloatingWindow` 的 `document`：不归主窗口所有、任务栏里有按钮），因为界面的对话框都画在主窗口里：主窗口弹出对话框时置前（`MainFrame::RaiseMainForModal`，每帧检查 `UIContext::HasModal`，`AppPlatform::ActivateMainWindow`）；对话框开着时文档窗口与浮出的面板窗口整个不接受指针（视口、停靠区里的面板、功能区都一样，焦点不会移进去），按键、字符与输入法组合串转给主窗口（Esc / 回车给对话框），点它只把主窗口置前（`MiniGUI::UIContext::SetModalOwner`，所有者是主窗口的上下文，在 `MainFrame::FloatDocument` 与 `FloatingPanelHost::Open` 里设）；经系统关它也只把主窗口置前（`MainFrame::BlockedByModal`）。系统的打开 / 另存为 / 选文件夹对话框则开在用户正在用的那个窗口上（`Win32Window::DialogOwner`：当前激活的本程序窗口，浮出的面板取拥有它的窗口），只禁用那个窗口
  - 每个窗口一套绘图区界面（`MainFrame::ViewChrome`：视图方块、导航条、命令行、状态栏与布局标签）：主窗口的 `MainFrame::m_main` 绑定主视口里的文档，文档窗口的 `DocWindow::chrome` 绑定它的文档（`MainFrame::DocOf`），由同一组函数建出来（`MainFrame::CreateDocumentArea`、`MainFrame::CreateStatusBar`、`MainFrame::CreateCommandLine`、`MainFrame::CreateLayoutTabs`、`MainFrame::CreateViewNavBar`，参数是那一套）；显示随各自的文档同步（主窗口在 `MainFrame::RenderFrame` 里，文档窗口在它自己的帧里 `MainFrame::SyncDocWindowChrome`；布局标签 `MainFrame::SyncLayoutTabs` 逐套同步）；命令行 / 视图方块的显示开关各窗口共用
  - 特性、图层、节点编辑器面板每个窗口一套（`MainFrame::PanelSet`，在 `MainFrame::ViewChrome` 的 `panels` 里；`MainFrame::CreatePropertiesPanel`、`MainFrame::CreateLayerPanel`、`MainFrame::CreateNodePanel` 的参数是那个窗口的一套），显示、修改那个窗口显示的文档（`MainFrame::DocOf`）；功能区的图层下拉框绑定在同一个 `BindingSet` 里。命令状态变化时各窗口一起刷新（`MainFrame::RefreshLayerPanel`、`BindingSet::Refresh`）。节点图的求值、预览、烘焙仍只对当前文档，用的是当前文档所在窗口的节点编辑器（`MainFrame::ActiveNodeEditor`）；"视图 > 面板"的开关作用于用户正在用的窗口（`MainFrame::CurrentDock`）
  - 主窗口里没有文档（一个都没有，或都拖到别的窗口了）时显示起始页（`MainFrame::SyncDocuments`，"返回"行只在主窗口里有文档时显示）；标签条在别的窗口有文档时仍显示，标签可以拖回来；在起始页上新建 / 打开的文档进主窗口
  - "当前文档"（`DocumentManager::GetActive`：特性 / 图层面板、功能区与菜单命令操作的对象）随用户最后操作的窗口切换：在视口里按下 / 滚动 / 敲键，或在某一套界面上按下（主窗口里任何地方经 `MainFrame::CreateUiHost` 的按下回调，对话框开着时除外；文档窗口里任何地方经它的根节点），那个窗口的文档就成为当前文档（`MainFrame::UseChrome`、`MainFrame::ActivateDocumentOf`），功能区、导航条等按钮的命令随后就作用于它。主视口显示的文档（`DocumentManager::GetMainDocument`）是标签条上选中的那个，当前文档在独立窗口里时它不变。视口的指针事件按视口交给那个文档的编辑器（`MainFrame::RouteViewportPointer`，每个视口自己的鼠标状态 `MainFrame::ViewInput`）；按键交给当前文档的编辑器（`MainFrame::OnViewportKey`），在绘图区敲字母进入当前文档所在窗口的命令行（`MainFrame::ChromeOf`）；快捷键登记在每个窗口自己的 `UIContext` 上（`MiniGUI::CommandRegistry::BindShortcuts` 可以绑定几张表），文档窗口的在主窗口开着对话框时不生效（按键都转给了对话框，见上），平时没处理的按键不转给主窗口；Ctrl+K 聚焦用户正在用的那个窗口的命令搜索框（`MainFrame::CurrentRibbon`）；光标旁的动态输入框每个窗口一个（`MainFrame::ViewChrome` 的 `dynInput`），只在当前文档所在的窗口里、光标在它的视口里时显示（`MainFrame::SyncDynamicInput`），工具进行中按数字进那个窗口的输入框，还没有锚点时进它的命令行（`MainFrame::RouteKeyToDynamicInput`）
  - 对等多窗口的计划与进度见 `docs/MultiWindow/PLAN.md`（四个阶段都已完成；对话框仍开在主窗口里）
  - 导航条在视图方块下面的固定位置：绘图区太矮（文档窗口上面还有功能区）放不下时藏起来，免得压住命令行（`MainFrame::SyncViewCube`）
  - 关闭的窗口推迟到下一帧开头销毁（`MainFrame::PurgeDocWindows`）；用户布局文件不保存文档窗口；平台给不出窗口或渲染目标（网页版、Linux 版）时拖出不做什么。自测第 30 节覆盖多窗口（一个窗口几个文档、标签在窗口之间拖动、只有一个标签的窗口拖标签即拖窗口、拖标题栏合并全部文档、窗口让开标签条、主窗口空着时显示起始页、关闭窗口关掉里面的文档）；第 29 节覆盖拖出（拖动中窗口已出现并跟随光标、同一次拖动拖回标签条）、文档窗口自己的一套界面（命令行与提示、布局标签各自绑定）、标题栏（系统标题区域、最大化 / 最小化、激活即成为当前文档、系统移动循环拖回标签条）、平移 / 缩放 / 画线 / 选择、绘图区底色、窗口自己的功能区 / "文件"菜单 / 图层下拉框 / 特性与图层面板 / 快捷键 / 动态输入框、点哪个窗口的功能区哪个文档成为当前文档、重新加载界面描述与切换主题时文档窗口一起、拖回、关闭（对话框开着时不接受编辑、点停靠区的面板焦点不进去、Esc 给对话框）。第 30 节的文档窗口没建出来时跳过本节其余部分（只报失败、不崩溃，后面的节照常跑）
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
- 桌面版的浮动窗口 `apps/Win32/src/Host/Win32FloatingWindow`（停靠面板与文档窗口共用）：无边框（保留系统的缩放边框）；停靠面板是主窗口拥有的工具窗口（总在主窗口之上、随它最小化、不占任务栏，能最大化不能最小化），文档窗口与主窗口平级（任务栏里有按钮，图标取主窗口的；能最小化 / 最大化，标题区域交给 `MiniGUI::Win32Frame`，系统的移动循环里 `WM_MOVING` / `WM_EXITSIZEMOVE` 按"按下点相对窗口左上角不变"换算光标位置上报），每个有自己的 `UIContext`、`Win32Input` 和窗口表面（`IGraphicsDevice::CreateWindowSurface`，D3D11 / D3D12 各自再建一个交换链，共用设备与界面后端），`WM_PAINT` 里自己布局、回调 `IFloatingWindow::SetOnBeforeRender`（文档窗口渲染视口）、绘制、呈现；停靠面板里没处理的按键转给主窗口（`Win32Window::ForwardKeyMessage`，Ctrl+S 等全局快捷键仍有效），文档窗口有自己的快捷键，不转（交给系统，Alt+F4 等）；焦点在浮动窗口里时主标题栏不变灰（`Win32Window::IsOurWindow`）；创建时位置不在任何显示器上就挪到主窗口旁边
- 桌面版的图形设备：`D3D12GraphicsDevice` 或 `D3D11GraphicsDevice`（都可用 WARP）。默认按 `apps/Render/GraphicsDevice.cpp` 里的表的顺序（D3D12 → D3D11 → WARP）创建，失败就试下一个；命令行 `--gpu d3d12|d3d11|warp` 或环境变量 `MINICAD_GPU` 指定（命令行优先，指定的不可用时回退默认顺序）。关于对话框显示实际用的（`AppPlatform::GetGraphicsName`）
- 网页版 `apps/Web/src/Host/WebApp`：图形接口先异步试 WebGPU（`CreateWebGPUGraphicsDeviceAsync`），取不到适配器 / 设备或网址带 `?webgl` 时用 WebGL2（`WebHost::CreateWebGLContext` 建上下文，`GLES3GraphicsDevice` 只使用当前上下文）；同一个画布只能有一种上下文，所以 WebGPU 取到设备后才建画布表面，失败时画布还是干净的；设备就绪（`WebApp::OnGraphicsReady`）之前不建界面、不画帧。`WebHost` 负责画布、DPI（`ResizeObserver` + devicePixelRatio）、`requestAnimationFrame` 按需刷新；打开的文件写进 `/upload`，保存后下载；`beforeunload` 提示未保存；没有窗口按钮（不注册 `window.*` 命令，标题栏就不显示）；`HasSaveDialog()` 为 false，另存为由 `MainFrame::OpenSaveAsDialog` 询问文件名、格式与版本；浏览器交互（文件选择、目录授权、下载）写在 `EM_JS` 里
- Linux 版 `apps/Linux/src/Host/LinuxApp`：SDL3 窗口（系统标题栏，`HasWindowControls()` 为 false）+ 图形设备：默认先建带 `SDL_WINDOW_VULKAN` 的窗口试 `VulkanGraphicsDevice`（表面经 `LinuxApp::VulkanWindow` 给出的回调由 SDL3 建），失败就重建带 `SDL_WINDOW_OPENGL` 的窗口和 EGL / GLES3 上下文用 `GLES3GraphicsDevice`（`EndUiPass` 里 `SDL_GL_SwapWindow`）；`--gpu vulkan|gl` / `MINICAD_GPU` 指定；`SDL_WaitEvent` 事件循环，重绘请求、后台任务完成、`Post` 都经 SDL 自定义事件唤醒；输入 / 剪贴板 / 界面字体在 `src/UI/Platform/SDL`（`SDLInput`、`SDLClipboard`、`LoadSDLUIFonts`）；文件对话框用 SDL3（portal / zenity），另存为在界面线程上等结果；切回窗口时检查界面描述文件是否改过
- 网页版输入：键盘按 `KeyboardEvent.keyCode` 映射（与 Windows 虚拟键码相同）；文本框获得焦点时，文字与输入法从跟随光标的隐藏 textarea 送来；Ctrl+V 等 `paste` 事件带着文字到达后再交给界面

## 测试与排查

- `MiniCADWin.exe --selftest`（注入窗口消息的端到端自测，应全部通过；`--full` 加跑 20 万条线的耗时测量）；改了渲染后端时 `--gpu d3d12`、`--gpu d3d11` 各跑一遍。第 31 节覆盖面板浮出成独立窗口（拖出、两个面板先后浮出时内容不丢、拖一个窗口的标题栏到另一个上合并成标签、最大化按钮）；停靠逻辑本身在 `MiniGUITests` 的 `Dock_*`（`Dock_FloatWindowsInSeparateContexts` 用每个窗口一个 `UIContext` 的假宿主模拟桌面版）
- D3D12 的调试版自动打开调试层（系统装有 Graphics Tools 时），警告与错误以 `[D3D12]` 开头打到 stderr：调试版跑一遍自测，stderr 里不应有这样的行
- MiniGUI 控件：`MiniGUITests`、`MiniGUIGolden`（截图对比：软件光栅 vs 基准，D3D11 / D3D12 vs 软件光栅；CI 不跑）
- 网页宿主的 JS：`tests/Web/*.cjs`；整体行为在浏览器里看（`build_wasm.bat serve`），两个图形后端都要看：默认 WebGPU，网址加 `?webgl` 是 WebGL2，关于对话框显示实际用的是哪个。浏览器里没法操作系统的文件对话框，验证时可以在页面里替换 `HTMLInputElement.prototype.click` / `showDirectoryPicker`，从本地服务器取样例文件
