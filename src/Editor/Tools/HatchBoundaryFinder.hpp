#pragma once
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cmath>

namespace MiniCAD::HatchBoundary
{
    // =========================================================================
    // 拾取点边界探测(类 AutoCAD BHATCH「拾取内部点」)
    //
    // 把场景中所有线形几何离散为线段并保留来源信息(provenance) → 在交点处打断构成
    // 平面图 → 追踪所有最小面 → 取「包含拾取点且面积最小的有界面」。再按每段边界
    // 的来源曲线类型,把面还原为「类型化边界边」(直线/圆弧 → Poly+bulge,椭圆弧 →
    // EllipseArc,样条 → Spline),从而填充边界与原曲线一致、夹点编辑也与之一致。
    //
    // 找不到这样的闭合域则返回 false(不创建填充)。
    // =========================================================================

    namespace detail
    {
        // 来源曲线:保存几何 + 折线采样及对应参数,供还原类型化边界边。
        struct Source
        {
            enum K { Line, Arc, Ellipse, Spline, Poly } kind;
            Math::Point3 a, b;                 // Line
            MiniCAD::Arc arc;                  // Arc / Circle(full)
            MiniCAD::Ellipse ell;              // Ellipse
            MiniCAD::Spline  spl;              // Spline
            bool closed = false;               // Circle / Ellipse / 闭合样条

            std::vector<Math::Point3> tp;      // 折线采样点
            std::vector<double>       tparam;  // 各采样点对应参数(随类型:t / 角度 / 分数)
        };

        inline void AddArcSource(std::vector<Source>& src, const MiniCAD::Arc& arc, bool closed)
        {
            if (arc.Radius <= Math::LengthEPS) return;
            Source s; s.kind = Source::Arc; s.arc = arc; s.closed = closed;
            double sweep = closed ? Math::TwoPI : arc.SweepAngle();
            int n = std::max(12, static_cast<int>(std::ceil(sweep / (Math::PI / 18.0))));
            for (int k = 0; k <= n; ++k)
            {
                double ang = arc.StartAngle + sweep * (static_cast<double>(k) / n);
                s.tp.push_back(arc.PointAt(ang));
                s.tparam.push_back(ang);
            }
            src.push_back(std::move(s));
        }

        inline void CollectSources(const Scene& scene, std::vector<Source>& src)
        {
            scene.ForEachObject([&](const Object& o)
            {
                const Entity* ent = static_cast<const Entity*>(&o);

                if (ent->IsKindOf<LineEntity>())
                {
                    const Line& L = static_cast<const LineEntity*>(ent)->GetLine();
                    Source s; s.kind = Source::Line; s.a = L.Start; s.b = L.End;
                    s.tp = { L.Start, L.End }; s.tparam = { 0.0, 1.0 };
                    src.push_back(std::move(s));
                    return;
                }
                if (ent->IsKindOf<ArcEntity>())
                {
                    AddArcSource(src, static_cast<const ArcEntity*>(ent)->GetArc(), false);
                    return;
                }
                if (ent->IsKindOf<CircleEntity>())
                {
                    const Circle& c = static_cast<const CircleEntity*>(ent)->GetCircle();
                    AddArcSource(src, MiniCAD::Arc{ c.Center, c.Radius, 0.0, 0.0 }, true);
                    return;
                }
                if (ent->IsKindOf<EllipseEntity>())
                {
                    const Ellipse& el = static_cast<const EllipseEntity*>(ent)->GetEllipse();
                    if (!el.IsValid()) return;
                    // 椭圆弧为开放来源;参数沿弧单调递增(可越过 2π,PointAt 按周期求值)。
                    // 来源只存整椭圆几何,区间由 tparam 表达——还原出的 HatchEdge 用 A0/A1 而非 Ell 的参数。
                    Source s; s.kind = Source::Ellipse; s.ell = el; s.closed = el.IsFull();
                    s.ell.StartParam = 0.0; s.ell.EndParam = Math::TwoPI;
                    const double t0    = el.IsFull() ? 0.0 : el.StartParam;
                    const double sweep = el.SweepParam();
                    int n = std::max(12, static_cast<int>(std::ceil(72.0 * sweep / Math::TwoPI)));
                    for (int k = 0; k <= n; ++k)
                    {
                        double t = t0 + sweep * (static_cast<double>(k) / n);
                        s.tp.push_back(el.PointAt(t)); s.tparam.push_back(t);
                    }
                    src.push_back(std::move(s));
                    return;
                }
                if (ent->IsKindOf<SplineEntity>())
                {
                    const Spline& sp = static_cast<const SplineEntity*>(ent)->GetSpline();
                    if (!sp.IsValid()) return;
                    Source s; s.kind = Source::Spline; s.spl = sp; s.closed = sp.IsClosed();
                    s.tp = sp.Tessellate(24);
                    int m = static_cast<int>(s.tp.size());
                    for (int k = 0; k < m; ++k)
                        s.tparam.push_back(m > 1 ? static_cast<double>(k) / (m - 1) : 0.0);
                    if (m >= 2) src.push_back(std::move(s));
                    return;
                }
                if (ent->IsKindOf<PolylineEntity>())
                {
                    Source s; s.kind = Source::Poly;
                    s.tp = static_cast<const PolylineEntity*>(ent)->GetPolyline().Tessellate();
                    int m = static_cast<int>(s.tp.size());
                    for (int k = 0; k < m; ++k)
                        s.tparam.push_back(k);
                    if (m >= 2) src.push_back(std::move(s));
                    return;
                }
                if (ent->IsKindOf<RectangleEntity>())
                {
                    const Rectangle& r = static_cast<const RectangleEntity*>(ent)->GetRectangle();
                    Source s; s.kind = Source::Poly;
                    s.tp = { r.P1, r.P2, r.P3, r.P4, r.P1 };
                    for (int k = 0; k < 5; ++k) s.tparam.push_back(k);
                    src.push_back(std::move(s));
                    return;
                }
            });
        }

