#pragma once
#include "Vulkan/VulkanContext.h"
#include "Render/IRenderer.h"
#include "Render/VertexTypes.hpp"
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
    class VulkanRenderTarget;

    // Vulkan 的 CAD 视口渲染器，功能与 D3D12Renderer 一一对应（着色器是 GLSL 编成的 SPIR-V，见 Vulkan/Shaders/）：
    //   - 缓存提交按 (slot, version) 保存在显存的缓冲里，版本变化时经上传缓冲复制
    //   - 非缓存提交、光栅图像、字形网格的数据走上下文的上传缓冲（帧内数据）
    //   - 常量（viewProj + 背景深浅）用推送常量；纹理一张一个描述符集
    //   - 视口的 Y 轴用负高度翻转（与 D3D 相同：第一行在顶部），同一套矩阵直接用
    //   - 换下来的缓冲 / 图像交给上下文延迟销毁（GPU 执行完这一帧之后）
    class VulkanRenderer : public IRenderer
    {
    public:
        explicit VulkanRenderer(MiniGUI::VulkanContext& ctx);
        ~VulkanRenderer() override;

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

        VkPipeline GetPipeline(Shader shader, PrimitiveType type, Depth depth, bool blend);
        // 管线 + 推送常量（viewProj、背景深浅）；不在画当前目标时以 LOAD 重新开始；没有当前渲染目标时返回 false。
        // 纹理要先 PrepareTexture（布局转换不能在渲染期间做），再 Bind，再 BindTexture
        bool Bind(Shader shader, PrimitiveType type, Depth depth, bool blend, const Math::Mat4& viewProj);
        static bool PrepareTexture(MiniGUI::VulkanContext& ctx, MiniGUI::VulkanImage* image);
        void BindTexture(MiniGUI::VulkanImage* image);
        static Depth LineDepth(bool depth, bool blend) { return !depth ? Depth::Off : blend ? Depth::ReadOnly : Depth::Write; }

        // 按 (slot, version) 缓存的持久顶点缓冲：version 未变直接复用
        struct CachedVB
        {
            MiniGUI::VulkanBuffer buffer;
            size_t   capacity  = 0;       // 顶点容量
            uint64_t version   = 0;
            uint32_t count     = 0;
            bool     valid     = false;
            bool     immutable = false;   // 大缓冲：按实际大小创建，内容变化时整体重建
        };
        // 返回 nullptr 表示创建缓冲失败（调用方回退到非缓存路径）
        CachedVB* EnsureCachedVB(std::unordered_map<uint32_t, CachedVB>& cache, uint32_t slot, uint64_t version,
                                 const void* data, size_t vertCount, size_t vertSize);
        bool      EnsureGlyphMeshes(const GlyphMeshView& meshes);
        // 非缓存提交：分批放进上传缓冲直接画（每批按图元顶点数对齐）
        void      DrawStreamed(const void* data, size_t count, size_t stride, size_t primSize);

    private:
        MiniGUI::VulkanContext&        m_ctx;
        VulkanRenderTarget*            m_current = nullptr;    // BeginFrame 绑定的渲染目标
        MiniGUI::VulkanRenderingTarget m_target;
        ViewportDesc                   m_viewport;
        uint64_t                       m_stateSerial = 0;      // 设置视口与管线时的渲染轮次（重新开始渲染后要重设）
        VkPipeline                     m_boundPipeline = VK_NULL_HANDLE;
        bool                           m_lightBackground = false;

        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        VkShaderModule   m_vs[4]  = {};
        VkShaderModule   m_fs[4]  = {};
        std::unordered_map<uint32_t, VkPipeline> m_pipelines;

        std::unordered_map<uint32_t, CachedVB> m_cachedVBs;      // Vertex_P3_C4
        std::unordered_map<uint32_t, CachedVB> m_cachedTextVBs;  // Vertex_P3_C4_UV
        std::unordered_map<uint32_t, CachedVB> m_cachedGlyphVBs; // GlyphInstance（实例缓冲）
        // 字形实例的分批与字形网格区间：调用方上传后可能释放自己的那份，绘制时用这里的副本
        std::unordered_map<uint32_t, std::vector<GlyphBatch>> m_glyphBatches;
        std::vector<GlyphMeshRange> m_glyphMeshes;
        MiniGUI::VulkanBuffer       m_glyphFillVB;   // 字形网格库：三角形顶点（float2）
        MiniGUI::VulkanBuffer       m_glyphLineVB;   // 字形网格库：线段顶点（float2）
        uint64_t                    m_glyphMeshVersion = 0;
        bool                        m_glyphMeshValid   = false;

        // 光栅图像纹理：按 ImageData::Key 缓存（创建失败记为空，避免每帧重试）
        std::unordered_map<uint64_t, std::unique_ptr<MiniGUI::VulkanImage>> m_imageTextures;
    };
}
