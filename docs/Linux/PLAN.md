# Linux 版：去掉 SDL3，直接用 X11 / Wayland

> 状态：计划
> 定位：Linux 版的窗口、输入、剪贴板、拖放、文件对话框都改用系统原生接口，不再依赖 SDL3；同时补齐与 Windows 版的差距：
> 自绘标题栏（`HasWindowControls()` 为 true）、停靠面板与文档能拖出成独立窗口（`AppPlatform::CreateFloatingWindow`）。
> 已定：X11 与原生 Wayland 都做，运行时按会话自动选；渲染仍是现有的 `VulkanGraphicsDevice` / `GLES3GraphicsDevice`。

---

## 1. 为什么换

当前与Windows版的差距：

| 项目 | Windows 版 | 现在的 Linux 版 | 原因 |
|---|---|---|---|
| 标题栏 | 自绘，与菜单栏 / 文档标签连成一体（`Win32Frame` + `TitleBar::IsCaptionAt`） | 系统标题栏，`HasWindowControls()` 为 false | 当初选了先跑起来的方案 |
| GNOME Wayland 下的标题栏 | — | 没有（CI 编 SDL3 没带 libdecor，Wayland 不给服务端装饰） | 打包配置 |
| 浮动窗口 | 面板、文档拖出主窗口（`Win32FloatingWindow`） | 没有，`CreateFloatingWindow` 返回空 | 没实现；Vulkan / GLES3 设备也没实现 `CreateWindowSurface` |
| OLE 预览 | GDI 回放 WMF / EMF | 没有 `RasterizeOle` | 不在本计划内 |

SDL3 能做到自绘标题栏（无边框窗口 + `SDL_SetWindowHitTest`）和多窗口，但用户决定直接用原生接口，好处是：
- 标题栏拖动、贴边、缩放边框、输入法候选窗位置、浮动窗口的移动循环都能按需要细调，不受 SDL 的抽象限制
- 少一个要从源码编译的依赖（CI 现在每次缓存未命中都要编 SDL3）
- 与 Win32 宿主的结构对齐：平台层（MiniGUI）管窗口框架与输入，宿主管 `AppPlatform`

## 2. 依赖

都是发行版自带的包，动态链接（每个桌面系统都装着运行库）：

| 用途 | 库 | Ubuntu / Debian 开发包 |
|---|---|---|
| X11 窗口、事件、XIM 输入法、选择区（剪贴板、拖放） | Xlib | `libx11-dev` |
| X11 光标主题 | Xcursor | `libxcursor-dev` |
| Wayland 客户端、光标主题、EGL 窗口 | wayland-client / wayland-cursor / wayland-egl | `libwayland-dev` |
| Wayland 协议 XML（构建时由 `wayland-scanner` 生成代码） | xdg-shell、xdg-decoration、fractional-scale-v1、viewporter、cursor-shape-v1、text-input-unstable-v3 | `wayland-protocols`（≥ 1.32，Ubuntu 24.04 有 1.45） |
| 键盘布局（Wayland 必需；X11 只用它的 keysym 转 UTF-8） | xkbcommon | `libxkbcommon-dev` |
| GL 上下文 | EGL + GLES3（已有） | `libegl-dev`、`libgles-dev` |
| Vulkan（已有，可选） | 加载器 + glslangValidator | `libvulkan-dev`、`glslang-tools` |

- 不引入 GTK / Qt / libdecor / libdbus
- CMake：X11 与 Wayland 各自可选，至少要有一个才编 `MiniCADLinux`；分别定义 `MINICAD_LINUX_X11` / `MINICAD_LINUX_WAYLAND`（只在 `src/UI/Platform/Linux` 与 `apps/Linux` 里用，`apps/Common` 仍然不写 `#ifdef`）
- 运行时选择：有 `WAYLAND_DISPLAY` 且连得上 → Wayland，否则 X11；环境变量 `MINICAD_WINDOWING=x11|wayland` 强制（排查问题、对比测试）

## 3. 结构

