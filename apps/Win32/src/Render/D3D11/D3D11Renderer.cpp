#include "Shader.h" 
#include "D3D11Renderer.h" 
#include "D3D11RenderTarget.h"
#include <d3d11.h>
#include "Render/GpuTypes.hpp"
#include "Render/IRenderer.h"
#include <algorithm>
#include <cstring>
namespace MiniCAD
{
    void checkOverrides()
    {
        // 编译器会在这里报出所有缺失的 override
        D3D11Renderer r(nullptr, nullptr);
    }

    D3D11Renderer::D3D11Renderer(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device)
        , m_context(context)
    {
        Initialize();
        m_lineShader.Initialize(m_device);
        m_textShader.Initialize(m_device);
        m_imageShader.Initialize(m_device);
    }

    void D3D11Renderer::Initialize()
    {
        // ===== VB =====
        D3D11_BUFFER_DESC vb = {};
        vb.ByteWidth      = sizeof(Vertex_P3_C4) * m_maxVertices;
        vb.Usage          = D3D11_USAGE_DYNAMIC;
        vb.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&vb, nullptr, m_vb.GetAddressOf());

        // ===== CB =====
        D3D11_BUFFER_DESC cb = {};
        cb.ByteWidth = sizeof(Float4x4);
        cb.Usage     = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        m_device->CreateBuffer(&cb, nullptr, m_cb.GetAddressOf());

        // ===== Depth =====
        D3D11_DEPTH_STENCIL_DESC d0 = {};
        d0.DepthEnable              = TRUE;
        d0.DepthWriteMask           = D3D11_DEPTH_WRITE_MASK_ALL;
        d0.DepthFunc                = D3D11_COMPARISON_LESS;
        m_device->CreateDepthStencilState(&d0, m_depthEnabled.GetAddressOf());

        D3D11_DEPTH_STENCIL_DESC d1 = {};
        d1.DepthEnable              = FALSE;
        d1.DepthWriteMask           = D3D11_DEPTH_WRITE_MASK_ZERO;
        m_device->CreateDepthStencilState(&d1, m_depthDisabled.GetAddressOf());

        // 透明：只测不写
        D3D11_DEPTH_STENCIL_DESC d2 = {};
        d2.DepthEnable              = TRUE;
        d2.DepthWriteMask           = D3D11_DEPTH_WRITE_MASK_ZERO;
        d2.DepthFunc                = D3D11_COMPARISON_LESS;
        m_device->CreateDepthStencilState(&d2, m_depthReadOnly.GetAddressOf());

        // ===== Rasterizer =====
        D3D11_RASTERIZER_DESC rs    = {};
        rs.FillMode                 = D3D11_FILL_SOLID;
        rs.CullMode                 = D3D11_CULL_NONE;
        rs.DepthClipEnable          = TRUE;
        m_device->CreateRasterizerState(&rs, m_rsNoCull.GetAddressOf());

