# 多平台渲染后端

> 状态：阶段 0 完成（2026-10-26，分支 `render-backends`）；阶段 1 完成（2026-10-26），Windows 默认 D3D12 → D3D11 → WARP；阶段 2 Linux 宿主 + OpenGL 完成（2026-10-26，只在 Xvfb 软件渲染下验证过）；阶段 5 网页 WebGPU 完成（2026-10-26）；阶段 3（Linux Vulkan）、阶段 4（macOS）未开始。三个阶段在各自的分支上做完后合并进 `render-backends`。
> 定位：保留现有渲染接口，每个平台编译各自的原生后端；网页上用浏览器的 WebGPU，不支持时用 WebGL2。取代桌面上的 wgpu 路线（见 `docs/WebGPU/PLAN.md`：原型可行，但多一层依赖，D3D12 后端启动就多约 500 MB 内存，已放弃）。

---

## 1. 目标

| 平台 | 默认 | 回退 | 没有显卡 |
|---|---|---|---|
| Windows | D3D12（新增） | D3D11（现有，保留） | WARP（D3D12 / D3D11 都支持） |
| Linux | Vulkan（新增） | OpenGL（复用网页版的 GLES3 渲染器，EGL） | Mesa 的 lavapipe / llvmpipe，驱动层自动选，不用写代码 |
| macOS | Metal（新增） | — | Metal 在所有支持的 Mac 上都有 |
| 网页 | WebGPU（新增，浏览器支持时） | WebGL2（现有）；网址加 `?webgl` 强制用它 | 浏览器负责 |

- 每个平台只编译自己的后端：Windows 不编 Vulkan / Metal，Linux 不编 D3D，以此类推
- 运行时按"默认 → 回退 → 软件"的顺序创建，失败就试下一个；桌面版用 `--gpu <名称>` 或环境变量 `MINICAD_GPU` 指定后端，网页版用网址参数（排查问题、对比测试用）
- 网页版的 WebGPU 是浏览器自带的接口，经 Emscripten 的 `webgpu.h` 调用，不引入 wgpu-native / Dawn 这类库；wgpu 原型的内存问题出在 wgpu-native 自己的分配器，不适用于浏览器
- 不做：桌面上的第三方抽象层（wgpu、Dawn、bgfx 等）

## 2. 接口：不变的与新增的

**不变**（核心库、MiniGUI、`apps/Common` 都不动）：

| 接口 | 位置 | 作用 |
|---|---|---|
| `IRenderer` | `src/Render/IRenderer.h` | CAD 视口渲染器：缓存提交、字形实例化、缓存层、光栅图像、7 号色反转 |
| `IRenderTarget` | `src/Render/IRenderTarget.h` | 离屏渲染目标；原生句柄按 `void*` 传递 |
| `MiniGUI::IRenderBackend` | `src/UI/Render/IRenderBackend.h` | 界面后端：纹理 + `DrawData` |
| `AppPlatform` | `apps/Common/src/GUI/AppPlatform.h` | `GetRenderer` / `GetUiBackend` / `GetViewportTarget` / `WaitForGpu` / `GetGraphicsName` 已经与图形接口无关 |

**新增：宿主侧的图形设备接口**（只在 `apps/` 里，核心库看不到）。现在 `Win32Window` 直接持有 D3D11 的设备、交换链，换成两个后端就要到处 `if`（WebGPU 原型就是这样做的）。改为每个后端实现一份：

已实现（`apps/Render/IGraphicsDevice.h`）：

