#pragma once
#include "Core/GeomKernel/Arc.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/Math/Point3.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// 三维出二维图（docs/Solid/PLAN.md，M4）：内核给出的截交线、投影线是采样点组成的折线，
// 这里把它们拟合回直线、圆弧、整圆和带凸度的多段线，出图后可以直接标注（半径、直径、长度）。
// 只看 XY，z 一律取 0。
namespace MiniCAD::Solid
{
    struct FittedCurve
    {
        enum class Kind { Line, Arc, Circle, Polyline };

        Kind     Type = Kind::Polyline;
        Polyline Poly;                    // Line：两个点；Polyline：带凸度（MiniCAD 约定），闭合时末点与首点相同
        Math::Point3 Center;              // Arc / Circle
        double   Radius     = 0.0;
        double   StartAngle = 0.0;        // Arc：逆时针，弧度
        double   EndAngle   = 0.0;
    };

    namespace CurveFitDetail
    {
        // 内核按约 10° 采样曲线；相邻采样点绕圆心的转角超过这个值就不当作圆弧（正八边形是 45°）
        constexpr double kMaxArcStep = 30.0 * Math::PI / 180.0;

        inline double Dist(const Math::Point3& a, const Math::Point3& b) { return std::hypot(b.x - a.x, b.y - a.y); }

        // 点到线段 ab 的距离
        inline double DistToSegment(const Math::Point3& p, const Math::Point3& a, const Math::Point3& b)
        {
            const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
            if (len2 < 1e-30)
                return Dist(p, a);
            const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
            return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
        }

        inline bool FitsLine(const std::vector<Math::Point3>& p, size_t i, size_t j, double tol)
        {
            for (size_t k = i + 1; k < j; ++k)
                if (DistToSegment(p[k], p[i], p[j]) > tol)
                    return false;
            return true;
        }

        // p[i..j] 是否在过 p[i]、p[(i+j)/2]、p[j] 的圆弧上：各点到圆心的距离与半径相差不超过 tol，
        // 且绕圆心单调同向转动、总转角小于一整圈
        inline bool FitsArc(const std::vector<Math::Point3>& p, size_t i, size_t j, double tol, Arc* out = nullptr)
        {
            if (j < i + 3)
                return false;   // 至少 4 个点：3 个点总能定一个圆，第 4 个点才能验证
            const auto arc = Arc::FromThreePoints(p[i], p[(i + j) / 2], p[j]);
            if (!arc || arc->Radius > 1e4 * Dist(p[i], p[j]))
                return false;
            const Math::Point3& c = arc->Center;
            double total = 0.0, sign = 0.0;
            for (size_t k = i; k <= j; ++k)
            {
                if (std::abs(Dist(p[k], c) - arc->Radius) > tol)
                    return false;
                if (k == i)
                    continue;
                const double cross = (p[k - 1].x - c.x) * (p[k].y - c.y) - (p[k - 1].y - c.y) * (p[k].x - c.x);
                const double dot   = (p[k - 1].x - c.x) * (p[k].x - c.x) + (p[k - 1].y - c.y) * (p[k].y - c.y);
                const double step  = std::atan2(cross, dot);
                if (std::abs(step) > kMaxArcStep)
                    return false;   // 采样太稀：是多边形的顶点恰好共圆，不是圆弧
                if (sign == 0.0)
                    sign = step > 0 ? 1.0 : -1.0;
                else if (step * sign <= 0.0)
                    return false;
                total += step;
            }
            if (std::abs(total) >= 2.0 * Math::PI - 1e-9)
                return false;
            if (out)
                *out = *arc;
            return true;
        }

        // 圆弧 a → b（经过 mid）在 MiniCAD 约定下的凸度：先按转向给出符号，再用 Polyline::ComputeArc 校验圆心，不对就取反
        inline double ArcBulge(const Math::Point3& a, const Math::Point3& mid, const Math::Point3& b, const Arc& arc)
        {
            const double cross = (mid.x - a.x) * (b.y - a.y) - (mid.y - a.y) * (b.x - a.x);   // > 0：mid 在 a→b 右侧，逆时针
            const double ccw   = cross > 0 ? 1.0 : -1.0;
            double sweep = std::atan2(b.y - arc.Center.y, b.x - arc.Center.x) - std::atan2(a.y - arc.Center.y, a.x - arc.Center.x);
            if (ccw > 0) { while (sweep <= 0) sweep += 2 * Math::PI; }
            else         { while (sweep >= 0) sweep -= 2 * Math::PI; }
            double bulge = -ccw * std::tan(std::abs(sweep) / 4.0);   // MiniCAD：正凸度为顺时针
            const ArcGeom g = Polyline::ComputeArc(a, b, bulge);
            if (Dist(g.Center, arc.Center) > 1e-6 * std::max(1.0, arc.Radius))
                bulge = -bulge;
            return bulge;
        }
    }

