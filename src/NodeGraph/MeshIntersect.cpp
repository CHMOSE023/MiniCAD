// 网格 → 曲线与网格求交（MeshOps.h 的 N10 部分，docs/NodeGraph/PLAN.md 第 7 节）：截面、网格边、轮廓线、
// 网格 × 网格交线、折线 × 网格交点、线段连成折线。不依赖内核（网页版可用），也用作内核求交失败时的退路
#include "NodeGraph/MeshOps.h"
#include "Core/Math/MathUtils.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>

namespace MiniCAD::Nodes::MeshOps
{
    namespace
    {
        using Math::Point3;
        using Math::Vec3;

        uint64_t EdgeKey(uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a); }

        Vec3 TriCross(const Mesh& m, size_t t)
        {
            const Point3& a = m.Positions[m.Indices[t * 3]];
            return Math::Cross(m.Positions[m.Indices[t * 3 + 1]] - a, m.Positions[m.Indices[t * 3 + 2]] - a);
        }

        double Tolerance(double size) { return 1e-9 * std::max(1.0, size); }

        // 无向图（节点为点，边为线段）连成折线：先从度数不为 2 的节点出发走到下一个度数不为 2 的节点，剩下的边都在环上
        std::vector<Polyline> ChainGraph(const std::vector<Point3>& pts, const std::vector<std::pair<uint32_t, uint32_t>>& edges)
        {
            std::vector<std::vector<std::pair<uint32_t, size_t>>> adj(pts.size());   // (相邻节点, 边)
            for (size_t e = 0; e < edges.size(); ++e)
            {
                adj[edges[e].first].push_back({ edges[e].second, e });
                adj[edges[e].second].push_back({ edges[e].first, e });
            }
            std::vector<bool> used(edges.size(), false);
            std::vector<Polyline> out;
            auto walk = [&](uint32_t start, size_t e)
            {
                Polyline pl;
                pl.Points.push_back(pts[start]);
                uint32_t cur = start;
                for (;;)
                {
                    used[e] = true;
                    cur = edges[e].first == cur ? edges[e].second : edges[e].first;
                    if (cur == start)
                    {
                        pl.Closed = true;
                        break;
                    }
                    pl.Points.push_back(pts[cur]);
                    if (adj[cur].size() != 2)
                        break;
                    size_t next = SIZE_MAX;
                    for (const auto& [nb, ei] : adj[cur])
                        if (!used[ei])
                            next = ei;
                    if (next == SIZE_MAX)
                        break;
                    e = next;
                }
                out.push_back(std::move(pl));
            };
            for (uint32_t v = 0; v < adj.size(); ++v)
                if (!adj[v].empty() && adj[v].size() != 2)
                    for (const auto& [nb, ei] : adj[v])
                        if (!used[ei])
                            walk(v, ei);
            for (size_t e = 0; e < edges.size(); ++e)
                if (!used[e])
                    walk(edges[e].first, e);
            return out;
        }

        // 去掉相邻的重合点（截面经过顶点时两条边上的交点重合）；闭合的少于 3 个点、开放的少于 2 个点的丢弃
        std::vector<Polyline> Tidy(std::vector<Polyline> lines, double tol)
        {
            std::vector<Polyline> out;
            for (auto& l : lines)
            {
                std::vector<Point3> pts;
                for (const auto& p : l.Points)
                    if (pts.empty() || (p - pts.back()).Length() > tol)
                        pts.push_back(p);
                if (l.Closed && pts.size() > 1 && (pts.front() - pts.back()).Length() <= tol)
                    pts.pop_back();
                if (pts.size() >= (l.Closed ? 3u : 2u))
                    out.push_back({ std::move(pts), l.Closed });
            }
            return out;
        }

        // 边 → 用到它的三角形
        std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeFaces(const Mesh& m)
        {
            std::unordered_map<uint64_t, std::vector<uint32_t>> ef;
            for (size_t t = 0; t < m.TriangleCount(); ++t)
                for (int i = 0; i < 3; ++i)
                    ef[EdgeKey(m.Indices[t * 3 + i], m.Indices[t * 3 + (i + 1) % 3])].push_back(static_cast<uint32_t>(t));
            return ef;
        }

