#pragma once
#include "Render/IRenderTarget.h"
#include <webgpu/webgpu.h>

namespace MiniCAD
{
    // WebGPU 离屏渲染目标：RGBA8 颜色纹理 + Depth32Float 深度纹理，与 GLRenderTarget / D3D11RenderTarget 对应。
    // 原生句柄：
    //   GetNativeHandle()         → 颜色纹理的视图（WGPUTextureView，渲染附件）
    //   GetNativeShaderResource() → 同一个视图（界面层把视口画面当作纹理显示）
    // 调整尺寸时重建纹理，视图随之改变：界面层发现句柄变了会重新登记（MainFrame）。
    // 先建新的再释放旧的，保证新视图的地址与旧视图不同（地址可能被复用，界面层按地址判断是否变化）
    class WebGPURenderTarget : public IRenderTarget
    {
    public:
        explicit WebGPURenderTarget(WGPUDevice device);
        ~WebGPURenderTarget() override;

        WebGPURenderTarget(const WebGPURenderTarget&) = delete;
        WebGPURenderTarget& operator=(const WebGPURenderTarget&) = delete;

        void Create(int width, int height) override;
        void Resize(int width, int height) override;
        void Release() override;

        int   GetWidth()  const override { return m_width; }
        int   GetHeight() const override { return m_height; }
        void* GetNativeHandle()         const override { return m_colorView; }
        void* GetNativeShaderResource() const override { return m_colorView; }

        WGPUTexture     GetColorTexture() const { return m_color; }
        WGPUTextureView GetColorView()    const { return m_colorView; }
        WGPUTextureView GetDepthView()    const { return m_depthView; }

        static constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
        static constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth32Float;

    private:
        WGPUDevice      m_device    = nullptr;
        WGPUTexture     m_color     = nullptr;
        WGPUTextureView m_colorView = nullptr;
        WGPUTexture     m_depth     = nullptr;
        WGPUTextureView m_depthView = nullptr;
        int             m_width     = 0;
        int             m_height    = 0;
    };
}
