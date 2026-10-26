#include "D3D12/D3D12Renderer.h"
#include "D3D12/D3D12RenderTarget.h"
#include "Hlsl/ViewportShaders.h"
#include "GpuTypes.hpp"
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;
using MiniGUI::D3D12Buffer;
using MiniGUI::D3D12Texture;
using MiniGUI::D3D12ThrowIfFailed;

namespace MiniCAD
{
    namespace
    {
        constexpr size_t kStaticBytes   = 4u << 20;  // 这么大的缓存提交按实际大小建缓冲（场景静态流），不预留
        constexpr size_t kBatchVertices = 65536;     // 非缓存提交每批的顶点数（同 D3D11 的动态顶点缓冲）

        // 根常量的布局与 HLSL 的 cbuffer VSConstants 相同：float4x4 vp（已转置）+ float4 options
        struct RootConstants
        {
            float vp[16];
            float options[4];
        };

        const D3D12_INPUT_ELEMENT_DESC kLineLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },   // VertexColor：RGBA 各 8 位
        };
        // 字形实例：槽 0 为字形网格的局部顶点（float2），槽 1 为逐实例的仿射变换与颜色
        const D3D12_INPUT_ELEMENT_DESC kGlyphLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,  0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT,    1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     1, 28, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };
        // 纹理文字与光栅图像：Vertex_P3_C4_UV
        const D3D12_INPUT_ELEMENT_DESC kTexturedLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_PRIMITIVE_TOPOLOGY Topology(PrimitiveType type)
        {
            return type == PrimitiveType::Line  ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
                 : type == PrimitiveType::Point ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
                                                : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }

        size_t PrimitiveSize(PrimitiveType type)
        {
            return type == PrimitiveType::Line ? 2 : type == PrimitiveType::Point ? 1 : 3;
        }
    }

    D3D12Renderer::D3D12Renderer(MiniGUI::D3D12Context& ctx)
        : m_ctx(ctx)
    {
        const char* sources[4] = { Hlsl::kLine, Hlsl::kGlyph, Hlsl::kText, Hlsl::kImage };
        for (int i = 0; i < 4; ++i)
        {
            m_vs[i] = MiniGUI::D3D12Context::CompileShader(sources[i], "VSMain", "vs_5_1", 0);
            m_ps[i] = MiniGUI::D3D12Context::CompileShader(sources[i], "PSMain", "ps_5_1", 0);
        }

        // 根签名：b0 = 根常量（RootConstants），t0 = 纹理（文字图集 / 光栅图像），s0 = 线性过滤、边缘截断
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = sizeof(RootConstants) / 4;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs = {};
        rs.NumParameters     = 2;
        rs.pParameters       = params;
        rs.NumStaticSamplers = 1;
        rs.pStaticSamplers   = &sampler;
        rs.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, errors;
        D3D12ThrowIfFailed(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf()), "SerializeRootSignature(视口)");
        D3D12ThrowIfFailed(m_ctx.GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                                  IID_PPV_ARGS(m_rootSignature.GetAddressOf())), "CreateRootSignature(视口)");
    }

    D3D12Renderer::~D3D12Renderer()
    {
        ReleaseCachedResources();
    }

    // 管线状态按 (着色器, 图元类型, 深度模式, 混合) 首次用到时创建；组合只有几十种
    ID3D12PipelineState* D3D12Renderer::GetPipeline(Shader shader, PrimitiveType type, Depth depth, bool blend)
    {
        const uint32_t key = static_cast<uint32_t>(shader) | (static_cast<uint32_t>(type) << 4) | (static_cast<uint32_t>(depth) << 8) | (blend ? 1u << 12 : 0u);
        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end())
            return it->second.Get();

        const int s = static_cast<int>(shader);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = m_rootSignature.Get();
        pd.VS             = { m_vs[s]->GetBufferPointer(), m_vs[s]->GetBufferSize() };
        pd.PS             = { m_ps[s]->GetBufferPointer(), m_ps[s]->GetBufferSize() };
        switch (shader)
        {
        case Shader::Line:  pd.InputLayout = { kLineLayout,     _countof(kLineLayout) };     break;
        case Shader::Glyph: pd.InputLayout = { kGlyphLayout,    _countof(kGlyphLayout) };    break;
        default:            pd.InputLayout = { kTexturedLayout, _countof(kTexturedLayout) }; break;
        }

        // 混合：颜色 SrcAlpha / InvSrcAlpha，alpha 取源值（同 D3D11Renderer）
        D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[0];
        b.BlendEnable           = blend ? TRUE : FALSE;
        b.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOp               = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha         = D3D12_BLEND_ONE;
        b.DestBlendAlpha        = D3D12_BLEND_ZERO;
        b.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        b.LogicOp               = D3D12_LOGIC_OP_NOOP;
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;

        // 深度：Write = 测试并写入；ReadOnly = 透明物体只测不写；Off = 不测
        pd.DepthStencilState.DepthEnable    = depth != Depth::Off ? TRUE : FALSE;
        pd.DepthStencilState.DepthWriteMask = depth == Depth::Write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
        pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = type == PrimitiveType::Line  ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                 : type == PrimitiveType::Point ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
                                                                : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pso;
        D3D12ThrowIfFailed(m_ctx.GetDevice()->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(pso.GetAddressOf())), "CreateGraphicsPipelineState(视口)");
        return m_pipelines.emplace(key, std::move(pso)).first->second.Get();
    }

    // =========================================================
    // 帧
    // =========================================================
    void D3D12Renderer::BeginFrame(IRenderTarget& target, const ViewportDesc& viewport)
    {
        auto& rt = static_cast<D3D12RenderTarget&>(target);
        m_current  = rt.GetColor() ? &rt : nullptr;
        m_viewport = viewport;
        if (!m_current)
            return;

        rt.GetColor()->Transition(m_ctx, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ApplyFrameState();
        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        const float clear[4] = {};
        cl->ClearRenderTargetView(rt.GetRtv(), clear, 0, nullptr);
        cl->ClearDepthStencilView(rt.GetDsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    void D3D12Renderer::ApplyFrameState()
    {
        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_current->GetRtv();
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_current->GetDsv();
        cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        D3D12_VIEWPORT vp = { m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height, m_viewport.minDepth, m_viewport.maxDepth };
        cl->RSSetViewports(1, &vp);
        // D3D12 的裁剪矩形总是打开：取整个渲染目标（D3D11 不开裁剪，只按视口裁）
        const D3D12_RECT scissor = { 0, 0, m_current->GetWidth(), m_current->GetHeight() };
        cl->RSSetScissorRects(1, &scissor);
        cl->SetGraphicsRootSignature(m_rootSignature.Get());
        m_ctx.BindSrvHeap();
        m_boundPipeline = nullptr;
        m_stateEpoch    = m_ctx.GetEpoch();
    }

    bool D3D12Renderer::Bind(Shader shader, PrimitiveType type, Depth depth, bool blend, const Math::Mat4& viewProj)
    {
        if (!m_current)
            return false;
        if (m_stateEpoch != m_ctx.GetEpoch())
            ApplyFrameState();          // 帧中途提交过（WaitIdle / 读回像素），新列表里状态要重设

        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        ID3D12PipelineState* pso = GetPipeline(shader, type, depth, blend);
        if (pso != m_boundPipeline)
        {
            cl->SetPipelineState(pso);
            m_boundPipeline = pso;
        }
        cl->IASetPrimitiveTopology(Topology(type));

        RootConstants rc = {};
        const Float4x4 gpuMat = Float4x4::FromMat4(viewProj.Transposed());
        std::memcpy(rc.vp, gpuMat.m, sizeof(rc.vp));
        rc.options[0] = m_lightBackground ? 1.0f : 0.0f;
        cl->SetGraphicsRoot32BitConstants(0, sizeof(RootConstants) / 4, &rc, 0);
        return true;
    }

    bool D3D12Renderer::BindTexture(D3D12Texture* texture)
    {
        if (!texture || texture->srv == D3D12Texture::kNoSrv)
            return false;
        texture->Transition(m_ctx, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_ctx.BindSrvHeap();
        m_ctx.GetList()->SetGraphicsRootDescriptorTable(1, m_ctx.GetSrvGpu(texture->srv));
        return true;
    }

    void D3D12Renderer::EndFrame()
    {
        m_current = nullptr;
    }

    // =========================================================
    // 非缓存提交
    // =========================================================
    void D3D12Renderer::DrawStreamed(const void* data, size_t count, size_t stride, size_t primSize)
    {
        // 顶点数可能很多，分批放进上传缓冲；每批按图元顶点数对齐，避免跨批拆分图元
        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        const size_t maxPerBatch = (kBatchVertices / primSize) * primSize;
        for (size_t base = 0; base < count; base += maxPerBatch)
        {
            const size_t n = std::min(maxPerBatch, count - base);
            const MiniGUI::D3D12Context::Upload up = m_ctx.AllocateUpload(n * stride, 16);
            std::memcpy(up.cpu, static_cast<const uint8_t*>(data) + base * stride, n * stride);
            const D3D12_VERTEX_BUFFER_VIEW vbv = { up.gpu, static_cast<UINT>(n * stride), static_cast<UINT>(stride) };
            cl->IASetVertexBuffers(0, 1, &vbv);
            cl->DrawInstanced(static_cast<UINT>(n), 1, 0, 0);
        }
    }

    void D3D12Renderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        if (verts.empty() || !Bind(Shader::Line, type, LineDepth(depth, blend), blend, viewProj))
            return;
        DrawStreamed(verts.data(), verts.size(), sizeof(Vertex_P3_C4), PrimitiveSize(type));
    }

    void D3D12Renderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        if (verts.empty() || !nativeSRV)
            return;
        if (!Bind(Shader::Text, PrimitiveType::Triangle, depth ? Depth::Write : Depth::Off, blend, viewProj)
            || !BindTexture(static_cast<D3D12Texture*>(nativeSRV)))
            return;
        DrawStreamed(verts.data(), verts.size(), sizeof(Vertex_P3_C4_UV), 3);
    }

    void D3D12Renderer::SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
    {
        if (quad.empty() || image.Width == 0 || image.Height == 0 || image.Rgba.empty() || !m_current)
            return;

        auto it = m_imageTextures.find(image.Key);
        if (it == m_imageTextures.end())
        {
            auto texture = m_ctx.CreateTexture(static_cast<int>(image.Width), static_cast<int>(image.Height), DXGI_FORMAT_R8G8B8A8_UNORM,
                                               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            if (texture)
                m_ctx.UploadTexture(*texture, 0, 0, texture->width, texture->height, image.Rgba.data(), texture->width * 4);
            it = m_imageTextures.emplace(image.Key, std::move(texture)).first;     // 失败也记下，避免每帧重试
        }
        if (!it->second)
            return;

        if (!Bind(Shader::Image, PrimitiveType::Triangle, Depth::Off, true, viewProj) || !BindTexture(it->second.get()))
            return;
        DrawStreamed(quad.data(), quad.size(), sizeof(Vertex_P3_C4_UV), 3);
    }

    // =========================================================
    // 缓存提交
    // =========================================================
    void D3D12Renderer::ReleaseBuffer(D3D12Buffer& buffer)
    {
        m_ctx.Defer(buffer.resource);
        buffer = {};
    }

    void D3D12Renderer::ReleaseCachedResources()
    {
        for (auto* cache : { &m_cachedVBs, &m_cachedTextVBs, &m_cachedGlyphVBs })
        {
            for (auto& [slot, cb] : *cache)
                ReleaseBuffer(cb.buffer);
            cache->clear();
        }
        m_glyphBatches.clear();
        m_glyphMeshes.clear();
        ReleaseBuffer(m_glyphFillVB);
        ReleaseBuffer(m_glyphLineVB);
        m_glyphMeshValid = false;
        for (auto& [key, texture] : m_imageTextures)
            m_ctx.ReleaseTexture(texture);
        m_imageTextures.clear();
    }

    // (slot, version) 持久顶点缓冲：version 未变直接复用；变化时经上传缓冲复制一次。
    // 容量不足按 1.5 倍扩容，用量不到容量的 1/4 时缩容（大图纸换成小图纸后归还显存）；
    // 大缓冲（场景静态流，几十 MB）按实际大小新建，旧缓冲延迟释放。创建失败返回 nullptr，调用方回退非缓存路径
    D3D12Renderer::CachedVB* D3D12Renderer::EnsureCachedVB(std::unordered_map<uint32_t, CachedVB>& cache, uint32_t slot, uint64_t version,
                                                           const void* data, size_t vertCount, size_t vertSize)
    {
        CachedVB& cb = cache[slot];
        if (cb.valid && cb.version == version)
            return &cb;

        const size_t bytes = vertCount * vertSize;
        if (bytes >= kStaticBytes)
        {
            ReleaseBuffer(cb.buffer);
            cb.capacity = 0;
            cb.valid    = false;
            cb.buffer   = m_ctx.CreateBuffer(bytes);
            if (!cb.buffer.resource)
                return nullptr;
            cb.capacity  = vertCount;
            cb.immutable = true;
        }
        else
        {
            const bool shrink = cb.capacity > 65536 && vertCount * 4 < cb.capacity;
            if (vertCount > cb.capacity || !cb.buffer.resource || shrink || cb.immutable)
            {
                size_t newCap = std::max<size_t>(vertCount, 1024);
                newCap += newCap / 2;   // 预留空间，减少频繁重建

                ReleaseBuffer(cb.buffer);
                cb.capacity = 0;
                cb.valid    = false;
                cb.buffer   = m_ctx.CreateBuffer(newCap * vertSize);
                if (!cb.buffer.resource)
                    return nullptr;
                cb.capacity  = newCap;
                cb.immutable = false;
            }
        }

        // 旧内容可能还被本列表里之前的绘制读着：UploadBuffer 先转到 COPY_DEST，屏障保证先读后写
        m_ctx.UploadBuffer(cb.buffer, 0, data, bytes);
        cb.count   = static_cast<UINT>(vertCount);
        cb.stride  = static_cast<UINT>(vertSize);
        cb.version = version;
        cb.valid   = true;
        return &cb;
    }

    void D3D12Renderer::SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                                     const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        CachedVB* cb = EnsureCachedVB(m_cachedVBs, slot, version, verts.data(), verts.size(), sizeof(Vertex_P3_C4));
        if (!cb)
        {
            Submit(verts, viewProj, type, depth, blend);   // 缓存失败：回退批量上传
            return;
        }
        if (cb->count == 0 || !Bind(Shader::Line, type, LineDepth(depth, blend), blend, viewProj))
            return;

        cb->buffer.Transition(m_ctx, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        const D3D12_VERTEX_BUFFER_VIEW vbv = { cb->buffer.resource->GetGPUVirtualAddress(), cb->count * cb->stride, cb->stride };
        m_ctx.GetList()->IASetVertexBuffers(0, 1, &vbv);
        m_ctx.GetList()->DrawInstanced(cb->count, 1, 0, 0);
    }

    void D3D12Renderer::SubmitTexturedCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4_UV> verts,
                                             const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        if (!nativeSRV)
            return;
        CachedVB* cb = EnsureCachedVB(m_cachedTextVBs, slot, version, verts.data(), verts.size(), sizeof(Vertex_P3_C4_UV));
        if (!cb)
        {
            SubmitTextured(verts, viewProj, nativeSRV, depth, blend);
            return;
        }
        if (cb->count == 0
            || !Bind(Shader::Text, PrimitiveType::Triangle, depth ? Depth::Write : Depth::Off, blend, viewProj)
            || !BindTexture(static_cast<D3D12Texture*>(nativeSRV)))
            return;

        cb->buffer.Transition(m_ctx, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        const D3D12_VERTEX_BUFFER_VIEW vbv = { cb->buffer.resource->GetGPUVirtualAddress(), cb->count * cb->stride, cb->stride };
        m_ctx.GetList()->IASetVertexBuffers(0, 1, &vbv);
        m_ctx.GetList()->DrawInstanced(cb->count, 1, 0, 0);
    }

    bool D3D12Renderer::IsCached(uint32_t slot, uint64_t version, bool textured) const
    {
        const auto& cache = textured ? m_cachedTextVBs : m_cachedVBs;
        auto it = cache.find(slot);
        return it != cache.end() && it->second.valid && it->second.version == version;
    }

    // 字形网格库：版本变化时整体重新创建两个缓冲（几 MB，只在新增字形时变化）
    bool D3D12Renderer::EnsureGlyphMeshes(const GlyphMeshView& meshes)
    {
        if (m_glyphMeshValid && m_glyphMeshVersion == meshes.Version)
            return true;
        m_glyphMeshValid = false;
        auto create = [&](std::span<const Math::Float2> verts, D3D12Buffer& out)
        {
            ReleaseBuffer(out);
            if (verts.empty())
                return true;
            out = m_ctx.CreateBuffer(verts.size_bytes());
            if (!out.resource)
                return false;
            m_ctx.UploadBuffer(out, 0, verts.data(), verts.size_bytes());
            return true;
        };
        if (!create(meshes.FillVerts, m_glyphFillVB) || !create(meshes.LineVerts, m_glyphLineVB))
            return false;
        m_glyphMeshes.assign(meshes.Meshes.begin(), meshes.Meshes.end());
        m_glyphMeshVersion = meshes.Version;
        m_glyphMeshValid   = true;
        return true;
    }

    void D3D12Renderer::SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
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
        if (ib->count == 0)
            return;

        D3D12Buffer& meshVB = type == PrimitiveType::Triangle ? m_glyphFillVB : m_glyphLineVB;
        if (!meshVB.resource || !Bind(Shader::Glyph, type, LineDepth(depth, blend), blend, viewProj))
            return;

        meshVB.Transition(m_ctx, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        ib->buffer.Transition(m_ctx, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        const D3D12_VERTEX_BUFFER_VIEW views[2] =
        {
            { meshVB.resource->GetGPUVirtualAddress(),     static_cast<UINT>(meshVB.size), sizeof(Math::Float2) },
            { ib->buffer.resource->GetGPUVirtualAddress(), ib->count * ib->stride,         sizeof(GlyphInstance) },
        };
        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        cl->IASetVertexBuffers(0, 2, views);
        for (const GlyphBatch& b : m_glyphBatches[slot])
        {
            if (b.mesh >= m_glyphMeshes.size())
                continue;
            const GlyphMeshRange& m = m_glyphMeshes[b.mesh];
            const UINT n     = type == PrimitiveType::Triangle ? m.fillCount : m.lineCount;
            const UINT start = type == PrimitiveType::Triangle ? m.fillStart : m.lineStart;
            if (n)
                cl->DrawInstanced(n, b.count, start, b.first);
        }
    }

    bool D3D12Renderer::IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const
    {
        auto it = m_cachedGlyphVBs.find(slot);
        return it != m_cachedGlyphVBs.end() && it->second.valid && it->second.version == version
            && m_glyphMeshValid && m_glyphMeshVersion == meshVersion;
    }

    // =========================================================
    // 缓存层
    // =========================================================
    std::unique_ptr<IRenderTarget> D3D12Renderer::CreateLayerTarget()
    {
        return std::make_unique<D3D12RenderTarget>(m_ctx);
    }

    void D3D12Renderer::DrawLayer(IRenderTarget& layer)
    {
        // GPU 内的整块纹理复制（只复制颜色；深度在 BeginFrame 已清空，与 D3D11 相同）
        if (!m_current)
            return;
        D3D12Texture* src = static_cast<D3D12RenderTarget&>(layer).GetColor();
        D3D12Texture* dst = m_current->GetColor();
        if (!src || !dst || src == dst || layer.GetWidth() != m_current->GetWidth() || layer.GetHeight() != m_current->GetHeight())
            return;
        src->Transition(m_ctx, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst->Transition(m_ctx, D3D12_RESOURCE_STATE_COPY_DEST);
        m_ctx.GetList()->CopyResource(dst->resource.Get(), src->resource.Get());
        dst->Transition(m_ctx, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
}
