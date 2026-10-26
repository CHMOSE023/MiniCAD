# 多平台渲染后端

> 状态：阶段 0 完成（2026-10-26，分支 `render-backends`），阶段 1～5 未开始。
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
    virtual const char* GetName() const = 0;          // "D3D11"、"WebGL2"；以后 "D3D12"、"Vulkan (lavapipe)" ……
};
std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height);
```

宿主只认 `IGraphicsDevice`，由 `CreateGraphicsDevice` 按平台创建（阶段 1 起加上偏好参数与回退顺序）。

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
| `apps/Render/WebGPU/` | 新增，渲染器与界面后端 | 网页 |
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
| WebGPU | — | WGSL 字符串（原型里写过一套，见阶段 5） |

着色器很少（视口：颜色 / 字形实例 / 纹理三种顶点着色器，颜色 / 文字 / 图像三种像素着色器；界面：一对），每个后端各写一份比引入转换工具链简单。以后着色器多了再考虑统一用 HLSL 经 DXC 出 SPIR-V。

## 5. 分阶段

### 阶段 0：重组（行为不变）——已完成
1. 加 `IGraphicsDevice`，把 `Win32Window` 里的 D3D11 设备 / 交换链 / 呈现挪进 `D3D11GraphicsDevice`；自测读回视口像素也挪进设备（`IGraphicsDevice::ReadPixels`）
2. 渲染代码移到 `apps/Render`，加 `cmake/Render.cmake`（目标 `MiniCADRender`、`MiniGUI_D3D11`、`MiniGUI_GLES3`、`MiniGUI_Software`）；网页版同样改用 `GLES3GraphicsDevice`
3. 验收结果：
   - 桌面版 `--selftest` 0 失败；`MiniGUIGolden` 47 个场景、`MiniGUITests` 165 个用例全部通过
   - `--bench`（总平面图1008、0525 总平面图）模型页、布局页截图与改动前逐字节相同；帧时间、内存不变。OLE 对象截图同一个 exe 连跑几次就不同，是原有的不确定性，与本次改动无关
   - 网页版构建通过，浏览器里打开起始页与三维实体示例，视口与界面显示正常，控制台无错误

### 阶段 1：Windows D3D12
1. `D3D12GraphicsDevice` / `D3D12Renderer` / `D3D12RenderTarget` / `D3D12Backend`，功能与 D3D11 版一一对应
2. 资源管理照 WebGPU 原型的经验：帧内常量环形缓冲、帧内流缓冲、换下来的资源等 GPU 用完（围栏）再释放；缓存层复制用 `CopyResource`
3. 创建顺序 D3D12 → D3D11 → WARP；`--gpu d3d12|d3d11|warp`
4. 验收（全部通过前默认仍是 D3D11）：
   - `--selftest` 0 失败，D3D12 与 D3D11 截图逐像素一致（允许几个像素的光栅化差异）
   - 性能不低于 D3D11（`--bench` 对比）
   - **内存**：启动与大图纸跑完时的私有内存与 D3D11 相差不超过 10%。这是 wgpu 原型失败的地方；原生 D3D12 自己控制堆的大小，要在这一步确认

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

### 阶段 5：网页 WebGPU
1. `WebGPUGraphicsDevice` / `WebGPURenderer` / `WebGPURenderTarget` / `WebGPUBackend`，用 Emscripten 的 WebGPU 绑定（`--use-port=emdawnwebgpu`）
2. 可以照 wgpu 原型的结构写（`docs/WebGPU/PLAN.md` 第 3 节）：渲染器、渲染目标、界面后端、WGSL 着色器都只用标准 `webgpu.h`，在原型里已经跑通全部自测；要重写的只有设备层（浏览器里取适配器、设备都是异步的，画布用 `HTMLCanvasElement` 建表面）
3. 启动顺序：`navigator.gpu` 存在且取到设备 → WebGPU；否则或网址带 `?webgl` → WebGL2。设备异步就绪之前不画第一帧
4. 读回像素在浏览器里也是异步的：网页版没有自测截图，不受影响
5. 验收：Chrome / Edge / Firefox / Safari 上打开样例图纸，与 WebGL2 截图对比；不支持 WebGPU 的浏览器自动走 WebGL2；`build_wasm.bat serve` 本地验证，CI 的网页版构建通过

## 6. 待定

- ~~Linux 的窗口系统~~：已定（阶段 2）用 SDL3，只用它开窗口、收事件、建 EGL / GLES3 上下文，渲染仍是自己的后端；一份代码覆盖 X11 / Wayland，macOS 也能用来先跑起来。Vulkan（阶段 3）同样由 SDL3 建表面（`SDL_Vulkan_CreateSurface`）
- 阶段 1、阶段 2、阶段 5 可以并行：阶段 0 完成后三者不碰同一批文件
- 网页版同时编 WebGPU 和 WebGL2 两个后端，wasm 会变大；要看增加多少，太大就考虑按需加载
- ~~文件对话框在 Linux 上用 xdg-desktop-portal 还是 GTK~~：已定（阶段 2）用 SDL3 的对话框接口，它走 xdg-desktop-portal，没有时用 zenity；不引入 GTK