        std::vector<Polyline> ChainMeshEdges(const Mesh& m, const std::vector<uint64_t>& keys)
        {
            std::vector<std::pair<uint32_t, uint32_t>> edges;
            for (uint64_t k : keys)
                edges.push_back({ static_cast<uint32_t>(k >> 32), static_cast<uint32_t>(k & 0xffffffffu) });
            std::sort(edges.begin(), edges.end());   // 结果与哈希表的遍历顺序无关
            return Tidy(ChainGraph(m.Positions, edges), Tolerance(Size(m)));
        }

        // ── 包围盒树 ──
        struct Box3
        {
            Point3 Lo{ 1e300, 1e300, 1e300 }, Hi{ -1e300, -1e300, -1e300 };
            void Add(const Point3& p)
            {
                Lo = { std::min(Lo.x, p.x), std::min(Lo.y, p.y), std::min(Lo.z, p.z) };
                Hi = { std::max(Hi.x, p.x), std::max(Hi.y, p.y), std::max(Hi.z, p.z) };
            }
            void Add(const Box3& b) { Add(b.Lo); Add(b.Hi); }
            bool Overlaps(const Box3& b) const
            {
                return Lo.x <= b.Hi.x && b.Lo.x <= Hi.x && Lo.y <= b.Hi.y && b.Lo.y <= Hi.y && Lo.z <= b.Hi.z && b.Lo.z <= Hi.z;
            }
        };

        Box3 TriBox(const Mesh& m, size_t t)
        {
            Box3 b;
            for (int i = 0; i < 3; ++i)
                b.Add(m.Positions[m.Indices[t * 3 + i]]);
            return b;
        }

        class TriangleTree
        {
        public:
            explicit TriangleTree(const Mesh& m)
            {
                for (size_t t = 0; t < m.TriangleCount(); ++t)
                {
                    m_boxes.push_back(TriBox(m, t));
                    m_tris.push_back(static_cast<uint32_t>(t));
                }
                if (m_tris.empty())
                    return;
                m_nodes.emplace_back();
                Build(0, 0, m_tris.size());
            }

            // 包围盒与 box 相交的三角形
            template <class F>
            void Query(const Box3& box, F&& f) const
            {
                if (m_nodes.empty())
                    return;
                std::vector<uint32_t> stack{ 0 };
                while (!stack.empty())
                {
                    const Node& n = m_nodes[stack.back()];
                    stack.pop_back();
                    if (!n.Box.Overlaps(box))
                        continue;
                    if (n.Count > 0)
                    {
                        for (uint32_t i = n.First; i < n.First + n.Count; ++i)
                            if (m_boxes[m_tris[i]].Overlaps(box))
                                f(m_tris[i]);
                    }
                    else
                    {
                        stack.push_back(n.First);
                        stack.push_back(n.First + 1);
                    }
                }
            }

        private:
            struct Node
            {
                Box3     Box;
                uint32_t First = 0;   // 叶子：m_tris 里的起点；内部节点：左孩子（右孩子紧跟其后）
                uint32_t Count = 0;   // 叶子的三角形数，内部节点为 0
            };
            std::vector<Node>     m_nodes;
            std::vector<Box3>     m_boxes;
            std::vector<uint32_t> m_tris;