```
src/UI/Platform/Linux/          目标 MiniGUI_Linux（取代 MiniGUI_SDL）
  LinuxDisplay.h/.cpp           抽象：连接、事件循环、唤醒、定时器、剪贴板、窗口工厂；Open() 按会话选 X11 / Wayland
  LinuxWindow.h/.cpp            抽象：一个顶层窗口 + 共用的输入翻译、标题区域 / 缩放边框的判断、光标与输入法同步
  LinuxKeys.h/.cpp              evdev 扫描码 → MiniGUI::Key（与键盘布局无关，X11 / Wayland 共用）、修饰键
  LinuxFonts.h/.cpp             界面字体（由 SDLFonts 改名，内容不变）
  X11/X11Display.*、X11Window.*、X11Clipboard.*、X11Dnd.*、X11Ime.*
  Wayland/WaylandDisplay.*、WaylandWindow.*、WaylandClipboard.*、WaylandTextInput.*、WaylandCursor.*
  Wayland/Protocols/            wayland-scanner 生成的 *-protocol.c/.h（构建目录里，不进仓库）

apps/Linux/src/Host/
  LinuxApp.*                    AppPlatform：图形设备、MainFrame、后台任务、文件对话框、资源 / 用户目录（重写，去掉 SDL）
  LinuxFloatingWindow.*         IFloatingWindow（对应 Win32FloatingWindow）
  LinuxGraphics.*               由 LinuxWindow 的原生句柄建 NativeWindow：Vulkan 表面（xlib / wayland）、EGL 显示与窗口表面
  LinuxDialogs.*                文件对话框（见 §6）
  LinuxMain.cpp                 入口（参数不变）
```

### 3.1 平台层接口（草案）

```cpp
namespace MiniGUI
{
    struct LinuxWindowParams
    {
        std::string title;
        int  width = 0, height = 0;        // 逻辑像素
        bool decorated = false;             // false = 自绘标题栏（主窗口、文档窗口、浮出的面板都是）
        enum class Role { Main, Document, Tool } role = Role::Main;
        LinuxWindow* owner = nullptr;       // Tool：总在 owner 之上（X11 的 WM_TRANSIENT_FOR，Wayland 的 xdg_toplevel.set_parent）
        bool clientMove = false;            // 拖标题栏时自己移动窗口并回报光标（浮动窗口合并要用），否则交给窗口管理器
    };

    class LinuxWindow
    {
    public:
        struct Listener
        {
            virtual void OnResize(int pixelWidth, int pixelHeight, float scale) {}
            virtual void OnExpose() {}
            virtual void OnCloseRequested() {}
            virtual void OnActiveChanged(bool active) {}
            virtual void OnStateChanged() {}                        // 最大化 / 还原
            virtual void OnDrop(std::vector<std::string> paths) {}
            virtual void OnMoving(bool done, Vec2 cursorRoot, Rect& windowRoot) {}   // clientMove 时
        };

        void AttachUI(UIContext* ui);                               // 输入翻译到这个 UIContext
        void SetCaptionTest(std::function<bool(Vec2 logical)> test);
        void SetTitle(const std::string& utf8);
        void SetIcon(const Image& rgba);
        void Show(bool visible); void Activate(); void Minimize(); void ToggleMaximize();
        bool IsMaximized() const; bool IsActive() const;
        int  PixelWidth() const; int PixelHeight() const; float Scale() const;
        bool SupportsPositions() const;                             // Wayland 为 false
        Rect GetRootRect() const; void SetRootRect(const Rect&);     // 屏幕像素（X11）
        NativeHandles GetNativeHandles() const;                     // Display* + Window，或 wl_display* + wl_surface*
        void SyncInput();                                           // 每帧呈现后：光标形状、输入法开关与候选窗位置
    };

    class LinuxDisplay
    {
    public:
        static std::unique_ptr<LinuxDisplay> Open();               // 按 MINICAD_WINDOWING / WAYLAND_DISPLAY / DISPLAY
        virtual std::unique_ptr<LinuxWindow> CreateWindow(const LinuxWindowParams&, LinuxWindow::Listener*) = 0;
        void Poll(int timeoutMs);           // 等待并分发：显示服务器的 fd + 唤醒 eventfd + 最近的定时器
        void Wakeup();                      // 线程安全
        IClipboard& GetClipboard();
        uint32_t NowMs() const;             // CLOCK_MONOTONIC，UIContext::SetClock 与双击判断用
    };
}
```

- 输入翻译写在 `LinuxWindow` 基类里（X11 / Wayland 只负责把原始事件换成"物理像素坐标 + evdev 码 + 文字"交给基类），翻译规则照搬 `SDLInput`：不在文本框时可打印的按键直接作为文字（在视口里直接键入命令）；在文本框时打开输入法
- 按键用 evdev 扫描码映射（X11 的 keycode 减 8 就是 evdev 码），俄文等非拉丁布局下 Ctrl+C 等快捷键照常；文字另由 keysym 经 xkbcommon 转 UTF-8
- 定时器（悬浮提示、光标闪烁、Wayland 的按键重复）放在 `LinuxDisplay` 的定时器表里，`Poll` 的超时取最近的到期时间，不开定时器线程
- 宿主循环与现在相同：`display->Poll(redraw ? 0 : -1)` → 执行 `Post` 进来的任务 → 有重绘请求就 `MainFrame::RenderFrame`

