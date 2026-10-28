#include "D3D12/D3D12GraphicsDevice.h"
#include "D3D12/D3D12Backend.h"
#include "D3D12/D3D12Renderer.h"
#include "D3D12/D3D12RenderTarget.h"
#include "Core/Log.h"
#include <cstring>
#include <exception>

using Microsoft::WRL::ComPtr;
using MiniGUI::D3D12ThrowIfFailed;

namespace MiniCAD
{
    // =========================================================
    // 窗口表面：一个系统窗口的交换链（翻转模型、垂直同步、帧延迟 1）。主窗口与浮动窗口各一个，共用上下文
    // =========================================================
    class D3D12WindowSurface : public IWindowSurface
    {
    public:
        // 失败时抛 std::runtime_error
        D3D12WindowSurface(MiniGUI::D3D12Context& ctx, HWND hwnd, int width, int height)
            : m_ctx(ctx)
        {
            m_width  = width > 0 ? width : 1;
            m_height = height > 0 ? height : 1;
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width            = static_cast<UINT>(m_width);
            desc.Height           = static_cast<UINT>(m_height);
            desc.Format           = kBackBufferFormat;
            desc.BufferCount      = kBufferCount;
            desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.SampleDesc.Count = 1;
            desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Flags            = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

            ComPtr<IDXGISwapChain1> swapChain;
            D3D12ThrowIfFailed(m_ctx.GetFactory()->CreateSwapChainForHwnd(m_ctx.GetQueue(), hwnd, &desc, nullptr, nullptr,
                                                                          swapChain.GetAddressOf()), "CreateSwapChainForHwnd");
            D3D12ThrowIfFailed(swapChain.As(&m_swapChain), "IDXGISwapChain3");
            // 帧延迟 1（同 D3D11 版）：BeginPass 等上一帧交给显示器之后再画，输入到上屏更跟手
            m_swapChain->SetMaximumFrameLatency(1);
            m_frameLatency = m_swapChain->GetFrameLatencyWaitableObject();
            m_ctx.GetFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            CreateBackBuffers();
        }

        ~D3D12WindowSurface() override
        {
            m_ctx.WaitIdle();           // 后备缓冲区要等 GPU 用完
            ReleaseBackBuffers();
            for (auto& rtv : m_backBufferRtv)
            {
                m_ctx.FreeRtv(rtv);
                rtv = {};
            }
            m_swapChain.Reset();
            if (m_frameLatency)
                CloseHandle(m_frameLatency);
        }

        void Resize(int width, int height) override
        {
            if (width <= 0 || height <= 0 || !m_swapChain)
                return;
            // 后备缓冲区的引用必须全部放掉才能 ResizeBuffers：先等 GPU 执行完
            m_ctx.WaitIdle();
            ReleaseBackBuffers();
            m_width  = width;
            m_height = height;
            D3D12ThrowIfFailed(m_swapChain->ResizeBuffers(kBufferCount, static_cast<UINT>(width), static_cast<UINT>(height), kBackBufferFormat,
                                                          DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT), "ResizeBuffers");
            CreateBackBuffers();
        }

        void BeginPass() override
        {
            // 等交换链交还一个后备缓冲区（垂直同步）；窗口被遮挡时最多等 1 秒
            if (m_frameLatency)
                WaitForSingleObjectEx(m_frameLatency, 1000, TRUE);
            m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
            ID3D12Resource* bb = m_backBuffers[m_backBufferIndex].Get();
            m_ctx.Barrier(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_inPass = true;

            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_backBufferRtv[m_backBufferIndex];
            ID3D12GraphicsCommandList* cl = m_ctx.GetList();
            const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
            cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
            m_ctx.SetUiTarget({ rtv, m_width, m_height, kBackBufferFormat });
        }

        void Present() override
        {
            if (m_inPass)
            {
                m_ctx.Barrier(m_backBuffers[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                m_ctx.SetUiTarget({});
                m_inPass = false;
            }
            m_ctx.Submit();
            const HRESULT hr = m_swapChain->Present(1, 0);     // 跟随显示器刷新率
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                LOG_ERROR("D3D12：设备已移除 (0x%08X)", static_cast<unsigned int>(m_ctx.GetDevice()->GetDeviceRemovedReason()));
            m_ctx.EndFrame();
        }

    private:
        void CreateBackBuffers()
        {
            for (UINT i = 0; i < kBufferCount; ++i)
            {
                D3D12ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(m_backBuffers[i].GetAddressOf())), "GetBuffer");
                if (!m_backBufferRtv[i].ptr)
                    m_backBufferRtv[i] = m_ctx.AllocateRtv();
                m_ctx.GetDevice()->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_backBufferRtv[i]);
            }
            m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        }

        void ReleaseBackBuffers()
        {
            for (auto& b : m_backBuffers)
                b.Reset();
        }

        static constexpr UINT kBufferCount = 2;
        static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

