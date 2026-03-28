#include "SwapChain.h"

using Microsoft::WRL::ComPtr;

namespace MiniCAD
{
    void SwapChain::Initialize(Device* device, HWND hwnd, UINT width, UINT height, const Options& opt)
    {
         m_device = device;
         m_hwnd   = hwnd;
         m_width  = (width  > 0) ? width  : 1;
         m_height = (height > 0) ? height : 1;
         m_opt    = opt;
         
         CreateSwapChain();
         CreateSizeDependent();
    }

    void SwapChain::CreateSwapChain()
    {
        auto factory = m_device->GetFactory();
        auto device = m_device->GetDevice();

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = m_opt.backBufferFormat;
        desc.BufferCount = m_opt.bufferCount;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SampleDesc.Count = 1;

        // 推荐 Flip Model
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        desc.Flags = m_opt.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ThrowIfFailed(factory->CreateSwapChainForHwnd(device, m_hwnd, &desc, nullptr, nullptr, m_swapChain.ReleaseAndGetAddressOf()));

        // 禁用 Alt+Enter
        factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    void SwapChain::CreateSizeDependent()
    {
        auto device = m_device->GetDevice();
        auto context = m_device->GetContext();

        // 清理旧资源
        context->OMSetRenderTargets(0, nullptr, nullptr);

        m_rtv.Reset();
        m_dsv.Reset();
        m_backBuffer.Reset();
        m_depth.Reset();

        // BackBuffer
        ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.ReleaseAndGetAddressOf())));

        ThrowIfFailed(device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf()));

        // Depth
        if (m_opt.depthFormat != DXGI_FORMAT_UNKNOWN)
        {
            D3D11_TEXTURE2D_DESC depthDesc = {};

            depthDesc.Width            = m_width;
            depthDesc.Height           = m_height;
            depthDesc.MipLevels        = 1;
            depthDesc.ArraySize        = 1;
            depthDesc.Format           = m_opt.depthFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;

            ThrowIfFailed(device->CreateTexture2D(&depthDesc, nullptr, m_depth.ReleaseAndGetAddressOf()));

            ThrowIfFailed(device->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsv.ReleaseAndGetAddressOf()));
        }

        // Viewport
        m_viewport = {};

        m_viewport.TopLeftX  = 0;
        m_viewport.TopLeftY  = 0;
        m_viewport.Width     = static_cast<float>(m_width);
        m_viewport.Height    = static_cast<float>(m_height);
        m_viewport.MinDepth  = 0.0f;
        m_viewport.MaxDepth  = 1.0f;

    }

    void SwapChain::Resize(UINT width, UINT height)
    {
        if (width == 0 || height == 0)
            return;

        m_width = width;
        m_height = height;

        auto context = m_device->GetContext();

        context->OMSetRenderTargets(0, nullptr, nullptr);

        m_rtv.Reset();
        m_dsv.Reset();
        m_backBuffer.Reset();
        m_depth.Reset();

        ThrowIfFailed(m_swapChain->ResizeBuffers(
                      m_opt.bufferCount,
                      m_width,
                      m_height,
                      m_opt.backBufferFormat,
                      m_opt.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0 )
        );

        CreateSizeDependent();
    }

    void SwapChain::Present()
    {
        m_swapChain->Present(1, 0);
    }


}

