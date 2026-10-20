#include "NodeGraph/MeshOps.h"
#include "Core/Math/MathUtils.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <queue>
#include <set>
#include <unordered_map>

namespace MiniCAD::Nodes::MeshOps
{
    namespace
    {
        using Math::Point3;
        using Math::Vec3;

        constexpr double kPi = std::numbers::pi;

        Vec3 ToVec(const Point3& p) { return { p.x, p.y, p.z }; }

        Vec3 TriCross(const Mesh& m, size_t t)   // 长度 = 面积的两倍
        {
            const Point3& a = m.Positions[m.Indices[t * 3]];
            return Math::Cross(m.Positions[m.Indices[t * 3 + 1]] - a, m.Positions[m.Indices[t * 3 + 2]] - a);
        }

        uint64_t EdgeKey(uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a); }

        // 只保留用到的顶点，按出现顺序重新编号
        Mesh Compact(const std::vector<Point3>& positions, const std::vector<uint32_t>& indices)
        {
            Mesh out;
            std::vector<uint32_t> remap(positions.size(), UINT32_MAX);
            for (uint32_t i : indices)
            {
                if (remap[i] == UINT32_MAX)
                {
                    remap[i] = static_cast<uint32_t>(out.Positions.size());
                    out.Positions.push_back(positions[i]);
                }
                out.Indices.push_back(remap[i]);
            }
            ComputeNormals(out);
            return out;
        }

        // 每个顶点的相邻顶点，以及只被一个三角形用到的边（边界）
        struct Topology
        {
            std::vector<std::vector<uint32_t>> Neighbors;
            std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeOpposite;   // 边 → 各三角形里对面的顶点
            std::vector<bool> OnBoundary;
        };

        Topology BuildTopology(const Mesh& m)
        {
            Topology t;
            t.Neighbors.resize(m.Positions.size());
            t.OnBoundary.assign(m.Positions.size(), false);
            for (size_t f = 0; f < m.TriangleCount(); ++f)
                for (int i = 0; i < 3; ++i)
                {
                    const uint32_t a = m.Indices[f * 3 + i], b = m.Indices[f * 3 + (i + 1) % 3], c = m.Indices[f * 3 + (i + 2) % 3];
                    t.EdgeOpposite[EdgeKey(a, b)].push_back(c);
                }
            for (const auto& [key, opp] : t.EdgeOpposite)
            {
                const uint32_t a = static_cast<uint32_t>(key >> 32), b = static_cast<uint32_t>(key & 0xffffffffu);
                t.Neighbors[a].push_back(b);
                t.Neighbors[b].push_back(a);
                if (opp.size() == 1)
                    t.OnBoundary[a] = t.OnBoundary[b] = true;
            }
            return t;
        }

