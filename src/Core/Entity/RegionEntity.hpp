#pragma once
#include "HatchEntity.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniCAD
{
    // 边界环的面积 / 周长计算（供面域和图案填充共用）。
    //
    // 面积按格林公式 ½∮(x dy − y dx) 沿边界求，直线段、bulge 圆弧段、椭圆弧都用解析公式，
    // 不依赖离散化，所以圆 / 椭圆的面积是精确的；样条按离散折线计算。
    // 边与边之间若有缝隙（首尾没接上），按直线闭合。
    namespace LoopMeasure
    {
        namespace detail
        {
            inline double Cross(const Math::Point3& a, const Math::Point3& b) { return a.x * b.y - a.y * b.x; }
            inline double Dist(const Math::Point3& a, const Math::Point3& b)
            {
                return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
            }

            // 单条边沿走向对 ∮ 的贡献（不含与相邻边之间的闭合弦），返回起点、终点
            inline void EdgeContribution(const HatchEdge& e, double& area2, double& length,
                                         Math::Point3& start, Math::Point3& end)
            {
                area2 = 0.0; length = 0.0;
                switch (e.Type)
                {
                case HatchEdge::Kind::Poly:
                {
                    const auto& pl = e.Poly;
                    if (pl.Points.empty()) { start = end = {}; return; }
                    start = pl.Points.front();
                    end   = pl.Points.back();
                    for (size_t i = 0; i + 1 < pl.Points.size(); ++i)
                    {
                        const auto& A = pl.Points[i];
                        const auto& B = pl.Points[i + 1];
                        area2  += Cross(A, B);
                        const double chord = Dist(A, B);
                        length += chord;

                        const double bulge = pl.SegBulge(static_cast<int>(i));
                        if (std::abs(bulge) > 1e-12 && chord > 1e-12)
                        {
                            // 圆弧段：圆心角 θ = 4·atan(bulge)，半径 r = chord / (2·|sin(θ/2)|)
                            // 弓形面积 ½r²(θ − sinθ)，对 θ 是奇函数，符号随 bulge（凸向行进方向右侧为正）
                            const double theta = 4.0 * std::atan(bulge);
                            const double r     = chord / (2.0 * std::abs(std::sin(theta * 0.5)));
                            area2  += r * r * (theta - std::sin(theta));
                            length += r * std::abs(theta) - chord;
                        }
                    }
                    break;
                }
                case HatchEdge::Kind::EllipseArc:
                {
                    double span = e.A1 - e.A0;
                    if (span <= 0.0) span += Math::TwoPI;
                    start = e.Ell.PointAt(e.A0);
                    end   = e.Ell.PointAt(e.A0 + span);
                    // 参数曲线 P(t) = C + U·cos t + V·sin t：∫cross(P, P')dt = cross(C, P1 − P0) + (U×V)·Δt，U×V = RxRy
                    const auto& C = e.Ell.Center;
                    area2 = Cross(C, { end.x - start.x, end.y - start.y, 0.0 }) + e.Ell.RadiusX * e.Ell.RadiusY * span;
                    const int n = std::max(16, static_cast<int>(std::ceil(span / (Math::PI / 90.0))));
                    Math::Point3 prev = start;
                    for (int k = 1; k <= n; ++k)
                    {
                        const Math::Point3 q = e.Ell.PointAt(e.A0 + span * (static_cast<double>(k) / n));
                        length += Dist(prev, q);
                        prev = q;
                    }
                    break;
                }
                case HatchEdge::Kind::Spline:
                {
                    const auto pts = e.Spl.Tessellate(24);
                    if (pts.empty()) { start = end = {}; return; }
                    start = pts.front();
                    end   = pts.back();
                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                    {
                        area2  += Cross(pts[i], pts[i + 1]);
                        length += Dist(pts[i], pts[i + 1]);
                    }
                    break;
                }
                }
            }
        }

        // 一个环的有向面积（逆时针为正）与周长
        inline void Measure(const HatchLoop& loop, double& signedArea, double& perimeter)
        {
            signedArea = 0.0; perimeter = 0.0;
            if (loop.Edges.empty()) return;

            double area2 = 0.0;
            Math::Point3 first{}, prevEnd{};
            for (size_t i = 0; i < loop.Edges.size(); ++i)
            {
                double a2, len; Math::Point3 s, e;
                detail::EdgeContribution(loop.Edges[i], a2, len, s, e);
                if (i == 0) first = s;
                else
                {   // 边与边之间的缝隙按直线连接
                    area2     += detail::Cross(prevEnd, s);
                    perimeter += detail::Dist(prevEnd, s);
                }
                area2     += a2;
                perimeter += len;
                prevEnd = e;
            }
            area2     += detail::Cross(prevEnd, first);        // 首尾闭合
            perimeter += detail::Dist(prevEnd, first);
            signedArea = area2 * 0.5;
        }

        // 一组环（奇偶规则：被奇数个环包住的环是孔，面积扣除）的总面积与总周长（含孔的周长）
        inline void Measure(const std::vector<HatchLoop>& loops, double& area, double& perimeter)
        {
            area = 0.0; perimeter = 0.0;
            const size_t n = loops.size();
            std::vector<std::vector<Math::Point3>> polys(n);
            for (size_t i = 0; i < n; ++i) polys[i] = loops[i].Tessellate();

            auto inside = [](const std::vector<Math::Point3>& poly, const Math::Point3& p)
            {
                bool in = false;
                for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                {
                    const auto& a = poly[i]; const auto& b = poly[j];
                    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                        in = !in;
                }
                return in;
            };

            for (size_t i = 0; i < n; ++i)
            {
                if (polys[i].size() < 3) continue;
                double sa, per;
                Measure(loops[i], sa, per);

                int depth = 0;                     // 有多少个环包住了本环
                for (size_t j = 0; j < n; ++j)
                    if (j != i && polys[j].size() >= 3 && inside(polys[j], polys[i].front()))
                        ++depth;

                area      += (depth % 2 == 0 ? 1.0 : -1.0) * std::abs(sa);
                perimeter += per;
            }
            area = std::max(area, 0.0);
        }
    }

    // 面域（对应 DXF REGION 的二维情形）：由一个或多个闭合环围成的平面区域，嵌套的环是孔（奇偶规则）。
    //
    // 边界沿用 HatchEntity 的类型化边界环（直线 + 圆弧 / 椭圆弧 / 样条），因此移动 / 复制 / 旋转 / 镜像 /
    // 夹点编辑边界 / 通用拾取都沿用图案填充的实现。默认只画边界线（线框，同 AutoCAD），
    // 打开 ShowFill 时按实心填充显示。
    class RegionEntity : public HatchEntity
    {
    public:
        RegionEntity(ObjectID id, std::vector<HatchLoop> loops)
            : HatchEntity(id, std::move(loops), HatchPattern::MakeSolid())
        {
        }

        bool GetShowFill() const    { return m_showFill; }
        void SetShowFill(bool on)   { m_showFill = on; }

        // 面积（孔已扣除）与周长（含孔的周长）
        double Area() const
        {
            double a, p; LoopMeasure::Measure(GetLoops(), a, p); return a;
        }
        double Perimeter() const
        {
            double a, p; LoopMeasure::Measure(GetLoops(), a, p); return p;
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<RegionEntity>(newId, GetLoops());
            e->SetAttr(GetAttr());
            e->m_showFill = m_showFill;
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_showFill)
                HatchEntity::Draw(sink, isSelected, isHovered);

            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            for (const auto& loop : GetLoops())
            {
                if (loop.Empty()) continue;
                std::vector<Math::Point3> pts = loop.Tessellate();
                if (pts.size() < 2) continue;
                const auto& f = pts.front(); const auto& b = pts.back();
                if (std::abs(f.x - b.x) > 1e-9 || std::abs(f.y - b.y) > 1e-9)
                    pts.push_back(f);                               // 闭合（已闭合的环不重复首点）
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    sink.DrawLine(pts[i], pts[i + 1], color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(RegionEntity, HatchEntity)

    private:
        bool m_showFill = false;
    };
}