```cpp
class IGraphicsDevice
{
public:
    virtual bool Initialize(const NativeWindow& window, int width, int height) = 0;   // HWND；网页不用
    virtual void Resize(int width, int height) = 0;
    virtual void BeginUiPass() = 0;                   // 绑定窗口的帧缓冲并清屏，界面后端画到这里
    virtual void Present() = 0;
    virtual void WaitIdle() {}
    virtual void ReleaseFreeMemory() {}
    virtual IRenderer&               GetRenderer()  = 0;     // 设备持有渲染器与界面后端，生命周期跟着设备
    virtual MiniGUI::IRenderBackend& GetUiBackend() = 0;
    virtual std::unique_ptr<IRenderTarget> CreateRenderTarget() = 0;
    virtual MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) = 0;
    virtual bool ReadPixels(IRenderTarget&, int& width, int& height, std::vector<uint8_t>& rgba);  // 自测截图
    virtual const char* GetName() const = 0;          // "D3D11"、"WebGL2"、"WebGPU (nvidia pascal)"；以后 "D3D12"、"Vulkan (lavapipe)" ……
};
std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height,
                                                      const std::string& preference = {});   // 名称小写："webgl2" ……
#ifdef __EMSCRIPTEN__
void CreateWebGPUGraphicsDeviceAsync(const NativeWindow& window, int width, int height,
                                     std::function<void(std::unique_ptr<IGraphicsDevice>)> done);   // 失败回调 nullptr
#endif
```

宿主只认 `IGraphicsDevice`，由 `CreateGraphicsDevice` 按平台创建（阶段 1 起按偏好参数与回退顺序）。网页上 WebGPU 的适配器、设备只能异步获取，所以另有 `CreateWebGPUGraphicsDeviceAsync`；`NativeWindow::handle` 在网页上是画布的 CSS 选择器（`"#canvas"`）。

## 3. 目录与编译

渲染后端从各宿主里挪出来，放到一个共用的目录，include 根为 `apps/Render`（与现在 `apps/Win32/src/Render` 的写法相同，`#include "D3D11/Device.h"` 不用改）：

| 目录 | 内容 | 编译条件 |
|---|---|---|
| `apps/Render/IGraphicsDevice.h`、`GraphicsDevice.cpp` | 第 2 节的接口与 `CreateGraphicsDevice` | 都编 |
| `apps/Render/D3D11/` | 从 `apps/Win32/src/Render/D3D11` 移过来；加 `D3D11GraphicsDevice`，系统头文件与 `ThrowIfFailed` 放在 `D3D11Common.h`（原来靠宿主强制包含的 `pch.h`） | Windows |
| `apps/Render/D3D12/` | 新增 | Windows |
| `apps/Render/Vulkan/` | 新增 | Linux（以后 Windows 也可以选编） |
| `apps/Render/GLES3/` | 从 `apps/Web/src/Render/GLES3`、`WebGL2` 移过来；界面后端 `WebGL2Backend` 改名 `GLES3Backend`（目标 `MiniGUI_GLES3`）；加 `GLES3GraphicsDevice` | 网页、Linux |
| `apps/Render/Metal/` | 新增（Objective-C++，`.mm`） | macOS |
| `apps/Render/WebGPU/` | `WebGPUGraphicsDevice` / `WebGPURenderer` / `WebGPURenderTarget` / 界面后端 `WebGPUBackend`（目标 `MiniGUI_WebGPU`），`WebGPUCommon.h` 放共用的小工具 | 网页（与 GLES3 一起编） |
| `apps/Render/Software/` | 从 `apps/Win32/src/Render/Software` 移过来（界面的软件光栅后端） | 都编 |

- CMake：新文件 `cmake/Render.cmake` 按 `WIN32` / `APPLE` / `UNIX` / `EMSCRIPTEN` 选源文件，定义 `MINICAD_RENDER_D3D12` 等宏，宿主只在创建设备的那一个文件里用这些宏
- GLES3 代码现在只依赖 `<GLES3/gl3.h>`：网页由 Emscripten 提供，Linux 用 EGL + Mesa 的 GLES3（同样的头文件），着色器 `#version 300 es` 不用改
- 注意 AGENTS.md 的"同一个相对路径不能同时存在于两个 include 根"：移动后删除旧目录，不留副本

## 4. 着色器

