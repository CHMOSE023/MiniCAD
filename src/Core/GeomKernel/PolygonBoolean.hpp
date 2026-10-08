#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MiniCAD::PolygonBoolean
{
    // 平面（XY）多边形区域的布尔运算：并 / 交 / 差。
    //
    // 区域 = 若干闭合环，按奇偶规则定义（嵌套的环是孔），所以输入输出都能带孔、带多个岛。
    // 算法（边分类法）：
    //   1. A、B 所有边互相求交（含端点接触、共线重叠），在交点处把边切成小段，顶点按容差合并；
    //   2. 每一小段取中点两侧各一个采样点，按奇偶规则判断两侧是否在 A / B 内，
    //      再代入运算（并 / 交 / 差）得到「两侧是否在结果里」；两侧不同的小段就是结果的边界，
    //      方向取为「结果在左侧」；A、B 重合的小段只保留一份；
    //   3. 把保留的有向小段首尾相连成环，合并共线顶点。
    // 与对方没有任何接触的整个环不拆开：整体判断去留，并在 Loop::Source 里标出它来自哪个输入环，
    // 调用方可据此保留原始（可能是圆弧 / 椭圆的）精确几何。
    //
    // 不检查输入本身是否自相交；z 取输入第一个顶点的 z。

    using Polygon = std::vector<Math::Point3>;      // 环的顶点，不重复首点
    using Region  = std::vector<Polygon>;

    enum class Op { Union, Intersect, Subtract };    // Subtract = A − B

    struct Loop
    {
        Polygon Points;
        int     Source = -1;      // ≥ 0：原样保留的输入环（A 的环在前、B 的环在后的全局序号）；-1：新生成的环
    };

    namespace detail
    {
        struct P { double x, y; };

        inline double Cross(const P& a, const P& b) { return a.x * b.y - a.y * b.x; }
        inline P      Sub(const P& a, const P& b)   { return { a.x - b.x, a.y - b.y }; }
        inline double Len(const P& a)               { return std::sqrt(a.x * a.x + a.y * a.y); }

        // 清理输入环：去掉首尾重复点、连续重复点；不足 3 点的环丢弃
        inline Polygon Clean(const Polygon& in, double tol)
        {
            Polygon out;
            for (const auto& p : in)
                if (out.empty() || std::hypot(p.x - out.back().x, p.y - out.back().y) > tol)
                    out.push_back(p);
            while (out.size() > 1 && std::hypot(out.front().x - out.back().x, out.front().y - out.back().y) <= tol)
                out.pop_back();
            if (out.size() < 3) out.clear();
            return out;
        }

        // 偶奇规则：点是否在区域内（所有环的穿越数之和）
        inline bool Inside(const Region& r, const P& pt)
        {
            bool in = false;
            for (const auto& poly : r)
                for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                {
                    const auto& a = poly[i]; const auto& b = poly[j];
                    if ((a.y > pt.y) != (b.y > pt.y) && pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x)
                        in = !in;
                }
            return in;
        }

        struct Edge
        {
            P   a, b;
            int owner;          // 0 = A, 1 = B
            int loop;           // 全局环序号
            std::vector<std::pair<double, P>> splits;     // (沿边的参数, 交点)
        };

        // 容差内合并的顶点表（空间哈希，查相邻 3×3 格）
        class VertexTable
        {
        public:
            explicit VertexTable(double tol) : m_tol(tol), m_cell(std::max(tol * 4.0, 1e-300)) {}

            int Add(const P& p)
            {
                const int64_t cx = Cell(p.x), cy = Cell(p.y);
                for (int64_t dx = -1; dx <= 1; ++dx)
                    for (int64_t dy = -1; dy <= 1; ++dy)
                    {
                        auto it = m_grid.find(Key(cx + dx, cy + dy));
                        if (it == m_grid.end()) continue;
                        for (int id : it->second)
                            if (std::hypot(m_pts[id].x - p.x, m_pts[id].y - p.y) <= m_tol)
                                return id;
                    }
                const int id = static_cast<int>(m_pts.size());
                m_pts.push_back(p);
                m_grid[Key(cx, cy)].push_back(id);
                return id;
            }
            const P& At(int id) const { return m_pts[id]; }
            size_t Size() const { return m_pts.size(); }

        private:
            int64_t Cell(double v) const { return static_cast<int64_t>(std::floor(v / m_cell)); }
            static uint64_t Key(int64_t x, int64_t y) { return (static_cast<uint64_t>(x) * 73856093ull) ^ (static_cast<uint64_t>(y) * 19349663ull); }

            double m_tol, m_cell;
            std::vector<P> m_pts;
            std::unordered_map<uint64_t, std::vector<int>> m_grid;
        };

        inline bool InResult(Op op, bool inA, bool inB)
        {
            switch (op)
            {
            case Op::Union:     return inA || inB;
            case Op::Intersect: return inA && inB;
            case Op::Subtract:  return inA && !inB;
            }
            return false;
        }
    }

    // 计算 a op b。tol ≤ 0 时按包围盒对角线自动取 1e-9 倍。
    inline std::vector<Loop> Compute(const Region& aIn, const Region& bIn, Op op, double tol = 0.0)
    {
        using namespace detail;
        std::vector<Loop> result;

        // 包围盒对角线 → 容差、采样偏移
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        double z = 0.0; bool haveZ = false;
        for (const Region* r : { &aIn, &bIn })
            for (const auto& poly : *r)
                for (const auto& p : poly)
                {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                    if (!haveZ) { z = p.z; haveZ = true; }
                }
        if (!haveZ) return result;
        const double diag = std::max(std::hypot(maxX - minX, maxY - minY), 1e-12);
        if (tol <= 0.0) tol = 1e-9 * diag;
        const double sampleOffset = 1e-6 * diag;

        // 清理，并给每个环一个全局序号（A 在前、B 在后）
        Region A, B;
        std::vector<int> globalOfA, globalOfB;       // 清理后序号 → 输入序号
        int g = 0;
        for (const auto& poly : aIn) { auto c = Clean(poly, tol); if (!c.empty()) { A.push_back(std::move(c)); globalOfA.push_back(g); } ++g; }
        for (const auto& poly : bIn) { auto c = Clean(poly, tol); if (!c.empty()) { B.push_back(std::move(c)); globalOfB.push_back(g); } ++g; }

        // 全部边；loopBase：清理后环的全局下标（A 环 0..nA-1，B 环 nA..）
        const int nA = static_cast<int>(A.size()), nB = static_cast<int>(B.size());
        std::vector<Edge> edges;
        std::vector<int> loopFirstEdge(nA + nB), loopEdgeCount(nA + nB);
        auto addLoop = [&](const Polygon& poly, int owner, int loopIdx)
        {
            loopFirstEdge[loopIdx] = static_cast<int>(edges.size());
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const auto& p = poly[i]; const auto& q = poly[(i + 1) % poly.size()];
                edges.push_back({ { p.x, p.y }, { q.x, q.y }, owner, loopIdx, {} });
            }
            loopEdgeCount[loopIdx] = static_cast<int>(poly.size());
        };
        for (int i = 0; i < nA; ++i) addLoop(A[i], 0, i);
        for (int i = 0; i < nB; ++i) addLoop(B[i], 1, nA + i);

        std::vector<char> touched(nA + nB, 0);

        // ── 1. A 的边 × B 的边求交 ───────────────────────────────────────────
        auto interior = [&](const Edge& e, const P& pt) -> double
        {
            // pt 在边 e 上的参数；落在两端点容差内返回 -1（不需要切）
            if (std::hypot(pt.x - e.a.x, pt.y - e.a.y) <= tol || std::hypot(pt.x - e.b.x, pt.y - e.b.y) <= tol) return -1.0;
            const P d = Sub(e.b, e.a);
            return ((pt.x - e.a.x) * d.x + (pt.y - e.a.y) * d.y) / (d.x * d.x + d.y * d.y);
        };
        auto addSplit = [&](Edge& e, const P& pt)
        {
            const double t = interior(e, pt);
            if (t > 0.0 && t < 1.0) e.splits.emplace_back(t, pt);
        };

        for (size_t ia = 0; ia < edges.size(); ++ia)
        {
            if (edges[ia].owner != 0) continue;
            for (size_t ib = 0; ib < edges.size(); ++ib)
            {
                if (edges[ib].owner != 1) continue;
                Edge& ea = edges[ia]; Edge& eb = edges[ib];

                // 包围盒快速排除（含容差）
                if (std::max(ea.a.x, ea.b.x) + tol < std::min(eb.a.x, eb.b.x) || std::max(eb.a.x, eb.b.x) + tol < std::min(ea.a.x, ea.b.x) ||
                    std::max(ea.a.y, ea.b.y) + tol < std::min(eb.a.y, eb.b.y) || std::max(eb.a.y, eb.b.y) + tol < std::min(ea.a.y, ea.b.y))
                    continue;

                const P r = Sub(ea.b, ea.a), s = Sub(eb.b, eb.a);
                const double lr = Len(r), ls = Len(s);
                if (lr <= tol || ls <= tol) continue;
                const double rxs = Cross(r, s);
                const P qp = Sub(eb.a, ea.a);

                if (std::abs(rxs) > 1e-12 * lr * ls)
                {
                    const double t = Cross(qp, s) / rxs;
                    const double u = Cross(qp, r) / rxs;
                    const double et = tol / lr, eu = tol / ls;
                    if (t < -et || t > 1.0 + et || u < -eu || u > 1.0 + eu) continue;

                    // 交点：优先吸附到端点，保证两条边切在同一个点上
                    P X{ ea.a.x + std::clamp(t, 0.0, 1.0) * r.x, ea.a.y + std::clamp(t, 0.0, 1.0) * r.y };
                    if (t <= et)              X = ea.a;
                    else if (t >= 1.0 - et)   X = ea.b;
                    else if (u <= eu)         X = eb.a;
                    else if (u >= 1.0 - eu)   X = eb.b;
                    addSplit(ea, X);
                    addSplit(eb, X);
                    touched[ea.loop] = touched[eb.loop] = 1;
                }
                else if (std::abs(Cross(qp, r)) / lr <= tol)
                {
                    // 平行且共线：看投影区间是否重叠，把对方落在本边内部的端点作为切点
                    const double tb0 = ((eb.a.x - ea.a.x) * r.x + (eb.a.y - ea.a.y) * r.y) / (lr * lr);
                    const double tb1 = ((eb.b.x - ea.a.x) * r.x + (eb.b.y - ea.a.y) * r.y) / (lr * lr);
                    const double lo = std::min(tb0, tb1), hi = std::max(tb0, tb1), et = tol / lr;
                    if (hi < -et || lo > 1.0 + et) continue;
                    touched[ea.loop] = touched[eb.loop] = 1;
                    addSplit(ea, eb.a); addSplit(ea, eb.b);
                    addSplit(eb, ea.a); addSplit(eb, ea.b);
                }
            }
        }

        auto inRes = [&](double x, double y)
        {
            const P p{ x, y };
            return InResult(op, Inside(A, p), Inside(B, p));
        };

        // 一条边两侧的采样判断：返回 -1 = 两侧相同（不是边界），1 = 结果在左侧，0 = 结果在右侧
        auto boundarySide = [&](const P& u, const P& v) -> int
        {
            const P d = Sub(v, u);
            const double len = Len(d);
            if (len <= 0.0) return -1;
            const P mid{ (u.x + v.x) * 0.5, (u.y + v.y) * 0.5 };
            const P n{ -d.y / len * sampleOffset, d.x / len * sampleOffset };
            const bool L = inRes(mid.x + n.x, mid.y + n.y);
            const bool R = inRes(mid.x - n.x, mid.y - n.y);
            if (L == R) return -1;
            return L ? 1 : 0;
        };

        // ── 2. 未接触的环：整体判断去留 ──────────────────────────────────────
        std::vector<char> loopHandled(nA + nB, 0);
        for (int li = 0; li < nA + nB; ++li)
        {
            if (touched[li]) continue;
            loopHandled[li] = 1;
            const Edge& e0 = edges[loopFirstEdge[li]];
            if (boundarySide(e0.a, e0.b) < 0) continue;       // 不是结果的边界
            const Polygon& src = li < nA ? A[li] : B[li - nA];
            result.push_back({ src, li < nA ? globalOfA[li] : globalOfB[li - nA] });
        }

        // ── 3. 接触的环：切段、合并顶点、分类、连环 ─────────────────────────
        VertexTable verts(tol);
        struct Seg { int u, v; };
        std::vector<Seg> kept;
        std::map<std::pair<int, int>, bool> seen;              // 无向去重（A、B 重合的边只留一份）

        for (const Edge& e : edges)
        {
            if (loopHandled[e.loop]) continue;

            std::vector<std::pair<double, P>> pts = e.splits;
            std::sort(pts.begin(), pts.end(), [](const auto& l, const auto& r) { return l.first < r.first; });

            std::vector<int> ids;
            ids.push_back(verts.Add(e.a));
            for (const auto& sp : pts) ids.push_back(verts.Add(sp.second));
            ids.push_back(verts.Add(e.b));

            for (size_t i = 0; i + 1 < ids.size(); ++i)
            {
                const int u = ids[i], v = ids[i + 1];
                if (u == v) continue;
                if (!seen.emplace(std::make_pair(std::min(u, v), std::max(u, v)), true).second) continue;

                const int side = boundarySide(verts.At(u), verts.At(v));
                if (side < 0) continue;
                kept.push_back(side == 1 ? Seg{ u, v } : Seg{ v, u });          // 方向：结果在左侧
            }
        }

        std::vector<std::vector<int>> outgoing(verts.Size());
        for (size_t i = 0; i < kept.size(); ++i) outgoing[kept[i].u].push_back(static_cast<int>(i));
        std::vector<char> used(kept.size(), 0);

        for (size_t start = 0; start < kept.size(); ++start)
        {
            if (used[start]) continue;

            std::vector<int> chain;                              // 顶点序号
            int cur = static_cast<int>(start);
            bool closed = false;
            for (;;)
            {
                used[cur] = 1;
                chain.push_back(kept[cur].u);
                const int head = kept[cur].v;
                if (head == kept[start].u) { closed = true; break; }

                // 从 head 出发的未用边里，取从「来路反方向」顺时针转过角度最小的一条
                const P in = Sub(verts.At(kept[cur].v), verts.At(kept[cur].u));
                const double back = std::atan2(-in.y, -in.x);
                int next = -1;
                double bestDelta = 1e300;
                for (int cand : outgoing[head])
                {
                    if (used[cand]) continue;
                    const P d = Sub(verts.At(kept[cand].v), verts.At(kept[cand].u));
                    double delta = back - std::atan2(d.y, d.x);
                    while (delta <= 0.0) delta += 2.0 * Math::PI;
                    while (delta > 2.0 * Math::PI) delta -= 2.0 * Math::PI;
                    if (delta < bestDelta) { bestDelta = delta; next = cand; }
                }
                if (next < 0) break;                              // 没有出路：数值缺口，丢弃这条链
                cur = next;
            }
            if (!closed || chain.size() < 3) continue;

            // 合并共线顶点
            Polygon poly;
            const size_t n = chain.size();
            for (size_t i = 0; i < n; ++i)
            {
                const P& prev = verts.At(chain[(i + n - 1) % n]);
                const P& here = verts.At(chain[i]);
                const P& next = verts.At(chain[(i + 1) % n]);
                const P d1 = Sub(here, prev), d2 = Sub(next, here);
                const double l1 = Len(d1), l2 = Len(d2);
                if (l1 > 0.0 && l2 > 0.0 && std::abs(Cross(d1, d2)) <= 1e-9 * l1 * l2 && (d1.x * d2.x + d1.y * d2.y) > 0.0)
                    continue;
                poly.push_back({ here.x, here.y, z });
            }
            if (poly.size() < 3) continue;

            // 面积太小的碎片（数值噪声）丢弃
            double a2 = 0.0;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const auto& p = poly[i]; const auto& q = poly[(i + 1) % poly.size()];
                a2 += p.x * q.y - q.x * p.y;
            }
            if (std::abs(a2) * 0.5 <= tol * diag) continue;

            result.push_back({ std::move(poly), -1 });
        }
        return result;
    }
}
