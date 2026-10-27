#include "D3D12/D3D12RenderTarget.h"

namespace MiniCAD
{
    void D3D12RenderTarget::Create(int width, int height)
    {
        Release();
        if (width <= 0 || height <= 0)
            return;

        D3D12_CLEAR_VALUE colorClear = {};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;         // 渲染器按 (0, 0, 0, 0) 清屏
        m_color = m_ctx.CreateTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear);
        // 深度缓冲（三维视图里着色面写深度、线按深度测试；二维显示不写深度，测试总能通过）
        D3D12_CLEAR_VALUE depthClear = {};
        depthClear.Format             = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;
        m_depth = m_ctx.CreateTexture(width, height, DXGI_FORMAT_D32_FLOAT,
                                      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear);
        if (!m_color || !m_depth)
        {
            Release();
            return;
        }
        m_rtv = m_ctx.AllocateRtv();
        m_dsv = m_ctx.AllocateDsv();
        m_ctx.GetDevice()->CreateRenderTargetView(m_color->resource.Get(), nullptr, m_rtv);
        m_ctx.GetDevice()->CreateDepthStencilView(m_depth->resource.Get(), nullptr, m_dsv);
        m_width  = width;
        m_height = height;
    }

    void D3D12RenderTarget::Resize(int width, int height)
    {
        if (width == m_width && height == m_height && m_color)
            return;
        // 先建新的再放掉旧的：颜色缓冲对象的地址一定变化，界面才会重新登记纹理
        auto oldColor = std::move(m_color);
        auto oldDepth = std::move(m_depth);
        const D3D12_CPU_DESCRIPTOR_HANDLE oldRtv = m_rtv, oldDsv = m_dsv;
        m_rtv = {};
        m_dsv = {};
        Create(width, height);
        m_ctx.ReleaseTexture(oldColor);
        m_ctx.ReleaseTexture(oldDepth);
        m_ctx.FreeRtv(oldRtv);
        m_ctx.FreeDsv(oldDsv);
    }

    void D3D12RenderTarget::Release()
    {
        m_ctx.ReleaseTexture(m_color);
        m_ctx.ReleaseTexture(m_depth);
        m_ctx.FreeRtv(m_rtv);
        m_ctx.FreeDsv(m_dsv);
        m_rtv    = {};
        m_dsv    = {};
        m_width  = 0;
        m_height = 0;
    }
}