| 后端 | 现在 | 计划 |
|---|---|---|
| D3D11 | HLSL 字符串，运行时 `D3DCompile`（`D3D11/Shader.h`、`D3D11Backend.cpp`） | 不变 |
| D3D12 | — | 同一份 HLSL（SM 5.1，`D3DCompile` 可编；不需要 DXC） |
| Vulkan | — | GLSL 450 → SPIR-V，构建时用 glslang 编好嵌进程序（Vulkan SDK 自带） |
| GLES3 | GLSL ES 300 字符串 | 不变 |
| Metal | — | 手写 MSL 字符串，运行时 `newLibraryWithSource` |
| WebGPU | WGSL 字符串（`WebGPURenderer.cpp`、`WebGPUBackend.cpp`） | 不变 |

着色器很少（视口：颜色 / 字形实例 / 纹理三种顶点着色器，颜色 / 文字 / 图像三种像素着色器；界面：一对），每个后端各写一份比引入转换工具链简单。以后着色器多了再考虑统一用 HLSL 经 DXC 出 SPIR-V。

## 5. 分阶段

### 阶段 0：重组（行为不变）——已完成
1. 加 `IGraphicsDevice`，把 `Win32Window` 里的 D3D11 设备 / 交换链 / 呈现挪进 `D3D11GraphicsDevice`；自测读回视口像素也挪进设备（`IGraphicsDevice::ReadPixels`）
2. 渲染代码移到 `apps/Render`，加 `cmake/Render.cmake`（目标 `MiniCADRender`、`MiniGUI_D3D11`、`MiniGUI_GLES3`、`MiniGUI_Software`）；网页版同样改用 `GLES3GraphicsDevice`
3. 验收结果：
   - 桌面版 `--selftest` 0 失败；`MiniGUIGolden` 47 个场景、`MiniGUITests` 165 个用例全部通过
   - `--bench`（总平面图1008、0525 总平面图）模型页、布局页截图与改动前逐字节相同；帧时间、内存不变。OLE 对象截图同一个 exe 连跑几次就不同，是原有的不确定性，与本次改动无关
   - 网页版构建通过，浏览器里打开起始页与三维实体示例，视口与界面显示正常，控制台无错误

### 阶段 1：Windows D3D12——已完成
1. `apps/Render/D3D12/`：`D3D12GraphicsDevice` / `D3D12Renderer` / `D3D12RenderTarget`（目标 `MiniCADRender`）与界面后端 `D3D12Backend`（目标 `MiniGUI_D3D12`），功能与 D3D11 版一一对应。两者共用 `MiniGUI::D3D12Context`（也在 `MiniGUI_D3D12` 里）：设备、队列、一条始终打开的命令列表、围栏、上传页、SRV 描述符堆、RTV / DSV 描述符、延迟释放。一帧的视口与界面命令录在同一条列表里，`Present` 时提交；`WaitIdle` / 读回像素会在帧中途提交，录制方按 `D3D12Context::GetEpoch` 发现后重设管线状态
2. 着色器：D3D11 的 HLSL 移到 `apps/Render/Hlsl/`（`ViewportShaders.h`、`UiShader.h`），D3D11 照旧 SM 5.0 / 4.0，D3D12 用 SM 5.1，都是运行时 `D3DCompile`
3. 资源管理（与原计划的差别：常量用**根常量**，不需要常量环形缓冲）：
   - 帧内数据（非缓存提交、界面顶点 / 索引、纹理更新）从上传页分配：默认 1 MB 一页，GPU 用完（围栏）后整页复用，空闲 60 帧归还系统；超过 4 MB 的单独建页，帧末释放
   - 缓存提交放在默认堆的缓冲里，规则同 D3D11（小的按 1.5 倍预留，≥ 4 MB 按实际大小）；超过 4 MB 的数据经 4 页 4 MB 的暂存区轮流分块复制（每块提交，写一页前等它上次的复制完成）。最初整块建一个同样大的上传缓冲、等围栏再放：切回文档那一帧新旧缓冲加暂存区同时占着，大图纸跑完私有内存比 D3D11 多约 46 MB；改成分块后反而比 D3D11 少
   - 换下的缓冲、纹理、旧描述符堆都 `D3D12Context::Defer`，执行完已录制的命令再释放；超过 4 MB 的在本帧 `EndFrame` 里等 GPU 后就释放
   - SRV 堆初始 64 个，不够时翻倍（CPU 端一份 + 着色器可见一份，换堆后重新 `SetDescriptorHeaps`）；RTV / DSV 按 16 个一块增长
   - 交换链：翻转模型、2 个缓冲、垂直同步、帧延迟 1（`BeginUiPass` 里等帧延迟对象，同 D3D11 的 `SetMaximumFrameLatency(1)`）
