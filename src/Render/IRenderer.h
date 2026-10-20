#pragma once
#include <memory>
#include <span>
#include <cstdint>
#include "Render/IRenderTarget.h"
#include "Render/VertexTypes.hpp"
#include "Render/GlyphInstancing.hpp"
#include <vector>
#include "Core/Image/ImageData.hpp"
#include "Core/Math/Mat4.hpp"

namespace MiniCAD 
{
    // Line / Triangle 是线段与三角形；Point 是**点图元**（一个顶点 = 一个点）。
    // 点图元由后端保证恒定 1 屏幕像素，与缩放无关 —— 图案填充的"点"笔划
    // （.pat 的 dash = 0）必须走这条路：画成世界尺寸的短线段会在缩小视图时
    // 小于 1px 而整片消失。
    enum class PrimitiveType { Line, Triangle, Point };

    struct ViewportDesc
    {
        float x        = 0.0f;
        float y        = 0.0f;
        float width    = 0.0f;
        float height   = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // IRenderTarget 替换原来的 D3D11_VIEWPORT + RenderTarget
        virtual void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) = 0;
        virtual void EndFrame  () = 0; 
        virtual void Submit    (std::span<const Vertex_P3_C4>    verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) = 0;
        virtual void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) = 0;

        // 绘制一张光栅图像（quad 为 6 个带 UV 的顶点）。后端按 image.Key 缓存纹理；
        // 默认不支持（WASM 暂未实现），图像不显示，实体只画边框。
        virtual void SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
        {
            (void)image; (void)quad; (void)viewProj;
        }

        // ── 缓存提交 ─────────────────────────────────────────────────────
        // 几何由 (slot, version) 标识：version 未变时后端可直接复用上次上传的
        // GPU 缓冲，避免每帧全量重新上传大体量场景顶点；version 变化才上传。
        // 默认实现回退到非缓存 Submit，无缓存能力的后端（WASM）仍然正确。
        virtual void SubmitCached(uint32_t slot, uint64_t version,
                                  std::span<const Vertex_P3_C4> verts,
                                  const Math::Mat4& viewProj, PrimitiveType type,
                                  bool depth = true, bool blend = false)
        {
            (void)slot; (void)version;
            Submit(verts, viewProj, type, depth, blend);
        }

        virtual void SubmitTexturedCached(uint32_t slot, uint64_t version,
                                          std::span<const Vertex_P3_C4_UV> verts,
                                          const Math::Mat4& viewProj, void* nativeSRV,
                                          bool depth = false, bool blend = true)
        {
            (void)slot; (void)version;
            SubmitTextured(verts, viewProj, nativeSRV, depth, blend);
        }

        // 字形实例（见 GlyphInstancing.hpp）：instances 按网格排序，batches 为同一网格的连续区间。
        // type 为 Triangle 画字形的三角形，Line 画轮廓线；两次提交共用同一 (slot, version) 的实例。
        // 默认实现在 CPU 上展开成顶点后走 Submit（无实例化能力的后端）
        virtual void SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                        std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                        const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false)
        {
            (void)slot; (void)version; (void)batches;
            std::vector<Vertex_P3_C4> verts;
            for (const GlyphInstance& g : instances)
                ExpandGlyph(g, meshes, type == PrimitiveType::Triangle, [&](const Vertex_P3_C4& v) { verts.push_back(v); });
            Submit(verts, viewProj, type, depth, blend);
        }

        // 后端是否已持有 (slot, version) 的字形实例与 meshes.Version 的字形网格（同 IsCached）
        virtual bool IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const
        {
            (void)slot; (void)version; (void)meshVersion;
            return false;
        }

        // 后端是否已持有 (slot, version) 的几何：上传成功，之后同一版本的提交不再读调用方传入的顶点。
        // 为 true 时调用方可以释放自己的那份顶点（几百万顶点的图纸省下一整份副本）；
        // 默认 false：无缓存能力的后端每次提交都要读顶点
        virtual bool IsCached(uint32_t slot, uint64_t version, bool textured) const
        {
            (void)slot; (void)version; (void)textured;
            return false;
        }

        // ── 离屏缓存层（可选能力）────────────────────────────────────────
        // Viewport 把不常变化的内容（网格、坐标轴、场景）画进缓存层，之后每帧
        // 只复制缓存层、再画光标 / 夹点 / 选中等动态内容，鼠标移动不再重画整张图。
        // 不支持的后端返回 nullptr，Viewport 退回每帧全量绘制。
        virtual std::unique_ptr<IRenderTarget> CreateLayerTarget() { return nullptr; }
        // 把缓存层的内容原样复制到当前帧的渲染目标（BeginFrame 之后调用，尺寸必须相同）
        virtual void DrawLayer(IRenderTarget& layer) { (void)layer; }

        // 背景深浅（AutoCAD 的 7 号色语义）：浅色背景时纯白顶点画成纯黑，深色背景时纯黑画成纯白，
        // 白色的实体、图层颜色、十字光标在两种背景下都看得见。只影响线 / 三角形 / 文字，不影响光栅图像
        virtual void SetLightBackground(bool light) { (void)light; }

        // 释放缓存提交的顶点缓冲与图像纹理（关闭文档后调用）：之后的提交按需重新创建、上传
        virtual void ReleaseCachedResources() {}

        virtual void* GetNativeDevice() = 0;
    };
}