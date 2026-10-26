# 多平台渲染后端

> 状态：阶段 0 完成（2026-10-26，分支 `render-backends`）；阶段 5 网页 WebGPU 完成（2026-10-26）；阶段 1～4 进行中或未开始。
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

### 阶段 1：Windows D3D12
1. `D3D12GraphicsDevice` / `D3D12Renderer` / `D3D12RenderTarget` / `D3D12Backend`，功能与 D3D11 版一一对应
2. 资源管理照 WebGPU 原型的经验：帧内常量环形缓冲、帧内流缓冲、换下来的资源等 GPU 用完（围栏）再释放；缓存层复制用 `CopyResource`
3. 创建顺序 D3D12 → D3D11 → WARP；`--gpu d3d12|d3d11|warp`
4. 验收（全部通过前默认仍是 D3D11）：
   - `--selftest` 0 失败，D3D12 与 D3D11 截图逐像素一致（允许几个像素的光栅化差异）
   - 性能不低于 D3D11（`--bench` 对比）
   - **内存**：启动与大图纸跑完时的私有内存与 D3D11 相差不超过 10%。这是 wgpu 原型失败的地方；原生 D3D12 自己控制堆的大小，要在这一步确认

### 阶段 2：Linux 宿主 + OpenGL
1. 宿主：实现 `AppPlatform`（窗口、重绘、文件对话框、目录、后台线程、系统字体目录）与 MiniGUI 的平台层（`src/UI/Platform/Linux`：输入、剪贴板、字体、窗口框架）。窗口系统待定（见第 6 节）
2. 图形：先上 `GLES3GraphicsDevice`（EGL），渲染器直接复用，最快看到画面
3. CI 加 Linux 编译与测试（核心库、MiniGUI 的测试本来与平台无关）

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

- Linux 的窗口系统：直接写 X11 + Wayland 两套，还是用 SDL3 / GLFW 统一窗口与输入（只用它开窗口和收事件，渲染仍是自己的后端）。倾向 SDL3：一份代码覆盖 X11 / Wayland，macOS 也能用来先跑起来
- 阶段 1、阶段 2、阶段 5 可以并行：阶段 0 完成后三者不碰同一批文件
- ~~网页版同时编 WebGPU 和 WebGL2 两个后端，wasm 会变大~~：实测只多约 83 KB（阶段 5），不需要按需加载
- 文件对话框在 Linux 上用 xdg-desktop-portal 还是 GTK