4. 创建：`CreateGraphicsDevice` 按 `apps/Render/GraphicsDevice.cpp` 的后端表依次尝试，默认顺序 D3D12 → D3D11 → WARP（`warp` 先试 D3D12 的 WARP，再试 D3D11 的）。桌面版 `--gpu d3d12|d3d11|warp` 或环境变量 `MINICAD_GPU`，指定的不可用时回退默认顺序；关于对话框显示实际用的后端
5. 调试版自动打开 D3D12 调试层，警告与错误以 `[D3D12]` 开头打到 stderr
6. 验收结果（2026-10-26，GTX 1060 6GB，Release 控制台版 `out/bench`，样例 `site.dwg`（布局页 + OLE）与 `big.dwg`（19 MB）；计时各跑 3 轮取范围）：
   - `--selftest --gpu d3d12` 0 失败，`--gpu d3d11` 0 失败；调试版（开调试层）的自测与 `big.dwg` 的 `--bench` 没有一条 `[D3D12]` 警告 / 错误
   - `MiniGUIGolden` 47 个场景全部通过，新加的 D3D12 与软件光栅对比在每个场景上的数值与 D3D11 完全相同；`--warp`（D3D11 / D3D12 都用 WARP）也全部通过
   - `--bench` 截图：两张图纸的模型页、布局页 D3D12 与 D3D11 **逐字节相同**。OLE 对象的截图 D3D11 自己连跑两次就不同，而且 D3D11 某一次与 D3D12 某一次逐字节相同——不确定性在 OLE 栅格化，与后端无关
   - 私有内存（D3D11 → D3D12，MB）：启动 128～131 → 134～136（+4～5%）；`big.dwg` 首帧 489～492 → 496～501（+1～2%），跑完 707～763 → 710～716（持平）；`site.dwg` 首帧 334～335 → 340～341（+2%），跑完 1094～1108 → 970～980（−11%）。都在 10% 以内。本机上显存分配本身计入进程私有内存（D3D11 / D3D12 都是），所以比较的是全部 GPU 缓冲加驱动开销
   - 帧时间：本机显示器约 10.7 Hz，帧总时间都是垂直同步的 93 ms，比较的是 `--bench` 里"视口""界面"两段（视口包含等 GPU 执行完）。稳定的帧持平或更快：`site.dwg` 平移视口 2.6～3.4 → 1.3～2.0 ms，布局页平移 10.5～11.2 → 4.1 ms，带选择集滚轮 13.2～13.7 → 4.7～5.4 ms；`big.dwg` 平移、滚轮两边都在"约 3 ms / 约 7 ms"两档之间跳（显卡频率状态），同一档里持平。界面 0.8～1.5 ms，两边相同
   - **接受的差距**：要大块上传的帧在 `big.dwg` 上比 D3D11 慢——框选全图首帧视口 194～227 → 233～237 ms（全选后的选中流约 168 MB），切回文档首帧视口 16～23 → 28～29 ms，首帧持平（836～849 → 835～849 ms）。`site.dwg` 上同类的帧反而更快（框选首帧 148～158 → 127～129 ms，布局页首帧 967～982 → 932～941 ms）。D3D12 的数据要经上传堆、再由 GPU 复制进显存，这一帧里等复制约 27 ms、建默认堆缓冲约 16 ms；D3D11 驱动带初始数据建缓冲的路径更快。可能的改进：复制放到 COPY 队列与渲染并行；支持 ReBAR / `D3D12_HEAP_TYPE_GPU_UPLOAD` 的显卡直接写显存
   - 上面这一项经确认接受（稳定帧持平或更快、内存达标，慢的只是换图纸、全选这类少见的帧），默认顺序已改成 D3D12 → D3D11 → WARP；改后不带 `--gpu` 的 `--selftest` 0 失败（用的是 D3D12），`--gpu warp` 用 D3D12 的 WARP 跑完 `--bench`

