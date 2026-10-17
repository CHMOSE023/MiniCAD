#pragma once
#include "Render/VertexTypes.hpp"
#include <cstdint>
#include <span>

namespace MiniCAD
{
    // ── 文字按字形实例化 ─────────────────────────────────────────────
    // 每个字形（字体里的一个字符）的三角形与轮廓线只存一份网格（字形局部坐标），
    // 文字里的每个字只记一条实例：局部 → 世界的仿射变换 + 颜色。
    // 世界坐标 = (x·m00 + y·m10 + tx, x·m01 + y·m11 + ty, z)（行向量约定，与 Mat4 一致）
    struct GlyphInstance
    {
        float       m00, m01, m10, m11;
        float       tx, ty, z;
        VertexColor color;
        uint32_t    mesh;       // 字形网格编号（GlyphMeshLibrary）
    };
    static_assert(sizeof(GlyphInstance) == 36);

    // 一个字形网格在网格库顶点数组里的区间，以及局部外包框
    struct GlyphMeshRange
    {
        uint32_t fillStart = 0, fillCount = 0;   // 三角形顶点（每 3 个一个三角形）
        uint32_t lineStart = 0, lineCount = 0;   // 线段顶点（每 2 个一条线段）
        float    minX = 0, minY = 0, maxX = 0, maxY = 0;
    };

    // 实例流按网格编号排序后，同一网格的实例连续：一批一次实例化绘制
    struct GlyphBatch
    {
        uint32_t mesh;
        uint32_t first;     // 实例流中的起点
        uint32_t count;
    };

    // 渲染后端看到的字形网格库：版本变化（新增了字形）时重新上传
    struct GlyphMeshView
    {
        std::span<const Math::Float2>   FillVerts;
        std::span<const Math::Float2>   LineVerts;
        std::span<const GlyphMeshRange> Meshes;
        uint64_t                        Version = 0;
    };

    // 把一个实例展开成世界坐标顶点（无实例化能力的后端、遮罩裁剪用）
    inline Vertex_P3_C4 GlyphVertex(const GlyphInstance& g, const Math::Float2& p)
    {
        return { { p.x * g.m00 + p.y * g.m10 + g.tx, p.x * g.m01 + p.y * g.m11 + g.ty, g.z }, g.color };
    }

    template <class Out>
    void ExpandGlyph(const GlyphInstance& g, const GlyphMeshView& meshes, bool fills, Out&& out)
    {
        if (g.mesh >= meshes.Meshes.size()) return;
        const GlyphMeshRange& m = meshes.Meshes[g.mesh];
        const Math::Float2* p = fills ? meshes.FillVerts.data() + m.fillStart : meshes.LineVerts.data() + m.lineStart;
        const uint32_t      n = fills ? m.fillCount : m.lineCount;
        for (uint32_t i = 0; i < n; ++i)
            out(GlyphVertex(g, p[i]));
    }
}
