#pragma once
#include "IGraphicsDevice.h"
#include <webgpu/webgpu.h>
#include <functional>
#include <memory>
#include <string>

namespace MiniGUI
{
    class WebGPUBackend;
}

namespace MiniCAD
{
    class WebGPURenderer;

    // WebGPU 图形设备（网页版）：WebGPURenderer + 界面后端 WebGPUBackend，经 Emscripten 的 emdawnwebgpu 绑定使用浏览器的 WebGPU。
    //   - 适配器、设备在浏览器里只能异步获取：用 CreateAsync 创建，取到设备后才建画布表面（取不到时画布上还没有上下文，宿主可以改建 WebGL2）
    //   - 窗口的帧缓冲是画布表面的当前纹理；浏览器在本次回调返回后呈现（不能调用 wgpuSurfacePresent，emdawnwebgpu 里它直接 abort）
    //   - 读回像素在浏览器里也是异步的：不支持 ReadPixels（网页版没有自测截图）
    class WebGPUGraphicsDevice : public IGraphicsDevice
    {
    public:
        using CreatedFunc = std::function<void(std::unique_ptr<IGraphicsDevice>)>;

        // 浏览器是否提供 navigator.gpu（不代表一定能取到适配器）
        static bool IsAvailable();
        // 取适配器与设备，成功后 Initialize（window.handle 为画布的 CSS 选择器），完成时回调；任何一步失败都回调 nullptr
        static void CreateAsync(const NativeWindow& window, int width, int height, CreatedFunc done);

        WebGPUGraphicsDevice(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device);
        ~WebGPUGraphicsDevice() override;

        bool Initialize(const NativeWindow& window, int width, int height) override;
        void Resize(int width, int height) override;
        void BeginUiPass() override;
        void Present() override;

        IRenderer&               GetRenderer() override;
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        const char* GetName() const override { return m_name.c_str(); }

    private:
        void Configure();

    private:
        WGPUInstance      m_instance = nullptr;
        WGPUAdapter       m_adapter  = nullptr;
        WGPUDevice        m_device   = nullptr;
        WGPUQueue         m_queue    = nullptr;
        WGPUSurface       m_surface  = nullptr;
        WGPUTextureFormat m_format   = WGPUTextureFormat_BGRA8Unorm;
        int               m_width    = 0;
        int               m_height   = 0;
        std::string       m_name     = "WebGPU";

        // 声明顺序即构造顺序，销毁时相反：渲染器先于界面后端
        std::unique_ptr<MiniGUI::WebGPUBackend> m_backend;
        std::unique_ptr<WebGPURenderer>         m_renderer;

        // 当前帧的窗口渲染通道（BeginUiPass → Present）
        WGPUTexture           m_frameTexture = nullptr;
        WGPUTextureView       m_frameView    = nullptr;
        WGPUCommandEncoder    m_encoder      = nullptr;
        WGPURenderPassEncoder m_pass         = nullptr;
    };
}