        inline double Cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

        // 还原:沿来源 s 在参数 p 处的世界点(折线插值,供样条/兜底采样)。
        inline Math::Point3 WorldAtParam(const Source& s, double p)
        {
            const auto& tp = s.tparam;
            if (tp.empty()) return {};
            if (p <= tp.front()) return s.tp.front();
            if (p >= tp.back())  return s.tp.back();
            for (size_t i = 1; i < tp.size(); ++i)
                if (tp[i] >= p)
                {
                    double d = tp[i] - tp[i - 1];
                    double f = d > 1e-12 ? (p - tp[i - 1]) / d : 0.0;
                    const auto& A = s.tp[i - 1]; const auto& B = s.tp[i];
                    return { A.x + (B.x - A.x) * f, A.y + (B.y - A.y) * f, A.z + (B.z - A.z) * f };
                }
            return s.tp.back();
        }
    }

    // 探测包含 pick 的最小有界闭合环;成功填入 outLoop(类型化边界环)。
    inline bool FindEnclosingLoop(const Scene& scene, const Math::Point3& pick, HatchLoop& outLoop)
    {
        using namespace detail;

        std::vector<Source> sources;
        CollectSources(scene, sources);
        if (sources.empty()) return false;

        // 原始线段(带来源 + 端点参数)。
        struct RawSeg { double x0, y0, x1, y1; int src; double pa, pb; };
        std::vector<RawSeg> raw;
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (int si = 0; si < static_cast<int>(sources.size()); ++si)
        {
            const auto& s = sources[si];
            for (size_t i = 1; i < s.tp.size(); ++i)
            {
                raw.push_back({ s.tp[i - 1].x, s.tp[i - 1].y, s.tp[i].x, s.tp[i].y,
                                si, s.tparam[i - 1], s.tparam[i] });
                for (const auto& p : { s.tp[i - 1], s.tp[i] })
                {
                    minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
                    miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
                }
            }
        }
        if (raw.size() < 3) return false;

        double diag = std::hypot(maxx - minx, maxy - miny);
        double tol  = std::max(1e-9, diag * 1e-7);

        // ── 在交点 / T 形接触处打断,得到带参数的子线段 ──────────────────────
        const int nr = static_cast<int>(raw.size());
        std::vector<std::vector<double>> cuts(nr);
        for (int i = 0; i < nr; ++i) { cuts[i] = { 0.0, 1.0 }; }
        auto addCut = [&](int i, double t) { if (t > tol && t < 1.0 - tol) cuts[i].push_back(t); };

        for (int i = 0; i < nr; ++i)
        {
            double pix = raw[i].x0, piy = raw[i].y0;
            double rix = raw[i].x1 - raw[i].x0, riy = raw[i].y1 - raw[i].y0;
            double rlen2 = rix * rix + riy * riy;
            if (rlen2 < tol * tol) continue;

            for (int j = i + 1; j < nr; ++j)
            {
                double qjx = raw[j].x0, qjy = raw[j].y0;
                double sjx = raw[j].x1 - raw[j].x0, sjy = raw[j].y1 - raw[j].y0;
                double slen2 = sjx * sjx + sjy * sjy;
                if (slen2 < tol * tol) continue;

                double denom = Cross(rix, riy, sjx, sjy);
                double qpx = qjx - pix, qpy = qjy - piy;
                if (std::abs(denom) > 1e-12)
                {
                    double t = Cross(qpx, qpy, sjx, sjy) / denom;
                    double u = Cross(qpx, qpy, rix, riy) / denom;
                    if (t >= -tol && t <= 1.0 + tol && u >= -tol && u <= 1.0 + tol)
                    {
                        addCut(i, t); addCut(j, u);
                    }
                }
                else
                {
                    auto proj = [&](double px, double py, double ox, double oy,
                                    double dx, double dy, double dl2)
                    { return ((px - ox) * dx + (py - oy) * dy) / dl2; };
                    for (double t : { proj(qjx, qjy, pix, piy, rix, riy, rlen2),
                                      proj(qjx + sjx, qjy + sjy, pix, piy, rix, riy, rlen2) })
                        if (t > 0.0 && t < 1.0)
                        {
                            double cx = pix + t * rix, cy = piy + t * riy;
                            double ux = proj(cx, cy, qjx, qjy, sjx, sjy, slen2);
                            if (ux > tol && ux < 1.0 - tol) addCut(i, t);
                        }
                    for (double u : { proj(pix, piy, qjx, qjy, sjx, sjy, slen2),
                                      proj(pix + rix, piy + riy, qjx, qjy, sjx, sjy, slen2) })
                        if (u > 0.0 && u < 1.0)
                        {
                            double cx = qjx + u * sjx, cy = qjy + u * sjy;
                            double tx = proj(cx, cy, pix, piy, rix, riy, rlen2);
                            if (tx > tol && tx < 1.0 - tol) addCut(j, u);
                        }
                }
            }
        }

        struct SubSeg { double x0, y0, x1, y1; int src; double pStart, pEnd; };
        std::vector<SubSeg> subs;
        for (int i = 0; i < nr; ++i)
        {
            auto& ts = cuts[i];
            std::sort(ts.begin(), ts.end());
            ts.erase(std::unique(ts.begin(), ts.end(),
                     [&](double a, double b){ return std::abs(a - b) < tol; }), ts.end());
            double pix = raw[i].x0, piy = raw[i].y0;
            double rix = raw[i].x1 - raw[i].x0, riy = raw[i].y1 - raw[i].y0;
            for (size_t k = 0; k + 1 < ts.size(); ++k)
            {
                double t0 = ts[k], t1 = ts[k + 1];
                subs.push_back({ pix + t0 * rix, piy + t0 * riy,
                                 pix + t1 * rix, piy + t1 * riy, raw[i].src,
                                 raw[i].pa + (raw[i].pb - raw[i].pa) * t0,
                                 raw[i].pa + (raw[i].pb - raw[i].pa) * t1 });
            }
        }

        // ── 建平面图:合并近邻点为节点,记录每条无向边的来源 / 参数 ──────────
        std::vector<Math::Point2> nodes;
        auto nodeOf = [&](double x, double y) -> int
        {
            for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
                if (std::abs(nodes[i].x - x) < tol && std::abs(nodes[i].y - y) < tol) return i;
            nodes.push_back({ x, y });
            return static_cast<int>(nodes.size()) - 1;
        };

        struct Prov { int src; double pMin, pMax; };   // 参数按 (minNode → maxNode) 取向
        std::set<std::pair<int, int>> edgeSet;
        std::map<std::pair<int, int>, Prov> prov;
        for (const auto& s : subs)
        {
            int a = nodeOf(s.x0, s.y0), b = nodeOf(s.x1, s.y1);
            if (a == b) continue;
            std::pair<int, int> key{ std::min(a, b), std::max(a, b) };
            edgeSet.insert(key);
            prov[key] = (a < b) ? Prov{ s.src, s.pStart, s.pEnd }
                                : Prov{ s.src, s.pEnd, s.pStart };
        }
        if (edgeSet.empty()) return false;

        struct Nbr { int to; double ang; };
        std::vector<std::vector<Nbr>> adj(nodes.size());
        for (const auto& e : edgeSet)
        {
            auto ang = [&](int f, int t){ return std::atan2(nodes[t].y - nodes[f].y, nodes[t].x - nodes[f].x); };
            adj[e.first].push_back({ e.second, ang(e.first, e.second) });
            adj[e.second].push_back({ e.first, ang(e.second, e.first) });
        }
        for (auto& l : adj) std::sort(l.begin(), l.end(), [](const Nbr& a, const Nbr& b){ return a.ang < b.ang; });

        auto idxInAdj = [&](int v, int target) -> int
        {
            const auto& l = adj[v];
            for (int k = 0; k < static_cast<int>(l.size()); ++k) if (l[k].to == target) return k;
            return -1;
        };

        // ── 面追踪,挑出包含 pick 的最小有界面 ──────────────────────────────
        struct Step { int from, to, src; double pStart, pEnd; };
        std::set<std::pair<int, int>> visited;
        std::vector<Step> bestFace;
        double bestArea = 1e300;

        for (const auto& e : edgeSet)
            for (int dir = 0; dir < 2; ++dir)
            {
                int su = dir == 0 ? e.first : e.second;
                int sv = dir == 0 ? e.second : e.first;
                if (visited.count({ su, sv })) continue;

                std::vector<Step> face;
                int u = su, v = sv; bool ok = true;
                while (true)
                {
                    visited.insert({ u, v });
                    std::pair<int, int> key{ std::min(u, v), std::max(u, v) };
                    const Prov& pr = prov[key];
                    Step st{ u, v, pr.src, pr.pMin, pr.pMax };
                    if (u > v) std::swap(st.pStart, st.pEnd);   // 取向到 u→v
                    face.push_back(st);

                    int k = idxInAdj(v, u);
                    if (k < 0) { ok = false; break; }
                    int deg = static_cast<int>(adj[v].size());
                    int w = adj[v][(k - 1 + deg) % deg].to;
                    u = v; v = w;
                    if (u == su && v == sv) break;
                    if (face.size() > nodes.size() + 4) { ok = false; break; }
                }
                if (!ok || face.size() < 2) continue;

                double area2 = 0.0;
                for (size_t i = 0; i < face.size(); ++i)
                {
                    const auto& p = nodes[face[i].from];
                    const auto& q = nodes[face[(i + 1) % face.size()].from];
                    area2 += p.x * q.y - q.x * p.y;
                }
                if (area2 <= tol * tol) continue;            // 外部面 / 退化面

                bool inside = false;
                for (size_t i = 0, j = face.size() - 1; i < face.size(); j = i++)
                {
                    const auto& a = nodes[face[i].from];
                    const auto& b = nodes[face[j].from];
                    if (((a.y > pick.y) != (b.y > pick.y)) &&
                        (pick.x < (b.x - a.x) * (pick.y - a.y) / (b.y - a.y) + a.x))
                        inside = !inside;
                }
                if (!inside) continue;

                if (area2 * 0.5 < bestArea) { bestArea = area2 * 0.5; bestFace = face; }
            }

        if (bestFace.empty()) return false;

        // ── 把面还原为类型化边界边(按来源分组) ─────────────────────────────
        outLoop.Edges.clear();

        // 整面只来自单一闭合曲线(圆 / 椭圆 / 闭合样条)→ 整条闭合边。
        bool singleClosed = true;
        for (const auto& st : bestFace) if (st.src != bestFace.front().src) { singleClosed = false; break; }
        if (singleClosed && sources[bestFace.front().src].closed)
        {
            const Source& s = sources[bestFace.front().src];
            if (s.kind == Source::Arc)
            {
                Ellipse circ{ s.arc.Center, s.arc.Radius, s.arc.Radius, 0.0 };
                outLoop.Edges.push_back(HatchEdge::MakeEllipseArc(circ, 0.0, Math::TwoPI));
            }
            else if (s.kind == Source::Ellipse)
                outLoop.Edges.push_back(HatchEdge::MakeEllipseArc(s.ell, 0.0, Math::TwoPI));
            else if (s.kind == Source::Spline)
                outLoop.Edges.push_back(HatchEdge::MakeSpline(s.spl));
            return !outLoop.Edges.empty();
        }

        // node 索引 → Point3。
        auto nodes3 = [&](int idx) -> Math::Point3 { return { nodes[idx].x, nodes[idx].y, pick.z }; };

        // 一般情形:连续同源 step 合并为一条 run,逐 run 还原类型化边界边。
        size_t i = 0;
        std::vector<HatchEdge> edges;
        while (i < bestFace.size())
        {
            size_t j = i + 1;
            while (j < bestFace.size() && bestFace[j].src == bestFace[i].src) ++j;
            std::vector<Step> run(bestFace.begin() + i, bestFace.begin() + j);
            const Source& s = sources[run.front().src];

            if (s.kind == Source::Arc)
            {
                double amin = run.front().pStart, amax = run.back().pEnd;
                if (amax < amin) std::swap(amin, amax);
                Math::Point3 A = s.arc.PointAt(amin), B = s.arc.PointAt(amax);
                Polyline pl({ A, B }, { -std::tan((amax - amin) * 0.25) });
                edges.push_back(HatchEdge::MakePoly(std::move(pl)));
            }
            else if (s.kind == Source::Ellipse)
            {
                double amin = run.front().pStart, amax = run.back().pEnd;
                if (amax < amin) std::swap(amin, amax);
                edges.push_back(HatchEdge::MakeEllipseArc(s.ell, amin, amax));
            }
            else if (s.kind == Source::Spline)
            {
                double amin = run.front().pStart, amax = run.back().pEnd;
                if (amax < amin) std::swap(amin, amax);
                std::vector<Math::Point3> fit;
                int n = 8;
                for (int k = 0; k <= n; ++k)
                    fit.push_back(WorldAtParam(s, amin + (amax - amin) * (static_cast<double>(k) / n)));
                edges.push_back(HatchEdge::MakeSpline(Spline(std::move(fit))));
            }
            else // Line / Poly
            {
                std::vector<Math::Point3> pts;
                pts.push_back(nodes3(run.front().from));
                for (const auto& st : run) pts.push_back(nodes3(st.to));
                edges.push_back(HatchEdge::MakePoly(Polyline(std::move(pts))));
            }
            i = j;
        }

        // 合并相邻 Poly 边为单条折线(直线 + 圆弧混合,保留 bulge)。
        auto mergePoly = [](HatchEdge& dst, const HatchEdge& add)
        {
            auto& P = dst.Poly.Points; auto& B = dst.Poly.Bulges;
            const auto& Q = add.Poly.Points; const auto& C = add.Poly.Bulges;
            for (size_t k = (P.empty() ? 0 : 1); k < Q.size(); ++k) P.push_back(Q[k]);
            for (double c : C) B.push_back(c);
        };
        outLoop.Edges.clear();
        for (auto& e : edges)
        {
            if (e.Type == HatchEdge::Kind::Poly && !outLoop.Edges.empty() &&
                outLoop.Edges.back().Type == HatchEdge::Kind::Poly)
                mergePoly(outLoop.Edges.back(), e);
            else
                outLoop.Edges.push_back(std::move(e));
        }
        // 首尾若同为 Poly,合并消除接缝顶点。
        if (outLoop.Edges.size() > 1 &&
            outLoop.Edges.front().Type == HatchEdge::Kind::Poly &&
            outLoop.Edges.back().Type == HatchEdge::Kind::Poly)
        {
            mergePoly(outLoop.Edges.back(), outLoop.Edges.front());
            outLoop.Edges.erase(outLoop.Edges.begin());
        }

        return !outLoop.Edges.empty();
    }
}