### 3.2 自绘标题栏（与 `Win32Frame` 对应）

在 `LinuxWindow` 基类里统一判断，指针事件先过这一层再交给界面：

| 位置（未最大化时） | 光标 | 按下左键 |
|---|---|---|
| 四边四角 `GetResizeBorder()`（6 逻辑像素）以内 | 对应的双向箭头 | 交给窗口管理器缩放（X11 `_NET_WM_MOVERESIZE`，Wayland `xdg_toplevel.resize`） |
| `SetCaptionTest` 判为标题区域 | 箭头 | 交给窗口管理器移动（X11 `_NET_WM_MOVERESIZE`，Wayland `xdg_toplevel.move`）；双击切换最大化；Wayland 上右键 `xdg_toplevel.show_window_menu` |
| 其余 | 界面决定 | 交给界面 |

- X11：`_MOTIF_WM_HINTS` 去掉装饰；最大化 / 还原经 `_NET_WM_STATE`，最小化 `XIconifyWindow`，状态从 `_NET_WM_STATE` 属性变化读回；窗口管理器不支持 `_NET_WM_MOVERESIZE` 时（Xvfb 没有窗口管理器）自己抓指针移动 / 缩放
- Wayland：不请求服务端装饰（`zxdg_decoration_manager_v1` 有就设为 client_side，GNOME 本来就没有）；激活、最大化状态从 `xdg_toplevel.configure` 的 states 读；`xdg_surface.set_window_geometry` 设为整个窗口
- 主标题栏的三个按钮已经由 `MainCommands.cpp` 在 `HasWindowControls()` 为 true 时注册，不用改 `apps/Common`
- 窗口图标：X11 写 `_NET_WM_ICON`；Wayland 设 `app_id` 为 `io.github.minicad.MiniCAD`，图标随 `.desktop` 文件（打包时附带，§8）

### 3.3 高分屏

- X11：缩放 = `Xft.dpi / 96`（`XResourceManagerString`，GNOME / KDE 都会写），没有时 1；监视根窗口的 `RESOURCE_MANAGER` 属性，改了就 `OnResize` 重新通知
- Wayland：有 `wp_fractional_scale_v1` + `wp_viewporter` 时按分数缩放（缓冲区像素 = 逻辑尺寸 × 缩放，viewport 目标设为逻辑尺寸）；没有时用 `wl_surface.preferred_buffer_scale`（compositor v6）或所在输出的整数缩放 + `set_buffer_scale`
- 界面缩放与现在一样经 `MainFrame::OnResize(逻辑宽, 逻辑高, 缩放)`

## 4. 输入法、剪贴板、拖放、光标

| 功能 | X11 | Wayland |
|---|---|---|
| 输入法 | XIM：优先 on-the-spot（`XIMPreeditCallbacks`，组合串画在我们的文本框里），不支持时 over-the-spot（`XIMPreeditPosition`，`XNSpotLocation` 跟随文字光标），再不行 root；不在文本框时 `XUnsetICFocus` | `zwp_text_input_v3`：进文本框 enable + `set_cursor_rectangle` + commit，preedit / commit_string 在 `done` 时一起交给 `UIContext::Composition*` / `TextInput`；没有该协议时只有 xkbcommon 的按键文字（没有输入法） |
| 剪贴板 | `CLIPBOARD` 选择区，提供 `UTF8_STRING` / `text/plain;charset=utf-8` / `STRING`；读取时 `XConvertSelection` 后在事件里等最多 500 ms；不支持 INCR（超大文本） | `wl_data_device`：写 = 建 data_source、`set_selection`（用最近一次输入的 serial）；读 = 当前 offer 的 `receive` 到管道，限时读完；自己是源时直接返回自己的文字 |
| 拖放文件 | XDND v5：`XdndAware`，只接受 `text/uri-list`，`file://` 解码成 UTF-8 路径 | `wl_data_device` 的 enter / drop，接受 `text/uri-list` |
| 光标 | Xcursor 按主题名加载（`default`、`text`、`pointer`、`ew-resize` …，旧主题回退到 `left_ptr`、`xterm` 等）；隐藏用空白光标 | 有 `wp_cursor_shape_v1` 就按形状设，没有时 wayland-cursor 读 `XCURSOR_THEME` / `XCURSOR_SIZE` 主题 |
| 双击间隔 | 500 ms（与 SDL 版相同） | 同左 |
| 按键重复 | 服务器发 | 客户端自己按 `wl_keyboard.repeat_info` 定时重复 |

