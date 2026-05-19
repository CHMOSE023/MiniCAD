#pragma once
#include "pch.h" 
#include "Shader.h"
#include "Core/Math/Mat4.hpp"
#include "../IRenderer.h"
#include "../VertexTypes.hpp"
#include <span>
#include <wrl/client.h>
#include <d3d11.h>

namespace MiniCAD
{
   
	class D3D11Renderer : public IRenderer
    {
    public:
        D3D11Renderer(ID3D11Device* device, ID3D11DeviceContext* context);

        virtual void BeginFrame    (IRenderTarget& target, const ViewportDesc& viewport) override;
        virtual void EndFrame      () override;
        virtual void Submit        (std::span<const Vertex_P3_C4>    verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        virtual void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;

        virtual void* GetNativeDevice() override;

        ID3D11Device* GetDevice() { return m_device; }
    private:
        void Initialize();

    private:
        ID3D11Device*        m_device  = nullptr;
        ID3D11DeviceContext* m_context = nullptr;

        ComPtr<ID3D11Buffer> m_vb;
        ComPtr<ID3D11Buffer> m_cb;
        ComPtr<ID3D11Buffer> m_textVB;

        int m_maxVertices     = 65536;
        int m_maxTextVertices = 65536;

        // ===== states =====
        ComPtr<ID3D11DepthStencilState> m_depthEnabled;
        ComPtr<ID3D11DepthStencilState> m_depthDisabled;
        ComPtr<ID3D11DepthStencilState> m_depthReadOnly;

        ComPtr<ID3D11RasterizerState>  m_rsNoCull;
        ComPtr<ID3D11BlendState>       m_blendAlpha;
        ComPtr<ID3D11SamplerState>     m_sampler;

        LineShader m_lineShader;
        TextShader m_textShader;
    };
}