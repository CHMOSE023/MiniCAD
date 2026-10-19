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
        m_glyphShader.Initialize(m_device);
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
        cb.ByteWidth = sizeof(Float4x4) + sizeof(float) * 4;   // viewProj + options（x = 浅色背景）
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
        ID3D11DepthStencilView* dsv = d3dTarget.GetDepthView();
        m_context->OMSetRenderTargets(1, &rtv, dsv);
        m_context->ClearRenderTargetView(rtv, clear);    // 清空
        if (dsv)
            m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
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

        if (!m_cbValid || m_lastLight != m_lightBackground || memcmp(m_lastViewProj, gpuMat.m, sizeof(m_lastViewProj)) != 0)
        {
            float data[20] = {};
            memcpy(data, gpuMat.m, sizeof(gpuMat.m));
            data[16] = m_lightBackground ? 1.0f : 0.0f;
            m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, data, 0, 0);
            memcpy(m_lastViewProj, gpuMat.m, sizeof(m_lastViewProj));
            m_lastLight = m_lightBackground;
            m_cbValid   = true;
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

    void D3D11Renderer::ReleaseCachedResources()
    {
        m_cachedVBs.clear();
        m_cachedTextVBs.clear();
        m_cachedGlyphVBs.clear();
        m_glyphBatches.clear();
        m_glyphMeshes.clear();
        m_glyphFillVB.Reset();
        m_glyphLineVB.Reset();
        m_glyphMeshValid = false;
        m_imageTextures.clear();
    }

    // (slot, version) 持久顶点缓冲：version 未变直接复用；变化时整体上传一次。
    // 容量不足按 1.5 倍扩容，用量不到容量的 1/4 时缩容（大图纸换成小图纸后归还显存）；
    // 创建失败返回 nullptr，调用方回退非缓存路径。
    D3D11Renderer::CachedVB* D3D11Renderer::EnsureCachedVB(
        std::unordered_map<uint32_t, CachedVB>& cache,
        uint32_t slot, uint64_t version,
        const void* data, size_t vertCount, size_t vertSize)
    {
        CachedVB& cb = cache[slot];

        if (cb.valid && cb.version == version)
            return &cb;

        // 大缓冲（场景静态流，几十 MB）：按实际大小创建 DEFAULT 缓冲并带初始数据，旧缓冲直接释放。
        // 动态缓冲每次 WRITE_DISCARD 时驱动要另备一份后备存储（重命名），几十 MB 的缓冲会让内存大幅抖动，
        // 还要按 1.5 倍预留；静态流只在图纸内容变化时才重新上传，不需要 CPU 可写
        constexpr size_t kStaticBytes = 4u << 20;
        if (vertCount * vertSize >= kStaticBytes)
        {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(vertSize * vertCount);
            desc.Usage     = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = data;
            cb.vb.Reset();
            cb.capacity = 0;
            cb.valid    = false;
            if (FAILED(m_device->CreateBuffer(&desc, &init, cb.vb.GetAddressOf())))
            {
                cb.vb.Reset();
                return nullptr;
            }
            cb.capacity = vertCount;
            cb.immutable = true;
            cb.count   = static_cast<UINT>(vertCount);
            cb.version = version;
            cb.valid   = true;
            return &cb;
        }

        const bool shrink = cb.capacity > 65536 && vertCount * 4 < cb.capacity;
        if (vertCount > cb.capacity || !cb.vb || shrink || cb.immutable)
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
            cb.capacity  = newCap;
            cb.immutable = false;
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

    bool D3D11Renderer::IsCached(uint32_t slot, uint64_t version, bool textured) const
    {
        const auto& cache = textured ? m_cachedTextVBs : m_cachedVBs;
        auto it = cache.find(slot);
        return it != cache.end() && it->second.valid && it->second.version == version;
    }

    // 字形网格库：版本变化时整体重新创建两个 DEFAULT 缓冲（几 MB，只在新增字形时变化）
    bool D3D11Renderer::EnsureGlyphMeshes(const GlyphMeshView& meshes)
    {
        if (m_glyphMeshValid && m_glyphMeshVersion == meshes.Version)
            return true;
        m_glyphMeshValid = false;
        auto create = [&](std::span<const Math::Float2> verts, ComPtr<ID3D11Buffer>& out)
        {
            out.Reset();
            if (verts.empty()) return true;
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(verts.size_bytes());
            desc.Usage     = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = verts.data();
            return SUCCEEDED(m_device->CreateBuffer(&desc, &init, out.GetAddressOf()));
        };
        if (!create(meshes.FillVerts, m_glyphFillVB) || !create(meshes.LineVerts, m_glyphLineVB))
            return false;
        m_glyphMeshes.assign(meshes.Meshes.begin(), meshes.Meshes.end());
        m_glyphMeshVersion = meshes.Version;
        m_glyphMeshValid   = true;
        return true;
    }

    void D3D11Renderer::SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                           std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                           const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        const bool fresh = !IsGlyphCached(slot, version, m_glyphMeshVersion);   // 实例这次要重新上传
        CachedVB* ib = EnsureCachedVB(m_cachedGlyphVBs, slot, version, instances.data(), instances.size(), sizeof(GlyphInstance));
        if (!ib || !EnsureGlyphMeshes(meshes))
        {
            IRenderer::SubmitGlyphsCached(slot, version, meshes, instances, batches, viewProj, type, depth, blend);
            return;
        }
        if (fresh)
            m_glyphBatches[slot].assign(batches.begin(), batches.end());
        if (ib->count == 0) return;

        ID3D11Buffer* meshVB = type == PrimitiveType::Triangle ? m_glyphFillVB.Get() : m_glyphLineVB.Get();
        if (!meshVB) return;

        SetLineState(viewProj, type, depth, blend);
        auto pso = m_glyphShader.GetPipeline();
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);

        ID3D11Buffer* vbs[2]     = { meshVB, ib->vb.Get() };
        UINT          strides[2] = { sizeof(Math::Float2), sizeof(GlyphInstance) };
        UINT          offsets[2] = { 0, 0 };
        m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
        for (const GlyphBatch& b : m_glyphBatches[slot])
        {
            if (b.mesh >= m_glyphMeshes.size()) continue;
            const GlyphMeshRange& m = m_glyphMeshes[b.mesh];
            const UINT n     = type == PrimitiveType::Triangle ? m.fillCount : m.lineCount;
            const UINT start = type == PrimitiveType::Triangle ? m.fillStart : m.lineStart;
            if (n) m_context->DrawInstanced(n, b.count, start, b.first);
        }
        // 槽 1 解绑：之后的提交只用槽 0
        ID3D11Buffer* none = nullptr;
        UINT zero = 0;
        m_context->IASetVertexBuffers(1, 1, &none, &zero, &zero);
    }

    bool D3D11Renderer::IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const
    {
        auto it = m_cachedGlyphVBs.find(slot);
        return it != m_cachedGlyphVBs.end() && it->second.valid && it->second.version == version
            && m_glyphMeshValid && m_glyphMeshVersion == meshVersion;
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

