#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniCAD::RegionBuilder
{
    // 由对象生成面域边界环（类 AutoCAD REGION）：
    //   · 自身封闭的对象直接成环：圆、整椭圆、矩形、首尾相接的多段线、封闭样条
    //   · 直线 / 圆弧 / 未封闭的多段线按端点（容差内）首尾相接，接成闭环后合并为一个多段线环
    //   · 其他对象（椭圆弧、开放样条、文字…）和接不成环的线段被忽略，计入 Result::Skipped
    // 多个环之间不检查是否相交；嵌套的环在面域里成为孔（奇偶规则）。
    struct Result
    {
        std::vector<HatchLoop> Loops;
        int                    Used    = 0;      // 参与成环的对象数
        int                    Skipped = 0;      // 被忽略的对象数
    };

    namespace detail
    {
        inline bool PointInPoly(const std::vector<Math::Point3>& poly, const Math::Point3& p)
        {
            bool in = false;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            {
                const auto& a = poly[i]; const auto& b = poly[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                    in = !in;
            }
            return in;
        }

        inline double D(const Math::Point3& a, const Math::Point3& b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        }

        inline Polyline Reversed(const Polyline& p)
        {
            std::vector<Math::Point3> pts(p.Points.rbegin(), p.Points.rend());
            std::vector<double> bulges;
            for (auto it = p.Bulges.rbegin(); it != p.Bulges.rend(); ++it) bulges.push_back(-*it);
            return Polyline(std::move(pts), std::move(bulges));
        }

        inline Polyline FromArc(const Arc& a)
        {
            const double sweep = a.SweepAngle();
            return Polyline({ a.StartPoint(), a.EndPoint() }, { std::tan(sweep / 4.0) });   // 逆时针弧，bulge 为正
        }
    }

    // 把一组环分成若干面域：互相嵌套的环（外环 + 孔，孔里的岛另算一个面域）归为一组，
    // 互不包含的环（包括互相交叠的）各成一个面域。避免把交叠的环塞进同一个面域得到自相交的结果。
    inline std::vector<std::vector<HatchLoop>> GroupIntoRegions(const std::vector<HatchLoop>& loops)
    {
        const size_t n = loops.size();
        std::vector<std::vector<Math::Point3>> polys(n);
        std::vector<double> area(n, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            polys[i] = loops[i].Tessellate();
            double sa, per;
            LoopMeasure::Measure(loops[i], sa, per);
            area[i] = std::abs(sa);
        }

        // 父环 = 包含本环（用首顶点判断）且面积更大的环里面积最小的一个
        std::vector<int> parent(n, -1), depth(n, 0);
        for (size_t i = 0; i < n; ++i)
        {
            if (polys[i].size() < 3) continue;
            for (size_t j = 0; j < n; ++j)
            {
                if (j == i || polys[j].size() < 3 || area[j] <= area[i]) continue;
                if (detail::PointInPoly(polys[j], polys[i].front()) && (parent[i] < 0 || area[j] < area[parent[i]]))
                    parent[i] = static_cast<int>(j);
            }
        }
        for (size_t i = 0; i < n; ++i)
            for (int p = parent[i]; p >= 0; p = parent[p]) ++depth[i];

        // 按嵌套深度从浅到深分组：偶数深度自成一组，奇数深度（孔）并入父环所在的组
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return depth[a] < depth[b]; });

        std::vector<int> groupOf(n, -1);
        std::vector<std::vector<HatchLoop>> groups;
        for (size_t i : order)
        {
            if (depth[i] % 2 == 0 || parent[i] < 0)
            {
                groupOf[i] = static_cast<int>(groups.size());
                groups.emplace_back();
            }
            else
                groupOf[i] = groupOf[parent[i]];
            groups[groupOf[i]].push_back(loops[i]);
        }
        return groups;
    }

    // tol：端点重合容差
    inline Result Build(const std::vector<const Entity*>& entities, double tol = 1e-6)
    {
        Result r;
        std::vector<Polyline> open;                 // 待接的开放线段
        std::vector<int>      openOwner;            // 各线段所属对象序号（用于统计 Used / Skipped）
        int idx = 0;
        std::vector<bool> used(entities.size(), false);

        for (const Entity* e : entities)
        {
            const int me = idx++;
            if (!e) continue;

            if (e->IsKindOf<CircleEntity>())
            {
                const Circle& c = static_cast<const CircleEntity*>(e)->GetCircle();
                if (c.Radius > Math::LengthEPS)
                {
                    HatchLoop lp;
                    lp.Edges.push_back(HatchEdge::MakeEllipseArc(Ellipse(c.Center, c.Radius, c.Radius), 0.0, Math::TwoPI));
                    r.Loops.push_back(std::move(lp));
                    used[me] = true;
                }
            }
            else if (e->IsKindOf<EllipseEntity>())
            {
                const Ellipse& el = static_cast<const EllipseEntity*>(e)->GetEllipse();
                if (el.IsValid() && el.IsFull())
                {
                    HatchLoop lp;
                    lp.Edges.push_back(HatchEdge::MakeEllipseArc(el, 0.0, Math::TwoPI));
                    r.Loops.push_back(std::move(lp));
                    used[me] = true;
                }
            }
            else if (e->IsKindOf<RectangleEntity>())
            {
                const Rectangle& rc = static_cast<const RectangleEntity*>(e)->GetRectangle();
                r.Loops.push_back(HatchLoop::FromPolyline(Polyline({ rc.P1, rc.P2, rc.P3, rc.P4, rc.P1 })));
                used[me] = true;
            }
            else if (e->IsKindOf<SplineEntity>())
            {
                const Spline& sp = static_cast<const SplineEntity*>(e)->GetSpline();
                if (sp.IsValid() && sp.IsClosed())
                {
                    HatchLoop lp;
                    lp.Edges.push_back(HatchEdge::MakeSpline(sp));
                    r.Loops.push_back(std::move(lp));
                    used[me] = true;
                }
            }
            else if (e->IsKindOf<PolylineEntity>())
            {
                const Polyline& pl = static_cast<const PolylineEntity*>(e)->GetPolyline();
                if (pl.IsValid())
                {
                    if (pl.Points.size() >= 4 && detail::D(pl.Points.front(), pl.Points.back()) <= tol)
                    {
                        Polyline closed = pl;
                        closed.Points.back() = closed.Points.front();       // 严格闭合
                        r.Loops.push_back(HatchLoop::FromPolyline(std::move(closed)));
                        used[me] = true;
                    }
                    else
                    {
                        open.push_back(pl);
                        openOwner.push_back(me);
                    }
                }
            }
            else if (e->IsKindOf<LineEntity>())
            {
                const Line& l = static_cast<const LineEntity*>(e)->GetLine();
                if (detail::D(l.Start, l.End) > tol)
                {
                    open.push_back(Polyline({ l.Start, l.End }));
                    openOwner.push_back(me);
                }
            }
            else if (e->IsKindOf<ArcEntity>())
            {
                const Arc& a = static_cast<const ArcEntity*>(e)->GetArc();
                if (a.Radius > Math::LengthEPS)
                {
                    open.push_back(detail::FromArc(a));
                    openOwner.push_back(me);
                }
            }
        }

        // 端点首尾相接：从任一线段出发，沿终点找下一段，回到起点即成环
        std::vector<bool> taken(open.size(), false);
        for (size_t s = 0; s < open.size(); ++s)
        {
            if (taken[s]) continue;
            taken[s] = true;
            Polyline chain = open[s];
            std::vector<size_t> members{ s };

            bool closed = false;
            for (;;)
            {
                if (chain.Points.size() >= 3 && detail::D(chain.Points.front(), chain.Points.back()) <= tol)
                {
                    closed = true;
                    break;
                }
                bool extended = false;
                for (size_t k = 0; k < open.size() && !extended; ++k)
                {
                    if (taken[k]) continue;
                    const Polyline& cand = open[k];
                    const Math::Point3& end = chain.Points.back();
                    Polyline next;
                    if      (detail::D(end, cand.Points.front()) <= tol) next = cand;
                    else if (detail::D(end, cand.Points.back())  <= tol) next = detail::Reversed(cand);
                    else continue;

                    chain.Points.insert(chain.Points.end(), next.Points.begin() + 1, next.Points.end());
                    chain.Bulges.insert(chain.Bulges.end(), next.Bulges.begin(), next.Bulges.end());
                    taken[k] = true;
                    members.push_back(k);
                    extended = true;
                }
                if (!extended) break;
            }

            if (closed)
            {
                chain.Points.back() = chain.Points.front();                 // 严格闭合
                r.Loops.push_back(HatchLoop::FromPolyline(std::move(chain)));
                for (size_t m : members) used[openOwner[m]] = true;
            }
        }

        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (!entities[i]) continue;
            if (used[i]) ++r.Used; else ++r.Skipped;
        }
        return r;
    }
}

namespace MiniCAD::RegionMeasure
{
    // 单个对象的面积与周长（用于 AREA 查询）。只支持能围成区域的对象；其他返回 false。
    inline bool Measure(const Entity& e, double& area, double& perimeter)
    {
        if (e.IsKindOf<HatchEntity>())                    // 面域与图案填充
        {
            LoopMeasure::Measure(static_cast<const HatchEntity&>(e).GetLoops(), area, perimeter);
            return true;
        }

        std::vector<const Entity*> one{ &e };
        auto res = RegionBuilder::Build(one);
        if (res.Loops.empty()) return false;
        LoopMeasure::Measure(res.Loops, area, perimeter);
        return true;
    }
}
