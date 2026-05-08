#include "Renderer.h" 
#include "Shader.h"
#include "RenderTarget.h"
#include <d3d11.h>

namespace MiniCAD
{
    Renderer::Renderer(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device)
        , m_context(context)
    {
        Initialize();
        m_lineShader.Initialize(m_device);
    }

    void Renderer::Initialize()
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
        cb.ByteWidth = sizeof(XMMATRIX);
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

    void Renderer::BeginFrame(const RenderTarget& target  , const D3D11_VIEWPORT& viewport)
    {  
        ID3D11RenderTargetView* rtv = target.GetRTV();
        float clear[4] = { 0.1f, 0.1f, 0.15f, 1.0f }; 
        auto pso       = m_lineShader.GetPipeline(); 

        m_context->RSSetViewports(1, &viewport);    
        m_context->OMSetRenderTargets(1, &rtv, nullptr); // nullptr 深度缓冲区
        m_context->ClearRenderTargetView(rtv, clear);    // 清空
        m_context->IASetInputLayout(pso.layout);
        m_context->VSSetShader(pso.shader->vs.Get(), nullptr, 0);
        m_context->PSSetShader(pso.shader->ps.Get(), nullptr, 0);
        m_context->RSSetState(m_rsNoCull.Get());
    }

    void Renderer::Submit(std::span<const Vertex_P3_C4> verts, const XMMATRIX& viewProj, PrimitiveType type, bool depth, bool blend)
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

        // ===== matrix =====
        XMMATRIX vp = XMMatrixTranspose(viewProj);
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &vp, 0, 0);
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

    ID3D11Device* Renderer::GetDevice()
    {
        return m_device;
    }

    void Renderer::EndFrame()
    {
        ID3D11RenderTargetView* nullRT[1] = { nullptr };
        m_context->OMSetRenderTargets(1, nullRT, nullptr);

        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(0, 1, nullSRV);
    }
}

