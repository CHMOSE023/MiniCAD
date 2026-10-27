#include "WebGPU/WebGPURenderTarget.h"
#include "WebGPU/WebGPUCommon.h"
#include <algorithm>

namespace MiniCAD
{
    namespace
    {
        WGPUTexture CreateTexture(WGPUDevice device, int width, int height, WGPUTextureFormat format, WGPUTextureUsage usage, const char* label)
        {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.label  = WebGPUCommon::Str(label);
            desc.usage  = usage;
            desc.size   = WGPUExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
            desc.format = format;
            return wgpuDeviceCreateTexture(device, &desc);
        }
    }

    WebGPURenderTarget::WebGPURenderTarget(WGPUDevice device)
        : m_device(device)
    {
        wgpuDeviceAddRef(m_device);
    }

    WebGPURenderTarget::~WebGPURenderTarget()
    {
        Release();
        wgpuDeviceRelease(m_device);
    }

    void WebGPURenderTarget::Create(int width, int height)
    {
        Resize(width, height);
    }

    void WebGPURenderTarget::Resize(int width, int height)
    {
        width  = std::max(width, 1);
        height = std::max(height, 1);
        if (m_color && width == m_width && height == m_height)
            return;

        // 颜色：渲染附件 + 界面采样 + 缓存层复制（DrawLayer 的源与目标）
        WGPUTexture color = CreateTexture(m_device, width, height, kColorFormat,
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
            "MiniCAD 视口颜色");
        WGPUTextureView colorView = wgpuTextureCreateView(color, nullptr);
        WGPUTexture depth = CreateTexture(m_device, width, height, kDepthFormat, WGPUTextureUsage_RenderAttachment, "MiniCAD 视口深度");
        WGPUTextureView depthView = wgpuTextureCreateView(depth, nullptr);

        Release();
        m_color     = color;
        m_colorView = colorView;
        m_depth     = depth;
        m_depthView = depthView;
        m_width     = width;
        m_height    = height;
    }

    void WebGPURenderTarget::Release()
    {
        if (m_colorView) wgpuTextureViewRelease(m_colorView);
        if (m_depthView) wgpuTextureViewRelease(m_depthView);
        m_colorView = nullptr;
        m_depthView = nullptr;
        WebGPUCommon::DestroyTexture(m_color);
        WebGPUCommon::DestroyTexture(m_depth);
        m_width  = 0;
        m_height = 0;
    }
}