            void Build(uint32_t index, size_t first, size_t last)
            {
                Box3 box, centers;
                for (size_t i = first; i < last; ++i)
                {
                    const Box3& b = m_boxes[m_tris[i]];
                    box.Add(b);
                    centers.Add(Point3{ (b.Lo.x + b.Hi.x) / 2, (b.Lo.y + b.Hi.y) / 2, (b.Lo.z + b.Hi.z) / 2 });
                }
                m_nodes[index].Box = box;
                if (last - first <= 8)
                {
                    m_nodes[index].First = static_cast<uint32_t>(first);
                    m_nodes[index].Count = static_cast<uint32_t>(last - first);
                    return;
                }
                const Vec3 ext = centers.Hi - centers.Lo;
                const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
                auto key = [&](uint32_t t)
                {
                    const Box3& b = m_boxes[t];
                    return axis == 0 ? b.Lo.x + b.Hi.x : axis == 1 ? b.Lo.y + b.Hi.y : b.Lo.z + b.Hi.z;
                };
                const size_t mid = (first + last) / 2;
                std::nth_element(m_tris.begin() + static_cast<std::ptrdiff_t>(first), m_tris.begin() + static_cast<std::ptrdiff_t>(mid),
                                 m_tris.begin() + static_cast<std::ptrdiff_t>(last), [&](uint32_t a, uint32_t b) { return key(a) < key(b); });
                const uint32_t left = static_cast<uint32_t>(m_nodes.size());
                m_nodes.emplace_back();
                m_nodes.emplace_back();
                m_nodes[index].First = left;
                Build(left, first, mid);
                Build(left + 1, mid, last);
            }
        };

        bool LexLess(const Point3& a, const Point3& b)
        {
            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
        }

        // 三角形 T 与平面（各顶点的有向距离 d）的交线段：d ≥ 0 视为正侧。交点按边的两个端点的字典序计算，
        // 所以共边的两个三角形与同一平面得到完全相同的交点
        bool ClipToPlane(const Point3 T[3], const double d[3], Point3& p, Point3& q)
        {
            Point3 r[2];
            int k = 0;
            for (int i = 0; i < 3 && k < 2; ++i)
            {
                const int j = (i + 1) % 3;
                if ((d[i] >= 0) == (d[j] >= 0))
                    continue;
                const int a = LexLess(T[i], T[j]) ? i : j, b = a == i ? j : i;
                r[k++] = T[a] + (T[b] - T[a]) * (d[a] / (d[a] - d[b]));
            }
            if (k < 2)
                return false;
            p = r[0];
            q = r[1];
            return true;
        }

        Vec3 AsVec(const Point3& p) { return { p.x, p.y, p.z }; }

        // 两个三角形的交线段（共面、只接触一个点时返回 false）
        bool TriTri(const Point3 A[3], const Point3 B[3], Point3& s0, Point3& s1)
        {
            const Vec3 nA = Math::Cross(A[1] - A[0], A[2] - A[0]), nB = Math::Cross(B[1] - B[0], B[2] - B[0]);
            if (nA.LengthSq() == 0 || nB.LengthSq() == 0)
                return false;
            double dA[3], dB[3];
            for (int i = 0; i < 3; ++i)
            {
                dA[i] = Math::Dot(A[i] - B[0], nB);
                dB[i] = Math::Dot(B[i] - A[0], nA);
            }
            Point3 a0, a1, b0, b1;
            if (!ClipToPlane(A, dA, a0, a1) || !ClipToPlane(B, dB, b0, b1))
                return false;   // 某个三角形全在另一个的平面一侧（共面也在这里排除）
            const Vec3 L = Math::Cross(nA, nB);
            if (L.LengthSq() <= 1e-24 * nA.LengthSq() * nB.LengthSq())
                return false;
            // 两段都在两平面的交线上：按交线方向的投影取重叠部分，端点直接用原来的交点
            struct End
            {
                double t;
                Point3 p;
            };
            End ea[2] = { { Math::Dot(AsVec(a0), L), a0 }, { Math::Dot(AsVec(a1), L), a1 } };
            End eb[2] = { { Math::Dot(AsVec(b0), L), b0 }, { Math::Dot(AsVec(b1), L), b1 } };
            if (ea[0].t > ea[1].t)
                std::swap(ea[0], ea[1]);
            if (eb[0].t > eb[1].t)
                std::swap(eb[0], eb[1]);
            const End& lo = ea[0].t >= eb[0].t ? ea[0] : eb[0];
            const End& hi = ea[1].t <= eb[1].t ? ea[1] : eb[1];
            if (hi.t <= lo.t)
                return false;
            s0 = lo.p;
            s1 = hi.p;
            return true;
        }