        MiniGUI::D3D12Context&      m_ctx;
        ComPtr<IDXGISwapChain3>     m_swapChain;
        HANDLE                      m_frameLatency = nullptr;   // 交换链的帧延迟等待对象
        ComPtr<ID3D12Resource>      m_backBuffers[kBufferCount];
        D3D12_CPU_DESCRIPTOR_HANDLE m_backBufferRtv[kBufferCount] = {};
        UINT                        m_backBufferIndex = 0;
        bool                        m_inPass = false;
        int                         m_width  = 0;
        int                         m_height = 0;
    };

    // =========================================================
    // 设备
    // =========================================================
    D3D12GraphicsDevice::D3D12GraphicsDevice(bool warp)
        : m_warp(warp)
    {
    }

    D3D12GraphicsDevice::~D3D12GraphicsDevice()
    {
        if (m_ctx)
            m_ctx->WaitIdle();          // 交换链的后备缓冲区、渲染器的缓冲都要等 GPU 用完
        m_backend.reset();
        m_renderer.reset();
        m_main.reset();
        m_ctx.reset();
    }

    bool D3D12GraphicsDevice::Initialize(const NativeWindow& window, int width, int height)
    {
        try
        {
            MiniGUI::D3D12Context::Options options;
            options.warp = m_warp;
#if defined(_DEBUG)
            options.debug = true;       // 调试版打开调试层（系统没装 Graphics Tools 时自动跳过）
#endif
            m_ctx = std::make_unique<MiniGUI::D3D12Context>();
            m_ctx->Initialize(options);
            m_main     = std::make_unique<D3D12WindowSurface>(*m_ctx, static_cast<HWND>(window.handle), width, height);
            m_renderer = std::make_unique<D3D12Renderer>(*m_ctx);
            m_backend  = std::make_unique<MiniGUI::D3D12Backend>(*m_ctx);
            LOG_INFO("D3D12：%s", m_ctx->GetAdapterName().c_str());
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("D3D12 初始化失败：%s", e.what());
            m_backend.reset();
            m_renderer.reset();
            m_main.reset();
            m_ctx.reset();
            return false;
        }
        return true;
    }

    std::unique_ptr<IWindowSurface> D3D12GraphicsDevice::CreateWindowSurface(const NativeWindow& window, int width, int height)
    {
        try
        {
            return std::make_unique<D3D12WindowSurface>(*m_ctx, static_cast<HWND>(window.handle), width, height);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("D3D12 窗口表面创建失败：%s", e.what());
            return nullptr;
        }
    }

    void D3D12GraphicsDevice::Resize(int width, int height)
    {
        if (m_main)
            m_main->Resize(width, height);
    }

    void D3D12GraphicsDevice::BeginUiPass()
    {
        m_main->BeginPass();
    }

    void D3D12GraphicsDevice::Present()
    {
        m_main->Present();
    }

    void D3D12GraphicsDevice::WaitIdle()
    {
        m_ctx->WaitIdle();
    }

    void D3D12GraphicsDevice::ReleaseFreeMemory()
    {
        m_ctx->ReleaseFreeMemory();
    }

    IRenderer& D3D12GraphicsDevice::GetRenderer()
    {
        return *m_renderer;
    }

    MiniGUI::IRenderBackend& D3D12GraphicsDevice::GetUiBackend()
    {
        return *m_backend;
    }

    std::unique_ptr<IRenderTarget> D3D12GraphicsDevice::CreateRenderTarget()
    {
        return std::make_unique<D3D12RenderTarget>(*m_ctx);
    }

    MiniGUI::TextureId D3D12GraphicsDevice::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_backend->RegisterExternalTexture(static_cast<MiniGUI::D3D12Texture*>(nativeShaderResource));
    }

    bool D3D12GraphicsDevice::ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba)
    {
        MiniGUI::D3D12Texture* color = static_cast<D3D12RenderTarget&>(target).GetColor();
        if (!color)
            return false;

        const UINT64 rowBytes = static_cast<UINT64>(color->width) * 4;
        const UINT64 rowPitch = (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const UINT64 size     = rowPitch * static_cast<UINT64>(color->height);

        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC   desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = size;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        if (FAILED(m_ctx->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                               IID_PPV_ARGS(readback.GetAddressOf()))))
            return false;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                          = readback.Get();
        dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width    = static_cast<UINT>(color->width);
        dst.PlacedFootprint.Footprint.Height   = static_cast<UINT>(color->height);
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource        = color->resource.Get();
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        color->Transition(*m_ctx, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_ctx->GetList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        m_ctx->WaitIdle();

        void* mapped = nullptr;
        const D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(size) };
        if (FAILED(readback->Map(0, &readRange, &mapped)))
            return false;
        width  = color->width;
        height = color->height;
        rgba.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            std::memcpy(&rgba[static_cast<size_t>(y) * width * 4], static_cast<const uint8_t*>(mapped) + rowPitch * static_cast<UINT64>(y), static_cast<size_t>(rowBytes));
        const D3D12_RANGE noWrite = { 0, 0 };
        readback->Unmap(0, &noWrite);
        return true;
    }
}