        // 2D 工具（平面坐标）
        struct P2
        {
            double x = 0, y = 0;
        };
        double Cross2(const P2& o, const P2& a, const P2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

        Point3 ClosestOnTriangle(const Point3& p, const Point3& a, const Point3& b, const Point3& c)
        {
            // Ericson, Real-Time Collision Detection 5.1.5
            const Vec3 ab = b - a, ac = c - a, ap = p - a;
            const double d1 = Math::Dot(ab, ap), d2 = Math::Dot(ac, ap);
            if (d1 <= 0 && d2 <= 0)
                return a;
            const Vec3 bp = p - b;
            const double d3 = Math::Dot(ab, bp), d4 = Math::Dot(ac, bp);
            if (d3 >= 0 && d4 <= d3)
                return b;
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0 && d1 >= 0 && d3 <= 0)
                return a + ab * (d1 / (d1 - d3));
            const Vec3 cp = p - c;
            const double d5 = Math::Dot(ab, cp), d6 = Math::Dot(ac, cp);
            if (d6 >= 0 && d5 <= d6)
                return c;
            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0 && d2 >= 0 && d6 <= 0)
                return a + ac * (d2 / (d2 - d6));
            const double va = d3 * d6 - d5 * d4;
            if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
            const double denom = 1.0 / (va + vb + vc);
            return a + ab * (vb * denom) + ac * (vc * denom);
        }
    }

    void ComputeNormals(Mesh& m)
    {
        m.Normals.assign(m.Positions.size(), Vec3{});
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const Vec3 n = TriCross(m, t);
            for (int i = 0; i < 3; ++i)
                m.Normals[m.Indices[t * 3 + i]] += n;
        }
        for (auto& n : m.Normals)
            n = n.LengthSq() > 0 ? n.Normalized() : Vec3{ 0, 0, 1 };
    }

    double Size(const Mesh& m)
    {
        if (m.Positions.empty())
            return 0.0;
        Point3 lo = m.Positions[0], hi = m.Positions[0];
        for (const auto& p : m.Positions)
        {
            lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
            hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
        }
        return (hi - lo).Length();
    }

    // ── 创建 ─────────────────────────────────────────────────────────

    Mesh Box(const Point3& c, const Vec3& size, int n)
    {
        n = std::max(1, n);
        const double hx = size.x / 2, hy = size.y / 2, hz = size.z / 2;
        struct Face { Point3 O; Vec3 U, V; };
        const Face faces[6] = {
            { { c.x + hx, c.y - hy, c.z - hz }, { 0, size.y, 0 }, { 0, 0, size.z } },   // +X
            { { c.x - hx, c.y - hy, c.z - hz }, { 0, 0, size.z }, { 0, size.y, 0 } },   // −X
            { { c.x - hx, c.y + hy, c.z - hz }, { 0, 0, size.z }, { size.x, 0, 0 } },   // +Y
            { { c.x - hx, c.y - hy, c.z - hz }, { size.x, 0, 0 }, { 0, 0, size.z } },   // −Y
            { { c.x - hx, c.y - hy, c.z + hz }, { size.x, 0, 0 }, { 0, size.y, 0 } },   // +Z
            { { c.x - hx, c.y - hy, c.z - hz }, { 0, size.y, 0 }, { size.x, 0, 0 } },   // −Z
        };
        Mesh m;
        for (const Face& f : faces)
        {
            const uint32_t base = static_cast<uint32_t>(m.Positions.size());
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i)
                    m.Positions.push_back(f.O + f.U * (double(i) / n) + f.V * (double(j) / n));
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t a = base + j * (n + 1) + i, b = a + 1, d = a + (n + 1), cc = d + 1;
                    m.Indices.insert(m.Indices.end(), { a, b, cc, a, cc, d });
                }
        }
        return Weld(m, 0.0);
    }

    Mesh Sphere(const Point3& c, double r, int u, int v)
    {
        u = std::max(3, u);
        v = std::max(2, v);
        Mesh m;
        m.Positions.push_back({ c.x, c.y, c.z + r });
        for (int k = 1; k < v; ++k)
        {
            const double phi = kPi * k / v;
            for (int i = 0; i < u; ++i)
            {
                const double th = 2 * kPi * i / u;
                m.Positions.push_back({ c.x + r * std::sin(phi) * std::cos(th), c.y + r * std::sin(phi) * std::sin(th), c.z + r * std::cos(phi) });
            }
        }
        m.Positions.push_back({ c.x, c.y, c.z - r });
        const uint32_t bottom = static_cast<uint32_t>(m.Positions.size() - 1);
        auto ring = [&](int k, int i) { return static_cast<uint32_t>(1 + (k - 1) * u + (i % u)); };
        for (int i = 0; i < u; ++i)
            m.Indices.insert(m.Indices.end(), { 0u, ring(1, i), ring(1, i + 1) });
        for (int k = 1; k + 1 < v; ++k)
            for (int i = 0; i < u; ++i)
            {
                const uint32_t a = ring(k, i), b = ring(k + 1, i), cc = ring(k + 1, i + 1), d = ring(k, i + 1);
                m.Indices.insert(m.Indices.end(), { a, b, cc, a, cc, d });
            }
        for (int i = 0; i < u; ++i)
            m.Indices.insert(m.Indices.end(), { bottom, ring(v - 1, i + 1), ring(v - 1, i) });
        ComputeNormals(m);
        return m;
    }

    Mesh Grid(const Plane& plane, double width, double height, int nx, int ny)
    {
        nx = std::max(1, nx);
        ny = std::max(1, ny);
        Mesh m;
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i)
            {
                const Vec3 p = plane.PointAt(-width / 2 + width * i / nx, -height / 2 + height * j / ny);
                m.Positions.push_back({ p.x, p.y, p.z });
            }
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                const uint32_t a = j * (nx + 1) + i, b = a + 1, d = a + (nx + 1), cc = d + 1;
                m.Indices.insert(m.Indices.end(), { a, b, cc, a, cc, d });
            }
        ComputeNormals(m);
        return m;
    }

    std::optional<Mesh> Construct(const std::vector<Point3>& vertices, const std::vector<int64_t>& indices, int faceSize, std::string& error)
    {
        if (faceSize != 3 && faceSize != 4)
        {
            error = "每个面的顶点数应为 3 或 4";
            return std::nullopt;
        }
        if (indices.size() % faceSize != 0)
        {
            error = "索引个数应是 " + std::to_string(faceSize) + " 的倍数";
            return std::nullopt;
        }
        Mesh m;
        m.Positions = vertices;
        for (size_t f = 0; f < indices.size(); f += faceSize)
        {
            uint32_t idx[4];
            for (int i = 0; i < faceSize; ++i)
            {
                const int64_t k = indices[f + i];
                if (k < 0 || k >= static_cast<int64_t>(vertices.size()))
                {
                    error = "第 " + std::to_string(f / faceSize + 1) + " 个面的索引 " + std::to_string(k) + " 超出顶点范围";
                    return std::nullopt;
                }
                idx[i] = static_cast<uint32_t>(k);
            }
            m.Indices.insert(m.Indices.end(), { idx[0], idx[1], idx[2] });
            if (faceSize == 4)
                m.Indices.insert(m.Indices.end(), { idx[0], idx[2], idx[3] });
        }
        ComputeNormals(m);
        return m;
    }

    Mesh Delaunay(const std::vector<Point3>& input, const Plane& plane)
    {
        // 平面坐标，去掉重合的点
        std::vector<Point3> pts;
        std::vector<P2> q;
        double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
        for (const auto& p : input)
        {
            const Vec3 c = plane.Coordinates(ToVec(p));
            lo[0] = std::min(lo[0], c.x), lo[1] = std::min(lo[1], c.y), hi[0] = std::max(hi[0], c.x), hi[1] = std::max(hi[1], c.y);
            q.push_back({ c.x, c.y });
            pts.push_back(p);
        }
        Mesh out;
        if (q.size() < 3)
            return out;
        const double extent = std::max({ hi[0] - lo[0], hi[1] - lo[1], 1e-12 });
        const double eps = extent * 1e-12;
        {
            std::vector<P2> uq;
            std::vector<Point3> up;
            for (size_t i = 0; i < q.size(); ++i)
            {
                bool dup = false;
                for (const auto& u : uq)
                    dup = dup || std::hypot(u.x - q[i].x, u.y - q[i].y) <= eps * 1e3;
                if (!dup)
                    uq.push_back(q[i]), up.push_back(pts[i]);
            }
            q = std::move(uq);
            pts = std::move(up);
        }
        const size_t n = q.size();
        // 超级三角形
        const double cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2, M = extent * 1e4;
        q.push_back({ cx - 2 * M, cy - M });
        q.push_back({ cx + 2 * M, cy - M });
        q.push_back({ cx, cy + 2 * M });

        struct Tri
        {
            uint32_t v[3];
            double cx, cy, r2;
        };
        auto make = [&](uint32_t a, uint32_t b, uint32_t c)
        {
            if (Cross2(q[a], q[b], q[c]) < 0)
                std::swap(b, c);
            const P2 &A = q[a], &B = q[b], &C = q[c];
            const double d = 2 * (A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y));
            Tri t{ { a, b, c }, 0, 0, 1e300 };
            if (std::abs(d) > 0)
            {
                const double a2 = A.x * A.x + A.y * A.y, b2 = B.x * B.x + B.y * B.y, c2 = C.x * C.x + C.y * C.y;
                t.cx = (a2 * (B.y - C.y) + b2 * (C.y - A.y) + c2 * (A.y - B.y)) / d;
                t.cy = (a2 * (C.x - B.x) + b2 * (A.x - C.x) + c2 * (B.x - A.x)) / d;
                t.r2 = (A.x - t.cx) * (A.x - t.cx) + (A.y - t.cy) * (A.y - t.cy);
            }
            return t;
        };
        std::vector<Tri> tris{ make(static_cast<uint32_t>(n), static_cast<uint32_t>(n + 1), static_cast<uint32_t>(n + 2)) };
        for (uint32_t i = 0; i < n; ++i)
        {
            const P2& p = q[i];
            std::vector<Tri> keep;
            std::map<std::pair<uint32_t, uint32_t>, int> edges;   // 坏三角形的边（有向）
            for (const Tri& t : tris)
            {
                const double d2 = (p.x - t.cx) * (p.x - t.cx) + (p.y - t.cy) * (p.y - t.cy);
                if (d2 < t.r2 * (1 - 1e-12))
                    for (int k = 0; k < 3; ++k)
                        edges[{ t.v[k], t.v[(k + 1) % 3] }]++;
                else
                    keep.push_back(t);
            }
            // 空洞的边界：只出现一次（反方向没有出现）的边
            for (const auto& [e, cnt] : edges)
                if (!edges.count({ e.second, e.first }))
                    keep.push_back(make(e.first, e.second, i));
            tris = std::move(keep);
        }
        out.Positions = pts;
        for (const Tri& t : tris)
        {
            if (t.v[0] >= n || t.v[1] >= n || t.v[2] >= n)
                continue;
            // 平面坐标里逆时针 → 正面朝平面法向
            out.Indices.insert(out.Indices.end(), { t.v[0], t.v[1], t.v[2] });
        }
        ComputeNormals(out);
        return out;
    }

    Mesh Polygon(const std::vector<Point3>& outer, const std::vector<std::vector<Point3>>& holes, const Plane& plane)
    {
        Mesh out;
        std::vector<P2> q;
        auto area = [&](const std::vector<uint32_t>& ring)
        {
            double s = 0;
            for (size_t i = 0; i < ring.size(); ++i)
            {
                const P2 &a = q[ring[i]], &b = q[ring[(i + 1) % ring.size()]];
                s += a.x * b.y - b.x * a.y;
            }
            return s / 2;
        };
        auto addRing = [&](const std::vector<Point3>& pts)
        {
            std::vector<uint32_t> ring;
            for (const auto& p : pts)
            {
                const Vec3 c = plane.Coordinates(ToVec(p));
                const P2 np{ c.x, c.y };
                if (!ring.empty() && std::hypot(np.x - q[ring.back()].x, np.y - q[ring.back()].y) < 1e-12)
                    continue;
                ring.push_back(static_cast<uint32_t>(out.Positions.size()));
                out.Positions.push_back(p);
                q.push_back(np);
            }
            if (ring.size() > 1 && std::hypot(q[ring.front()].x - q[ring.back()].x, q[ring.front()].y - q[ring.back()].y) < 1e-12)
                ring.pop_back();
            return ring;
        };
        std::vector<uint32_t> poly = addRing(outer);
        if (poly.size() < 3)
            return {};
        if (area(poly) < 0)
            std::reverse(poly.begin(), poly.end());

        // 洞：顺时针，按最右顶点从右到左依次桥接到外圈
        std::vector<std::vector<uint32_t>> hs;
        for (const auto& h : holes)
        {
            auto ring = addRing(h);
            if (ring.size() < 3)
                continue;
            if (area(ring) > 0)
                std::reverse(ring.begin(), ring.end());
            hs.push_back(std::move(ring));
        }
        auto maxX = [&](const std::vector<uint32_t>& r) { return *std::max_element(r.begin(), r.end(), [&](uint32_t a, uint32_t b) { return q[a].x < q[b].x; }); };
        std::sort(hs.begin(), hs.end(), [&](const auto& a, const auto& b) { return q[maxX(a)].x > q[maxX(b)].x; });
        for (const auto& h : hs)
        {
            const uint32_t mi = maxX(h);
            const P2 M = q[mi];
            // 从 M 向 +X 的射线与当前多边形的边的最近交点
            double bestX = 1e300;
            size_t bestEdge = SIZE_MAX;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const P2 &a = q[poly[i]], &b = q[poly[(i + 1) % poly.size()]];
                if ((a.y > M.y) == (b.y > M.y))
                    continue;
                const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x >= M.x && x < bestX)
                    bestX = x, bestEdge = i;
            }
            if (bestEdge == SIZE_MAX)
                return {};
            size_t pick = q[poly[bestEdge]].x > q[poly[(bestEdge + 1) % poly.size()]].x ? bestEdge : (bestEdge + 1) % poly.size();
            // 三角形 M、交点、候选点内有别的顶点时，改取与射线夹角最小的那个
            const P2 I{ bestX, M.y }, Pc = q[poly[pick]];
            double bestAngle = 1e300;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const P2& v = q[poly[i]];
                if (i == pick || v.x < M.x)
                    continue;
                const double s1 = Cross2(M, I, v), s2 = Cross2(I, Pc, v), s3 = Cross2(Pc, M, v);
                const bool inside = (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
                if (!inside)
                    continue;
                const double ang = std::abs(std::atan2(v.y - M.y, v.x - M.x));
                if (ang < bestAngle)
                    bestAngle = ang, pick = i;
            }
            // 拼接：…, P, M, 洞的其余顶点, M, P, …
            std::vector<uint32_t> merged(poly.begin(), poly.begin() + pick + 1);
            const size_t start = std::find(h.begin(), h.end(), mi) - h.begin();
            for (size_t k = 0; k <= h.size(); ++k)
                merged.push_back(h[(start + k) % h.size()]);
            merged.push_back(poly[pick]);
            merged.insert(merged.end(), poly.begin() + pick + 1, poly.end());
            poly = std::move(merged);
        }

        // 耳切
        std::vector<uint32_t> ring = poly;
        auto same = [&](uint32_t a, uint32_t b) { return a == b || (std::abs(q[a].x - q[b].x) < 1e-12 && std::abs(q[a].y - q[b].y) < 1e-12); };
        while (ring.size() > 3)   // 每一轮要么切掉一个耳朵，要么去掉一个退化顶点，要么失败返回
        {
            bool clipped = false;
            for (size_t i = 0; i < ring.size() && !clipped; ++i)
            {
                const uint32_t a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i], c = ring[(i + 1) % ring.size()];
                if (Cross2(q[a], q[b], q[c]) <= 1e-15)
                    continue;   // 凹点或共线
                bool ear = true;
                for (uint32_t v : ring)
                {
                    if (same(v, a) || same(v, b) || same(v, c))
                        continue;
                    if (Cross2(q[a], q[b], q[v]) >= 0 && Cross2(q[b], q[c], q[v]) >= 0 && Cross2(q[c], q[a], q[v]) >= 0)
                    {
                        ear = false;
                        break;
                    }
                }
                if (!ear)
                    continue;
                out.Indices.insert(out.Indices.end(), { a, b, c });
                ring.erase(ring.begin() + i);
                clipped = true;
            }
            if (!clipped)
            {
                // 剩下的是退化的（共线）顶点：去掉一个继续
                for (size_t i = 0; i < ring.size() && !clipped; ++i)
                {
                    const uint32_t a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i], c = ring[(i + 1) % ring.size()];
                    if (std::abs(Cross2(q[a], q[b], q[c])) <= 1e-15)
                        ring.erase(ring.begin() + i), clipped = true;
                }
                if (!clipped)
                    return {};
            }
        }
        if (ring.size() == 3 && Cross2(q[ring[0]], q[ring[1]], q[ring[2]]) > 1e-15)
            out.Indices.insert(out.Indices.end(), { ring[0], ring[1], ring[2] });
        ComputeNormals(out);
        return out;
    }

    Mesh FromBody(Solid::ISolidKernel& kernel, const Solid::BodyPtr& body, const Solid::TessParams& params)
    {
        if (!body)
            return {};
        const Mesh raw = kernel.Tessellate(body, params);
        if (raw.TriangleCount() == 0)
            return {};
        return Weld(raw, 0.0);
    }

    // ── 修改 ─────────────────────────────────────────────────────────

    Mesh Weld(const Mesh& m, double tol)
    {
        if (!(tol > 0))
            tol = std::max(Size(m), 1.0) * 1e-9;
        // 空间哈希：格子边长 = 容差，查相邻 27 格
        std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
        auto cell = [&](double v) { return static_cast<int64_t>(std::floor(v / tol)); };
        auto key = [](int64_t x, int64_t y, int64_t z) { return (uint64_t(x) * 73856093u) ^ (uint64_t(y) * 19349663u) ^ (uint64_t(z) * 83492791u); };
        std::vector<Point3> pos;
        std::vector<uint32_t> remap(m.Positions.size());
        for (size_t i = 0; i < m.Positions.size(); ++i)
        {
            const Point3& p = m.Positions[i];
            const int64_t cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);
            uint32_t found = UINT32_MAX;
            for (int dx = -1; dx <= 1 && found == UINT32_MAX; ++dx)
                for (int dy = -1; dy <= 1 && found == UINT32_MAX; ++dy)
                    for (int dz = -1; dz <= 1 && found == UINT32_MAX; ++dz)
                    {
                        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                        if (it == grid.end())
                            continue;
                        for (uint32_t j : it->second)
                            if ((pos[j] - p).Length() <= tol)
                            {
                                found = j;
                                break;
                            }
                    }
            if (found == UINT32_MAX)
            {
                found = static_cast<uint32_t>(pos.size());
                pos.push_back(p);
                grid[key(cx, cy, cz)].push_back(found);
            }
            remap[i] = found;
        }
        std::vector<uint32_t> idx;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const uint32_t a = remap[m.Indices[t * 3]], b = remap[m.Indices[t * 3 + 1]], c = remap[m.Indices[t * 3 + 2]];
            if (a != b && b != c && c != a)
                idx.insert(idx.end(), { a, b, c });
        }
        return Compact(pos, idx);
    }

    Mesh Flip(const Mesh& m)
    {
        Mesh out = m;
        for (size_t t = 0; t < out.TriangleCount(); ++t)
            std::swap(out.Indices[t * 3 + 1], out.Indices[t * 3 + 2]);
        for (auto& n : out.Normals)
            n = n * -1.0;
        return out;
    }

    Mesh Join(const std::vector<const Mesh*>& meshes)
    {
        Mesh out;
        for (const Mesh* m : meshes)
        {
            const uint32_t base = static_cast<uint32_t>(out.Positions.size());
            out.Positions.insert(out.Positions.end(), m->Positions.begin(), m->Positions.end());
            for (uint32_t i : m->Indices)
                out.Indices.push_back(base + i);
        }
        ComputeNormals(out);
        return out;
    }

    std::vector<Mesh> Split(const Mesh& m)
    {
        std::vector<uint32_t> parent(m.Positions.size());
        for (uint32_t i = 0; i < parent.size(); ++i)
            parent[i] = i;
        auto find = [&](uint32_t x)
        {
            while (parent[x] != x)
                x = parent[x] = parent[parent[x]];
            return x;
        };
        for (size_t t = 0; t < m.TriangleCount(); ++t)
            for (int i = 1; i < 3; ++i)
                parent[find(m.Indices[t * 3 + i])] = find(m.Indices[t * 3]);
        std::map<uint32_t, std::vector<uint32_t>> groups;   // 按首次出现的顺序：键为根，值为三角形的索引
        std::vector<uint32_t> order;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const uint32_t r = find(m.Indices[t * 3]);
            auto [it, inserted] = groups.try_emplace(r);
            if (inserted)
                order.push_back(r);
            it->second.insert(it->second.end(), { m.Indices[t * 3], m.Indices[t * 3 + 1], m.Indices[t * 3 + 2] });
        }
        std::vector<Mesh> out;
        for (uint32_t r : order)
            out.push_back(Compact(m.Positions, groups[r]));
        return out;
    }

    Mesh Subdivide(const Mesh& m)
    {
        const Topology topo = BuildTopology(m);
        const size_t nv = m.Positions.size();
        Mesh out;
        out.Positions.resize(nv);
        // 原顶点的新位置
        for (size_t v = 0; v < nv; ++v)
        {
            const Point3& p = m.Positions[v];
            const auto& nb = topo.Neighbors[v];
            if (topo.OnBoundary[v])
            {
                std::vector<uint32_t> bn;
                for (uint32_t w : nb)
                    if (topo.EdgeOpposite.at(EdgeKey(static_cast<uint32_t>(v), w)).size() == 1)
                        bn.push_back(w);
                out.Positions[v] = bn.size() == 2 ? p + ((m.Positions[bn[0]] - p) + (m.Positions[bn[1]] - p)) * 0.125 : p;
                continue;
            }
            if (nb.empty())
            {
                out.Positions[v] = p;
                continue;
            }
            const double n = static_cast<double>(nb.size());
            const double beta = nb.size() == 3 ? 3.0 / 16.0 : 3.0 / (8.0 * n);
            Vec3 sum;
            for (uint32_t w : nb)
                sum += ToVec(m.Positions[w]);
            out.Positions[v] = Point3{ 0, 0, 0 } + ToVec(p) * (1 - n * beta) + sum * beta;
        }
        // 边上的新顶点
        std::unordered_map<uint64_t, uint32_t> edgeVertex;
        for (const auto& [key, opp] : topo.EdgeOpposite)
        {
            const Point3& a = m.Positions[key >> 32];
            const Point3& b = m.Positions[key & 0xffffffffu];
            Point3 p = Point3{ 0, 0, 0 } + (ToVec(a) + ToVec(b)) * 0.5;
            if (opp.size() == 2)
                p = Point3{ 0, 0, 0 } + (ToVec(a) + ToVec(b)) * 0.375 + (ToVec(m.Positions[opp[0]]) + ToVec(m.Positions[opp[1]])) * 0.125;
            edgeVertex[key] = static_cast<uint32_t>(out.Positions.size());
            out.Positions.push_back(p);
        }
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const uint32_t a = m.Indices[t * 3], b = m.Indices[t * 3 + 1], c = m.Indices[t * 3 + 2];
            const uint32_t ab = edgeVertex[EdgeKey(a, b)], bc = edgeVertex[EdgeKey(b, c)], ca = edgeVertex[EdgeKey(c, a)];
            out.Indices.insert(out.Indices.end(), { a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca });
        }
        ComputeNormals(out);
        return out;
    }

    Mesh Smooth(const Mesh& m, int iterations, double factor, bool lockBoundary)
    {
        const Topology topo = BuildTopology(m);
        Mesh out = m;
        for (int it = 0; it < iterations; ++it)
        {
            std::vector<Point3> next = out.Positions;
            for (size_t v = 0; v < next.size(); ++v)
            {
                const auto& nb = topo.Neighbors[v];
                if (nb.empty() || (lockBoundary && topo.OnBoundary[v]))
                    continue;
                Vec3 avg;
                for (uint32_t w : nb)
                    avg += ToVec(out.Positions[w]);
                avg = avg / static_cast<double>(nb.size());
                next[v] = out.Positions[v] + (avg - ToVec(out.Positions[v])) * factor;
            }
            out.Positions = std::move(next);
        }
        ComputeNormals(out);
        return out;
    }

    Mesh Reduce(const Mesh& m, size_t target)
    {
        using Quadric = std::array<double, 10>;   // 对称 4×4：a² ab ac ad b² bc bd c² cd d²
        auto planeQ = [](const Vec3& n, double d, double w)
        {
            return Quadric{ w * n.x * n.x, w * n.x * n.y, w * n.x * n.z, w * n.x * d, w * n.y * n.y, w * n.y * n.z, w * n.y * d,
                            w * n.z * n.z, w * n.z * d, w * d * d };
        };
        auto addQ = [](Quadric& a, const Quadric& b) { for (int i = 0; i < 10; ++i) a[i] += b[i]; };
        auto evalQ = [](const Quadric& q, const Point3& p)
        {
            const double x = p.x, y = p.y, z = p.z;
            return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y +
                   q[7] * z * z + 2 * q[8] * z + q[9];
        };

        std::vector<Point3> P = m.Positions;
        std::vector<std::array<uint32_t, 3>> T(m.TriangleCount());
        for (size_t t = 0; t < T.size(); ++t)
            T[t] = { m.Indices[t * 3], m.Indices[t * 3 + 1], m.Indices[t * 3 + 2] };
        std::vector<bool> triAlive(T.size(), true), vAlive(P.size(), true);
        std::vector<std::vector<uint32_t>> vt(P.size());
        for (uint32_t t = 0; t < T.size(); ++t)
            for (uint32_t v : T[t])
                vt[v].push_back(t);
        std::vector<Quadric> Q(P.size(), Quadric{});
        auto normalOf = [&](const std::array<uint32_t, 3>& t) { return Math::Cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]); };
        const Topology topo = BuildTopology(m);
        for (const auto& t : T)
        {
            const Vec3 c = normalOf(t);
            const double area = c.Length() / 2;
            if (area <= 0)
                continue;
            const Vec3 n = c.Normalized();
            const Quadric q = planeQ(n, -Math::Dot(n, ToVec(P[t[0]])), area);
            for (uint32_t v : t)
                addQ(Q[v], q);
            // 边界边：过边、垂直于面的平面，权重大，保持边界形状
            for (int i = 0; i < 3; ++i)
            {
                const uint32_t a = t[i], b = t[(i + 1) % 3];
                if (topo.EdgeOpposite.at(EdgeKey(a, b)).size() != 1)
                    continue;
                const Vec3 e = P[b] - P[a];
                const Vec3 bn = Math::Cross(e, n).Normalized();
                const Quadric bq = planeQ(bn, -Math::Dot(bn, ToVec(P[a])), 1000.0 * std::max(area, e.LengthSq()));
                addQ(Q[a], bq);
                addQ(Q[b], bq);
            }
        }

        std::vector<uint32_t> stamp(P.size(), 0);
        struct Candidate
        {
            double   Cost;
            uint32_t A, B, StampA, StampB;
            Point3   Target;
            bool operator>(const Candidate& o) const { return Cost > o.Cost; }
        };
        std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
        auto push = [&](uint32_t a, uint32_t b)
        {
            Quadric q = Q[a];
            addQ(q, Q[b]);
            // 最优点：解 A x = −b
            const double a11 = q[0], a12 = q[1], a13 = q[2], a22 = q[4], a23 = q[5], a33 = q[7];
            const double det = a11 * (a22 * a33 - a23 * a23) - a12 * (a12 * a33 - a23 * a13) + a13 * (a12 * a23 - a22 * a13);
            std::vector<Point3> cands{ P[a], P[b], Point3{ 0, 0, 0 } + (ToVec(P[a]) + ToVec(P[b])) * 0.5 };
            const double scale = std::max({ std::abs(a11), std::abs(a22), std::abs(a33), 1e-300 });
            if (std::abs(det) > 1e-9 * scale * scale * scale)
            {
                const double b1 = -q[3], b2 = -q[6], b3 = -q[8];
                const double x = (b1 * (a22 * a33 - a23 * a23) - a12 * (b2 * a33 - a23 * b3) + a13 * (b2 * a23 - a22 * b3)) / det;
                const double y = (a11 * (b2 * a33 - a23 * b3) - b1 * (a12 * a33 - a23 * a13) + a13 * (a12 * b3 - b2 * a13)) / det;
                const double z = (a11 * (a22 * b3 - b2 * a23) - a12 * (a12 * b3 - b2 * a13) + b1 * (a12 * a23 - a22 * a13)) / det;
                const Point3 opt{ x, y, z };
                // 最优点离边太远（几乎奇异）时不用
                if ((opt - P[a]).Length() < 10 * std::max((P[b] - P[a]).Length(), 1e-12))
                    cands.insert(cands.begin(), opt);
            }
            Candidate best{ 1e300, a, b, stamp[a], stamp[b], P[a] };
            for (const auto& c : cands)
            {
                const double cost = evalQ(q, c);
                if (cost < best.Cost)
                    best.Cost = cost, best.Target = c;
            }
            heap.push(best);
        };
        for (const auto& [key, opp] : topo.EdgeOpposite)
            push(static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xffffffffu));

        size_t alive = T.size();
        auto neighbors = [&](uint32_t v)
        {
            std::set<uint32_t> s;
            for (uint32_t t : vt[v])
                if (triAlive[t])
                    for (uint32_t w : T[t])
                        if (w != v)
                            s.insert(w);
            return s;
        };
        while (alive > target && !heap.empty())
        {
            const Candidate c = heap.top();
            heap.pop();
            const uint32_t a = c.A, b = c.B;
            if (!vAlive[a] || !vAlive[b] || stamp[a] != c.StampA || stamp[b] != c.StampB)
                continue;
            // 共同的三角形
            std::vector<uint32_t> shared;
            for (uint32_t t : vt[a])
                if (triAlive[t] && std::find(T[t].begin(), T[t].end(), b) != T[t].end())
                    shared.push_back(t);
            if (shared.empty() || shared.size() > 2)
                continue;
            // 连接条件：两端的共同邻点只能是共同三角形的第三个顶点（否则折叠后不是流形）
            const auto na = neighbors(a), nb = neighbors(b);
            size_t common = 0;
            for (uint32_t w : na)
                common += nb.count(w);
            if (common != shared.size())
                continue;
            // 翻转检查
            bool flips = false;
            for (uint32_t v : { a, b })
                for (uint32_t t : vt[v])
                {
                    if (!triAlive[t] || std::find(shared.begin(), shared.end(), t) != shared.end())
                        continue;
                    // 这个三角形只含 a、b 之一：把该顶点临时移到折叠后的位置
                    const Vec3 before = normalOf(T[t]);
                    const Point3 keep = P[v];
                    P[v] = c.Target;
                    const Vec3 after = normalOf(T[t]);
                    P[v] = keep;
                    if (after.Length() < 1e-12 * std::max(before.Length(), 1e-300) || Math::Dot(before.Normalized(), after.Normalized()) < 0.2)
                        flips = true;
                }
            if (flips)
                continue;
            // 折叠 b → a
            for (uint32_t t : shared)
            {
                triAlive[t] = false;
                --alive;
            }
            for (uint32_t t : vt[b])
            {
                if (!triAlive[t])
                    continue;
                for (auto& w : T[t])
                    if (w == b)
                        w = a;
                vt[a].push_back(t);
            }
            vt[b].clear();
            vAlive[b] = false;
            P[a] = c.Target;
            addQ(Q[a], Q[b]);
            ++stamp[a];
            for (uint32_t w : neighbors(a))
            {
                ++stamp[w];   // 邻点的其他边代价也变了：之后重新算
                for (uint32_t x : neighbors(w))
                    if (vAlive[x])
                        push(w, x);
            }
        }
        std::vector<uint32_t> idx;
        for (size_t t = 0; t < T.size(); ++t)
            if (triAlive[t])
                idx.insert(idx.end(), T[t].begin(), T[t].end());
        return Compact(P, idx);
    }

    Mesh Transform(const Mesh& m, const Math::Mat4& M)
    {
        Mesh out = m;
        for (auto& p : out.Positions)
            p = M.TransformPoint(p);
        const double* a = M.m;
        const double det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[1] * (a[4] * a[10] - a[6] * a[8]) + a[2] * (a[4] * a[9] - a[5] * a[8]);
        if (det < 0)
            for (size_t t = 0; t < out.TriangleCount(); ++t)
                std::swap(out.Indices[t * 3 + 1], out.Indices[t * 3 + 2]);
        ComputeNormals(out);
        return out;
    }

    // ── 分析 ─────────────────────────────────────────────────────────

    double Area(const Mesh& m)
    {
        double s = 0;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
            s += TriCross(m, t).Length() / 2;
        return s;
    }

    double Volume(const Mesh& m)
    {
        double s = 0;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const Vec3 a = ToVec(m.Positions[m.Indices[t * 3]]), b = ToVec(m.Positions[m.Indices[t * 3 + 1]]), c = ToVec(m.Positions[m.Indices[t * 3 + 2]]);
            s += Math::Dot(a, Math::Cross(b, c)) / 6;
        }
        return s;
    }

    bool IsClosed(const Mesh& m)
    {
        if (m.TriangleCount() == 0)
            return false;
        std::unordered_map<uint64_t, int> directed;   // (a, b) 有向边的次数
        for (size_t t = 0; t < m.TriangleCount(); ++t)
            for (int i = 0; i < 3; ++i)
                directed[uint64_t(m.Indices[t * 3 + i]) << 32 | m.Indices[t * 3 + (i + 1) % 3]]++;
        for (const auto& [key, n] : directed)
        {
            const uint64_t rev = (key & 0xffffffffu) << 32 | (key >> 32);
            auto it = directed.find(rev);
            if (n != 1 || it == directed.end() || it->second != 1)
                return false;
        }
        return true;
    }

    Vec3 FaceNormal(const Mesh& m, size_t face)
    {
        const Vec3 c = TriCross(m, face);
        return c.LengthSq() > 0 ? c.Normalized() : Vec3{};
    }

    bool ClosestPoint(const Mesh& m, const Point3& p, Point3& closest, double& distance, size_t& face)
    {
        if (m.TriangleCount() == 0)
            return false;
        distance = 1e300;
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const Point3 q = ClosestOnTriangle(p, m.Positions[m.Indices[t * 3]], m.Positions[m.Indices[t * 3 + 1]], m.Positions[m.Indices[t * 3 + 2]]);
            const double d = (q - p).Length();
            if (d < distance)
                distance = d, closest = q, face = t;
        }
        return true;
    }
}