        // 线段 p + s·dir（s ∈ [0, 1]）与三角形的交点参数（Möller–Trumbore，含边界；线段与三角形共面时不算）
        bool SegTri(const Point3& p, const Vec3& dir, const Point3& a, const Point3& b, const Point3& c, double& s)
        {
            const Vec3 e1 = b - a, e2 = c - a;
            const Vec3 h = Math::Cross(dir, e2);
            const double det = Math::Dot(e1, h);
            if (std::abs(det) <= 1e-14 * dir.Length() * e1.Length() * e2.Length())
                return false;
            const double inv = 1.0 / det;
            const Vec3 sv = p - a;
            const double u = Math::Dot(sv, h) * inv;
            constexpr double eps = 1e-12;
            if (u < -eps || u > 1 + eps)
                return false;
            const Vec3 qv = Math::Cross(sv, e1);
            const double v = Math::Dot(dir, qv) * inv;
            if (v < -eps || u + v > 1 + eps)
                return false;
            s = Math::Dot(e2, qv) * inv;
            return s >= -eps && s <= 1 + eps;
        }
    }

    Mesh FromBodyFine(Solid::ISolidKernel& kernel, const Solid::BodyPtr& body)
    {
        if (!body)
            return {};
        const AABB box = kernel.Bounds(body);
        Solid::TessParams p;
        p.CurveAngleDeg = p.SurfaceAngleDeg = 5.0;
        if (box.Min.x <= box.Max.x)
            p.ChordHeight = (box.Max - box.Min).Length() * 1e-3;
        return FromBody(kernel, body, p);
    }

    std::vector<Polyline> ChainSegments(const std::vector<std::pair<Point3, Point3>>& segments, double tol)
    {
        tol = std::max(tol, 1e-300);
        const double cell = tol * 2;
        std::map<std::array<int64_t, 3>, std::vector<uint32_t>> grid;
        std::vector<Point3> pts;
        auto cellOf = [&](const Point3& p)
        {
            return std::array<int64_t, 3>{ static_cast<int64_t>(std::floor(p.x / cell)), static_cast<int64_t>(std::floor(p.y / cell)),
                                           static_cast<int64_t>(std::floor(p.z / cell)) };
        };
        // 端点 → 点编号：相邻格子里距离在容差内的已有点视为同一点
        auto pointId = [&](const Point3& p)
        {
            const auto c = cellOf(p);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz)
                        if (auto it = grid.find({ c[0] + dx, c[1] + dy, c[2] + dz }); it != grid.end())
                            for (uint32_t id : it->second)
                                if ((pts[id] - p).Length() <= tol)
                                    return id;
            const uint32_t id = static_cast<uint32_t>(pts.size());
            pts.push_back(p);
            grid[c].push_back(id);
            return id;
        };
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        std::set<std::pair<uint32_t, uint32_t>> seen;
        for (const auto& [p, q] : segments)
        {
            const uint32_t a = pointId(p), b = pointId(q);
            if (a != b && seen.insert({ std::min(a, b), std::max(a, b) }).second)
                edges.push_back({ a, b });
        }
        return Tidy(ChainGraph(pts, edges), tol);
    }

    std::vector<Polyline> Section(const Mesh& m, const Point3& origin, const Vec3& normal)
    {
        if (normal.LengthSq() == 0 || m.TriangleCount() == 0)
            return {};
        const Vec3 n = normal.Normalized();
        std::vector<double> d(m.Positions.size());
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = Math::Dot(m.Positions[i] - origin, n);
        // 截交点按网格边编号：共边的两个三角形连到同一个点上
        std::unordered_map<uint64_t, uint32_t> nodeOfEdge;
        std::vector<Point3> pts;
        auto node = [&](uint32_t a, uint32_t b)
        {
            const uint64_t key = EdgeKey(a, b);
            if (auto it = nodeOfEdge.find(key); it != nodeOfEdge.end())
                return it->second;
            if (a > b)
                std::swap(a, b);
            const uint32_t id = static_cast<uint32_t>(pts.size());
            pts.push_back(m.Positions[a] + (m.Positions[b] - m.Positions[a]) * (d[a] / (d[a] - d[b])));
            nodeOfEdge.emplace(key, id);
            return id;
        };
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            uint32_t ends[2] = {};
            int k = 0;
            for (int i = 0; i < 3; ++i)
            {
                const uint32_t a = m.Indices[t * 3 + i], b = m.Indices[t * 3 + (i + 1) % 3];
                if ((d[a] >= 0) != (d[b] >= 0) && k < 2)
                    ends[k++] = node(a, b);
            }
            if (k == 2)
                edges.push_back({ ends[0], ends[1] });
        }
        return Tidy(ChainGraph(pts, edges), Tolerance(Size(m)));
    }

    std::vector<Polyline> Edges(const Mesh& m, EdgeKind kind, double angleDeg)
    {
        const double cosLimit = std::cos(angleDeg * std::numbers::pi / 180.0);
        std::vector<uint64_t> keys;
        for (const auto& [key, faces] : EdgeFaces(m))
        {
            bool take = kind == EdgeKind::All || faces.size() != 2;   // 边界边（以及非流形的边）总是要
            if (!take && kind == EdgeKind::Crease)
                take = Math::Dot(FaceNormal(m, faces[0]), FaceNormal(m, faces[1])) < cosLimit;
            if (take)
                keys.push_back(key);
        }
        return ChainMeshEdges(m, keys);
    }

    std::vector<Polyline> Outline(const Mesh& m, const Vec3& direction)
    {
        std::vector<uint64_t> keys;
        for (const auto& [key, faces] : EdgeFaces(m))
        {
            bool take = faces.size() != 2;
            if (!take)
                take = (Math::Dot(TriCross(m, faces[0]), direction) < 0) != (Math::Dot(TriCross(m, faces[1]), direction) < 0);
            if (take)
                keys.push_back(key);
        }
        return ChainMeshEdges(m, keys);
    }

    std::vector<Polyline> Intersect(const Mesh& a, const Mesh& b)
    {
        if (a.TriangleCount() == 0 || b.TriangleCount() == 0)
            return {};
        const TriangleTree tree(b);
        std::vector<std::pair<Point3, Point3>> segs;
        for (size_t t = 0; t < a.TriangleCount(); ++t)
        {
            const Point3 A[3] = { a.Positions[a.Indices[t * 3]], a.Positions[a.Indices[t * 3 + 1]], a.Positions[a.Indices[t * 3 + 2]] };
            tree.Query(TriBox(a, t), [&](uint32_t u)
            {
                const Point3 B[3] = { b.Positions[b.Indices[u * 3]], b.Positions[b.Indices[u * 3 + 1]], b.Positions[b.Indices[u * 3 + 2]] };
                Point3 s0, s1;
                if (TriTri(A, B, s0, s1))
                    segs.push_back({ s0, s1 });
            });
        }
        return ChainSegments(segs, Tolerance(std::max(Size(a), Size(b))));
    }

    std::vector<Point3> IntersectPolyline(const Mesh& m, const std::vector<Point3>& points)
    {
        std::vector<Point3> out;
        if (m.TriangleCount() == 0 || points.size() < 2)
            return out;
        const TriangleTree tree(m);
        std::vector<std::pair<double, Point3>> hits;   // (沿折线的参数, 点)
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const Point3& p = points[i];
            const Vec3 dir = points[i + 1] - p;
            if (dir.LengthSq() == 0)
                continue;
            Box3 box;
            box.Add(p);
            box.Add(points[i + 1]);
            tree.Query(box, [&](uint32_t t)
            {
                double s = 0;
                if (SegTri(p, dir, m.Positions[m.Indices[t * 3]], m.Positions[m.Indices[t * 3 + 1]], m.Positions[m.Indices[t * 3 + 2]], s))
                {
                    s = std::clamp(s, 0.0, 1.0);
                    hits.push_back({ static_cast<double>(i) + s, p + dir * s });
                }
            });
        }
        std::sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        // 交在共用的边 / 顶点上时几个三角形各报一次，交在折线顶点上时相邻两段各报一次：同一点只留一个
        const double tol = 1e-7 * std::max(1.0, Size(m));
        for (const auto& [s, p] : hits)
            if (std::none_of(out.begin(), out.end(), [&](const Point3& q) { return (q - p).Length() <= tol; }))
                out.push_back(p);
        return out;
    }
}
