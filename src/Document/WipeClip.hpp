#pragma once
#include "Core/Math/Point3.hpp"
#include "Render/VertexTypes.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniCAD::WipeClip
{
    // 区域覆盖（WIPEOUT）的顶点流裁剪：把已经生成的场景顶点中落在多边形内的部分删掉，
    // 由 Editor 在拼接场景顶点流时按绘制序调用（只影响 wipeout 之前的实体）。
    // 多边形为世界坐标 XY 平面内的闭合环（首尾点可相同），不要求凸。

    struct Poly
    {
        std::vector<Math::Point3> pts;       // 去掉重复的闭合点
        double minX = 0, minY = 0, maxX = 0, maxY = 0;

        explicit Poly(const std::vector<Math::Point3>& src)
        {
            pts = src;
            if (pts.size() > 1 && std::abs(pts.front().x - pts.back().x) < 1e-12 && std::abs(pts.front().y - pts.back().y) < 1e-12)
                pts.pop_back();
            if (pts.empty()) return;
            minX = maxX = pts[0].x;
            minY = maxY = pts[0].y;
            for (const auto& p : pts)
            {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
        }

        bool Valid() const { return pts.size() >= 3; }

        bool BoxOverlaps(double x0, double y0, double x1, double y1) const
        {
            return !(x1 < minX || x0 > maxX || y1 < minY || y0 > maxY);
        }

        // 偶奇规则的点在多边形内判定
        bool Contains(double x, double y) const
        {
            bool in = false;
            const size_t n = pts.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const auto& a = pts[i];
                const auto& b = pts[j];
                if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                    in = !in;
            }
            return in;
        }
    };

    namespace detail
    {
        // 线段 p→p+r 与 q→q+s 的交点参数：成功返回 true，t 为在第一条上的参数
        inline bool SegSeg(double px, double py, double rx, double ry,
                           double qx, double qy, double sx, double sy, double& t)
        {
            const double den = rx * sy - ry * sx;
            if (std::abs(den) < 1e-18) return false;
            const double qpx = qx - px, qpy = qy - py;
            t = (qpx * sy - qpy * sx) / den;
            const double u = (qpx * ry - qpy * rx) / den;
            return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0;
        }

        inline Math::Float3 Mid(const Math::Float3& a, const Math::Float3& b)
        {
            return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f };
        }

        // 三角形的边是否穿过多边形边界，或多边形整个落在三角形里
        inline bool TriTouchesBoundary(const Poly& poly, const Math::Float3 v[3])
        {
            const size_t n = poly.pts.size();
            for (int e = 0; e < 3; ++e)
            {
                const auto& a = v[e];
                const auto& b = v[(e + 1) % 3];
                for (size_t i = 0; i < n; ++i)
                {
                    const auto& p = poly.pts[i];
                    const auto& q = poly.pts[(i + 1) % n];
                    double t;
                    if (SegSeg(a.x, a.y, b.x - a.x, b.y - a.y, p.x, p.y, q.x - p.x, q.y - p.y, t))
                        return true;
                }
            }
            // 多边形一个顶点在三角形内 ⇒ 多边形（边不相交时）整个在三角形内
            const auto& p0 = poly.pts[0];
            auto side = [&](const Math::Float3& a, const Math::Float3& b)
            {
                return (b.x - a.x) * (p0.y - a.y) - (b.y - a.y) * (p0.x - a.x);
            };
            const double s0 = side(v[0], v[1]), s1 = side(v[1], v[2]), s2 = side(v[2], v[0]);
            return (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
        }

        inline void ClipTri(const Poly& poly, const Vertex_P3_C4 t[3], int depth, std::vector<Vertex_P3_C4>& out)
        {
            const Math::Float3 v[3] = { t[0].pos, t[1].pos, t[2].pos };
            const bool partial = TriTouchesBoundary(poly, v);

            if (!partial || depth == 0)
            {
                const double cx = (v[0].x + v[1].x + v[2].x) / 3.0;
                const double cy = (v[0].y + v[1].y + v[2].y) / 3.0;
                if (!poly.Contains(cx, cy))
                    out.insert(out.end(), t, t + 3);
                return;
            }

            // 跨边界：四分，递归到最大深度后按重心取舍（边缘误差 ≤ 三角形尺寸 / 2^depth）
            auto vx = [&](int a, int b)
            {
                Vertex_P3_C4 m;
                m.pos   = Mid(t[a].pos, t[b].pos);
                m.color = t[a].color;
                return m;
            };
            const Vertex_P3_C4 m01 = vx(0, 1), m12 = vx(1, 2), m20 = vx(2, 0);
            const Vertex_P3_C4 c0[3] = { t[0], m01, m20 };
            const Vertex_P3_C4 c1[3] = { m01, t[1], m12 };
            const Vertex_P3_C4 c2[3] = { m20, m12, t[2] };
            const Vertex_P3_C4 c3[3] = { m01, m12, m20 };
            ClipTri(poly, c0, depth - 1, out);
            ClipTri(poly, c1, depth - 1, out);
            ClipTri(poly, c2, depth - 1, out);
            ClipTri(poly, c3, depth - 1, out);
        }
    }

    // 线段流（每两个顶点一条）：删去落在多边形内的部分，穿过边界的线段被截断
    inline void ClipLines(std::vector<Vertex_P3_C4>& v, const Poly& poly)
    {
        if (!poly.Valid() || v.size() < 2) return;

        std::vector<Vertex_P3_C4> out;
        out.reserve(v.size());
        const size_t n = poly.pts.size();

        for (size_t k = 0; k + 1 < v.size(); k += 2)
        {
            const Vertex_P3_C4& a = v[k];
            const Vertex_P3_C4& b = v[k + 1];
            if (!poly.BoxOverlaps(std::min(a.pos.x, b.pos.x), std::min(a.pos.y, b.pos.y),
                                  std::max(a.pos.x, b.pos.x), std::max(a.pos.y, b.pos.y)))
            {
                out.push_back(a); out.push_back(b);
                continue;
            }

            const double rx = b.pos.x - a.pos.x, ry = b.pos.y - a.pos.y;
            std::vector<double> ts = { 0.0, 1.0 };
            for (size_t i = 0; i < n; ++i)
            {
                const auto& p = poly.pts[i];
                const auto& q = poly.pts[(i + 1) % n];
                double t;
                if (detail::SegSeg(a.pos.x, a.pos.y, rx, ry, p.x, p.y, q.x - p.x, q.y - p.y, t))
                    ts.push_back(t);
            }
            std::sort(ts.begin(), ts.end());

            auto at = [&](double t)
            {
                Vertex_P3_C4 m;
                m.pos   = { static_cast<float>(a.pos.x + rx * t), static_cast<float>(a.pos.y + ry * t),
                            static_cast<float>(a.pos.z + (b.pos.z - a.pos.z) * t) };
                m.color = a.color;
                return m;
            };

            for (size_t i = 0; i + 1 < ts.size(); ++i)
            {
                if (ts[i + 1] - ts[i] < 1e-9) continue;
                const double tm = (ts[i] + ts[i + 1]) * 0.5;
                if (poly.Contains(a.pos.x + rx * tm, a.pos.y + ry * tm)) continue;
                out.push_back(i == 0 ? a : at(ts[i]));
                out.push_back(i + 2 == ts.size() ? b : at(ts[i + 1]));
            }
        }
        v.swap(out);
    }

    // 填充三角形流（每三个顶点一个）：删去落在多边形内的部分
    inline void ClipFills(std::vector<Vertex_P3_C4>& v, const Poly& poly, int maxDepth = 5)
    {
        if (!poly.Valid() || v.size() < 3) return;

        std::vector<Vertex_P3_C4> out;
        out.reserve(v.size());
        for (size_t k = 0; k + 2 < v.size(); k += 3)
        {
            const Vertex_P3_C4* t = &v[k];
            const double x0 = std::min({ t[0].pos.x, t[1].pos.x, t[2].pos.x });
            const double x1 = std::max({ t[0].pos.x, t[1].pos.x, t[2].pos.x });
            const double y0 = std::min({ t[0].pos.y, t[1].pos.y, t[2].pos.y });
            const double y1 = std::max({ t[0].pos.y, t[1].pos.y, t[2].pos.y });
            if (!poly.BoxOverlaps(x0, y0, x1, y1))
                out.insert(out.end(), t, t + 3);
            else
                detail::ClipTri(poly, t, maxDepth, out);
        }
        v.swap(out);
    }

    // 纹理文字流（每个字符 6 个顶点）：字符中心落在多边形内则整个字符删去
    inline void ClipTexts(std::vector<Vertex_P3_C4_UV>& v, const Poly& poly)
    {
        if (!poly.Valid() || v.size() < 6) return;

        std::vector<Vertex_P3_C4_UV> out;
        out.reserve(v.size());
        for (size_t k = 0; k + 5 < v.size(); k += 6)
        {
            double cx = 0, cy = 0;
            for (int i = 0; i < 6; ++i) { cx += v[k + i].pos.x; cy += v[k + i].pos.y; }
            if (!poly.Contains(cx / 6.0, cy / 6.0))
                out.insert(out.end(), v.begin() + k, v.begin() + k + 6);
        }
        v.swap(out);
    }
}