### 阶段 2：Linux 宿主 + OpenGL——已完成（2026-10-26），CI 未加
1. 宿主 `apps/Linux`（目标 `MiniCADLinux`，`LinuxApp` 实现 `AppPlatform`）：SDL3 开窗口、收事件、建 EGL / GLES3 上下文（X11 上也强制 EGL），只做这些，渲染仍走 `GLES3GraphicsDevice`
   - 窗口用系统标题栏（`HasWindowControls()` 为 false，与网页版一样不显示自绘的窗口按钮）；按需重绘：`RequestRedraw` 置标志并推一个 SDL 自定义事件，空闲时阻塞在 `SDL_WaitEvent`
   - 后台线程 `std::thread`，完成回调与 `Post` 经 SDL 自定义事件交回界面线程；资源目录 = 可执行文件目录（构建后复制 `assets`），用户数据 `$XDG_DATA_HOME/MiniCAD`（默认 `~/.local/share/MiniCAD`），系统字体目录逐级列出 `/usr/share/fonts` 等（`FontEngine` 不进子目录），打开链接 `SDL_OpenURL`
   - 文件对话框用 SDL3 的接口（xdg-desktop-portal，没有时 zenity）：打开、选文件夹是异步的；另存为、导出要当场返回路径，在界面线程上等对话框关闭（只取系统事件，不画界面），所选筛选器决定 DWG / DXF 版本。都打不开时弹提示，改用拖放或命令行给出图纸
   - 没有监视界面描述文件的目录：切回窗口时调用 `MainFrame::CheckUiFileChanged`；没有自测 / `--bench`（`SelfTest.cpp` 仍只在 Win32）
   - 命令行：`MiniCADLinux [--gpu gl] [--ui 文件] [图纸 ...]`，后端也可用 `MINICAD_GPU`；`CreateGraphicsDevice` 的偏好参数在 Linux 只认 `gl`（Vulkan 是阶段 3）
2. MiniGUI 平台层 `src/UI/Platform/SDL`（目标 `MiniGUI_SDL`）：`SDLInput`（指针、键盘、文字与输入法、光标、定时器）、`SDLClipboard`、界面字体 `LoadSDLUIFonts`（主字体用随程序携带的 NotoSansSC-UI.ttf，与网页版相同；系统里的 Noto Sans CJK / 文泉驿作后备）。焦点不在文本框时关掉 SDL 的文字输入（相当于 Win32 关输入法），可打印的按键由按键事件换成文字
3. CMake：`cmake/MiniGUI.cmake` 找 SDL3（`find_package(SDL3 CONFIG)`），`cmake/Render.cmake` 的 Linux 段找 `GLES3/gl3.h` 与 `libGLESv2`，编 GLES3 渲染器 + `MiniGUI_GLES3`、定义 `MINICAD_RENDER_GLES3`；缺任何一个时不编译宿主，核心库与测试照常
4. GCC 13 的可移植性修改（Windows 行为不变）：成员与类型同名时类型写成 `MiniCAD::Lineweight` 等（`-Wchanges-meaning`）、补 `<cstring>`、SMLib 一处析构函数写法（见 `3rd/SMLib/README.md`）、`RecentFiles::DisplayName` 取父目录时两种分隔符都认、测试里的 Windows 调用加条件编译；`MiniCADTests`、`MiniCADDrawingOpenTests` 不再只在 Windows 上编译
5. 验收结果（局域网 Ubuntu 24.04 服务器，GCC 13.3，SDL3 3.4.18 自己编译）：
   - Linux 上 ctest 22 项全部通过（`MiniCADTests`、`MiniGUITests`、`MiniDWGTests`、节点图、实体等）；依赖微软雅黑的界面文字用例在 Linux 上跳过
   - `MiniCADLinux` 在 Xvfb + Mesa llvmpipe（软件渲染）下启动，打开 `site.dwg`：模型页「全部显示」与 Windows 版画面一致（图纸本身有离群对象，主体很小），缩放窗口后、切到「总图」布局页显示正常；用 XTest 模拟的鼠标移动、滚轮、键入命令（`L`）、点击都生效
   - Windows 桌面版、网页版照常编译，Windows 上 `MiniCADTests`、`MiniGUITests`、`MiniDWGTests` 通过
   - **还没在真实显示器、Wayland、WSLg 上验证**；输入法（IBus / Fcitx）、文件对话框（portal / zenity）、高 DPI 缩放、拖放都没有实际试过
