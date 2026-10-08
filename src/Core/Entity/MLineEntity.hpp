#pragma once
#include "LeaderEntity.hpp"
#include "Scene/MLineStyleTable.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 多线对正方式（DXF MLINE 组码 70）：哪一条元素线落在用户指定的路径上。
    enum class MLineJustify : uint8_t
    {
        Top    = 0,     // 偏移最大的元素线在路径上，其余在路径右侧
        Zero   = 1,     // 偏移为 0 的位置在路径上
        Bottom = 2      // 偏移最小的元素线在路径上，其余在路径左侧
    };

    // 多线实体（对应 DXF MLINE）：一串路径顶点 + 样式快照，按样式里的偏移画出多条平行线，
    // 内部顶点处做斜接，可选起止封口和顶点连接线。
    //
    // 路径顶点沿用 LeaderEntity 的存储，因此移动 / 旋转 / 镜像 / 顶点夹点 / 通用拾取都沿用引线的实现。
    // 实体持有样式的快照（不随样式表后续修改而变化）。
    class MLineEntity : public LeaderEntity
    {
    public:
        MLineEntity(ObjectID id, std::vector<Math::Point3> vertices, MLineStyleRecord style,
                    MLineJustify justify = MLineJustify::Zero, double scale = 1.0, bool closed = false)
            : LeaderEntity(id, std::move(vertices))
            , m_style(std::move(style))
            , m_justify(justify)
            , m_scale(scale)
            , m_closed(closed)
        {
            m_style.Normalize();
        }

        const MLineStyleRecord& GetMLineStyle() const         { return m_style; }
        void SetMLineStyle(MLineStyleRecord s)                { m_style = std::move(s); m_style.Normalize(); }
        MLineJustify GetJustify() const                       { return m_justify; }
        void         SetJustify(MLineJustify j)               { m_justify = j; }
        double       GetScale() const                         { return m_scale; }
        void         SetScale(double s)                       { m_scale = s; }
        bool         IsClosed() const                         { return m_closed; }
        void         SetClosed(bool c)                        { m_closed = c; }

        // ── 几何：每条元素线一条折线（closed 时首尾相接，不重复首点）──────────────
        struct Geometry
        {
            std::vector<Math::Point3>              Path;       // 去掉重复点之后的路径顶点
            std::vector<std::vector<Math::Point3>> Elements;   // 与样式元素线一一对应，每条一个点 / 路径顶点
        };

        Geometry ComputeGeometry() const
        {
            return Compute(GetVertices(), m_style, m_justify, m_scale, m_closed);
        }

        // 纯函数版本：供绘制工具预览、测试使用
        static Geometry Compute(const std::vector<Math::Point3>& vertices, const MLineStyleRecord& style,
                                MLineJustify justify, double scale, bool closed)
        {
            Geometry g;

            // 去掉连续重复点（闭合时也去掉与首点重合的末点）
            for (const auto& v : vertices)
                if (g.Path.empty() || Dist2(g.Path.back(), v) > 1e-18)
                    g.Path.push_back(v);
            if (closed && g.Path.size() > 1 && Dist2(g.Path.front(), g.Path.back()) <= 1e-18)
                g.Path.pop_back();

            const size_t n = g.Path.size();
            if (n < 2 || style.Elements.empty())
                return g;
            if (closed && n < 3)
                closed = false;

            const size_t segCount = closed ? n : n - 1;
            std::vector<Math::Vec3> normal(segCount);       // 每段的单位左法线
            for (size_t i = 0; i < segCount; ++i)
            {
                const auto& a = g.Path[i];
                const auto& b = g.Path[(i + 1) % n];
                const double dx = b.x - a.x, dy = b.y - a.y;
                const double len = std::sqrt(dx * dx + dy * dy);
                normal[i] = { -dy / len, dx / len, 0.0 };
            }

            // 每个顶点的斜接偏移方向（乘上偏移量即得元素线上的点）
            std::vector<Math::Vec3> miter(n);
            for (size_t i = 0; i < n; ++i)
            {
                const bool hasPrev = closed || i > 0;
                const bool hasNext = closed || i + 1 < n;
                if (!hasPrev)       { miter[i] = normal[i]; continue; }
                if (!hasNext)       { miter[i] = normal[closed ? (i + n - 1) % n : i - 1]; continue; }

                const Math::Vec3& n0 = normal[(i + n - 1) % n];
                const Math::Vec3& n1 = normal[i];
                const double s = 1.0 + n0.x * n1.x + n0.y * n1.y;
                const double k = 1.0 / std::max(s, 0.1);     // 接近折返时限制斜接长度
                miter[i] = { (n0.x + n1.x) * k, (n0.y + n1.y) * k, 0.0 };
            }

            double shift = 0.0;
            if (justify == MLineJustify::Top)         shift = -style.MaxOffset();
            else if (justify == MLineJustify::Bottom) shift = -style.MinOffset();

            for (const auto& el : style.Elements)
            {
                const double off = (el.Offset + shift) * scale;
                std::vector<Math::Point3> pts(n);
                for (size_t i = 0; i < n; ++i)
                    pts[i] = { g.Path[i].x + miter[i].x * off, g.Path[i].y + miter[i].y * off, g.Path[i].z };
                g.Elements.push_back(std::move(pts));
            }
            return g;
        }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            const Geometry g = ComputeGeometry();
            AABB box = AABB::Empty();
            for (const auto& el : g.Elements)
                for (const auto& p : el)
                    box.Expand(p);
            if (g.Elements.empty())
                for (const auto& v : GetVertices())
                    box.Expand(v);
            if (GetVertices().empty())
                box = AABB({ 0, 0, 0 }, { 0, 0, 0 });
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const Geometry g = ComputeGeometry();
            if (g.Elements.empty()) return;

            const Math::Color4 base = isSelected ? IDrawSink::kSelectionColor
                                    : isHovered  ? IDrawSink::kHoverColor
                                                 : ResolveDrawColor(sink);
            const size_t n       = g.Path.size();
            const bool   closed  = m_closed && n >= 3;
            const size_t segs    = closed ? n : n - 1;

            for (size_t k = 0; k < g.Elements.size(); ++k)
            {
                const auto& el = m_style.Elements[k];
                const Math::Color4& c = (!isSelected && !isHovered && el.UseColor) ? el.Color : base;
                const auto& pts = g.Elements[k];
                for (size_t i = 0; i < segs; ++i)
                    sink.DrawLine(pts[i], pts[(i + 1) % n], c, false);
            }

            // 封口 / 连接线：连接最外侧两条元素线（只有一条元素线时没有意义）
            if (g.Elements.size() < 2) return;
            const auto& outer = g.Elements.front();
            const auto& inner = g.Elements.back();
            if (!closed && m_style.StartCap) sink.DrawLine(outer[0],     inner[0],     base, false);
            if (!closed && m_style.EndCap)   sink.DrawLine(outer[n - 1], inner[n - 1], base, false);
            if (m_style.Joints)
                for (size_t i = closed ? 0 : 1; i + (closed ? 0 : 1) < n; ++i)
                    sink.DrawLine(outer[i], inner[i], base, false);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MLineEntity>(newId, GetVertices(), m_style, m_justify, m_scale, m_closed);
            e->SetAttr(GetAttr());
            return e;
        }

        DECLARE_RUNTIME_TYPE(MLineEntity, LeaderEntity)

    private:
        static double Dist2(const Math::Point3& a, const Math::Point3& b)
        {
            const double dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy;
        }

        MLineStyleRecord m_style;
        MLineJustify     m_justify = MLineJustify::Zero;
        double           m_scale   = 1.0;
        bool             m_closed  = false;
    };
}
