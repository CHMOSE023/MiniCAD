#include "D3D11/D3D11Backend.h"
#include "Paint/DrawList.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace MiniGUI
{
    namespace
    {
        void ThrowIfFailed(HRESULT hr, const char* what)
        {
            if (FAILED(hr))
            {
                char buffer[128] = {};
                std::snprintf(buffer, sizeof(buffer), "MiniGUI D3D11: %s 失败 (HRESULT 0x%08X)", what, static_cast<unsigned int>(hr));
                throw std::runtime_error(buffer);
            }
        }

        // 整个界面只有这一套着色器：输出 = 纹理采样 × 顶点色。
        // 纯色图元采样白色像素，所以和贴图、文字可以合并到同一批次
        const char* kShaderSource = R"(
            cbuffer VSConstants : register(b0)
            {
                float2 scale;       // 逻辑像素 → 裁剪空间
                float2 translate;
            };

            struct VSInput
            {
                float2 pos   : POSITION;
                float2 uv    : TEXCOORD;
                float4 color : COLOR;
            };

            struct PSInput
            {
                float4 pos   : SV_POSITION;
                float4 color : COLOR;
                float2 uv    : TEXCOORD;
            };

            Texture2D    gTex  : register(t0);
            SamplerState gSamp : register(s0);

            PSInput VSMain(VSInput input)
            {
                PSInput o;
                o.pos   = float4(input.pos * scale + translate, 0.0f, 1.0f);
                o.color = input.color;
                o.uv    = input.uv;
                return o;
            }

            float4 PSMain(PSInput input) : SV_TARGET
            {
                return input.color * gTex.Sample(gSamp, input.uv);
            })";

        struct VSConstants
        {
            float scale[2];
            float translate[2];
        };

        ComPtr<ID3DBlob> CompileShader(const char* entry, const char* target)
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> errors;
            HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "MiniGUI", nullptr, nullptr,
                                    entry, target, flags, 0, blob.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(hr))
            {
                std::string msg = "MiniGUI D3D11: 着色器编译失败 ";
                msg += entry;
                if (errors)
                {
                    msg += "\n";
                    msg += static_cast<const char*>(errors->GetBufferPointer());
                }
                throw std::runtime_error(msg);
            }
            return blob;
        }
    }

    D3D11Backend::D3D11Backend(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device)
        , m_context(context)
    {
        assert(device != nullptr && context != nullptr);
        CreatePipeline();
    }

    D3D11Backend::~D3D11Backend() = default;

    void D3D11Backend::CreatePipeline()
    {
        // ── 着色器与输入布局 ────────────────────────────────────
        ComPtr<ID3DBlob> vsBlob = CompileShader("VSMain", "vs_4_0");
        ComPtr<ID3DBlob> psBlob = CompileShader("PSMain", "ps_4_0");

        ThrowIfFailed(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.ReleaseAndGetAddressOf()), "CreateVertexShader");
        ThrowIfFailed(m_device->CreatePixelShader (psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.ReleaseAndGetAddressOf()), "CreatePixelShader");

        const D3D11_INPUT_ELEMENT_DESC desc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(DrawVert, pos),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(DrawVert, uv),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(DrawVert, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ThrowIfFailed(m_device->CreateInputLayout(desc, _countof(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_layout.ReleaseAndGetAddressOf()), "CreateInputLayout");

        // ── 常量缓冲 ────────────────────────────────────────────
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(VSConstants);
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(m_device->CreateBuffer(&cbDesc, nullptr, m_cb.ReleaseAndGetAddressOf()), "CreateBuffer(CB)");

        // ── 混合：标准 alpha 混合；目标 alpha 按"覆盖"累积，便于以后渲染到离屏纹理 ──
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable           = TRUE;
        blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(m_device->CreateBlendState(&blendDesc, m_blend.ReleaseAndGetAddressOf()), "CreateBlendState");

        // ── 光栅化：不剔除（羽化几何的绕向不固定），开启裁剪矩形 ──
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode        = D3D11_FILL_SOLID;
        rsDesc.CullMode        = D3D11_CULL_NONE;
        rsDesc.ScissorEnable   = TRUE;
        rsDesc.DepthClipEnable = TRUE;
        ThrowIfFailed(m_device->CreateRasterizerState(&rsDesc, m_raster.ReleaseAndGetAddressOf()), "CreateRasterizerState");

        // ── 深度：关闭 ──────────────────────────────────────────
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable    = FALSE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc      = D3D11_COMPARISON_ALWAYS;
        dsDesc.StencilEnable  = FALSE;
        ThrowIfFailed(m_device->CreateDepthStencilState(&dsDesc, m_depth.ReleaseAndGetAddressOf()), "CreateDepthStencilState");

        // ── 采样器：线性过滤，边缘截断 ──────────────────────────
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
        ThrowIfFailed(m_device->CreateSamplerState(&sampDesc, m_sampler.ReleaseAndGetAddressOf()), "CreateSamplerState");
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId D3D11Backend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;

        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = static_cast<UINT>(width);
        desc.Height           = static_cast<UINT>(height);
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = pixels;
        init.SysMemPitch = static_cast<UINT>(width * 4);

        TextureEntry entry;
        entry.width  = width;
        entry.height = height;
        ThrowIfFailed(m_device->CreateTexture2D(&desc, pixels ? &init : nullptr, entry.texture.GetAddressOf()), "CreateTexture2D");
        ThrowIfFailed(m_device->CreateShaderResourceView(entry.texture.Get(), nullptr, entry.srv.GetAddressOf()), "CreateShaderResourceView");

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    void D3D11Backend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end() || it->second.external || pixels == nullptr)
            return;

        const TextureEntry& e = it->second;
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x + region.width > e.width || region.y + region.height > e.height)
        {
            assert(false && "UpdateTexture 区域越界");
            return;
        }

        D3D11_BOX box = {};
        box.left   = static_cast<UINT>(region.x);
        box.top    = static_cast<UINT>(region.y);
        box.right  = static_cast<UINT>(region.x + region.width);
        box.bottom = static_cast<UINT>(region.y + region.height);
        box.front  = 0;
        box.back   = 1;
        m_context->UpdateSubresource(e.texture.Get(), 0, &box, pixels, static_cast<UINT>(pitch), 0);
    }

    void D3D11Backend::DestroyTexture(TextureId id)
    {
        m_textures.erase(id);
    }

    TextureId D3D11Backend::RegisterExternalTexture(ID3D11ShaderResourceView* srv)
    {
        if (srv == nullptr)
            return InvalidTextureId;

        TextureEntry entry;
        entry.srv      = srv;
        entry.external = true;

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    // =========================================================
    // 渲染
    // =========================================================
    bool D3D11Backend::EnsureBuffers(size_t vertexCount, size_t indexCount)
    {
        // 容量不足时按 1.5 倍扩容，避免窗口内容逐渐变多时频繁重建
        if (m_vbCapacity < vertexCount)
        {
            const size_t capacity = std::max<size_t>(vertexCount + vertexCount / 2, 4096);

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = static_cast<UINT>(capacity * sizeof(DrawVert));
            desc.Usage          = D3D11_USAGE_DYNAMIC;
            desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            m_vb.Reset();
            m_vbCapacity = 0;
            if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_vb.GetAddressOf())))
                return false;
            m_vbCapacity = capacity;
        }

        if (m_ibCapacity < indexCount)
        {
            const size_t capacity = std::max<size_t>(indexCount + indexCount / 2, 8192);

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = static_cast<UINT>(capacity * sizeof(DrawIndex));
            desc.Usage          = D3D11_USAGE_DYNAMIC;
            desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            m_ib.Reset();
            m_ibCapacity = 0;
            if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_ib.GetAddressOf())))
                return false;
            m_ibCapacity = capacity;
        }
        return true;
    }

    void D3D11Backend::SetupRenderState(const DrawData& data, float fbWidth, float fbHeight)
    {
        // 逻辑像素 → 裁剪空间：x ∈ [0, W] → [-1, 1]，y ∈ [0, H] → [1, -1]
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            auto* cb = static_cast<VSConstants*>(mapped.pData);
            cb->scale[0]     =  2.0f / data.displaySize.x;
            cb->scale[1]     = -2.0f / data.displaySize.y;
            cb->translate[0] = -1.0f;
            cb->translate[1] =  1.0f;
            m_context->Unmap(m_cb.Get(), 0);
        }

        D3D11_VIEWPORT vp = {};
        vp.Width    = fbWidth;
        vp.Height   = fbHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        const UINT stride = sizeof(DrawVert);
        const UINT offset = 0;
        m_context->IASetInputLayout(m_layout.Get());
        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
        m_context->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        m_context->GSSetShader(nullptr, nullptr, 0);
        m_context->HSSetShader(nullptr, nullptr, 0);
        m_context->DSSetShader(nullptr, nullptr, 0);

        const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_context->OMSetBlendState(m_blend.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_depth.Get(), 0);
        m_context->RSSetState(m_raster.Get());
    }

    void D3D11Backend::Render(const DrawData& data)
    {
        const float scale    = data.framebufferScale;
        const float fbWidth  = data.displaySize.x * scale;
        const float fbHeight = data.displaySize.y * scale;
        if (fbWidth <= 0.0f || fbHeight <= 0.0f)
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

        if (!EnsureBuffers(totalVertices, totalIndices))
            return;

        // ── 上传所有 DrawList 的顶点和索引，连续存放 ────────────
        D3D11_MAPPED_SUBRESOURCE vbMap = {};
        D3D11_MAPPED_SUBRESOURCE ibMap = {};
        if (FAILED(m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vbMap)))
            return;
        if (FAILED(m_context->Map(m_ib.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ibMap)))
        {
            m_context->Unmap(m_vb.Get(), 0);
            return;
        }

        auto* vtxDst = static_cast<DrawVert*>(vbMap.pData);
        auto* idxDst = static_cast<DrawIndex*>(ibMap.pData);
        for (const DrawList* list : data.lists)
        {
            const auto& vertices = list->GetVertices();
            const auto& indices  = list->GetIndices();
            std::memcpy(vtxDst, vertices.data(), vertices.size() * sizeof(DrawVert));
            std::memcpy(idxDst, indices.data(),  indices.size()  * sizeof(DrawIndex));
            vtxDst += vertices.size();
            idxDst += indices.size();
        }
        m_context->Unmap(m_vb.Get(), 0);
        m_context->Unmap(m_ib.Get(), 0);

        SetupRenderState(data, fbWidth, fbHeight);

        // ── 逐命令绘制 ──────────────────────────────────────────
        // DrawList 内的索引从 0 开始，用 BaseVertexLocation 偏移到各自的顶点区间
        UINT      indexStart  = 0;
        INT       vertexStart = 0;
        TextureId boundTexture = InvalidTextureId;

        for (const DrawList* list : data.lists)
        {
            for (const DrawCmd& cmd : list->GetCommands())
            {
                if (cmd.indexCount == 0)
                    continue;

                // 裁剪矩形换算成物理像素；四舍五入保证相邻区域既不重叠也不留缝
                D3D11_RECT scissor;
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
                    ID3D11ShaderResourceView* srv = it->second.srv.Get();
                    m_context->PSSetShaderResources(0, 1, &srv);
                    boundTexture = cmd.texture;
                }

                m_context->RSSetScissorRects(1, &scissor);
                m_context->DrawIndexed(cmd.indexCount, indexStart + cmd.indexOffset, vertexStart);
            }

            indexStart  += static_cast<UINT>(list->GetIndices().size());
            vertexStart += static_cast<INT>(list->GetVertices().size());
        }
    }
}
