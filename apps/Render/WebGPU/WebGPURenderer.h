#pragma once
#include "Render/IRenderer.h"
#include "WebGPU/WebGPUCommon.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
    class WebGPURenderTarget;

    // WebGPU 的 CAD 视口渲染器，与 GLES3Renderer / D3D11Renderer 一一对应（结构照 docs/WebGPU/PLAN.md 第 3 节的原型）：
    //   - 线 / 三角形 / 点：顶点色；文字：纹理 alpha × 顶点色；光栅图像：纹理 × 顶点色；字形实例化
    //   - (slot, version) 缓存提交：version 未变时复用上次上传的顶点缓冲
    //   - 缓存层：CreateLayerTarget 返回 WebGPURenderTarget，DrawLayer 结束通道 → CopyTextureToTexture → 以 Load 重新开始
    // 与 GL 版不同的地方：
    //   - 缓冲写入（wgpuQueueWriteBuffer）在提交前统一生效，同一帧里不能反复改写同一块缓冲：
    //     每次提交的矩阵占帧内 uniform 环形缓冲的一段（256 字节对齐，动态偏移），
    //     不缓存的顶点写进帧内流缓冲的不同区段；EndFrame 提交后复位，帧内换下来的资源提交后再销毁
    //   - 渲染目标总带深度缓冲，"不测试深度"的管线也声明深度格式（比较总是通过、不写入）
    //   - 管线按 程序 × 图元 × 深度模式 × 混合 用到时创建（最多 72 条）
    //   - 点图元：point-list 本来就是 1 像素
    // 纹理句柄（nativeSRV）约定为 WGPUTextureView。BeginFrame 到 EndFrame 之间录一个命令编码器，EndFrame 提交。
    class WebGPURenderer : public IRenderer
    {
    public:
        WebGPURenderer(WGPUDevice device, WGPUQueue queue);
        ~WebGPURenderer() override;

        WebGPURenderer(const WebGPURenderer&) = delete;
        WebGPURenderer& operator=(const WebGPURenderer&) = delete;

        void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) override;
        void EndFrame() override;
        void Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;
        void SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj) override;

        void SubmitCached(uint32_t slot, uint64_t version,
                          std::span<const Vertex_P3_C4> verts,
                          const Math::Mat4& viewProj, PrimitiveType type,
                          bool depth = true, bool blend = false) override;
        void SubmitTexturedCached(uint32_t slot, uint64_t version,
                                  std::span<const Vertex_P3_C4_UV> verts,
                                  const Math::Mat4& viewProj, void* nativeSRV,
                                  bool depth = false, bool blend = true) override;

        bool IsCached(uint32_t slot, uint64_t version, bool textured) const override;
        void SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        bool IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const override;

        std::unique_ptr<IRenderTarget> CreateLayerTarget() override;
        void  DrawLayer(IRenderTarget& layer) override;
        void  SetLightBackground(bool light) override { m_lightBackground = light; }
        void  ReleaseCachedResources() override;
        void* GetNativeDevice() override { return m_device; }

    private:
        enum class Program   { Color, Text, Image, Glyph, Count };
        enum class DepthMode { Off, Test, TestWrite };      // 与 GL / D3D11 相同：不测试 / 混合的只测试不写入 / 不混合的测试并写入

        // 持久缓存的顶点缓冲（或字形实例缓冲）
        struct GpuBuffer
        {
            WGPUBuffer buffer   = nullptr;
            uint64_t   capacity = 0;    // 字节
            uint64_t   version  = 0;
            uint32_t   count    = 0;
            bool       valid    = false;
        };

        // 帧内顺序写入的缓冲（uniform 环形缓冲、不缓存的顶点），EndFrame 后从头写
        struct FrameBuffer
        {
            WGPUBuffer buffer   = nullptr;
            uint64_t   capacity = 0;
            uint64_t   used     = 0;
        };

        void CreateLayouts();
        WGPURenderPipeline GetPipeline(Program p, PrimitiveType type, DepthMode depth, bool blend);
        void BeginPass(bool clear);
        void EndPass();
        // 写入本次提交的矩阵与换色参数，绑定管线和 group 0；未在帧内时返回 false
        bool Bind(Program p, PrimitiveType type, const Math::Mat4& viewProj, bool depth, bool blend);
        // 写进帧内流缓冲，返回偏移（容量不足时换一块更大的，旧的提交后销毁）
        uint64_t Stream(const void* data, uint64_t bytes);
        void Upload(GpuBuffer& gb, const void* data, uint64_t bytes, WGPUBufferUsage usage);
        WGPUBindGroup TextureBindGroup(WGPUTextureView view);
        void Retire(WGPUBuffer& buffer);
        void ReleaseGlyphResources();
        void EnsureGlyphMeshes(const GlyphMeshView& meshes);

    private:
        WGPUDevice m_device = nullptr;
        WGPUQueue  m_queue  = nullptr;

        WGPUShaderModule    m_shader          = nullptr;
        WGPUBindGroupLayout m_uniformLayout   = nullptr;   // group 0：矩阵 + 换色参数（动态偏移）
        WGPUBindGroupLayout m_textureLayout   = nullptr;   // group 1：纹理 + 采样器
        WGPUPipelineLayout  m_colorPipeLayout = nullptr;
        WGPUPipelineLayout  m_texPipeLayout   = nullptr;
        WGPUSampler         m_sampler         = nullptr;
        std::array<WGPURenderPipeline, 4 * 3 * 3 * 2> m_pipelines{};

        FrameBuffer   m_uniforms;
        WGPUBindGroup m_uniformGroup = nullptr;
        FrameBuffer   m_stream;

        std::unordered_map<uint32_t, GpuBuffer> m_cached;
        std::unordered_map<uint32_t, GpuBuffer> m_cachedText;

        // 字形实例（GlyphInstancing.hpp）：网格库两个缓冲 + 各槽的实例缓冲与分批副本
        struct GlyphSlot
        {
            GpuBuffer               instances;
            std::vector<GlyphBatch> batches;
        };
        std::unordered_map<uint32_t, GlyphSlot> m_glyphSlots;
        std::vector<GlyphMeshRange>             m_glyphMeshes;
        WGPUBuffer m_glyphFill = nullptr;
        WGPUBuffer m_glyphLine = nullptr;
        uint64_t   m_glyphMeshVersion = 0;
        bool       m_glyphMeshValid   = false;

        struct ImageTexture
        {
            WGPUTexture     texture   = nullptr;     // 全为空 = 创建失败，不再重试
            WGPUTextureView view      = nullptr;
            WGPUBindGroup   bindGroup = nullptr;
        };
        std::unordered_map<uint64_t, ImageTexture>          m_imageTextures;   // ImageData::Key
        std::unordered_map<WGPUTextureView, WGPUBindGroup>  m_textGroups;      // 文字纹理（nativeSRV）
        uint32_t m_maxTextureSize = 8192;

        // 当前帧
        WebGPURenderTarget*    m_target  = nullptr;
        WGPUCommandEncoder     m_encoder = nullptr;
        WGPURenderPassEncoder  m_pass    = nullptr;
        ViewportDesc           m_viewport;
        WebGPUCommon::RetireList m_retired;
        bool m_lightBackground = false;
    };
}
