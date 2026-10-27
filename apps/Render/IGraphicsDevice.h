#pragma once
#include "Render/IRenderer.h"
#include "Render/IRenderTarget.h"
#include "Render/IRenderBackend.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 宿主要画的窗口：Win32 为 HWND；网页版为画布的 CSS 选择器（const char*，如 "#canvas"），
    // WebGPU 由它建画布表面，WebGL2 不用（上下文由 WebHost 创建并设为当前）；Linux 不用。
    // 宿主还要说明它为哪种图形接口准备好了窗口：GLES3 要当前的 GL 上下文，Vulkan 要建表面的办法
    struct NativeWindow
    {
        void* handle = nullptr;

        // 宿主已建好 GL / GLES 上下文并设为当前（网页的 WebGL2、Linux 经 SDL3 的 EGL）；否则 GLES3 不可用
        bool glContext = false;

        // Vulkan：建实例要加的扩展，以及在实例（VkInstance）上建窗口表面、把 VkSurfaceKHR 写进 surface。
        // 不依赖窗口库（Linux 由 SDL3 实现）；createVulkanSurface 为空时 Vulkan 不可用
        std::vector<const char*> vulkanInstanceExtensions;
        std::function<bool(void* instance, uint64_t* surface)> createVulkanSurface;
    };

    // 主窗口之外的系统窗口（停靠面板拖出主窗口后的独立窗口）各有一个窗口表面：自己的交换链，与主窗口共用设备、渲染器与界面后端。
    // 一帧：BeginPass 绑定该窗口的后备缓冲并清屏 → 界面后端 Render → Present
    class IWindowSurface
    {
    public:
        virtual ~IWindowSurface() = default;
        virtual void Resize(int width, int height) = 0;     // 像素尺寸
        virtual void BeginPass() = 0;
        virtual void Present() = 0;
    };

    // 宿主侧的图形设备：一种图形接口一份实现（D3D11GraphicsDevice、VulkanGraphicsDevice、GLES3GraphicsDevice ……），见 docs/Render/PLAN.md。
    // 持有设备、窗口的交换链 / 帧缓冲、CAD 视口渲染器与界面后端；宿主只通过本接口使用图形接口，
    // 核心库、MiniGUI、apps/Common 看不到它（它们只认 IRenderer / IRenderTarget / IRenderBackend）。
    class IGraphicsDevice
    {
    public:
        virtual ~IGraphicsDevice() = default;

        virtual bool Initialize(const NativeWindow& window, int width, int height) = 0;
        virtual void Resize(int width, int height) = 0;    // 窗口绘图缓冲的像素尺寸
        virtual void BeginUiPass() = 0;                     // 绑定窗口的帧缓冲并清屏
        virtual void Present() = 0;
        virtual void WaitIdle() {}                          // 等 GPU 执行完已提交的命令（自测计时用）
        virtual void ReleaseFreeMemory() {}                 // 让驱动真正销毁已释放的缓冲（关闭文档后）

        // 为另一个系统窗口建窗口表面（D3D11 / D3D12 支持）；不支持的后端返回 nullptr，宿主就不提供独立的浮动窗口
        virtual std::unique_ptr<IWindowSurface> CreateWindowSurface(const NativeWindow& window, int width, int height)
        {
            (void)window; (void)width; (void)height;
            return nullptr;
        }

        virtual IRenderer&               GetRenderer()  = 0;
        virtual MiniGUI::IRenderBackend& GetUiBackend() = 0;
        virtual std::unique_ptr<IRenderTarget> CreateRenderTarget() = 0;
        // 把渲染目标的着色器资源（IRenderTarget::GetNativeShaderResource）登记为界面纹理
        virtual MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) = 0;

        // 读回渲染目标的像素（RGBA8，第一行在顶部；自测与 MINICAD_BENCH_SHOT 截图用）。不支持时返回 false
        virtual bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba)
        {
            (void)target; (void)width; (void)height; (void)rgba;
            return false;
        }

        virtual const char* GetName() const = 0;            // 关于对话框显示，例如 "D3D11"
    };

    // 按本平台编译进来的图形接口创建设备并初始化：先试 preference（小写名称：d3d12、d3d11、warp、vulkan、gl、webgpu、webgl2），
    // 为空或失败时按平台的默认顺序逐个尝试；全部失败返回 nullptr。
    // 网页版这里只建 WebGL2（WebGPU 的设备要异步获取，见 CreateWebGPUGraphicsDeviceAsync）
    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height, const std::string& preference = {});

#ifdef __EMSCRIPTEN__
    // 网页：浏览器支持时异步取 WebGPU 的适配器与设备，建好图形设备后回调；不支持或任何一步失败都回调 nullptr，
    // 这时画布上还没有上下文，宿主再建 WebGL2 上下文、用 CreateGraphicsDevice(..., "webgl2")。
    // 同一个画布只能有一种上下文，所以要先试 WebGPU、确定不用它再建 WebGL2
    void CreateWebGPUGraphicsDeviceAsync(const NativeWindow& window, int width, int height,
                                         std::function<void(std::unique_ptr<IGraphicsDevice>)> done);
#endif
}
