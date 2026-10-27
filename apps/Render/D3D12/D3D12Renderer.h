#pragma once
#include "D3D12/D3D12Context.h"
#include "Render/IRenderer.h"
#include "Render/VertexTypes.hpp"
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
    class D3D12RenderTarget;

    // D3D12 的 CAD 视口渲染器，功能与 D3D11Renderer 一一对应（着色器是同一份 HLSL，见 Hlsl/ViewportShaders.h）：
    //   - 缓存提交按 (slot, version) 保存在默认堆的缓冲里，版本变化时经上传缓冲复制
    //   - 非缓存提交、光栅图像、字形网格的数据走上下文的上传缓冲（帧内数据）
    //   - 常量（viewProj + 背景深浅）用根常量，不需要常量缓冲
    //   - 换下来的缓冲 / 纹理交给上下文延迟释放（GPU 执行完已录制的命令之后）
    class D3D12Renderer : public IRenderer
    {
    public:
        explicit D3D12Renderer(MiniGUI::D3D12Context& ctx);
        ~D3D12Renderer() override;

        void BeginFrame    (IRenderTarget& target, const ViewportDesc& viewport) override;
        void EndFrame      () override;
        void Submit        (std::span<const Vertex_P3_C4>    verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        void SubmitImage   (const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj) override;
        void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;

        void SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                          const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        void SubmitTexturedCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4_UV> verts,
                                  const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;
        bool IsCached(uint32_t slot, uint64_t version, bool textured) const override;
        bool SupportsCachedAppend() const override { return true; }
        bool AppendCached(uint32_t slot, uint64_t baseVersion, uint64_t version, std::span<const Vertex_P3_C4> verts) override;
        void SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        bool IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const override;

        std::unique_ptr<IRenderTarget> CreateLayerTarget() override;
        void DrawLayer(IRenderTarget& layer) override;

        void SetLightBackground(bool light) override { m_lightBackground = light; }
        void ReleaseCachedResources() override;

        void* GetNativeDevice() override { return m_ctx.GetDevice(); }

    private:
        enum class Shader : uint32_t { Line, Glyph, Text, Image };
        enum class Depth  : uint32_t { Off, Write, ReadOnly };

        ID3D12PipelineState* GetPipeline(Shader shader, PrimitiveType type, Depth depth, bool blend);
        void ApplyFrameState();                     // 渲染目标、视口、根签名、描述符堆
        // 管线 + 拓扑 + 根常量（viewProj、背景深浅）；没有当前渲染目标时返回 false
        bool Bind(Shader shader, PrimitiveType type, Depth depth, bool blend, const Math::Mat4& viewProj);
        bool BindTexture(MiniGUI::D3D12Texture* texture);
        static Depth LineDepth(bool depth, bool blend) { return !depth ? Depth::Off : blend ? Depth::ReadOnly : Depth::Write; }

        // 按 (slot, version) 缓存的持久顶点缓冲：version 未变直接复用
        struct CachedVB
        {
            MiniGUI::D3D12Buffer buffer;
            size_t   capacity  = 0;       // 顶点容量
            uint64_t version   = 0;
            UINT     count     = 0;
            UINT     stride    = 0;
            bool     valid     = false;
            bool     immutable = false;   // 大缓冲：按实际大小创建，内容变化时整体重建
        };
        // 返回 nullptr 表示创建缓冲失败（调用方回退到非缓存路径）
        CachedVB* EnsureCachedVB(std::unordered_map<uint32_t, CachedVB>& cache, uint32_t slot, uint64_t version,
                                 const void* data, size_t vertCount, size_t vertSize);
        void      ReleaseBuffer(MiniGUI::D3D12Buffer& buffer);
        bool      EnsureGlyphMeshes(const GlyphMeshView& meshes);
        // 非缓存提交：分批放进上传缓冲直接画（每批按图元顶点数对齐）
        void      DrawStreamed(const void* data, size_t count, size_t stride, size_t primSize);

    private:
        MiniGUI::D3D12Context& m_ctx;
        D3D12RenderTarget*     m_current = nullptr;    // BeginFrame 绑定的渲染目标
        ViewportDesc           m_viewport;
        uint64_t               m_stateEpoch = 0;       // ApplyFrameState 时的命令列表轮次（提交过就要重新设置）
        ID3D12PipelineState*   m_boundPipeline = nullptr;
        bool                   m_lightBackground = false;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3DBlob>            m_vs[4];
        Microsoft::WRL::ComPtr<ID3DBlob>            m_ps[4];
        std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_pipelines;

        std::unordered_map<uint32_t, CachedVB> m_cachedVBs;      // Vertex_P3_C4
        std::unordered_map<uint32_t, CachedVB> m_cachedTextVBs;  // Vertex_P3_C4_UV
        std::unordered_map<uint32_t, CachedVB> m_cachedGlyphVBs; // GlyphInstance（实例缓冲）
        // 字形实例的分批与字形网格区间：调用方上传后可能释放自己的那份，绘制时用这里的副本
        std::unordered_map<uint32_t, std::vector<GlyphBatch>> m_glyphBatches;
        std::vector<GlyphMeshRange> m_glyphMeshes;
        MiniGUI::D3D12Buffer        m_glyphFillVB;   // 字形网格库：三角形顶点（float2）
        MiniGUI::D3D12Buffer        m_glyphLineVB;   // 字形网格库：线段顶点（float2）
        uint64_t                    m_glyphMeshVersion = 0;
        bool                        m_glyphMeshValid   = false;

        // 光栅图像纹理：按 ImageData::Key 缓存（创建失败记为空，避免每帧重试）
        std::unordered_map<uint64_t, std::unique_ptr<MiniGUI::D3D12Texture>> m_imageTextures;
    };
}