6. 后续：CI 加 Linux 编译与测试（需要 SDL3：Ubuntu 25.04 起有 `libsdl3-dev`，24.04 要自己编译）；Linux 版打包

### 阶段 3：Linux Vulkan
1. `VulkanGraphicsDevice` / `VulkanRenderer` / `VulkanRenderTarget` / `VulkanBackend`
2. 枚举物理设备时独立显卡优先；创建实例或设备失败（SolidCraft 遇到过 Intel 的 Vulkan 驱动在初始化时崩溃）就回退 OpenGL
3. 验收同阶段 1（与 GL 后端对比），另在没有显卡的环境里确认 lavapipe 能跑

### 阶段 4：macOS 宿主 + Metal
1. 宿主：Cocoa（`NSWindow` / `NSView` + `CAMetalLayer`），`AppPlatform` 与 MiniGUI 平台层（`src/UI/Platform/macOS`）
2. `MetalGraphicsDevice` / `MetalRenderer` / `MetalRenderTarget` / `MetalBackend`
3. CI 加 macOS

### 阶段 5：网页 WebGPU——已完成（2026-10-26）
1. `apps/Render/WebGPU/`：`WebGPUGraphicsDevice` / `WebGPURenderer` / `WebGPURenderTarget` / `WebGPUBackend`（目标 `MiniGUI_WebGPU`），功能与 GLES3 版一一对应，着色器为 WGSL。只用标准 `webgpu.h`，经 Emscripten 自带的 emdawnwebgpu 绑定（`--use-port=emdawnwebgpu:cpp_bindings=false`，编译和链接都加，见 `cmake/Render.cmake`）。emsdk 5.0.7 已自带这个端口（固定 Dawn v20251002），第一次构建时从 GitHub 下载端口包到 emsdk 的缓存目录，不用升级 emsdk；CI 用同一个版本
2. 结构照 wgpu 原型（`docs/WebGPU/PLAN.md` 第 3、4 节）：
   - 写缓冲在提交前统一生效：矩阵 + 换色参数占帧内 uniform 环形缓冲的一段（256 字节对齐，动态偏移）；不缓存的顶点写帧内流缓冲的不同区段；`EndFrame` 提交后复位。帧内换下来的缓冲 / 绑定组 / 纹理放进 `WebGPUCommon::RetireList`，提交后再销毁（销毁后再提交会报校验错误）。界面后端同样：顶点、索引、uniform 都按帧内区段写，`WebGPUBackend::EndFrame` 在宿主提交后清理
   - `WebGPURenderer::DrawLayer`：结束通道 → `CopyTextureToTexture`（只复制颜色，与 GL 版相同）→ 以 Load 重新开始
   - 渲染目标总带 Depth32Float；管线按 程序 × 图元 × 深度模式（Off / Test / TestWrite）× 混合 懒创建，最多 72 条
   - 混合：视口颜色 srcAlpha / 1-srcAlpha、alpha one / zero；界面 alpha one / 1-srcAlpha（与 GLES3Backend 相同）
   - 字形实例化用 `firstInstance`，不用像 WebGL2 那样逐批重设实例属性；界面后端用 `baseVertex`，不用改写索引
   - 渲染目标调整尺寸时重建纹理，视图（`GetNativeShaderResource`）随之改变，`MainFrame` 发现后重新登记界面纹理；先建新的再释放旧的，新视图的地址不会与旧的相同
   - 窗口：`wgpuSurfaceGetCurrentTexture` 取画布当前纹理，`BeginUiPass` 开始通道并交给界面后端（`WebGPUBackend::BeginFrame`），`Present` 结束通道并提交。**不能调用 `wgpuSurfacePresent`**（emdawnwebgpu 里直接 abort），浏览器在 `requestAnimationFrame` 回调返回后自动呈现
   - 设备按适配器的上限申请 `maxBufferSize` 与 `maxTextureDimension2D`；未捕获的错误与设备丢失只记日志（`LOG_ERROR`），不恢复
