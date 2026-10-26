#include "D3D12/D3D12Backend.h"
#include "Paint/DrawList.h"
#include "Hlsl/UiShader.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace MiniGUI
{
    namespace
    {
        struct VSConstants
        {
            float scale[2];
            float translate[2];
        };
    }

    D3D12Backend::D3D12Backend(D3D12Context& context)
        : m_ctx(context)
    {
        // ── 着色器：与 D3D11Backend 同一份 HLSL ─────────────────
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        m_vs = D3D12Context::CompileShader(Hlsl::kUi, "VSMain", "vs_5_1", flags);
        m_ps = D3D12Context::CompileShader(Hlsl::kUi, "PSMain", "ps_5_1", flags);

        // ── 根签名：b0 = 根常量（scale + translate），t0 = 纹理，s0 = 线性过滤、边缘截断 ──
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors     = 1;
        range.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = sizeof(VSConstants) / 4;
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
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs = {};
        rs.NumParameters     = 2;
        rs.pParameters       = params;
        rs.NumStaticSamplers = 1;
        rs.pStaticSamplers   = &sampler;
        rs.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, errors;
        D3D12ThrowIfFailed(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf()), "SerializeRootSignature(界面)");
        D3D12ThrowIfFailed(m_ctx.GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                                  IID_PPV_ARGS(m_rootSignature.GetAddressOf())), "CreateRootSignature(界面)");
    }

    D3D12Backend::~D3D12Backend()
    {
        for (auto& [id, e] : m_textures)
            m_ctx.ReleaseTexture(e.owned);
    }

    ID3D12PipelineState* D3D12Backend::GetPipeline(DXGI_FORMAT format)
    {
        auto it = m_pipelines.find(static_cast<int>(format));
        if (it != m_pipelines.end())
            return it->second.Get();

        const D3D12_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(DrawVert, pos),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(DrawVert, uv),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(DrawVert, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = m_rootSignature.Get();
        pd.VS             = { m_vs->GetBufferPointer(), m_vs->GetBufferSize() };
        pd.PS             = { m_ps->GetBufferPointer(), m_ps->GetBufferSize() };
        pd.InputLayout    = { layout, _countof(layout) };

        // 混合：标准 alpha 混合；目标 alpha 按"覆盖"累积（同 D3D11Backend）
        D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[0];
        b.BlendEnable           = TRUE;
        b.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOp               = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha         = D3D12_BLEND_ONE;
        b.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        b.LogicOp               = D3D12_LOGIC_OP_NOOP;
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // 光栅化：不剔除（羽化几何的绕向不固定）；D3D12 的裁剪矩形总是打开
        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable   = FALSE;
        pd.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = format;
        pd.SampleDesc.Count      = 1;

        ComPtr<ID3D12PipelineState> pso;
        D3D12ThrowIfFailed(m_ctx.GetDevice()->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(pso.GetAddressOf())), "CreateGraphicsPipelineState(界面)");
        return m_pipelines.emplace(static_cast<int>(format), std::move(pso)).first->second.Get();
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId D3D12Backend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;
        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        TextureEntry entry;
        entry.owned = m_ctx.CreateTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!entry.owned)
            return InvalidTextureId;
        // 没有初始数据时清零（D3D11 新建的纹理驱动给的也是零），图集边缘的双线性采样不会读到垃圾
        m_ctx.UploadTexture(*entry.owned, 0, 0, width, height, pixels, width * 4);
        entry.texture = entry.owned.get();

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    void D3D12Backend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end() || !it->second.owned || pixels == nullptr)
            return;

        D3D12Texture& t = *it->second.owned;
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x + region.width > t.width || region.y + region.height > t.height)
        {
            assert(false && "UpdateTexture 区域越界");
            return;
        }
        m_ctx.UploadTexture(t, region.x, region.y, region.width, region.height, pixels, pitch);
    }

    void D3D12Backend::DestroyTexture(TextureId id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end())
            return;
        m_ctx.ReleaseTexture(it->second.owned);
        m_textures.erase(it);
    }

    TextureId D3D12Backend::RegisterExternalTexture(D3D12Texture* texture)
    {
        if (texture == nullptr || texture->srv == D3D12Texture::kNoSrv)
            return InvalidTextureId;

        TextureEntry entry;
        entry.texture = texture;
        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    // =========================================================
    // 渲染
    // =========================================================
    void D3D12Backend::Render(const DrawData& data)
    {
        const D3D12Context::UiTarget& target = m_ctx.GetUiTarget();
        const float scale    = data.framebufferScale;
        const float fbWidth  = data.displaySize.x * scale;
        const float fbHeight = data.displaySize.y * scale;
        if (fbWidth <= 0.0f || fbHeight <= 0.0f || target.rtv.ptr == 0)
            return;

        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (const DrawList* list : data.lists)
        {
            totalVertices += list->GetVertices().size();
            totalIndices  += list->GetIndices().size();
        }
        if (totalIndices == 0)
            return;

        // ── 所有 DrawList 的顶点和索引连续放进上传缓冲（帧内数据，GPU 用完后整页复用）──
        const D3D12Context::Upload vb = m_ctx.AllocateUpload(totalVertices * sizeof(DrawVert), 16);
        const D3D12Context::Upload ib = m_ctx.AllocateUpload(totalIndices * sizeof(DrawIndex), 16);
        auto* vtxDst = reinterpret_cast<DrawVert*>(vb.cpu);
        auto* idxDst = reinterpret_cast<DrawIndex*>(ib.cpu);
        for (const DrawList* list : data.lists)
        {
            const auto& vertices = list->GetVertices();
            const auto& indices  = list->GetIndices();
            std::memcpy(vtxDst, vertices.data(), vertices.size() * sizeof(DrawVert));
            std::memcpy(idxDst, indices.data(),  indices.size()  * sizeof(DrawIndex));
            vtxDst += vertices.size();
            idxDst += indices.size();
        }

        // ── 管线状态 ────────────────────────────────────────────
        ID3D12GraphicsCommandList* cl = m_ctx.GetList();
        cl->OMSetRenderTargets(1, &target.rtv, FALSE, nullptr);

        D3D12_VIEWPORT vp = {};
        vp.Width    = fbWidth;
        vp.Height   = fbHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        cl->RSSetViewports(1, &vp);

        // 逻辑像素 → 裁剪空间：x ∈ [0, W] → [-1, 1]，y ∈ [0, H] → [1, -1]
        VSConstants cb;
        cb.scale[0]     =  2.0f / data.displaySize.x;
        cb.scale[1]     = -2.0f / data.displaySize.y;
        cb.translate[0] = -1.0f;
        cb.translate[1] =  1.0f;

        cl->SetGraphicsRootSignature(m_rootSignature.Get());
        cl->SetPipelineState(GetPipeline(target.format));
        cl->SetGraphicsRoot32BitConstants(0, sizeof(VSConstants) / 4, &cb, 0);
        m_ctx.BindSrvHeap();

        D3D12_VERTEX_BUFFER_VIEW vbv = { vb.gpu, static_cast<UINT>(totalVertices * sizeof(DrawVert)), sizeof(DrawVert) };
        D3D12_INDEX_BUFFER_VIEW  ibv = { ib.gpu, static_cast<UINT>(totalIndices * sizeof(DrawIndex)), DXGI_FORMAT_R32_UINT };
        cl->IASetVertexBuffers(0, 1, &vbv);
        cl->IASetIndexBuffer(&ibv);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ── 逐命令绘制 ──────────────────────────────────────────
        // DrawList 内的索引从 0 开始，用 BaseVertexLocation 偏移到各自的顶点区间
        UINT      indexStart   = 0;
        INT       vertexStart  = 0;
        TextureId boundTexture = InvalidTextureId;

        for (const DrawList* list : data.lists)
        {
            for (const DrawCmd& cmd : list->GetCommands())
            {
                if (cmd.indexCount == 0)
                    continue;

                // 裁剪矩形换算成物理像素；四舍五入保证相邻区域既不重叠也不留缝
                D3D12_RECT scissor;
                scissor.left   = static_cast<LONG>(std::floor(std::max(cmd.clipRect.min.x * scale, 0.0f) + 0.5f));
                scissor.top    = static_cast<LONG>(std::floor(std::max(cmd.clipRect.min.y * scale, 0.0f) + 0.5f));
                scissor.right  = static_cast<LONG>(std::floor(std::min(cmd.clipRect.max.x * scale, fbWidth)  + 0.5f));
                scissor.bottom = static_cast<LONG>(std::floor(std::min(cmd.clipRect.max.y * scale, fbHeight) + 0.5f));
                if (scissor.right <= scissor.left || scissor.bottom <= scissor.top)
                    continue;

                if (cmd.texture != boundTexture)
                {
                    auto it = m_textures.find(cmd.texture);
                    if (it == m_textures.end())
                    {
                        assert(false && "DrawCmd 引用了不存在的纹理");
                        continue;
                    }
                    D3D12Texture* t = it->second.texture;
                    t->Transition(m_ctx, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // 视口纹理刚画完，还是渲染目标状态
                    cl->SetGraphicsRootDescriptorTable(1, m_ctx.GetSrvGpu(t->srv));
                    boundTexture = cmd.texture;
                }

                cl->RSSetScissorRects(1, &scissor);
                cl->DrawIndexedInstanced(cmd.indexCount, 1, indexStart + cmd.indexOffset, vertexStart, 0);
            }

            indexStart  += static_cast<UINT>(list->GetIndices().size());
            vertexStart += static_cast<INT>(list->GetVertices().size());
        }
    }
}