## 5. 渲染：多窗口

`IGraphicsDevice::CreateWindowSurface` 现在只有 D3D11 / D3D12 实现，Linux 的两个后端都要补：

- **Vulkan**：把交换链从 `VulkanGraphicsDevice` 拆成 `VulkanSwapchain`（表面、交换链、图像视图、每帧的 imageAvailable 与每张图像的 renderFinished 信号量、Acquire / BeginPass / Present / Resize），主窗口与浮动窗口各一个；浮动窗口的表面经 `NativeWindow::createVulkanSurface` 在已有实例上建，先用 `vkGetPhysicalDeviceSurfaceSupportKHR` 确认能在当前队列上呈现，不能就返回空（面板在主窗口里浮动）。每个窗口 Present 时各自 `SubmitFrame`，与现在 D3D 的多窗口相同
- **GLES3**：一个 EGL 上下文、每个窗口一个 EGL 窗口表面。`NativeWindow` 加两个回调 `glMakeCurrent` / `glSwapBuffers`（宿主实现：`eglMakeCurrent(显示, 表面, 表面, 上下文)` / `eglSwapBuffers`）；`BeginUiPass` 与窗口表面的 `BeginPass` 先切到自己的表面再绑 0 号帧缓冲。主窗口交换间隔 1，浮动窗口 0（几个窗口不轮流等垂直同步）
- 窗口表面不在 `apps/Render` 里建（与现在一致）：`apps/Linux/src/Host/LinuxGraphics` 按 `LinuxWindow::GetNativeHandles()` 调 `vkCreateXlibSurfaceKHR` / `vkCreateWaylandSurfaceKHR`，实例扩展为 `VK_KHR_surface` + `VK_KHR_xlib_surface` 或 `VK_KHR_wayland_surface`；EGL 用 `eglGetPlatformDisplay`（`EGL_PLATFORM_X11_KHR` / `EGL_PLATFORM_WAYLAND_KHR`），Wayland 的窗口表面经 `wl_egl_window`，改尺寸时 `wl_egl_window_resize`
- 风险：Mesa 的 Wayland WSI 在 FIFO 模式下，窗口不可见（最小化、在别的工作区）时 `vkQueuePresentKHR` 可能一直等帧回调。我们按需重绘，影响有限；实测卡住的话 Wayland 上改用 MAILBOX（有的话），或者窗口不可见时不呈现

## 6. 浮动窗口

`LinuxFloatingWindow` 实现 `IFloatingWindow`，结构照 `Win32FloatingWindow`：自己的 `UIContext`、自己的窗口表面、与主窗口共用设备 / 渲染器 / 界面后端。

| 要求（`AppPlatform.h` 的约定） | X11 | Wayland |
|---|---|---|
| 位置用主窗口客户区的逻辑坐标 | `XTranslateCoordinates` 换算到根窗口 | **做不到**：Wayland 客户端既不能知道也不能指定窗口的屏幕位置 |
| 停靠面板：总在拥有者之上、不占任务栏 | `WM_TRANSIENT_FOR` + `_NET_WM_WINDOW_TYPE_UTILITY` + `_NET_WM_STATE_SKIP_TASKBAR` | `set_parent` 能做到置顶，但位置不行 |
| 文档窗口：与主窗口平级，任务栏有按钮 | 普通顶层窗口 | 同左，但拖回主窗口合并要靠位置 |
| 拖标题栏移动时上报光标、松开时合并（`SetOnMoving`） | `clientMove`：抓指针、自己 `XMoveWindow`，移动中 / 松开时回调 | 做不到（移动由合成器完成，不告诉客户端位置） |

所以：**X11 上提供浮动窗口；Wayland 上 `CreateFloatingWindow` 仍返回空**，面板在主窗口里浮动，与网页版相同。这是 Wayland 协议的限制（`xdg-toplevel-drag` 等协议还没有主流合成器普遍支持），等普及了再看。GNOME / KDE 的 Wayland 会话里想要浮动窗口，可以 `MINICAD_WINDOWING=x11` 走 XWayland。

## 7. 其余平台服务

