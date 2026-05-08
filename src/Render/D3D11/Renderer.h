#pragma once
#include "pch.h"
#include "RenderTarget.h"
#include "Shader.h"
#include <span>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <d3d11.h>

namespace MiniCAD
{
    enum class PrimitiveType
    {
        Line,
        Triangle
    };

    class Renderer
    {
    public:
        Renderer(ID3D11Device* device, ID3D11DeviceContext* context);

        void BeginFrame(const RenderTarget& target, const D3D11_VIEWPORT& viewport);
        void EndFrame();

        void Submit(std::span<const Vertex_P3_C4> verts,
            const XMMATRIX&   viewProj,
            PrimitiveType     type,
            bool              depth = true,
            bool              blend = false);
    public:
        ID3D11Device* GetDevice();
            
    private:
        void Initialize();

    private:
        ID3D11Device*        m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;

        ComPtr<ID3D11Buffer> m_vb;
        ComPtr<ID3D11Buffer> m_cb;

        int m_maxVertices = 65536;

        // ===== states =====
        ComPtr<ID3D11DepthStencilState> m_depthEnabled;
        ComPtr<ID3D11DepthStencilState> m_depthDisabled;
        ComPtr<ID3D11DepthStencilState> m_depthReadOnly;  // 透明用

        ComPtr<ID3D11RasterizerState>   m_rsNoCull;
        ComPtr<ID3D11BlendState>        m_blendAlpha;

        LineShader m_lineShader;
    };
}