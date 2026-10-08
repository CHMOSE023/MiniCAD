#include "D3D11Capture.h"
#include <dxgi.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace MiniGUI::Test
{
    bool D3D11Capture::Initialize(bool forceWarp)
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        HRESULT hr = E_FAIL;
        if (!forceWarp)
        {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, m_device.GetAddressOf(), nullptr, m_context.GetAddressOf());
        }
        if (FAILED(hr))
        {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, m_device.GetAddressOf(), nullptr, m_context.GetAddressOf());
        }
        if (FAILED(hr))
            return false;

        // 显卡名称，写进测试输出便于排查"只在某台机器上失败"的问题
        ComPtr<IDXGIDevice>  dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC    desc = {};
        if (SUCCEEDED(m_device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) &&
            SUCCEEDED(adapter->GetDesc(&desc)))
        {
            const int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (len > 1)
            {
                m_adapterName.resize(static_cast<size_t>(len - 1));
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, m_adapterName.data(), len, nullptr, nullptr);
            }
        }

        m_backend = std::make_unique<D3D11Backend>(m_device.Get(), m_context.Get());
        return true;
    }

    void D3D11Capture::BeginFrame(int width, int height, Color32 clearColor)
    {
        if (width != m_width || height != m_height || !m_target)
        {
            // 与 SoftwareImage 相同的 RGBA 字节顺序，读回时不用交换通道
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width            = static_cast<UINT>(width);
            desc.Height           = static_cast<UINT>(height);
            desc.MipLevels        = 1;
            desc.ArraySize        = 1;
            desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage            = D3D11_USAGE_DEFAULT;
            desc.BindFlags        = D3D11_BIND_RENDER_TARGET;

            m_rtv.Reset();
            m_target.Reset();
            m_device->CreateTexture2D(&desc, nullptr, m_target.GetAddressOf());
            m_device->CreateRenderTargetView(m_target.Get(), nullptr, m_rtv.GetAddressOf());
            m_width  = width;
            m_height = height;
        }

        const float c[4] = { static_cast<float>(clearColor & 0xFF) / 255.0f,
                             static_cast<float>((clearColor >> 8) & 0xFF) / 255.0f,
                             static_cast<float>((clearColor >> 16) & 0xFF) / 255.0f,
                             static_cast<float>(clearColor >> 24) / 255.0f };
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        m_context->ClearRenderTargetView(m_rtv.Get(), c);
    }

    SoftwareImage D3D11Capture::Readback()
    {
        SoftwareImage img;

        D3D11_TEXTURE2D_DESC desc = {};
        m_target->GetDesc(&desc);
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
            return img;
        m_context->CopyResource(staging.Get(), m_target.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return img;

        img.width  = m_width;
        img.height = m_height;
        img.pixels.resize(static_cast<size_t>(m_width) * m_height);
        for (int y = 0; y < m_height; ++y)
        {
            std::memcpy(&img.At(0, y), static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                        static_cast<size_t>(m_width) * 4);
        }
        m_context->Unmap(staging.Get(), 0);
        return img;
    }
}