| `AppPlatform` | 做法 |
|---|---|
| 文件对话框 | 依次试 zenity、kdialog（`posix_spawn` 子进程，后台线程读标准输出，结果 `Post` 回界面线程）；都没有时提示改用拖放或命令行（与现在相同）。另存为 / 导出要当场返回路径：在界面线程上一边 `Poll` 一边等子进程结束，期间只处理窗口事件不画界面（同现在的 SDL 版）。xdg-desktop-portal 要走 D-Bus，不引入 libdbus 就不做，以后需要再议 |
| `OpenUrl` | `xdg-open` 子进程 |
| `HttpGet` | 不变（系统的 curl 命令） |
| 资源目录 | `/proc/self/exe` 所在目录（取代 `SDL_GetBasePath`） |
| 用户数据、系统语言、系统字体目录、后台线程 | 不变 |
| `ActivateMainWindow` | X11 `_NET_ACTIVE_WINDOW`；Wayland 有 `xdg_activation_v1` 时用，没有就不动 |

## 8. 构建、CI、发布

- `cmake/MiniGUI.cmake`：删 `MiniGUI_SDL`，加 `MiniGUI_Linux`；找 X11（`find_package(X11)`，要 `X11_Xcursor_FOUND`）、Wayland（`pkg_check_modules` wayland-client / wayland-cursor / wayland-egl / xkbcommon，`wayland-scanner` 与 `wayland-protocols` 的 pkgdatadir），在构建目录生成协议代码
- `apps/Linux/CMakeLists.txt`：条件改成"`MiniGUI_Linux` 存在且有 GLES3"，链接 `EGL`、`Vulkan::Vulkan`（有时）
- `.github/workflows/build.yml`：删掉 `SDL3_VERSION` / `SDL3_SHA256`、缓存与编译 SDL3 两步；apt 装 `libx11-dev libxcursor-dev libwayland-dev wayland-protocols libxkbcommon-dev`（EGL / GLES / Vulkan 的包已经在装）
- 发布包：多一个 `minicad.desktop` 与图标（Wayland 下任务栏图标靠它，可选安装到 `~/.local/share/applications`）；运行库要求改为系统的 libX11、libXcursor、libwayland-client / cursor / egl、libxkbcommon（桌面发行版默认都有）
- 文档：README / README.zh-CN 的 Linux 构建说明、`THIRD_PARTY_NOTICES.md` 删 SDL3 一节、`AGENTS.md` 的目录与输入流程、`.agents/features/main-window.md`、`.agents/skills/test/SKILL.md`、`docs/Render/PLAN.md` 阶段 2 的结论（标注"已改为原生接口，见本文"）；翻译文件里 `SDL_*` 开头的日志随代码删除后由 `tools/i18n_extract.py` 清掉

## 9. 阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| 1 | 平台层骨架 + X11：窗口、事件循环、定时器、输入翻译、光标、剪贴板、XIM、拖放；宿主改用它，Vulkan / GLES3 表面由宿主建；系统标题栏（`decorated = true`）先跑通 | qmlinux 上 Xvfb 截图与现在的 SDL 版一致；`ctest` 照过；输入法用 fcitx5 在 Xvfb 里试组合串 |
| 2 | X11 自绘标题栏：`HasWindowControls()` 为 true，缩放边框、拖动、双击最大化、最小化 / 最大化 / 关闭按钮 | 在有窗口管理器的环境（qmlinux 装 openbox 或 xfwm4 跑在 Xvfb 上）里拖动、缩放、贴边 |
| 3 | 多窗口渲染：`VulkanSwapchain` 拆分 + `CreateWindowSurface`（Vulkan / GLES3），`LinuxFloatingWindow`，X11 上面板与文档拖出、拖回合并 | 拖出面板、文档；改尺寸；关闭；拖回主窗口合并；两种后端各试一遍 |
| 4 | Wayland：xdg-shell、自绘标题栏、分数缩放、text-input-v3、剪贴板、拖放、光标；浮动窗口返回空 | qmlinux 上 `weston --backend=headless` 跑通并用 weston 的截图协议截图；用户在实机 GNOME / KDE 上试（含 VMware 里的 Ubuntu 26.04，原来报没有标题栏的那台） |
| 5 | 删除 SDL3：`src/UI/Platform/SDL`、CMake、CI、文档、第三方声明 | CI 的 `Linux (Vulkan / GLES3)` 通过；发布包在干净的 Ubuntu 24.04 上能运行 |

阶段 1 到 3 只动 X11，做完就能替换 SDL 版发布（Wayland 会话经 XWayland 运行，这时已经有自绘标题栏，原来 GNOME Wayland 没有标题栏的问题也就没了）；阶段 4 可以晚一些。

