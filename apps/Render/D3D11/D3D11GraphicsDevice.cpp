#include "D3D11/D3D11GraphicsDevice.h"
#include "D3D11/D3D11Backend.h"
#include "D3D11/D3D11Renderer.h"
#include "D3D11/D3D11RenderTarget.h"
#include "Core/Log.h"
#include <cstring>
#include <exception>

namespace MiniCAD
{
    D3D11GraphicsDevice::D3D11GraphicsDevice() = default;
    D3D11GraphicsDevice::~D3D11GraphicsDevice() = default;

    bool D3D11GraphicsDevice::Initialize(const NativeWindow& window, int width, int height)
    {
        try
        {
            m_device = std::make_unique<Device>();
            m_device->Initialize();

            SwapChain::Options opt;
            opt.enableVSync  = true;    // 帧率钉在刷新率；输入在 Present 之前全部处理完，不会因此晚一帧
            opt.allowTearing = false;
            m_swapChain = std::make_unique<SwapChain>();
            m_swapChain->Initialize(m_device.get(), static_cast<HWND>(window.handle), width, height, opt);

            m_renderer = std::make_unique<D3D11Renderer>(m_device->GetDevice(), m_device->GetContext());
            m_backend  = std::make_unique<MiniGUI::D3D11Backend>(m_device->GetDevice(), m_device->GetContext());
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("D3D11 初始化失败：%s", e.what());
            return false;
        }
        return true;
    }

    void D3D11GraphicsDevice::Resize(int width, int height)
    {
        m_swapChain->Resize(static_cast<UINT>(width), static_cast<UINT>(height));
    }

    void D3D11GraphicsDevice::BeginUiPass()
    {
        ID3D11DeviceContext*    ctx = m_device->GetContext();
        ID3D11RenderTargetView* rtv = m_swapChain->GetRTV();
        const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
    }

    void D3D11GraphicsDevice::Present()
    {
        m_swapChain->Present();
    }

    void D3D11GraphicsDevice::WaitIdle()
    {
        // 事件查询：GPU 执行完之前提交的全部命令后才返回（只用于自测计时）
        D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &query)))
            return;
        ID3D11DeviceContext* ctx = m_device->GetContext();
        ctx->End(query.Get());
        BOOL done = FALSE;
        while (ctx->GetData(query.Get(), &done, sizeof(done), 0) != S_OK || !done)
            ;
    }

    void D3D11GraphicsDevice::ReleaseFreeMemory()
    {
        // 已释放的 D3D11 缓冲要等命令提交后驱动才真正销毁
        m_device->GetContext()->Flush();
    }

    MiniGUI::IRenderBackend& D3D11GraphicsDevice::GetUiBackend()
    {
        return *m_backend;
    }

    std::unique_ptr<IRenderTarget> D3D11GraphicsDevice::CreateRenderTarget()
    {
        return std::make_unique<D3D11RenderTarget>(m_device->GetDevice());
    }

    MiniGUI::TextureId D3D11GraphicsDevice::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_backend->RegisterExternalTexture(static_cast<ID3D11ShaderResourceView*>(nativeShaderResource));
    }

    bool D3D11GraphicsDevice::ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba)
    {
        ID3D11Device*    device  = m_device->GetDevice();
        ID3D11Texture2D* texture = static_cast<D3D11RenderTarget&>(target).GetTexture();
        if (!texture)
            return false;
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
            return false;
        ID3D11DeviceContext* context = m_device->GetContext();
        context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        width  = static_cast<int>(desc.Width);
        height = static_cast<int>(desc.Height);
        rgba.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            std::memcpy(&rgba[static_cast<size_t>(y) * width * 4], static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, static_cast<size_t>(width) * 4);
        context->Unmap(staging.Get(), 0);
        return true;
    }
}
