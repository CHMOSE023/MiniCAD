#pragma once
#include "Render/GlyphInstancing.hpp"
#include "Text/Glyph/Glyph.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
    // 字形网格库：每个字形（按 Glyph 对象地址区分——字体的字形缓存在字体存活期间地址不变）只存一份
    // 局部坐标网格，文字的每个字只是一条 GlyphInstance。由 Editor 持有，换文档时清空。
    class GlyphMeshLibrary
    {
    public:
        // 取字形的网格编号，第一次见到时把网格加入库（版本加一）；空字形（空格等）返回 false
        bool Register(const Glyph& glyph, uint32_t& id)
        {
            if (auto it = m_ids.find(&glyph); it != m_ids.end())
            {
                id = it->second;
                return id != kEmpty;
            }
            if (glyph.Lines.empty() && (!glyph.Filled || glyph.Triangles.empty()))
            {
                m_ids.emplace(&glyph, kEmpty);
                return false;
            }

            GlyphMeshRange m;
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            auto add = [&](std::vector<Math::Float2>& out, const Math::Point3& p)
            {
                const Math::Float2 v{ static_cast<float>(p.x), static_cast<float>(p.y) };
                x0 = std::min(x0, v.x); y0 = std::min(y0, v.y);
                x1 = std::max(x1, v.x); y1 = std::max(y1, v.y);
                out.push_back(v);
            };
            m.fillStart = static_cast<uint32_t>(m_fillVerts.size());
            if (glyph.Filled)
                for (const auto& t : glyph.Triangles)
                {
                    add(m_fillVerts, t.a); add(m_fillVerts, t.b); add(m_fillVerts, t.c);
                }
            m.fillCount = static_cast<uint32_t>(m_fillVerts.size()) - m.fillStart;
            m.lineStart = static_cast<uint32_t>(m_lineVerts.size());
            for (const auto& l : glyph.Lines)
            {
                add(m_lineVerts, l.Start); add(m_lineVerts, l.End);
            }
            m.lineCount = static_cast<uint32_t>(m_lineVerts.size()) - m.lineStart;
            m.minX = x0; m.minY = y0; m.maxX = x1; m.maxY = y1;

            id = static_cast<uint32_t>(m_meshes.size());
            m_meshes.push_back(m);
            m_ids.emplace(&glyph, id);
            ++m_version;
            return true;
        }

        GlyphMeshView View() const { return { m_fillVerts, m_lineVerts, m_meshes, m_version }; }

        // 实例的世界坐标外包框
        void Bounds(const GlyphInstance& g, double& x0, double& y0, double& x1, double& y1) const
        {
            const GlyphMeshRange& m = m_meshes[g.mesh];
            x0 = y0 = 1e300;
            x1 = y1 = -1e300;
            for (const Math::Float2 p : { Math::Float2{ m.minX, m.minY }, Math::Float2{ m.maxX, m.minY },
                                          Math::Float2{ m.maxX, m.maxY }, Math::Float2{ m.minX, m.maxY } })
            {
                const Vertex_P3_C4 v = GlyphVertex(g, p);
                x0 = std::min<double>(x0, v.pos.x); x1 = std::max<double>(x1, v.pos.x);
                y0 = std::min<double>(y0, v.pos.y); y1 = std::max<double>(y1, v.pos.y);
            }
        }

        // 展开成世界坐标的线段与三角形顶点
        void Expand(const GlyphInstance& g, std::vector<Vertex_P3_C4>& lines, std::vector<Vertex_P3_C4>& fills) const
        {
            const GlyphMeshView view = View();
            ExpandGlyph(g, view, false, [&](const Vertex_P3_C4& v) { lines.push_back(v); });
            ExpandGlyph(g, view, true,  [&](const Vertex_P3_C4& v) { fills.push_back(v); });
        }

        void Clear()
        {
            decltype(m_ids){}.swap(m_ids);
            std::vector<Math::Float2>{}.swap(m_fillVerts);
            std::vector<Math::Float2>{}.swap(m_lineVerts);
            std::vector<GlyphMeshRange>{}.swap(m_meshes);
            ++m_version;
        }

    private:
        static constexpr uint32_t kEmpty = UINT32_MAX;

        std::unordered_map<const Glyph*, uint32_t> m_ids;
        std::vector<Math::Float2>                  m_fillVerts;
        std::vector<Math::Float2>                  m_lineVerts;
        std::vector<GlyphMeshRange>                m_meshes;
        uint64_t                                   m_version = 0;
    };

    // 实例流按网格编号排序并分批（同一网格的实例连续，一批一次实例化绘制）
    inline void SortGlyphInstances(std::vector<GlyphInstance>& inst, std::vector<GlyphBatch>& batches)
    {
        std::sort(inst.begin(), inst.end(), [](const GlyphInstance& a, const GlyphInstance& b) { return a.mesh < b.mesh; });
        batches.clear();
        for (uint32_t i = 0; i < inst.size(); ++i)
        {
            if (batches.empty() || batches.back().mesh != inst[i].mesh)
                batches.push_back({ inst[i].mesh, i, 0 });
            ++batches.back().count;
        }
    }
}
