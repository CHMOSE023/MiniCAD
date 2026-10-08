#pragma once
#include "Entity.hpp"
#include "DimensionEntity.hpp"          // 复用 DimStyle / DimStyleID(LEADER 引用 DIMSTYLE)
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // 引线路径形式(DXF LEADER 组码 72)。
    enum class LeaderPath : uint8_t
    {
        Straight = 0,   // 折线段
        Spline          // 样条(此处以 Catmull-Rom 折线近似绘制)
    };

    // 引线随附注释类型(DXF LEADER 组码 73)。注释实体本身独立存储,这里仅记录关联语义,
    // 并内置一个便捷文本字段,便于不挂接外部实体时也能直接显示标签。
    enum class LeaderAnnotation : uint8_t
    {
        None      = 3,
        Text      = 0,
        Tolerance = 1,
        Block     = 2
    };

    // 引线实体(对应 DXF LEADER)。一串顶点构成折线/样条路径,起端可带箭头,
    // 末端可带水平基线(hookline)承接注释。外观(箭头大小/颜色/字高)取自引用的 DIMSTYLE。
    class LeaderEntity : public Entity
    {
    public:
        LeaderEntity(ObjectID id, std::vector<Math::Point3> vertices, const DimStyle& style = {})
            : Entity(id)
            , m_vertices(std::move(vertices))
            , m_style(style)
        {}

        // ── 路径顶点(组码 10)──────────────────────────────────────────────
        const std::vector<Math::Point3>& GetVertices() const { return m_vertices; }
        std::vector<Math::Point3>&        GetVertices()       { return m_vertices; }
        void SetVertices(std::vector<Math::Point3> v)         { m_vertices = std::move(v); }
        void AddVertex(const Math::Point3& p)                 { m_vertices.push_back(p); }

        // ── 外观开关 ───────────────────────────────────────────────────────
        bool GetArrowEnabled() const  { return m_arrow; }            // 组码 71
        void SetArrowEnabled(bool on) { m_arrow = on; }

        LeaderPath GetPath() const      { return m_path; }           // 组码 72
        void       SetPath(LeaderPath p) { m_path = p; }

        bool GetHookline() const   { return m_hookline; }            // 组码 75
        void SetHookline(bool on)  { m_hookline = on; }

        // ── 注释(组码 73 / 40 / 41 + 关联文本)────────────────────────────
        LeaderAnnotation GetAnnotationType() const { return m_annotation; }
        void SetAnnotationType(LeaderAnnotation a)  { m_annotation = a; }

        const std::string& GetText() const { return m_text; }
        void SetText(std::string t)        { m_text = std::move(t); }

        double GetTextHeight() const   { return m_textHeight; }      // 组码 40
        void   SetTextHeight(double h) { m_textHeight = h; }

        // ── 样式 ───────────────────────────────────────────────────────────
        DimStyle&       GetStyle()       { return m_style; }
        const DimStyle& GetStyle() const { return m_style; }
        void SetStyle(const DimStyle& s) { m_style = s; }
        DimStyleID GetStyleId() const    { return m_styleId; }       // 组码 3
        void SetStyleId(DimStyleID id)   { m_styleId = id; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            for (const auto& v : m_vertices)
                box.Expand(v);
            if (!m_text.empty() && !m_vertices.empty())
            {
                const Math::Point3& tail = m_vertices.back();
                const double w = static_cast<double>(m_text.size()) * m_textHeight * 0.6;
                box.Expand({ tail.x + w, tail.y + m_textHeight, tail.z });
            }
            if (m_vertices.empty()) box = AABB({ 0,0,0 }, { 0,0,0 });
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_vertices.size() < 1) return;

            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);

            // 路径折线(样条以 Catmull-Rom 采样近似)。
            std::vector<Math::Point3> path =
                (m_path == LeaderPath::Spline && m_vertices.size() >= 3)
                    ? SampleSpline(m_vertices) : m_vertices;
            for (size_t i = 1; i < path.size(); ++i)
                sink.DrawLine(path[i - 1], path[i], color, false);

            // 末端水平基线(hookline):自末顶点沿尾段方向延伸一个箭头长。
            Math::Point3 tail = m_vertices.back();
            if (m_hookline && m_vertices.size() >= 2)
            {
                const Math::Point3& prev = m_vertices[m_vertices.size() - 2];
                Math::Vec3 d = (tail - prev).Normalized();
                Math::Point3 hookEnd{ tail.x + d.x * m_style.ArrowSize,
                                      tail.y + d.y * m_style.ArrowSize, tail.z };
                sink.DrawLine(tail, hookEnd, color, false);
                tail = hookEnd;
            }

            // 起端箭头:尖端在首顶点,沿首段反向(指向引线外侧)。
            if (m_arrow && m_vertices.size() >= 2)
            {
                Math::Vec3 d = (m_vertices[0] - m_vertices[1]).Normalized();
                if (d.LengthSq() < 0.5) d = { 1, 0, 0 };
                EmitArrow(sink, m_vertices[0], d, color);
            }

            // 关联文本(便捷显示):置于末端右上方。
            if (!m_text.empty())
            {
                Math::Point3 origin{ tail.x + m_style.TextGap, tail.y + m_style.TextGap, tail.z };
                const double h = m_textHeight > 0.0 ? m_textHeight : m_style.TextHeight;
                sink.EmitMText(origin, m_text, m_style.TextStyle, h, 0.0, 0.0, color);
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<LeaderEntity>(newId, m_vertices, m_style);
            e->SetAttr(GetAttr());
            e->m_arrow      = m_arrow;
            e->m_path       = m_path;
            e->m_hookline   = m_hookline;
            e->m_annotation = m_annotation;
            e->m_text       = m_text;
            e->m_textHeight = m_textHeight;
            e->m_styleId    = m_styleId;
            return e;
        }

        DECLARE_RUNTIME_TYPE(LeaderEntity, Entity)

    private:
        // 实心箭头:尖端在 tip,沿 outDir(指向外侧)展开。GB 箭头宽:长 ≈ 1:3。
        void EmitArrow(IDrawSink& sink, const Math::Point3& tip,
                       const Math::Vec3& outDir, const Math::Color4& color) const
        {
            const double L = m_style.ArrowSize;
            const double halfW = L * (1.0 / 6.0);
            Math::Vec3 n{ -outDir.y, outDir.x, 0.0 };
            Math::Point3 base{ tip.x - outDir.x * L, tip.y - outDir.y * L, tip.z };
            Math::Point3 w1{ base.x + n.x * halfW, base.y + n.y * halfW, base.z };
            Math::Point3 w2{ base.x - n.x * halfW, base.y - n.y * halfW, base.z };
            sink.FillTriangle(tip, w1, w2, color);
        }

        // Catmull-Rom 样条采样:经过所有控制顶点,端点用镜像虚点。
        static std::vector<Math::Point3> SampleSpline(const std::vector<Math::Point3>& cp)
        {
            std::vector<Math::Point3> out;
            const int seg = 12;
            auto at = [&](int i) -> Math::Point3 {
                int n = static_cast<int>(cp.size());
                if (i < 0) i = 0; if (i >= n) i = n - 1;
                return cp[static_cast<size_t>(i)];
            };
            out.push_back(cp.front());
            for (size_t i = 0; i + 1 < cp.size(); ++i)
            {
                Math::Point3 p0 = at(static_cast<int>(i) - 1);
                Math::Point3 p1 = at(static_cast<int>(i));
                Math::Point3 p2 = at(static_cast<int>(i) + 1);
                Math::Point3 p3 = at(static_cast<int>(i) + 2);
                for (int s = 1; s <= seg; ++s)
                {
                    double t = static_cast<double>(s) / seg;
                    double t2 = t * t, t3 = t2 * t;
                    auto comp = [&](double a, double b, double c, double d) {
                        return 0.5 * ((2.0 * b) + (-a + c) * t
                               + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2
                               + (-a + 3.0 * b - 3.0 * c + d) * t3);
                    };
                    out.push_back({ comp(p0.x, p1.x, p2.x, p3.x),
                                    comp(p0.y, p1.y, p2.y, p3.y),
                                    comp(p0.z, p1.z, p2.z, p3.z) });
                }
            }
            return out;
        }

        std::vector<Math::Point3> m_vertices;
        DimStyle                  m_style;
        DimStyleID                m_styleId   = DimStyle_StandardID;
        bool                      m_arrow     = true;
        bool                      m_hookline  = true;
        LeaderPath                m_path      = LeaderPath::Straight;
        LeaderAnnotation          m_annotation = LeaderAnnotation::Text;
        std::string               m_text;
        double                    m_textHeight = 3.5;
    };
}