        // ===== Text VB =====
        D3D11_BUFFER_DESC tvb = {};
        tvb.ByteWidth      = sizeof(Vertex_P3_C4_UV) * m_maxTextVertices;
        tvb.Usage          = D3D11_USAGE_DYNAMIC;
        tvb.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        tvb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&tvb, nullptr, m_textVB.GetAddressOf());

        // ===== Sampler =====
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        m_device->CreateSamplerState(&sd, m_sampler.GetAddressOf());

        // ===== Blend =====
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable    = TRUE;
        bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;

        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        m_device->CreateBlendState(&bd, m_blendAlpha.GetAddressOf());
    }

    void D3D11Renderer::BeginFrame(IRenderTarget& target, const ViewportDesc& vp)
    {  
        m_currentTarget = &target;
        auto& d3dTarget = static_cast<D3D11RenderTarget&>(target);
        auto* rtv       = static_cast<ID3D11RenderTargetView*>(d3dTarget.GetNativeHandle());

        float clear[4] = {};
        auto pso       = m_lineShader.GetPipeline(); 

        D3D11_VIEWPORT d3dVp = {};

        d3dVp.TopLeftX = vp.x;
        d3dVp.TopLeftY = vp.y;
        d3dVp.Width    = vp.width;
        d3dVp.Height   = vp.height;
        d3dVp.MinDepth = vp.minDepth;
        d3dVp.MaxDepth = vp.maxDepth;

        m_context->RSSetViewports(1, &d3dVp);
        m_context->OMSetRenderTargets(1, &rtv, nullptr); // nullptr 深度缓冲区
        m_context->ClearRenderTargetView(rtv, clear);    // 清空
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);
        m_context->RSSetState(m_rsNoCull.Get());
    }

    // 线/三角形通路的公共状态：着色器 + topology + depth + blend + viewProj 常量缓冲
    void D3D11Renderer::SetLineState(const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        // 显式绑定线着色器：保证文字提交之后的线提交仍使用正确管线
        auto pso = m_lineShader.GetPipeline();
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);

        // ===== topology =====
        m_context->IASetPrimitiveTopology(type == PrimitiveType::Line ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ===== depth =====
        if (!depth)
        {
            m_context->OMSetDepthStencilState(m_depthDisabled.Get(), 0);
        }
        else if (blend)
        {
            m_context->OMSetDepthStencilState(m_depthReadOnly.Get(), 0); // 关键
        }
        else
        {
            m_context->OMSetDepthStencilState(m_depthEnabled.Get(), 0);
        }

        // ===== blend =====
        if (blend)
        {
            float f[4] = { 0,0,0,0 };
            m_context->OMSetBlendState(m_blendAlpha.Get(), f, 0xffffffff);
        }
        else
        {
            m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        }

        // ===== matrix =====
        UploadViewProj(viewProj);
    }

    void D3D11Renderer::UploadViewProj(const Math::Mat4& viewProj)
    {
        Float4x4 gpuMat = Float4x4::FromMat4(viewProj.Transposed());

        if (!m_cbValid || memcmp(m_lastViewProj, gpuMat.m, sizeof(m_lastViewProj)) != 0)
        {
            m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, gpuMat.m, 0, 0);
            memcpy(m_lastViewProj, gpuMat.m, sizeof(m_lastViewProj));
            m_cbValid = true;
        }
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    }

    void D3D11Renderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        if (verts.empty()) return;

        SetLineState(viewProj, type, depth, blend);

        UINT stride = sizeof(Vertex_P3_C4);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);

        // ===== batch upload + draw =====
        // 顶点数可能超过顶点缓冲容量(m_maxVertices)，需要分多批绘制。
        // 每批大小按图元顶点数对齐(线=2, 三角=3)，避免跨批拆分图元。
        const size_t primSize    = (type == PrimitiveType::Line) ? 2 : 3;
        const size_t maxPerBatch = ((size_t)m_maxVertices / primSize) * primSize;

        for (size_t base = 0; base < verts.size(); base += maxPerBatch)
        {
            const size_t count = std::min(maxPerBatch, verts.size() - base);

            D3D11_MAPPED_SUBRESOURCE mapped;
            m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            memcpy(mapped.pData, verts.data() + base, count * sizeof(Vertex_P3_C4));
            m_context->Unmap(m_vb.Get(), 0);

            m_context->Draw((UINT)count, 0);
        }
    }

    void D3D11Renderer::SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
    {
        if (quad.empty() || image.Width == 0 || image.Height == 0 || image.Rgba.empty()) return;

        auto it = m_imageTextures.find(image.Key);
        if (it == m_imageTextures.end())
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width            = image.Width;
            td.Height           = image.Height;
            td.MipLevels        = 1;
            td.ArraySize        = 1;
            td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage            = D3D11_USAGE_IMMUTABLE;
            td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem     = image.Rgba.data();
            sd.SysMemPitch = image.Width * 4;

            ComPtr<ID3D11Texture2D>          tex;
            ComPtr<ID3D11ShaderResourceView> srv;
            if (SUCCEEDED(m_device->CreateTexture2D(&td, &sd, tex.GetAddressOf())))
                m_device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
            it = m_imageTextures.emplace(image.Key, std::move(srv)).first;   // 失败也记下，避免每帧重试
        }
        if (!it->second) return;

        auto pso = m_imageShader.GetPipeline();
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);
        m_context->RSSetState(m_rsNoCull.Get());
        m_context->OMSetDepthStencilState(m_depthDisabled.Get(), 0);
        float f[4] = {};
        m_context->OMSetBlendState(m_blendAlpha.Get(), f, 0xffffffff);
        UploadViewProj(viewProj);

        ID3D11ShaderResourceView* srv = it->second.Get();
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

        UINT stride = sizeof(Vertex_P3_C4_UV);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_textVB.GetAddressOf(), &stride, &offset);

        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_textVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, quad.data(), quad.size() * sizeof(Vertex_P3_C4_UV));
        m_context->Unmap(m_textVB.Get(), 0);
        m_context->Draw(static_cast<UINT>(quad.size()), 0);
    }

    // 文字通路的公共状态：文字着色器 + topology + depth + blend + 矩阵 + 纹理
    void D3D11Renderer::SetTextState(const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        auto pso = m_textShader.GetPipeline();
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);
        m_context->RSSetState(m_rsNoCull.Get());

        m_context->OMSetDepthStencilState(depth ? m_depthEnabled.Get() : m_depthDisabled.Get(), 0);
        float f[4] = {};
        m_context->OMSetBlendState(blend ? m_blendAlpha.Get() : nullptr, f, 0xffffffff);

        UploadViewProj(viewProj);

        auto* srv = static_cast<ID3D11ShaderResourceView*>(nativeSRV);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    }

    void D3D11Renderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                                       const Math::Mat4& viewProj,
                                       void* nativeSRV,
                                       bool depth, bool blend)
    {
        if (verts.empty() || !nativeSRV) return;

        SetTextState(viewProj, nativeSRV, depth, blend);

        UINT stride = sizeof(Vertex_P3_C4_UV);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_textVB.GetAddressOf(), &stride, &offset);

        // ===== batch upload + draw =====
        // 顶点数可能超过文字顶点缓冲容量(m_maxTextVertices)，分多批绘制。
        // 文字为三角形列表，每批按 3 个顶点对齐，避免跨批拆分三角形。
        const size_t maxPerBatch = ((size_t)m_maxTextVertices / 3) * 3;

        for (size_t base = 0; base < verts.size(); base += maxPerBatch)
        {
            const size_t count = std::min(maxPerBatch, verts.size() - base);

            D3D11_MAPPED_SUBRESOURCE mapped;
            m_context->Map(m_textVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            memcpy(mapped.pData, verts.data() + base, count * sizeof(Vertex_P3_C4_UV));
            m_context->Unmap(m_textVB.Get(), 0);

            m_context->Draw((UINT)count, 0);
        }
    }

    // (slot, version) 持久顶点缓冲：version 未变直接复用；变化时整体上传一次。
    // 容量不足按 1.5 倍扩容；创建失败返回 nullptr，调用方回退非缓存路径。
    D3D11Renderer::CachedVB* D3D11Renderer::EnsureCachedVB(
        std::unordered_map<uint32_t, CachedVB>& cache,
        uint32_t slot, uint64_t version,
        const void* data, size_t vertCount, size_t vertSize)
    {
        CachedVB& cb = cache[slot];

        if (cb.valid && cb.version == version)
            return &cb;

        if (vertCount > cb.capacity || !cb.vb)
        {
            size_t newCap = std::max<size_t>(vertCount, 1024);
            newCap += newCap / 2;   // 预留空间，减少频繁重建

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = static_cast<UINT>(vertSize * newCap);
            desc.Usage          = D3D11_USAGE_DYNAMIC;
            desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            cb.vb.Reset();
            cb.capacity = 0;
            cb.valid    = false;
            if (FAILED(m_device->CreateBuffer(&desc, nullptr, cb.vb.GetAddressOf())))
            {
                cb.vb.Reset();
                return nullptr;
            }
            cb.capacity = newCap;
        }

        if (vertCount > 0)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (FAILED(m_context->Map(cb.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                cb.valid = false;
                return nullptr;
            }
            memcpy(mapped.pData, data, vertCount * vertSize);
            m_context->Unmap(cb.vb.Get(), 0);
        }

        cb.count   = static_cast<UINT>(vertCount);
        cb.version = version;
        cb.valid   = true;
        return &cb;
    }

    void D3D11Renderer::SubmitCached(uint32_t slot, uint64_t version,
                                     std::span<const Vertex_P3_C4> verts,
                                     const Math::Mat4& viewProj, PrimitiveType type,
                                     bool depth, bool blend)
    {
        CachedVB* cb = EnsureCachedVB(m_cachedVBs, slot, version,
                                      verts.data(), verts.size(), sizeof(Vertex_P3_C4));
        if (!cb)
        {
            Submit(verts, viewProj, type, depth, blend);   // 缓存失败：回退批量上传
            return;
        }
        if (cb->count == 0) return;

        SetLineState(viewProj, type, depth, blend);

        UINT stride = sizeof(Vertex_P3_C4);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, cb->vb.GetAddressOf(), &stride, &offset);
        m_context->Draw(cb->count, 0);
    }

    void D3D11Renderer::SubmitTexturedCached(uint32_t slot, uint64_t version,
                                             std::span<const Vertex_P3_C4_UV> verts,
                                             const Math::Mat4& viewProj, void* nativeSRV,
                                             bool depth, bool blend)
    {
        if (!nativeSRV) return;

        CachedVB* cb = EnsureCachedVB(m_cachedTextVBs, slot, version,
                                      verts.data(), verts.size(), sizeof(Vertex_P3_C4_UV));
        if (!cb)
        {
            SubmitTextured(verts, viewProj, nativeSRV, depth, blend);
            return;
        }
        if (cb->count == 0) return;

        SetTextState(viewProj, nativeSRV, depth, blend);

        UINT stride = sizeof(Vertex_P3_C4_UV);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, cb->vb.GetAddressOf(), &stride, &offset);
        m_context->Draw(cb->count, 0);
    }

    std::unique_ptr<IRenderTarget> D3D11Renderer::CreateLayerTarget()
    {
        return std::make_unique<D3D11RenderTarget>(m_device);
    }

    void D3D11Renderer::DrawLayer(IRenderTarget& layer)
    {
        // GPU 内的整块纹理复制：比重新绘制几十万条线便宜得多（1080p 约 8 MB 显存带宽）
        if (!m_currentTarget)
            return;
        auto* src = static_cast<D3D11RenderTarget&>(layer).GetTexture();
        auto* dst = static_cast<D3D11RenderTarget*>(m_currentTarget)->GetTexture();
        if (src && dst && src != dst
            && layer.GetWidth() == m_currentTarget->GetWidth() && layer.GetHeight() == m_currentTarget->GetHeight())
            m_context->CopyResource(dst, src);
    }

    void* D3D11Renderer::GetNativeDevice()
    {
        return m_device;
    }

    void D3D11Renderer::EndFrame()
    {
        m_currentTarget = nullptr;
        ID3D11RenderTargetView* nullRT[1] = { nullptr };
        m_context->OMSetRenderTargets(1, nullRT, nullptr);

        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(0, 1, nullSRV);
    }
}

