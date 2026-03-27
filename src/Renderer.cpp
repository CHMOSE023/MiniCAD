#include "Renderer.h" 
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace MiniCAD
{
    Renderer::Renderer(ID3D11Device* device, ID3D11DeviceContext* context): 
        m_device(device),
        m_context(context)
    {
        Initialize();
    }

    void Renderer::Initialize()
    {
        // ==== Shader 编译 ====
        ComPtr<ID3DBlob> vsBlob, psBlob;

        ThrowIfFailed(D3DCompileFromFile(L"Basic.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr));

        ThrowIfFailed(D3DCompileFromFile(L"Basic.hlsl", nullptr, nullptr,   "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr));

        m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            m_vs.GetAddressOf());

        m_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            m_ps.GetAddressOf());

        // ==== InputLayout ====
        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",  0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        };

        m_device->CreateInputLayout(
            layout, 2,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            m_layout.GetAddressOf());

        // ==== 动态顶点缓冲（关键优化）====
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(LineVertex) * m_maxVertices;
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        m_device->CreateBuffer(&vbDesc, nullptr, m_vb.GetAddressOf());

        // ==== 常量缓冲 ====
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(XMMATRIX);
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        m_device->CreateBuffer(&cbDesc, nullptr, m_cb.GetAddressOf());
    }

    void Renderer::Begin(const RenderTarget& target, const XMMATRIX& mvp)
    {
        target.Bind(m_context);

        float clear[4] = { 0.1f, 0.1f, 0.15f, 1 };
        target.Clear(m_context, clear);

        m_context->IASetInputLayout(m_layout.Get());
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);

        XMMATRIX mat = XMMatrixTranspose(mvp);
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &mat, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

        m_cpuBuffer.clear();
    }

    // 只收集
    void Renderer::DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT4& color)
    {
        m_cpuBuffer.push_back({ a, color });
        m_cpuBuffer.push_back({ b, color });

        if (m_cpuBuffer.size() >= m_maxVertices)
        {
            Flush();
        }
    }

    //  提交 GPU
    void Renderer::Flush()
    {
        if (m_cpuBuffer.empty()) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

        memcpy(mapped.pData, m_cpuBuffer.data(),
            sizeof(LineVertex) * m_cpuBuffer.size());

        m_context->Unmap(m_vb.Get(), 0);

        UINT stride = sizeof(LineVertex);
        UINT offset = 0;

        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        m_context->Draw((UINT)m_cpuBuffer.size(), 0);

        m_cpuBuffer.clear();
    }

    void Renderer::End()
    {
        Flush();
    }
}