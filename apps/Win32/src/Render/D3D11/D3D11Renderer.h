#pragma once
#include "Shader.h"
#include "Core/Math/Mat4.hpp"
#include "Render/IRenderer.h"
#include "Render/VertexTypes.hpp"
#include <span>
#include <unordered_map>
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
        virtual void SubmitImage   (const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj) override;
        virtual void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;

        virtual void SubmitCached(uint32_t slot, uint64_t version,
                                  std::span<const Vertex_P3_C4> verts,
                                  const Math::Mat4& viewProj, PrimitiveType type,
                                  bool depth = true, bool blend = false) override;
        virtual void SubmitTexturedCached(uint32_t slot, uint64_t version,
                                          std::span<const Vertex_P3_C4_UV> verts,
                                          const Math::Mat4& viewProj, void* nativeSRV,
                                          bool depth = false, bool blend = true) override;

        virtual std::unique_ptr<IRenderTarget> CreateLayerTarget() override;
        virtual void DrawLayer(IRenderTarget& layer) override;

        virtual void SetLightBackground(bool light) override { m_lightBackground = light; }

        virtual void* GetNativeDevice() override;

        ID3D11Device* GetDevice() { return m_device; }
    private:
        void Initialize();

        // 公共渲染状态设置（topology / depth / blend / viewProj 常量缓冲）
        void SetLineState(const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend);
        void SetTextState(const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend);

        // viewProj 矩阵未变时跳过 UpdateSubresource（同帧多次 Submit 仅两种矩阵）
        void UploadViewProj(const Math::Mat4& viewProj);

        // 按 (slot, version) 缓存的持久顶点缓冲：version 未变直接复用，
        // 容量不足时按 1.5 倍扩容重建。
        struct CachedVB
        {
            ComPtr<ID3D11Buffer> vb;
            size_t   capacity = 0;   // 顶点容量
            uint64_t version  = 0;
            UINT     count    = 0;
            bool     valid    = false;
        };
        // 返回 nullptr 表示创建缓冲失败（调用方回退到非缓存路径）
        CachedVB* EnsureCachedVB(std::unordered_map<uint32_t, CachedVB>& cache,
                                 uint32_t slot, uint64_t version,
                                 const void* data, size_t vertCount, size_t vertSize);

    private:
        ID3D11Device*        m_device  = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        IRenderTarget*       m_currentTarget = nullptr;     // BeginFrame 绑定的渲染目标（DrawLayer 复制到这里）

        ComPtr<ID3D11Buffer> m_vb;
        ComPtr<ID3D11Buffer> m_cb;
        ComPtr<ID3D11Buffer> m_textVB;

        std::unordered_map<uint32_t, CachedVB> m_cachedVBs;      // Vertex_P3_C4
        std::unordered_map<uint32_t, CachedVB> m_cachedTextVBs;  // Vertex_P3_C4_UV

        int m_maxVertices     = 65536;
        int m_maxTextVertices = 65536;

        // 当前常量缓冲里的 viewProj（已转置），用于跳过重复上传
        float m_lastViewProj[16] = {};
        bool  m_cbValid          = false;
        bool  m_lightBackground  = false;
        bool  m_lastLight        = false;

        // ===== states =====
        ComPtr<ID3D11DepthStencilState> m_depthEnabled;
        ComPtr<ID3D11DepthStencilState> m_depthDisabled;
        ComPtr<ID3D11DepthStencilState> m_depthReadOnly;

        ComPtr<ID3D11RasterizerState>  m_rsNoCull;
        ComPtr<ID3D11BlendState>       m_blendAlpha;
        ComPtr<ID3D11SamplerState>     m_sampler;

        LineShader m_lineShader;
        TextShader  m_textShader;
        ImageShader m_imageShader;

        // 光栅图像纹理：按 ImageData::Key 缓存（同一路径重新加载会得到新 Key）
        std::unordered_map<uint64_t, ComPtr<ID3D11ShaderResourceView>> m_imageTextures;
    };
}