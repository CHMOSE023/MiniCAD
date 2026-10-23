#pragma once
#include "Core/Math/Point3.hpp"
#include "Render/VertexTypes.hpp"
#include "Document/GlyphMeshLibrary.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
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

            // 凸多边形（遮罩绝大多数是矩形）统一成逆时针，填充裁剪可以按半平面精确求差
            const size_t n = pts.size();
            if (n < 3) return;
            double area = 0;
            int sign = 0;
            convex = true;
            for (size_t i = 0; i < n; ++i)
            {
                const auto& a = pts[i];
                const auto& b = pts[(i + 1) % n];
                const auto& c = pts[(i + 2) % n];
                area += a.x * b.y - b.x * a.y;
                const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                const double scale = std::abs(b.x - a.x) + std::abs(b.y - a.y) + std::abs(c.x - b.x) + std::abs(c.y - b.y);
                if (std::abs(cross) <= 1e-12 * scale * scale) continue;   // 共线点
                const int s = cross > 0 ? 1 : -1;
                if (sign == 0) sign = s;
                else if (s != sign) convex = false;
            }
            if (sign == 0) convex = false;   // 退化（全部共线）
            if (convex)
            {
                // 转向一致还可能是自交的星形：外角和须为一圈
                double turn = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    const auto& a = pts[i];
                    const auto& b = pts[(i + 1) % n];
                    const auto& c = pts[(i + 2) % n];
                    const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
                    turn += std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
                }
                convex = std::abs(std::abs(turn) - 2.0 * 3.14159265358979323846) < 1e-6;
            }
            if (convex && area < 0) std::reverse(pts.begin(), pts.end());
        }

        bool convex = false;   // 为 true 时 pts 为逆时针

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

        // keepInside = false（默认）：保留多边形**外**的部分（挖洞）
        // keepInside = true        ：保留多边形**内**的部分（布局视口裁剪）
        inline void ClipTri(const Poly& poly, const Vertex_P3_C4 t[3], int depth,
                            std::vector<Vertex_P3_C4>& out, bool keepInside = false)
        {
            const Math::Float3 v[3] = { t[0].pos, t[1].pos, t[2].pos };
            const bool partial = TriTouchesBoundary(poly, v);

            if (!partial || depth == 0)
            {
                const double cx = (v[0].x + v[1].x + v[2].x) / 3.0;
                const double cy = (v[0].y + v[1].y + v[2].y) / 3.0;
                if (poly.Contains(cx, cy) == keepInside)
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
            ClipTri(poly, c0, depth - 1, out, keepInside);
            ClipTri(poly, c1, depth - 1, out, keepInside);
            ClipTri(poly, c2, depth - 1, out, keepInside);
            ClipTri(poly, c3, depth - 1, out, keepInside);
        }

        // ── 凸多边形片段（填充三角形被凸遮罩切开后的各块，仍是凸的）──
        using Piece = std::vector<Vertex_P3_C4>;

        inline Vertex_P3_C4 Lerp(const Vertex_P3_C4& a, const Vertex_P3_C4& b, double t)
        {
            const float f = static_cast<float>(t);
            Vertex_P3_C4 m;
            m.pos   = { a.pos.x + (b.pos.x - a.pos.x) * f, a.pos.y + (b.pos.y - a.pos.y) * f, a.pos.z + (b.pos.z - a.pos.z) * f };
            const Math::Float4 ca = a.color.ToFloat4(), cb = b.color.ToFloat4();
            m.color = { ca.x + (cb.x - ca.x) * f, ca.y + (cb.y - ca.y) * f, ca.z + (cb.z - ca.z) * f, ca.w + (cb.w - ca.w) * f };
            return m;
        }

        // 用有向直线 p→q 切凸片段：left 为 true 保留左侧，否则保留右侧（线上的点两侧都保留）
        inline void ClipHalf(const Piece& in, const Math::Point3& p, const Math::Point3& q, bool left, Piece& out)
        {
            out.clear();
            const double ex = q.x - p.x, ey = q.y - p.y;
            auto side = [&](const Vertex_P3_C4& v)
            {
                const double s = ex * (v.pos.y - p.y) - ey * (v.pos.x - p.x);
                return left ? s : -s;
            };
            const size_t n = in.size();
            for (size_t i = 0; i < n; ++i)
            {
                const Vertex_P3_C4& a = in[i];
                const Vertex_P3_C4& b = in[(i + 1) % n];
                const double sa = side(a), sb = side(b);
                if (sa >= 0) out.push_back(a);
                if ((sa > 0 && sb < 0) || (sa < 0 && sb > 0))
                    out.push_back(Lerp(a, b, sa / (sa - sb)));
            }
        }

        inline double PieceArea2(const Piece& p)
        {
            double a = 0;
            for (size_t i = 0, n = p.size(); i < n; ++i)
            {
                const auto& u = p[i].pos;
                const auto& v = p[(i + 1) % n].pos;
                a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
            }
            return a;
        }

        // 凸片段减去凸遮罩（逆时针）：依次取遮罩每条边外侧的部分（互不重叠），剩下的在遮罩内，丢弃
        inline void SubtractConvex(const Piece& piece, const Poly& poly, std::vector<Piece>& out, Piece& cur, Piece& tmp)
        {
            cur = piece;
            const size_t n = poly.pts.size();
            for (size_t i = 0; i < n && cur.size() >= 3; ++i)
            {
                const auto& p = poly.pts[i];
                const auto& q = poly.pts[(i + 1) % n];
                ClipHalf(cur, p, q, false, tmp);
                if (tmp.size() >= 3 && std::abs(PieceArea2(tmp)) > 0)
                    out.push_back(tmp);
                ClipHalf(cur, p, q, true, tmp);
                cur.swap(tmp);
            }
        }

        // 多个（非凸）遮罩一起细分：任一遮罩边界穿过就四分，到最大深度后重心落在任一遮罩内则删去
        inline void ClipTriMulti(const std::vector<const Poly*>& polys, const Vertex_P3_C4 t[3], int depth, std::vector<Vertex_P3_C4>& out)
        {
            const Math::Float3 v[3] = { t[0].pos, t[1].pos, t[2].pos };
            const double x0 = std::min({ v[0].x, v[1].x, v[2].x }), x1 = std::max({ v[0].x, v[1].x, v[2].x });
            const double y0 = std::min({ v[0].y, v[1].y, v[2].y }), y1 = std::max({ v[0].y, v[1].y, v[2].y });
            bool partial = false;
            for (const Poly* p : polys)
                if (p->BoxOverlaps(x0, y0, x1, y1) && TriTouchesBoundary(*p, v)) { partial = true; break; }

            if (!partial || depth == 0)
            {
                const double cx = (v[0].x + v[1].x + v[2].x) / 3.0;
                const double cy = (v[0].y + v[1].y + v[2].y) / 3.0;
                for (const Poly* p : polys)
                    if (p->BoxOverlaps(cx, cy, cx, cy) && p->Contains(cx, cy)) return;
                out.insert(out.end(), t, t + 3);
                return;
            }
            const Vertex_P3_C4 m01 = Lerp(t[0], t[1], 0.5), m12 = Lerp(t[1], t[2], 0.5), m20 = Lerp(t[2], t[0], 0.5);
            const Vertex_P3_C4 c0[3] = { t[0], m01, m20 };
            const Vertex_P3_C4 c1[3] = { m01, t[1], m12 };
            const Vertex_P3_C4 c2[3] = { m20, m12, t[2] };
            const Vertex_P3_C4 c3[3] = { m01, m12, m20 };
            ClipTriMulti(polys, c0, depth - 1, out);
            ClipTriMulti(polys, c1, depth - 1, out);
            ClipTriMulti(polys, c2, depth - 1, out);
            ClipTriMulti(polys, c3, depth - 1, out);
        }
    }

    // 线段流（每两个顶点一条）：删去落在多边形内的部分，穿过边界的线段被截断。
    // 只处理下标 from 起的顶点（from 为 2 的倍数）；与多边形外包框都不相交时不重建顶点流
    // keepInside = false（默认）：挖掉多边形内部的线段（区域覆盖）
    // keepInside = true        ：只保留多边形内部的线段（布局视口裁剪）
    inline void ClipLines(std::vector<Vertex_P3_C4>& v, const Poly& poly, size_t from = 0,
                          bool keepInside = false)
    {
        if (!poly.Valid() || v.size() < from + 2) return;

        auto overlaps = [&](const Vertex_P3_C4& a, const Vertex_P3_C4& b)
        {
            return poly.BoxOverlaps(std::min(a.pos.x, b.pos.x), std::min(a.pos.y, b.pos.y),
                                    std::max(a.pos.x, b.pos.x), std::max(a.pos.y, b.pos.y));
        };
        size_t first = from;
        if (!keepInside)
        {
            // 挖洞：跳过开头不重叠的线段（它们本来就不受这块区域覆盖影响）
            while (first + 1 < v.size() && !overlaps(v[first], v[first + 1])) first += 2;
            if (first + 1 >= v.size()) return;
        }
        else
        {
            // 保留内部：不重叠的线段要直接丢掉。先原地压缩一遍，
            // 只留下与矩形包围盒有交叠的线段，后面再按多边形边界精确切。
            std::vector<Vertex_P3_C4> kept;
            kept.reserve(v.size() - from);
            for (size_t k = from; k + 1 < v.size(); k += 2)
                if (overlaps(v[k], v[k + 1]))
                {
                    kept.push_back(v[k]);
                    kept.push_back(v[k + 1]);
                }
            v.resize(from);
            v.insert(v.end(), kept.begin(), kept.end());
            if (v.size() < from + 2) return;
            // 压缩后没有需要再判断的前缀，统一从 from 开始按多边形精确切
        }

        std::vector<Vertex_P3_C4> out;
        out.reserve(v.size() - first);
        const size_t n = poly.pts.size();

        for (size_t k = first; k + 1 < v.size(); k += 2)
        {
            const Vertex_P3_C4& a = v[k];
            const Vertex_P3_C4& b = v[k + 1];
            if (!overlaps(a, b))
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
                const bool inside = poly.Contains(a.pos.x + rx * tm, a.pos.y + ry * tm);
                // keepInside=false（默认）= 挖掉多边形内部（区域覆盖的语义）
                // keepInside=true        = 只保留多边形内部（布局视口裁剪的语义）
                if (inside != keepInside) continue;
                out.push_back(i == 0 ? a : at(ts[i]));
                out.push_back(i + 2 == ts.size() ? b : at(ts[i + 1]));
            }
        }
        v.resize(first);
        v.insert(v.end(), out.begin(), out.end());
    }

    // 填充三角形流（每三个顶点一个）：删去落在多边形内的部分
    // 只处理下标 from 起的顶点（from 为 3 的倍数）
    //
    // keepInside = false（默认）：挖掉多边形内部（区域覆盖）
    // keepInside = true        ：只保留多边形内部（布局视口裁剪）
    inline void ClipFills(std::vector<Vertex_P3_C4>& v, const Poly& poly, int maxDepth = 5,
                          size_t from = 0, bool keepInside = false)
    {
        if (!poly.Valid() || v.size() < from + 3) return;

        auto overlaps = [&](const Vertex_P3_C4* t)
        {
            const double x0 = std::min({ t[0].pos.x, t[1].pos.x, t[2].pos.x });
            const double x1 = std::max({ t[0].pos.x, t[1].pos.x, t[2].pos.x });
            const double y0 = std::min({ t[0].pos.y, t[1].pos.y, t[2].pos.y });
            const double y1 = std::max({ t[0].pos.y, t[1].pos.y, t[2].pos.y });
            return poly.BoxOverlaps(x0, y0, x1, y1);
        };
        size_t first = from;
        if (!keepInside)
        {
            // 挖洞：跳过开头不重叠的三角形（不受这块区域覆盖影响）
            while (first + 2 < v.size() && !overlaps(&v[first])) first += 3;
            if (first + 2 >= v.size()) return;
        }
        else
        {
            // 保留内部：不重叠的三角形直接丢掉
            std::vector<Vertex_P3_C4> kept;
            kept.reserve(v.size() - from);
            for (size_t k = from; k + 2 < v.size(); k += 3)
                if (overlaps(&v[k]))
                    kept.insert(kept.end(), &v[k], &v[k] + 3);
            v.resize(from);
            v.insert(v.end(), kept.begin(), kept.end());
            if (v.size() < from + 3) return;
        }

        std::vector<Vertex_P3_C4> out;
        out.reserve(v.size() - first);
        for (size_t k = first; k + 2 < v.size(); k += 3)
        {
            const Vertex_P3_C4* t = &v[k];
            if (!overlaps(t))
            {
                if (!keepInside)
                    out.insert(out.end(), t, t + 3);       // 挖洞：区外原样保留
                // 保留内部：区外的丢掉
            }
            else
                detail::ClipTri(poly, t, maxDepth, out, keepInside);
        }
        v.resize(first);
        v.insert(v.end(), out.begin(), out.end());
    }

    // 点流（每个顶点一个点）：keepInside=false 删去落在多边形内的点（区域覆盖）；
    // keepInside=true 只保留落在多边形内的点（布局视口裁剪）。只处理下标 from 起的顶点。
    inline void ClipPoints(std::vector<Vertex_P3_C4>& v, const Poly& poly, size_t from = 0,
                           bool keepInside = false)
    {
        if (!poly.Valid() || v.size() <= from) return;
        size_t w = from;
        for (size_t k = from; k < v.size(); ++k)
        {
            const double x = v[k].pos.x, y = v[k].pos.y;
            const bool inside = poly.Contains(x, y);
            // 保留条件：keepInside 时保留内部的点，否则保留外部的点
            if (inside == keepInside)
            {
                v[w++] = v[k];
            }
        }
        v.resize(w);
    }

    // 纹理文字流（每个字符 6 个顶点）：字符中心落在多边形内则整个字符删去
    // 只处理下标 from 起的顶点（from 为 6 的倍数）
    inline void ClipTexts(std::vector<Vertex_P3_C4_UV>& v, const Poly& poly, size_t from = 0)
    {
        if (!poly.Valid() || v.size() < from + 6) return;

        auto inside = [&](size_t k)
        {
            double cx = 0, cy = 0;
            for (int i = 0; i < 6; ++i) { cx += v[k + i].pos.x; cy += v[k + i].pos.y; }
            cx /= 6.0; cy /= 6.0;
            return poly.BoxOverlaps(cx, cy, cx, cy) && poly.Contains(cx, cy);
        };
        size_t first = from;
        while (first + 5 < v.size() && !inside(first)) first += 6;
        if (first + 5 >= v.size()) return;

        std::vector<Vertex_P3_C4_UV> out;
        out.reserve(v.size() - first);
        for (size_t k = first; k + 5 < v.size(); k += 6)
            if (!inside(k))
                out.insert(out.end(), v.begin() + k, v.begin() + k + 6);
        v.resize(first);
        v.insert(v.end(), out.begin(), out.end());
    }

    // ── 字形实例流的视口裁剪（布局页专用）────────────────────────────────
    //
    // 文字在桌面 / 网页两版都走**字形实例**（`GlyphInstance`，见 GlyphInstancing.hpp）：
    // 每个字只记一条「局部 → 世界的仿射变换 + 颜色」，字形网格在 GlyphMeshLibrary 里
    // 每个字形只存一份。实例流与线段 / 填充是**分开提交**的，因此必须单独裁。
    //
    // ⚠ 这是「布局页视口外图元依然显示」的一个真实来源：
    //   ComposeModelIntoViewport 此前只裁了线 / 填充 / 点 / 纹理文字四路，
    //   字形实例一路**完全没裁** —— 模型空间的文字（大图纸里往往是最大的一类）
    //   整行溢出到视口外的纸面上，看起来就是「没经过视口裁剪」。
    //
    // keepInside = true（默认）：只保留落在多边形**内**的实例（布局视口裁剪）；
    // keepInside = false       ：删去落在多边形内的实例（与区域覆盖同语义，备用）。
    //
    // 跨边界的字**不切碎**：按实例原点（tx, ty）落在框内 / 框外整体取舍，
    // 与 ClipTexts「按字符中心整体取舍」是同一套语义。一行文字横跨视口边界时，
    // 框外的那几个字消失、框内的照常显示 —— 这正是 ClipTexts 对纹理文字的行为，
    // 两路文字因此观感一致（不会出现「矢量文字被整行藏掉、纹理文字却被切开」）。
    // 只处理下标 from 起的实例。
    inline void ClipGlyphInstances(std::vector<GlyphInstance>& g, const Poly& poly,
                                   size_t from = 0, bool keepInside = true)
    {
        if (!poly.Valid() || g.size() <= from) return;
        size_t w = from;
        for (size_t k = from; k < g.size(); ++k)
        {
            const bool inside = poly.Contains(g[k].tx, g[k].ty);
            if (inside == keepInside)
            {
                if (w != k) g[w] = g[k];
                ++w;
            }
        }
        g.resize(w);
    }

    // ── 光栅图像流的视口裁剪（布局页专用）────────────────────────────────
    //
    // 图像（IMAGE）与线段 / 填充 / 文字一样是**单独提交**的一路（Viewport 里
    // 逐个 SubmitImage）。ComposeModelIntoViewport 若不裁它，贴图就整张溢出到
    // 视口外的纸面上 —— 与字形实例同属「漏了一路」的问题。
    //
    // 按**中心点**整体取舍（与 ClipDrawSink::EmitImage 的既有语义一致）：
    // 跨边界的图像不切碎，中心在框内就整张画、在框外就整张丢。
    // 只处理下标 from 起的图像。
    inline void ClipImages(std::vector<ImageDraw>& v, const Poly& poly,
                           size_t from = 0, bool keepInside = true)
    {
        if (!poly.Valid() || v.size() <= from) return;
        size_t w = from;
        for (size_t k = from; k < v.size(); ++k)
        {
            // 两个三角形共 6 个顶点，中心取对角顶点 (0, 2) 的中点
            const double cx = (static_cast<double>(v[k].Verts[0].pos.x) + v[k].Verts[2].pos.x) * 0.5;
            const double cy = (static_cast<double>(v[k].Verts[0].pos.y) + v[k].Verts[2].pos.y) * 0.5;
            if (poly.Contains(cx, cy) == keepInside)
            {
                if (w != k) v[w] = std::move(v[k]);
                ++w;
            }
        }
        v.resize(w);
    }

    // 批量裁剪：区域覆盖只裁剪在它之前画的内容。逐个遮罩调用上面的函数时，每个遮罩都要把其后的整段顶点流
    // 复制重建一遍，天正图块里成千上万个遮罩时成了平方复杂度（打开一张暖通图要几分钟）。
    // 这里先记下每个遮罩出现时各顶点流的长度，最后一次裁剪：下标 k 的图元只被"出现时流长度 > k"的遮罩裁剪，
    // 裁掉的是这些多边形的并集，与遮罩的先后无关；候选遮罩经均匀网格按外包框查找。
    // 记录的长度是未裁剪时的下标，因此一批遮罩全部记录完之前不能裁剪对应的顶点流。
    class Batch
    {
    public:
        explicit Batch(int streams) : m_streams(streams) {}

        // ends：各顶点流此刻的长度（个数与构造时的 streams 相同）
        void Add(const std::vector<Math::Point3>& polygon, std::initializer_list<size_t> ends)
        {
            Poly poly(polygon);
            if (!poly.Valid()) return;
            m_polys.push_back(std::move(poly));
            m_ends.insert(m_ends.end(), ends.begin(), ends.end());
            m_ends.resize(m_polys.size() * m_streams, 0);
            m_gridDirty = true;
        }

        bool Empty() const { return m_polys.empty(); }
        void Clear()
        {
            m_polys.clear();
            m_ends.clear();
            m_gridDirty = true;
        }

        // 线段流（每两个顶点一条），只处理下标 from 起的顶点
        void ClipLines(std::vector<Vertex_P3_C4>& v, int stream, size_t from = 0)
        {
            if (m_polys.empty() || v.size() < from + 2) return;
            BuildGrid();
            std::vector<Vertex_P3_C4> out;
            size_t first = SIZE_MAX;
            std::vector<double> ts;
            std::vector<std::pair<double, double>> pieces, next;
            for (size_t k = from; k + 1 < v.size(); k += 2)
            {
                const Vertex_P3_C4 a = v[k], b = v[k + 1];
                Candidates(std::min(a.pos.x, b.pos.x), std::min(a.pos.y, b.pos.y),
                           std::max(a.pos.x, b.pos.x), std::max(a.pos.y, b.pos.y), k, stream);
                if (m_cand.empty())
                {
                    if (first != SIZE_MAX) { out.push_back(a); out.push_back(b); }
                    continue;
                }
                if (first == SIZE_MAX)
                {
                    first = k;
                    out.reserve(v.size() - k);
                }

                // 依次用每个候选多边形切掉段内落在其中的部分（参数区间）
                const double rx = b.pos.x - a.pos.x, ry = b.pos.y - a.pos.y;
                pieces.assign(1, { 0.0, 1.0 });
                for (uint32_t pi : m_cand)
                {
                    const Poly& poly = m_polys[pi];
                    const size_t n = poly.pts.size();
                    ts.clear();
                    for (size_t i = 0; i < n; ++i)
                    {
                        const auto& p = poly.pts[i];
                        const auto& q = poly.pts[(i + 1) % n];
                        double t;
                        if (detail::SegSeg(a.pos.x, a.pos.y, rx, ry, p.x, p.y, q.x - p.x, q.y - p.y, t))
                            ts.push_back(t);
                    }
                    std::sort(ts.begin(), ts.end());
                    next.clear();
                    for (const auto& [t0, t1] : pieces)
                    {
                        double s = t0;
                        auto keep = [&](double e)
                        {
                            if (e - s < 1e-9) return;
                            const double tm = (s + e) * 0.5;
                            if (!poly.Contains(a.pos.x + rx * tm, a.pos.y + ry * tm))
                            {
                                if (!next.empty() && std::abs(next.back().second - s) < 1e-12)
                                    next.back().second = e;    // 与上一段相接：合并
                                else
                                    next.emplace_back(s, e);
                            }
                            s = e;
                        };
                        for (double t : ts)
                            if (t > t0 && t < t1) keep(t);
                        keep(t1);
                    }
                    pieces.swap(next);
                    if (pieces.empty()) break;
                }

                auto at = [&](double t)
                {
                    if (t <= 0.0) return a;
                    if (t >= 1.0) return b;
                    Vertex_P3_C4 m;
                    m.pos   = { static_cast<float>(a.pos.x + rx * t), static_cast<float>(a.pos.y + ry * t),
                                static_cast<float>(a.pos.z + (b.pos.z - a.pos.z) * t) };
                    m.color = a.color;
                    return m;
                };
                for (const auto& [t0, t1] : pieces)
                {
                    out.push_back(at(t0));
                    out.push_back(at(t1));
                }
            }
            if (first == SIZE_MAX) return;
            v.resize(first);
            v.insert(v.end(), out.begin(), out.end());
        }

        // 填充三角形流（每三个顶点一个），只处理下标 from 起的顶点
        void ClipFills(std::vector<Vertex_P3_C4>& v, int stream, int maxDepth = 5, size_t from = 0)
        {
            if (m_polys.empty() || v.size() < from + 3) return;
            BuildGrid();
            std::vector<Vertex_P3_C4> out, cur;
            size_t first = SIZE_MAX;
            for (size_t k = from; k + 2 < v.size(); k += 3)
            {
                const Vertex_P3_C4* t = &v[k];
                Candidates(std::min({ t[0].pos.x, t[1].pos.x, t[2].pos.x }), std::min({ t[0].pos.y, t[1].pos.y, t[2].pos.y }),
                           std::max({ t[0].pos.x, t[1].pos.x, t[2].pos.x }), std::max({ t[0].pos.y, t[1].pos.y, t[2].pos.y }), k, stream);
                if (m_cand.empty())
                {
                    if (first != SIZE_MAX) out.insert(out.end(), t, t + 3);
                    continue;
                }
                if (first == SIZE_MAX)
                {
                    first = k;
                    out.reserve(v.size() - k);
                }
                // 凸遮罩精确求差（片段数只与遮罩边数有关）；非凸遮罩最后合在一起细分一次，
                // 不能逐个遮罩细分——每个遮罩把三角形再四分 maxDepth 层，几个遮罩叠在一起就是指数膨胀
                m_pieces.assign(1, detail::Piece(t, t + 3));
                m_nonConvex.clear();
                for (uint32_t pi : m_cand)
                {
                    const Poly& poly = m_polys[pi];
                    if (!poly.convex)
                    {
                        m_nonConvex.push_back(&poly);
                        continue;
                    }
                    m_nextPieces.clear();
                    for (const detail::Piece& pc : m_pieces)
                    {
                        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
                        for (const auto& pv : pc)
                        {
                            x0 = std::min<double>(x0, pv.pos.x); x1 = std::max<double>(x1, pv.pos.x);
                            y0 = std::min<double>(y0, pv.pos.y); y1 = std::max<double>(y1, pv.pos.y);
                        }
                        if (poly.BoxOverlaps(x0, y0, x1, y1))
                            detail::SubtractConvex(pc, poly, m_nextPieces, m_cur, m_tmp);
                        else
                            m_nextPieces.push_back(pc);
                    }
                    m_pieces.swap(m_nextPieces);
                    if (m_pieces.empty()) break;
                }
                cur.clear();
                for (const detail::Piece& pc : m_pieces)
                    for (size_t j = 1; j + 1 < pc.size(); ++j)
                    {
                        cur.push_back(pc[0]); cur.push_back(pc[j]); cur.push_back(pc[j + 1]);
                    }
                if (m_nonConvex.empty())
                    out.insert(out.end(), cur.begin(), cur.end());
                else
                    for (size_t j = 0; j + 2 < cur.size(); j += 3)
                        detail::ClipTriMulti(m_nonConvex, &cur[j], maxDepth, out);
            }
            if (first == SIZE_MAX) return;
            v.resize(first);
            v.insert(v.end(), out.begin(), out.end());
        }

        // 纹理文字流（每个字符 6 个顶点）：字符中心落在任一适用的多边形内则删去，只处理下标 from 起的顶点
        void ClipTexts(std::vector<Vertex_P3_C4_UV>& v, int stream, size_t from = 0)
        {
            if (m_polys.empty() || v.size() < from + 6) return;
            BuildGrid();
            size_t w = from;
            for (size_t k = from; k + 5 < v.size(); k += 6)
            {
                double cx = 0, cy = 0;
                for (int i = 0; i < 6; ++i) { cx += v[k + i].pos.x; cy += v[k + i].pos.y; }
                cx /= 6.0; cy /= 6.0;
                Candidates(cx, cy, cx, cy, k, stream);
                const bool inside = std::any_of(m_cand.begin(), m_cand.end(), [&](uint32_t pi) { return m_polys[pi].Contains(cx, cy); });
                if (inside) continue;
                if (w != k) std::copy(v.begin() + k, v.begin() + k + 6, v.begin() + w);
                w += 6;
            }
            v.resize(w);
        }

        // 点图元流（每个顶点一个点）：落在任一适用的多边形内则删去，只处理下标 from 起的顶点
        void ClipPoints(std::vector<Vertex_P3_C4>& v, int stream, size_t from = 0)
        {
            if (m_polys.empty() || v.size() <= from) return;
            BuildGrid();
            size_t w = from;
            for (size_t k = from; k < v.size(); ++k)
            {
                const double x = v[k].pos.x, y = v[k].pos.y;
                Candidates(x, y, x, y, k, stream);
                if (std::any_of(m_cand.begin(), m_cand.end(), [&](uint32_t pi) { return m_polys[pi].Contains(x, y); }))
                    continue;
                v[w++] = v[k];
            }
            v.resize(w);
        }

        // 字形实例流（见 GlyphMeshLibrary），只处理下标 from 起的实例：外包框与适用的遮罩相交的实例从流中删去，
        // 展开成线段与三角形、用这些遮罩裁剪后追加到 lines / fills 尾部。
        // 要在 lines / fills 两条流本身的裁剪之后调用：追加的顶点下标超出记录的流长度，不会被再裁剪一次
        void ClipGlyphs(std::vector<GlyphInstance>& g, int stream, size_t from, const GlyphMeshLibrary& lib,
                        std::vector<Vertex_P3_C4>& lines, std::vector<Vertex_P3_C4>& fills)
        {
            if (m_polys.empty() || g.size() <= from) return;
            BuildGrid();
            std::vector<Vertex_P3_C4> gl, gf;
            size_t w = from;
            for (size_t k = from; k < g.size(); ++k)
            {
                double x0, y0, x1, y1;
                lib.Bounds(g[k], x0, y0, x1, y1);
                Candidates(x0, y0, x1, y1, k, stream);
                if (m_cand.empty())
                {
                    if (w != k) g[w] = g[k];
                    ++w;
                    continue;
                }
                // 只用这些遮罩裁剪展开后的顶点
                Batch sub(2);
                for (uint32_t pi : m_cand)
                {
                    sub.m_polys.push_back(m_polys[pi]);
                    sub.m_ends.insert(sub.m_ends.end(), { SIZE_MAX, SIZE_MAX });
                }
                gl.clear();
                gf.clear();
                lib.Expand(g[k], gl, gf);
                sub.ClipLines(gl, 0);
                sub.ClipFills(gf, 1);
                lines.insert(lines.end(), gl.begin(), gl.end());
                fills.insert(fills.end(), gf.begin(), gf.end());
            }
            g.resize(w);
        }

    private:
        void BuildGrid()
        {
            if (!m_gridDirty) return;
            m_gridDirty = false;
            m_gx0 = m_gy0 = 1e300;
            double gx1 = -1e300, gy1 = -1e300;
            for (const Poly& p : m_polys)
            {
                m_gx0 = std::min(m_gx0, p.minX); m_gy0 = std::min(m_gy0, p.minY);
                gx1 = std::max(gx1, p.maxX);     gy1 = std::max(gy1, p.maxY);
            }
            m_gx1 = gx1; m_gy1 = gy1;
            m_gn = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(m_polys.size()))) + 1, 1, 256);
            m_cw = std::max((gx1 - m_gx0) / m_gn, 1e-12);
            m_ch = std::max((gy1 - m_gy0) / m_gn, 1e-12);
            m_cells.assign(static_cast<size_t>(m_gn) * m_gn, {});
            for (uint32_t i = 0; i < m_polys.size(); ++i)
            {
                const Poly& p = m_polys[i];
                const int cx0 = Cell(p.minX, m_gx0, m_cw), cx1 = Cell(p.maxX, m_gx0, m_cw);
                const int cy0 = Cell(p.minY, m_gy0, m_ch), cy1 = Cell(p.maxY, m_gy0, m_ch);
                for (int y = cy0; y <= cy1; ++y)
                    for (int x = cx0; x <= cx1; ++x)
                        m_cells[static_cast<size_t>(y) * m_gn + x].push_back(i);
            }
            m_stamp.assign(m_polys.size(), 0);
            m_stampId = 0;
        }

        int Cell(double v, double origin, double size) const
        {
            return std::clamp(static_cast<int>(std::floor((v - origin) / size)), 0, m_gn - 1);
        }

        // 收集外包框与 [x0,x1]×[y0,y1] 相交、且出现在下标 index 之后的遮罩到 m_cand（按加入顺序）
        void Candidates(double x0, double y0, double x1, double y1, size_t index, int stream)
        {
            m_cand.clear();
            if (x1 < m_gx0 || x0 > m_gx1 || y1 < m_gy0 || y0 > m_gy1) return;
            if (++m_stampId == 0)
            {
                std::fill(m_stamp.begin(), m_stamp.end(), 0);
                m_stampId = 1;
            }
            const int cx0 = Cell(x0, m_gx0, m_cw), cx1 = Cell(x1, m_gx0, m_cw);
            const int cy0 = Cell(y0, m_gy0, m_ch), cy1 = Cell(y1, m_gy0, m_ch);
            for (int y = cy0; y <= cy1; ++y)
                for (int x = cx0; x <= cx1; ++x)
                    for (uint32_t i : m_cells[static_cast<size_t>(y) * m_gn + x])
                    {
                        if (m_stamp[i] == m_stampId) continue;
                        m_stamp[i] = m_stampId;
                        if (m_ends[static_cast<size_t>(i) * m_streams + stream] > index && m_polys[i].BoxOverlaps(x0, y0, x1, y1))
                            m_cand.push_back(i);
                    }
            std::sort(m_cand.begin(), m_cand.end());
        }

        int                             m_streams;
        std::vector<Poly>               m_polys;
        std::vector<size_t>             m_ends;     // [遮罩 × 顶点流]
        bool                            m_gridDirty = true;
        double                          m_gx0 = 0, m_gy0 = 0, m_gx1 = 0, m_gy1 = 0, m_cw = 1, m_ch = 1;
        int                             m_gn = 1;
        std::vector<std::vector<uint32_t>> m_cells;
        std::vector<uint32_t>           m_stamp;
        uint32_t                        m_stampId = 0;
        std::vector<uint32_t>           m_cand;
        std::vector<detail::Piece>      m_pieces, m_nextPieces;   // 填充裁剪的临时缓冲（复用容量）
        detail::Piece                   m_cur, m_tmp;
        std::vector<const Poly*>        m_nonConvex;
    };
}