3. 启动顺序（`WebApp::Initialize`）：网址带 `?webgl` → 直接 WebGL2；否则 `CreateWebGPUGraphicsDeviceAsync`（`navigator.gpu` 存在 → 取适配器 → 取设备 → 建画布表面），回调里成功就用 WebGPU，失败（回调 nullptr）再 `WebHost::CreateWebGLContext` + `CreateGraphicsDevice(..., "webgl2")`。同一个画布只能有一种上下文：取到设备之后才建表面，所以失败时画布还是干净的。`WebHost::Initialize` 只跟踪尺寸与 DPI，不建上下文；设备就绪（`WebApp::OnGraphicsReady`）之后才初始化 `MainFrame`、输入，并开始画帧
4. 关于对话框加了一行"图形 <名称>"（`AppPlatform::GetGraphicsName`，所有平台都显示）；WebGPU 的名称带浏览器给的厂商与架构，例如 "WebGPU (nvidia pascal)"
5. 读回像素在浏览器里是异步的：不实现 `ReadPixels`（网页版没有自测截图）
6. 控件展示 `MiniGUIWebGallery` 仍只用 WebGL2（`GLES3Backend`）
7. 验收结果（Windows 11、GTX 1060、Claude 桌面版的内置浏览器，即 Chromium）：
   - `build_wasm.bat` 构建通过
   - 默认走 WebGPU（控制台 `图形接口 WebGPU (nvidia pascal)`，关于对话框同样显示）；`?webgl` 强制 WebGL2；页面里把 `navigator.gpu` 藏掉时自动退回 WebGL2
   - 起始页示例"曲线：圆角、偏移与等分"（二维）与"实体：法兰座"（三维着色、深度、轮廓线，西南等轴测）：同样操作下视口区域与 WebGL2 截图对比，三维逐像素相同，二维 0.07% 的像素不同（线的光栅化）；`总平面图1008.dwg`（大量文字、填充）模型页 0.58% 的像素不同（细线的抗锯齿与十字光标差 1 像素，内容一致），布局页正常；画直线时的橡皮筋、动态输入、十字光标（缓存层复制 + 动态层）正常
   - 全程控制台没有 WebGPU 校验错误（只有 Chrome 自己的提示：Windows 上忽略 `powerPreference`）
   - 体积：`index.wasm` 10,821,269 → 10,906,078 字节（+84,809，+0.8%），`index.js` 122,616 → 166,842 字节（+44,226）。增加很少，不需要按需加载
   - **没测**：Firefox / Safari（本机测不了）、Edge（同为 Chromium，应与 Chrome 相同）、触屏设备。Firefox 141 起在 Windows 上默认开启 WebGPU，Safari 26 起默认开启；不支持的版本会自动走 WebGL2

## 6. 待定

- ~~Linux 的窗口系统~~：已定（阶段 2）用 SDL3，只用它开窗口、收事件、建 EGL / GLES3 上下文，渲染仍是自己的后端；一份代码覆盖 X11 / Wayland，macOS 也能用来先跑起来。Vulkan（阶段 3）同样由 SDL3 建表面（`SDL_Vulkan_CreateSurface`）
- 阶段 1、阶段 2、阶段 5 可以并行：阶段 0 完成后三者不碰同一批文件
- ~~网页版同时编 WebGPU 和 WebGL2 两个后端，wasm 会变大~~：实测只多约 83 KB（阶段 5），不需要按需加载
- ~~文件对话框在 Linux 上用 xdg-desktop-portal 还是 GTK~~：已定（阶段 2）用 SDL3 的对话框接口，它走 xdg-desktop-portal，没有时用 zenity；不引入 GTK
