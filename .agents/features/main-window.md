---
covers: assets/ui/minicad_ui.json, apps/Common/src/GUI/MainFrame.*, apps/Common/src/GUI/MainCommands.cpp, apps/Common/src/GUI/CommandLine.cpp, apps/Common/src/GUI/MainPanels.cpp, apps/Common/src/GUI/LayerPanels.cpp, apps/Common/src/GUI/StartPage.cpp, apps/Common/src/GUI/ViewNavBar.cpp, apps/Common/src/GUI/AppPlatform.h, apps/Win32/src/Host/Win32Window.*, apps/Web/src/Host/*, apps/Linux/src/Host/*, src/UI/Platform/SDL/*
---

# 主窗口界面

## 概述

整个界面由 MiniGUI 绘制，代码在 `apps/Common/src/GUI`，桌面版、网页版与 Linux 版共用；平台相关的一律经 `AppPlatform`。界面描述文件 `assets/ui/minicad_ui.json` 定义菜单、功能区、停靠布局，保存后自动重新加载。

## 布局

参照 SolidCraft（`docs/MiniGUI/UI-REFRESH-PLAN.md`）：

- 绘图区（面板 `canvas`）铺满标题栏以下；功能区、停靠面板、状态栏都是浮在上面的圆角卡片
- 停靠：浮动停靠区 `"floating": true`；文档区 `documents` 是透明的"洞"，里面放视图方块、竖排导航条（`GUI/ViewNavBar.cpp`）和底部居中的命令行
- 浮动面板：特性 / 图层 / 节点等面板的标签拖到停靠区外松开或双击标签即浮出（`MiniGUI::DockSpace::FloatPanel`），拖标题栏回停靠区停靠、双击标题栏回原处；位置随用户布局保存（`layout.json` 里 `dock.floatingPanels`）。没有平台宿主时是窗口内的浮动面板（`MiniGUI::FloatingPanel` 弹层，网页版即如此）
- "全部显示"按没被挡住的区域留边（`Camera::SetFitInsets`，`MainFrame::UpdateFitInsets`）
- 标题栏：MiniCAD、首页、"文件"菜单、保存 / 撤销 / 重做、文档标签（面板 `doctabs`）、右侧 AI / 设置 / 帮助 / 版本（面板 `appbar`）
- 功能区（`MiniGUI::Ribbon`，描述文件顶层 `ribbons`）：选项卡行右端是收起按钮和命令搜索框（Ctrl+K）；图标由 `tools/make_icons.py` 生成
- 起始页 `GUI/StartPage.cpp`（面板 `start`，显示时隐藏标题栏以下的其余界面；首页键 `view.start`）
- 状态栏左侧是模型 / 布局标签

## 主要文件

- `MainFrame`：文档标签、视口输入 → `Editor`、逐帧渲染 `RenderFrame`、尺寸变化 `OnResize`（由宿主调用）；后台打开图纸 `OpenDrawings` → `DocumentManager::Load`（其他线程）→ 界面线程 `DocumentManager::Add`，状态栏显示进度；`RunSelfTest` 只在桌面版里定义
- `MainCommands.cpp`：所有操作注册为 MiniGUI 命令（id、标签、图标、快捷键、执行、可用条件）
- `CommandLine.cpp`：命令行；界面层命令（`NODES`、`NAVVCUBE`、`XREF` 等）在这里处理，其余交给 `Editor::RunCommand`
- `MainPanels.cpp`、`LayerPanels.cpp`、`DynamicInput.cpp`、`EditorDialogs.cpp`、`StatusBarView`、`ViewCube`：特性 / 图层面板、动态输入、对话框、状态栏、视图方块
- 图层排序：面板、特性面板、工具栏下拉框都按名称的自然顺序（`MainFrame::SortedLayerIds`，0 层最前，按 "ID:名称" 缓存，增删改名才重排）；图层面板有搜索框（不分大小写，Esc 清空），点"名称"表头切换升降序
- 停靠卡片：激活的标签与面板内容同色连成一片（`DockGroup::BodyColor` 读内容 `Panel` 的背景色，标题条深一级）

## 宿主

- 图形：各宿主都只经 `IGraphicsDevice`（`apps/Render/IGraphicsDevice.h`，由 `CreateGraphicsDevice` 按平台创建）使用图形接口：渲染器、界面后端、视口渲染目标、开始界面通道、呈现、等 GPU、读回像素（自测截图）都从它来；`AppPlatform` 的图形函数直接转给它
- 桌面版 `apps/Win32/src/Host/Win32Window`：无边框窗口（`MiniGUI::Win32Frame`）、图形设备、消息循环、监视界面描述文件；另存为用系统对话框（"保存类型"里区分 2018 与 2013/2014）
- 桌面版的图形设备：`D3D12GraphicsDevice` 或 `D3D11GraphicsDevice`（都可用 WARP）。默认按 `apps/Render/GraphicsDevice.cpp` 里的表的顺序（D3D12 → D3D11 → WARP）创建，失败就试下一个；命令行 `--gpu d3d12|d3d11|warp` 或环境变量 `MINICAD_GPU` 指定（命令行优先，指定的不可用时回退默认顺序）。关于对话框显示实际用的（`AppPlatform::GetGraphicsName`）
- 网页版 `apps/Web/src/Host/WebApp`：图形接口先异步试 WebGPU（`CreateWebGPUGraphicsDeviceAsync`），取不到适配器 / 设备或网址带 `?webgl` 时用 WebGL2（`WebHost::CreateWebGLContext` 建上下文，`GLES3GraphicsDevice` 只使用当前上下文）；同一个画布只能有一种上下文，所以 WebGPU 取到设备后才建画布表面，失败时画布还是干净的；设备就绪（`WebApp::OnGraphicsReady`）之前不建界面、不画帧。`WebHost` 负责画布、DPI（`ResizeObserver` + devicePixelRatio）、`requestAnimationFrame` 按需刷新；打开的文件写进 `/upload`，保存后下载；`beforeunload` 提示未保存；没有窗口按钮（不注册 `window.*` 命令，标题栏就不显示）；`HasSaveDialog()` 为 false，另存为由 `MainFrame::OpenSaveAsDialog` 询问文件名、格式与版本；浏览器交互（文件选择、目录授权、下载）写在 `EM_JS` 里
- Linux 版 `apps/Linux/src/Host/LinuxApp`：SDL3 窗口（系统标题栏，`HasWindowControls()` 为 false）+ 图形设备：默认先建带 `SDL_WINDOW_VULKAN` 的窗口试 `VulkanGraphicsDevice`（表面经 `LinuxApp::VulkanWindow` 给出的回调由 SDL3 建），失败就重建带 `SDL_WINDOW_OPENGL` 的窗口和 EGL / GLES3 上下文用 `GLES3GraphicsDevice`（`EndUiPass` 里 `SDL_GL_SwapWindow`）；`--gpu vulkan|gl` / `MINICAD_GPU` 指定；`SDL_WaitEvent` 事件循环，重绘请求、后台任务完成、`Post` 都经 SDL 自定义事件唤醒；输入 / 剪贴板 / 界面字体在 `src/UI/Platform/SDL`（`SDLInput`、`SDLClipboard`、`LoadSDLUIFonts`）；文件对话框用 SDL3（portal / zenity），另存为在界面线程上等结果；切回窗口时检查界面描述文件是否改过
- 网页版输入：键盘按 `KeyboardEvent.keyCode` 映射（与 Windows 虚拟键码相同）；文本框获得焦点时，文字与输入法从跟随光标的隐藏 textarea 送来；Ctrl+V 等 `paste` 事件带着文字到达后再交给界面

## 测试与排查

- `MiniCADWin.exe --selftest`（注入窗口消息的端到端自测，应全部通过；`--full` 加跑 20 万条线的耗时测量）；改了渲染后端时 `--gpu d3d12`、`--gpu d3d11` 各跑一遍
- D3D12 的调试版自动打开调试层（系统装有 Graphics Tools 时），警告与错误以 `[D3D12]` 开头打到 stderr：调试版跑一遍自测，stderr 里不应有这样的行
- MiniGUI 控件：`MiniGUITests`、`MiniGUIGolden`（截图对比：软件光栅 vs 基准，D3D11 / D3D12 vs 软件光栅；CI 不跑）
- 网页宿主的 JS：`tests/Web/*.cjs`；整体行为在浏览器里看（`build_wasm.bat serve`），两个图形后端都要看：默认 WebGPU，网址加 `?webgl` 是 WebGL2，关于对话框显示实际用的是哪个。浏览器里没法操作系统的文件对话框，验证时可以在页面里替换 `HTMLInputElement.prototype.click` / `showDirectoryPicker`，从本地服务器取样例文件
