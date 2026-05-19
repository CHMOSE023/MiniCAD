#include "Shader.h" 
#include "D3D11Renderer.h" 
#include "D3D11RenderTarget.h"
#include <d3d11.h>
#include "../GpuTypes.hpp"
#include "../IRenderer.h"
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
        auto& d3dTarget = static_cast<D3D11RenderTarget&>(target);
        auto* rtv       = static_cast<ID3D11RenderTargetView*>(d3dTarget.GetNativeHandle());

        float clear[4] = { 0.1f, 0.1f, 0.15f, 1.0f }; 
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

    void D3D11Renderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        if (verts.empty()) return;

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
 
        Float4x4 gpuMat = Float4x4::FromMat4(viewProj.Transposed()); 

        // ===== matrix ===== 
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, gpuMat.m, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

        // ===== upload =====
        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, verts.data(), verts.size_bytes());
        m_context->Unmap(m_vb.Get(), 0);

        UINT stride = sizeof(Vertex_P3_C4);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);

        // ===== draw =====
        m_context->Draw((UINT)verts.size(), 0);
    }

    void D3D11Renderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                                       const Math::Mat4& viewProj,
                                       void* nativeSRV,
                                       bool depth, bool blend)
    {
        if (verts.empty() || !nativeSRV) return;

        auto pso = m_textShader.GetPipeline();
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);
        m_context->RSSetState(m_rsNoCull.Get());

        m_context->OMSetDepthStencilState(depth ? m_depthEnabled.Get() : m_depthDisabled.Get(), 0);
        float f[4] = {};
        m_context->OMSetBlendState(blend ? m_blendAlpha.Get() : nullptr, f, 0xffffffff);

        Float4x4 gpuMat = Float4x4::FromMat4(viewProj.Transposed());
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, gpuMat.m, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

        auto* srv = static_cast<ID3D11ShaderResourceView*>(nativeSRV);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_textVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, verts.data(), verts.size_bytes());
        m_context->Unmap(m_textVB.Get(), 0);

        UINT stride = sizeof(Vertex_P3_C4_UV);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_textVB.GetAddressOf(), &stride, &offset);
        m_context->Draw((UINT)verts.size(), 0);
    }

    void* D3D11Renderer::GetNativeDevice()
    {
        return m_device;
    }

    void D3D11Renderer::EndFrame()
    {
        ID3D11RenderTargetView* nullRT[1] = { nullptr };
        m_context->OMSetRenderTargets(1, nullRT, nullptr);

        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(0, 1, nullSRV);
    }
}

