#include "Renderer.h" 
#include <d3dcompiler.h>
#include <format>

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

        auto compileShader = [](const wchar_t* file, const char* entry, const char* target, ID3DBlob** shaderBlob)
        {
            ComPtr<ID3DBlob> errorBlob;
            HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target, 0, 0, shaderBlob, errorBlob.GetAddressOf());
            if (FAILED(hr))
            {
                std::string details = errorBlob
                    ? static_cast<const char*>(errorBlob->GetBufferPointer())
                    : std::format("HRESULT=0x{:08X}", static_cast<unsigned int>(hr));
                throw std::runtime_error(std::format("Shader compile failed: {} [{} -> {}]", details, entry, target));
            }
        };

        compileShader(L"Basic.hlsl", "VSMain", "vs_5_0", vsBlob.GetAddressOf());
        compileShader(L"Basic.hlsl", "PSMain", "ps_5_0", psBlob.GetAddressOf());

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

        // ==== 深度状态 ====
        // 深度测试启用（用于一般绘制）
        D3D11_DEPTH_STENCIL_DESC depthEnableDesc = {};
        depthEnableDesc.DepthEnable = TRUE;
        depthEnableDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthEnableDesc.DepthFunc = D3D11_COMPARISON_LESS;
        m_device->CreateDepthStencilState(&depthEnableDesc, m_depthStateEnabled.GetAddressOf());

        // 深度测试禁用（用于光标，确保总是显示）
        D3D11_DEPTH_STENCIL_DESC depthDisableDesc = {};
        depthDisableDesc.DepthEnable = FALSE;
        depthDisableDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        m_device->CreateDepthStencilState(&depthDisableDesc, m_depthStateDisabled.GetAddressOf());
    }

    void Renderer::Begin(const RenderTarget& target, const XMMATRIX& mvp)
    {
        target.Bind(m_context);

        float clear[4] = { 0.1f, 0.1f, 0.15f, 1 };
        target.Clear(m_context, clear);

        m_context->IASetInputLayout(m_layout.Get());
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);

        // 启用深度测试（用于一般绘制）
        m_context->OMSetDepthStencilState(m_depthStateEnabled.Get(), 0);

        XMMATRIX mat = XMMatrixTranspose(mvp);
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &mat, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

        m_cpuBuffer.clear();
        m_hasCursor = false;  // 重置光标标志
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

    void Renderer::DrawGrad(const Grid& grid)
    {
        for (auto&line : grid.GetLines())
        {
            DrawLine(line.a, line.b, line.color);
        }
    }

    //  提交 GPU
    void Renderer::Flush()
    {
        if (m_cpuBuffer.empty()) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

        memcpy(mapped.pData, m_cpuBuffer.data(), sizeof(LineVertex) * m_cpuBuffer.size());

        m_context->Unmap(m_vb.Get(), 0);

        UINT stride = sizeof(LineVertex);
        UINT offset = 0;

        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        m_context->Draw((UINT)m_cpuBuffer.size(), 0);

        m_cpuBuffer.clear();
    }
     
	void Renderer::SetCursor(float screenX, float screenY, float screenW, float screenH, 
							const CursorConfig& config)
	{ 
		m_cursorX = screenX;
		m_cursorY = screenY;
		m_screenW = screenW;
		m_screenH = screenH;
		m_cursorConfig = config;
		m_hasCursor = config.enabled;
	}

	void Renderer::SetCursorConfig(const CursorConfig& config)
	{
		m_cursorConfig = config;
	}

    void Renderer::FlushWithMVP(const XMMATRIX& mvp)
    {
        if (m_cpuBuffer.empty()) return;

        // 切换 MVP
        XMMATRIX mat = XMMatrixTranspose(mvp);
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &mat, 0, 0);

        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, m_cpuBuffer.data(), sizeof(LineVertex) * m_cpuBuffer.size());
        m_context->Unmap(m_vb.Get(), 0);

        UINT stride = sizeof(LineVertex);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context->Draw((UINT)m_cpuBuffer.size(), 0);

        m_cpuBuffer.clear();
    }

    void Renderer::DrawCursorImpl()
    {
        if (!m_hasCursor || !m_cursorConfig.enabled) return;

        assert(m_screenW > 0 && m_screenH > 0);

        // 构建屏幕空间正交 MVP 
        XMMATRIX screenMVP = XMMatrixOrthographicOffCenterLH(0.f, m_screenW, m_screenH, 0.f, -1.f, 1.f);

        float x = m_cursorX;
        float y = m_cursorY;
        const auto& color = m_cursorConfig.color;

        // ================================
        // 1️ 十字线（贯穿屏幕）
        // ================================

        // 横线
        m_cpuBuffer.push_back({ {0.f, y, 0.f}, color });
        m_cpuBuffer.push_back({ {m_screenW, y, 0.f}, color });

        // 竖线
        m_cpuBuffer.push_back({ {x, 0.f, 0.f}, color });
        m_cpuBuffer.push_back({ {x, m_screenH, 0.f}, color });

        // ================================
        // 2️ 中心矩形（空心）
        // ================================

        float halfX = m_cursorConfig.sizeX / 2.f;
        float halfY = m_cursorConfig.sizeY / 2.f;

        float left = x - halfX;
        float right = x + halfX;
        float top = y - halfY;
        float bottom = y + halfY;

        // 上
        m_cpuBuffer.push_back({ {left, top, 0.f}, color });
        m_cpuBuffer.push_back({ {right, top, 0.f}, color });

        // 下
        m_cpuBuffer.push_back({ {left, bottom, 0.f}, color });
        m_cpuBuffer.push_back({ {right, bottom, 0.f}, color });

        // 左
        m_cpuBuffer.push_back({ {left, top, 0.f}, color });
        m_cpuBuffer.push_back({ {left, bottom, 0.f}, color });

        // 右
        m_cpuBuffer.push_back({ {right, top, 0.f}, color });
        m_cpuBuffer.push_back({ {right, bottom, 0.f}, color });

        // 禁用深度测试，确保光标总是显示
        m_context->OMSetDepthStencilState(m_depthStateDisabled.Get(), 0);

        FlushWithMVP(screenMVP);

        // 恢复深度测试和世界空间 MVP
        m_context->OMSetDepthStencilState(m_depthStateEnabled.Get(), 0);

        XMMATRIX mat = XMMatrixTranspose(m_worldMVP);
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &mat, 0, 0);
    }

    void Renderer::End()
    {
        Flush();
        DrawCursorImpl();  // 最后绘制光标（保证在最上层）
    }
}
