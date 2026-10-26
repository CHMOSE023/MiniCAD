#include "D3D12Capture.h"
#include <cstring>
#include <exception>

using Microsoft::WRL::ComPtr;

namespace MiniGUI::Test
{
    D3D12Capture::~D3D12Capture()
    {
        if (!m_ctx)
            return;
        m_ctx->WaitIdle();
        m_backend.reset();
        m_ctx->ReleaseTexture(m_target);
        m_ctx->FreeRtv(m_rtv);
    }

    bool D3D12Capture::Initialize(bool forceWarp)
    {
        // 硬件设备建不出来时退回 WARP
        for (const bool warp : { forceWarp, true })
        {
            try
            {
                m_ctx = std::make_unique<D3D12Context>();
                D3D12Context::Options options;
                options.warp = warp;
                m_ctx->Initialize(options);
                m_backend = std::make_unique<D3D12Backend>(*m_ctx);
                return true;
            }
            catch (const std::exception&)
            {
                m_backend.reset();
                m_ctx.reset();
            }
            if (warp)
                break;
        }
        return false;
    }

    void D3D12Capture::BeginFrame(int width, int height, Color32 clearColor)
    {
        if (width != m_width || height != m_height || !m_target)
        {
            // 与 SoftwareImage 相同的 RGBA 字节顺序，读回时不用交换通道
            m_ctx->ReleaseTexture(m_target);
            m_target = m_ctx->CreateTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (!m_rtv.ptr)
                m_rtv = m_ctx->AllocateRtv();
            m_ctx->GetDevice()->CreateRenderTargetView(m_target->resource.Get(), nullptr, m_rtv);
            m_width  = width;
            m_height = height;
        }

        const float c[4] = { static_cast<float>(clearColor & 0xFF) / 255.0f,
                             static_cast<float>((clearColor >> 8) & 0xFF) / 255.0f,
                             static_cast<float>((clearColor >> 16) & 0xFF) / 255.0f,
                             static_cast<float>(clearColor >> 24) / 255.0f };
        m_target->Transition(*m_ctx, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_ctx->GetList()->ClearRenderTargetView(m_rtv, c, 0, nullptr);
        m_ctx->SetUiTarget({ m_rtv, width, height, DXGI_FORMAT_R8G8B8A8_UNORM });
    }

    SoftwareImage D3D12Capture::Readback()
    {
        SoftwareImage img;
        const UINT64 rowBytes = static_cast<UINT64>(m_width) * 4;
        const UINT64 rowPitch = (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC   desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = rowPitch * static_cast<UINT64>(m_height);
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        if (FAILED(m_ctx->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                               IID_PPV_ARGS(readback.GetAddressOf()))))
            return img;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                          = readback.Get();
        dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width    = static_cast<UINT>(m_width);
        dst.PlacedFootprint.Footprint.Height   = static_cast<UINT>(m_height);
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_target->resource.Get();
        src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        m_ctx->SetUiTarget({});
        m_target->Transition(*m_ctx, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_ctx->GetList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        m_ctx->WaitIdle();
        m_ctx->EndFrame();

        void* mapped = nullptr;
        if (FAILED(readback->Map(0, nullptr, &mapped)))
            return img;
        img.width  = m_width;
        img.height = m_height;
        img.pixels.resize(static_cast<size_t>(m_width) * m_height);
        for (int y = 0; y < m_height; ++y)
            std::memcpy(&img.At(0, y), static_cast<const uint8_t*>(mapped) + rowPitch * static_cast<UINT64>(y), static_cast<size_t>(rowBytes));
        const D3D12_RANGE noWrite = { 0, 0 };
        readback->Unmap(0, &noWrite);
        return img;
    }
}