    // 首尾相接的折线连成一条（截交线按面分段给出，连起来才是闭合的轮廓）。只在端点处连接，方向不同的会被反转
    inline std::vector<std::vector<Math::Point3>> JoinPolylines(std::vector<std::vector<Math::Point3>> lines, double tol)
    {
        using CurveFitDetail::Dist;
        std::vector<std::vector<Math::Point3>> out;
        std::vector<bool> used(lines.size(), false);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (used[i] || lines[i].size() < 2)
                continue;
            used[i] = true;
            std::vector<Math::Point3> chain = std::move(lines[i]);
            for (bool grown = true; grown && Dist(chain.front(), chain.back()) > tol;)
            {
                grown = false;
                for (size_t j = 0; j < lines.size(); ++j)
                {
                    if (used[j] || lines[j].size() < 2)
                        continue;
                    auto& l = lines[j];
                    if (Dist(chain.back(), l.back()) <= tol || Dist(chain.front(), l.front()) <= tol)
                        std::reverse(l.begin(), l.end());
                    if (Dist(chain.back(), l.front()) <= tol)
                        chain.insert(chain.end(), l.begin() + 1, l.end());
                    else if (Dist(chain.front(), l.back()) <= tol)
                        chain.insert(chain.begin(), l.begin(), l.end() - 1);
                    else
                        continue;
                    used[j] = true;
                    grown   = true;
                    break;
                }
            }
            out.push_back(std::move(chain));
        }
        return out;
    }

    // 拟合一条折线。tol：允许的偏差（绘图单位），通常取图形尺寸的 1e-5 左右
    inline FittedCurve FitCurve(const std::vector<Math::Point3>& input, double tol)
    {
        using namespace CurveFitDetail;
        FittedCurve out;

        std::vector<Math::Point3> p;
        for (const auto& q : input)
        {
            const Math::Point3 flat{ q.x, q.y, 0.0 };
            if (p.empty() || Dist(p.back(), flat) > tol * 1e-3)
                p.push_back(flat);
        }
        if (p.size() < 2)
        {
            out.Poly = Polyline(p);
            return out;
        }
        const bool closed = p.size() >= 4 && Dist(p.front(), p.back()) <= tol;
        if (closed)
            p.back() = p.front();

        // 整圆：所有点到圆心等距，并绕一圈
        if (closed && p.size() >= 7)
        {
            const size_t n = p.size() - 1;
            if (const auto arc = Arc::FromThreePoints(p[0], p[n / 3], p[2 * n / 3]))
            {
                bool round = true;
                for (size_t k = 0; k < p.size() && round; ++k)
                {
                    round = std::abs(Dist(p[k], arc->Center) - arc->Radius) <= tol;
                    if (round && k > 0)   // 相邻点的弦不能太长（与 FitsArc 相同）
                        round = Dist(p[k - 1], p[k]) <= 2.0 * arc->Radius * std::sin(kMaxArcStep / 2.0);
                }
                if (round)
                {
                    out.Type   = FittedCurve::Kind::Circle;
                    out.Center = arc->Center;
                    out.Radius = arc->Radius;
                    return out;
                }
            }
        }

        // 贪心分段：每段取直线与圆弧中能覆盖更多点的一种
        std::vector<Math::Point3> pts{ p[0] };
        std::vector<double>       bulges;
        size_t i = 0;
        while (i + 1 < p.size())
        {
            size_t lineEnd = i + 1;
            while (lineEnd + 1 < p.size() && FitsLine(p, i, lineEnd + 1, tol))
                ++lineEnd;
            size_t arcEnd = i;
            Arc    arc;
            for (size_t j = i + 3; j < p.size() && FitsArc(p, i, j, tol); ++j)
                arcEnd = j;
            if (arcEnd > lineEnd && FitsArc(p, i, arcEnd, tol, &arc))
            {
                bulges.push_back(ArcBulge(p[i], p[(i + arcEnd) / 2], p[arcEnd], arc));
                pts.push_back(p[arcEnd]);
                i = arcEnd;
            }
            else
            {
                bulges.push_back(0.0);
                pts.push_back(p[lineEnd]);
                i = lineEnd;
            }
        }

        if (pts.size() == 2 && !closed)
        {
            if (bulges[0] == 0.0)
            {
                out.Type = FittedCurve::Kind::Line;
                out.Poly = Polyline(pts);
                return out;
            }
            // 一段圆弧：用三点重建（Arc 总是逆时针存储）
            const ArcGeom g = Polyline::ComputeArc(pts[0], pts[1], bulges[0]);
            const double  midAngle = g.StartAngle + g.SweepAngle * 0.5;
            const Math::Point3 mid{ g.Center.x + g.Radius * std::cos(midAngle), g.Center.y + g.Radius * std::sin(midAngle), 0.0 };
            if (const auto a = Arc::FromThreePoints(pts[0], mid, pts[1]))
            {
                out.Type       = FittedCurve::Kind::Arc;
                out.Center     = a->Center;
                out.Radius     = a->Radius;
                out.StartAngle = a->StartAngle;
                out.EndAngle   = a->EndAngle;
                return out;
            }
        }

        out.Type = FittedCurve::Kind::Polyline;
        out.Poly = Polyline(std::move(pts), std::move(bulges));
        return out;
    }
}
