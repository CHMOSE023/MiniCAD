#pragma once
#include "Polyline.hpp"
#include "../Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniCAD::PolylineOps
{
    // =========================================================================
    // 多段线的「分段 / 部分曲线」表示
    //
    // 多段线没有单一的参数域，这里给它一套统一的参数 u ∈ [0, N]（N = 段数）：
    //   整数部分 = 段号 i，小数部分 = 沿第 i 段的进度 f ∈ [0, 1]
    //     · 直线段：f 按弦长线性
    //     · 圆弧段（bulge ≠ 0）：f 按角度线性（弧上单调推进，与 ICurve 的约定一致）
    //   u = i 恰好是第 i 个顶点。
    //
    // 在这套参数上提供：求点 / 投影求参数 / 取子段 Sub(u0, u1)（部分弧的 bulge 按包角重新计算）/
    // 首尾相接 Join / 按参数区间去掉一段 RemoveBetween（修剪、打断都用它）。
    // 首尾重合的多段线视为封闭环（IsRing），参数区间可以绕过首尾接缝。
    // =========================================================================
    inline constexpr double kParamEps = 1e-9;

    inline double ParamCount(const Polyline& pl) { return static_cast<double>(std::max(0, pl.SegCount())); }

    inline bool IsRing(const Polyline& pl)
    {
        if (pl.Points.size() < 3) return false;
        const auto& a = pl.Points.front();
        const auto& b = pl.Points.back();
        return std::hypot(a.x - b.x, a.y - b.y) < 1e-9;
    }

    namespace detail
    {
        // 段 i 上进度 f 处的点（f 取 0 / 1 时直接返回顶点，避免浮点误差）
        inline Math::Point3 SegPoint(const Polyline& pl, int i, double f)
        {
            const Math::Point3& A = pl.Points[static_cast<size_t>(i)];
            const Math::Point3& B = pl.Points[static_cast<size_t>(i) + 1];
            if (f <= 0.0) return A;
            if (f >= 1.0) return B;
            if (!pl.SegIsArc(i))
                return { A.x + (B.x - A.x) * f, A.y + (B.y - A.y) * f, A.z + (B.z - A.z) * f };
            const auto g = Polyline::ComputeArc(A, B, pl.SegBulge(i));
            if (g.Radius < Math::LengthEPS)
                return { A.x + (B.x - A.x) * f, A.y + (B.y - A.y) * f, A.z + (B.z - A.z) * f };
            const double ang = g.StartAngle + g.SweepAngle * f;
            return { g.Center.x + g.Radius * std::cos(ang), g.Center.y + g.Radius * std::sin(ang), A.z + (B.z - A.z) * f };
        }

        inline void Split(const Polyline& pl, double u, int& seg, double& f)
        {
            const int n = pl.SegCount();
            u = std::clamp(u, 0.0, static_cast<double>(n));
            seg = std::min(static_cast<int>(std::floor(u)), n - 1);
            f = u - seg;
        }

        inline double PartialBulge(double bulge, double f0, double f1)
        {
            if (std::abs(bulge) < 1e-12) return 0.0;
            const double theta = 4.0 * std::atan(std::abs(bulge)) * (f1 - f0);
            return (bulge > 0 ? 1.0 : -1.0) * std::tan(theta / 4.0);
        }

        // 去掉退化（长度为零）的段后，是否还是有效多段线
        inline bool Usable(const Polyline& pl)
        {
            if (pl.Points.size() < 2) return false;
            return pl.Length() > 1e-9;
        }
    }

    // 参数 u 处的点（u 夹在 [0, N]）
    inline Math::Point3 PointAt(const Polyline& pl, double u)
    {
        if (pl.Points.empty()) return {};
        if (pl.Points.size() == 1) return pl.Points[0];
        int seg; double f;
        detail::Split(pl, u, seg, f);
        return detail::SegPoint(pl, seg, f);
    }

    // 参数 u 处的切向（沿参数增大的方向，未归一化）
    inline Math::Vec3 TangentAt(const Polyline& pl, double u)
    {
        if (pl.Points.size() < 2) return {};
        int seg; double f;
        detail::Split(pl, u, seg, f);
        const Math::Point3& A = pl.Points[static_cast<size_t>(seg)];
        const Math::Point3& B = pl.Points[static_cast<size_t>(seg) + 1];
        if (!pl.SegIsArc(seg))
            return { B.x - A.x, B.y - A.y, B.z - A.z };
        const auto g = Polyline::ComputeArc(A, B, pl.SegBulge(seg));
        const double ang = g.StartAngle + g.SweepAngle * f;
        const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
        return { -std::sin(ang) * g.Radius * dir, std::cos(ang) * g.Radius * dir, 0.0 };
    }

    // 点 p 投影到多段线上最近处的参数（总是落在 [0, N]）
    inline double ParamOf(const Polyline& pl, const Math::Point3& p)
    {
        double bestU = 0.0, bestD = 1e300;
        for (int i = 0; i < pl.SegCount(); ++i)
        {
            const Math::Point3& A = pl.Points[static_cast<size_t>(i)];
            const Math::Point3& B = pl.Points[static_cast<size_t>(i) + 1];
            double f = 0.0;
            if (!pl.SegIsArc(i))
            {
                const double dx = B.x - A.x, dy = B.y - A.y;
                const double len2 = dx * dx + dy * dy;
                f = len2 < 1e-24 ? 0.0 : std::clamp(((p.x - A.x) * dx + (p.y - A.y) * dy) / len2, 0.0, 1.0);
            }
            else
            {
                const auto g = Polyline::ComputeArc(A, B, pl.SegBulge(i));
                if (g.Radius < Math::LengthEPS)
                    f = 0.0;
                else
                {
                    const double ang = std::atan2(p.y - g.Center.y, p.x - g.Center.x);
                    const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
                    double d = std::fmod(dir * (ang - g.StartAngle), Math::TwoPI);
                    if (d < 0) d += Math::TwoPI;
                    f = d / std::abs(g.SweepAngle);
                    if (f > 1.0)    // 投影角落在弧之外：取离得近的那个端点
                    {
                        const double toStart = std::hypot(p.x - A.x, p.y - A.y);
                        const double toEnd   = std::hypot(p.x - B.x, p.y - B.y);
                        f = toEnd < toStart ? 1.0 : 0.0;
                    }
                }
            }
            const Math::Point3 q = detail::SegPoint(pl, i, f);
            const double dist = std::hypot(p.x - q.x, p.y - q.y);
            if (dist < bestD) { bestD = dist; bestU = i + f; }
        }
        return bestU;
    }

    // 参数区间 [u0, u1] 之间的一段（0 ≤ u0 < u1 ≤ N，会被夹到范围内）；部分弧段的 bulge 按包角重新计算。
    // 区间退化时返回空多段线
    inline Polyline Sub(const Polyline& pl, double u0, double u1)
    {
        Polyline out;
        const int n = pl.SegCount();
        if (n < 1) return out;
        u0 = std::clamp(u0, 0.0, static_cast<double>(n));
        u1 = std::clamp(u1, 0.0, static_cast<double>(n));
        if (u1 - u0 < kParamEps) return out;

        for (int i = static_cast<int>(std::floor(u0)); i < n && i < u1; ++i)
        {
            const double fa = std::max(u0, static_cast<double>(i)) - i;
            const double fb = std::min(u1, static_cast<double>(i + 1)) - i;
            if (fb - fa < kParamEps) continue;
            if (out.Points.empty())
                out.Points.push_back(detail::SegPoint(pl, i, fa));
            out.Points.push_back(detail::SegPoint(pl, i, fb));
            out.Bulges.push_back(detail::PartialBulge(pl.SegBulge(i), fa, fb));
        }
        return out;
    }

    // a 的终点接 b 的起点（两点应重合，b 的起点被丢弃）
    inline Polyline Join(const Polyline& a, const Polyline& b)
    {
        if (a.Points.empty()) return b;
        if (b.Points.empty()) return a;
        Polyline out = a;
        out.Points.insert(out.Points.end(), b.Points.begin() + 1, b.Points.end());
        out.Bulges.insert(out.Bulges.end(), b.Bulges.begin(), b.Bulges.end());
        return out;
    }

    // 按参数区间去掉一段，返回剩下的 0～2 段（打断 / 修剪用）。
    //   · 开放多段线：u1 ≤ u2 时去掉 [u1, u2]，剩下 [0, u1] 和 [u2, N]；u1 > u2 时两点互换
    //   · 封闭环：沿参数增大的方向从 u1 去掉到 u2（可以绕过首尾接缝），剩下一段开放多段线
    //   · u1 ≈ u2：开放多段线在该点断成两段（点在端点上则不变）；封闭环在该点断开成一条开放多段线
    // 剩下的段长度为零时不返回。
    inline std::vector<Polyline> RemoveBetween(const Polyline& pl, double u1, double u2)
    {
        std::vector<Polyline> out;
        const double N = ParamCount(pl);
        if (N < 1) return out;
        u1 = std::clamp(u1, 0.0, N);
        u2 = std::clamp(u2, 0.0, N);

        auto keep = [&](Polyline p)
        {
            if (detail::Usable(p)) out.push_back(std::move(p));
        };

        if (IsRing(pl))
        {
            if (std::abs(u1 - u2) < kParamEps)
            {
                keep(Join(Sub(pl, u1, N), Sub(pl, 0.0, u1)));           // 在该点断开：从该点出发绕一圈
            }
            else if (u1 < u2)
                keep(Join(Sub(pl, u2, N), Sub(pl, 0.0, u1)));           // 去掉 [u1,u2]，剩下 u2 → 接缝 → u1
            else
                keep(Sub(pl, u2, u1));                                   // 去掉的区间绕过接缝，剩下 [u2, u1]
            return out;
        }

        if (u1 > u2) std::swap(u1, u2);
        keep(Sub(pl, 0.0, u1));
        keep(Sub(pl, u2, N));
        if (std::abs(u1 - u2) < kParamEps && out.size() < 2)
            out.clear();            // 在端点处「断开」没有意义：保持不变
        return out;
    }

    // RemoveBetween 实际去掉的那一段（预览用）：u1 ≈ u2 时为空；封闭环沿参数增大的方向从 u1 到 u2，可绕过接缝
    inline Polyline RemovedSpan(const Polyline& pl, double u1, double u2)
    {
        const double N = ParamCount(pl);
        u1 = std::clamp(u1, 0.0, N);
        u2 = std::clamp(u2, 0.0, N);
        if (std::abs(u1 - u2) < kParamEps)
            return {};
        if (IsRing(pl) && u1 > u2)
            return Join(Sub(pl, u1, N), Sub(pl, 0.0, u2));
        return Sub(pl, std::min(u1, u2), std::max(u1, u2));
    }

    // 把开放多段线的起点（atEnd = false）或终点（atEnd = true）移到 newPt。
    // 端点所在的段是圆弧时，newPt 应落在这段圆弧所在的圆上：保持半径和方向不变，按新的包角重新计算 bulge（延伸圆弧段用）。
    inline Polyline MoveEndpoint(const Polyline& pl, bool atEnd, const Math::Point3& newPt)
    {
        Polyline out = pl;
        const int n = pl.SegCount();
        if (n < 1) return out;
        const int seg = atEnd ? n - 1 : 0;
        const size_t vi = atEnd ? static_cast<size_t>(n) : 0;

        if (pl.SegIsArc(seg))
        {
            const Math::Point3& A = pl.Points[static_cast<size_t>(seg)];
            const Math::Point3& B = pl.Points[static_cast<size_t>(seg) + 1];
            const auto g = Polyline::ComputeArc(A, B, pl.SegBulge(seg));
            if (g.Radius > Math::LengthEPS)
            {
                const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
                const double aNew = std::atan2(newPt.y - g.Center.y, newPt.x - g.Center.x);
                const Math::Point3& fixed = atEnd ? A : B;
                const double aFixed = std::atan2(fixed.y - g.Center.y, fixed.x - g.Center.x);
                // 沿弧的方向，从起点转到终点的包角
                double span = std::fmod(atEnd ? dir * (aNew - aFixed) : dir * (aFixed - aNew), Math::TwoPI);
                if (span < 0) span += Math::TwoPI;
                const double sign = pl.SegBulge(seg) > 0 ? 1.0 : -1.0;
                out.Bulges[static_cast<size_t>(seg)] = sign * std::tan(span / 4.0);
            }
        }
        out.Points[vi] = newPt;
        return out;
    }

    // ── 偏移 ─────────────────────────────────────────────────────────────────
    // 每一段各自偏移（直线 → 平行线；圆弧 → 同心圆弧，半径随偏移方向增减），相邻两段的偏移曲线求交得到新顶点
    // （直线与直线是尖角延伸到交点）；没有交点时用一小段直线连接。
    // dist > 0 偏向前进方向的左侧，< 0 偏向右侧。偏移后方向反过来的段（被挤没了）会被去掉；
    // 圆弧偏到半径 ≤ 0 的段也去掉；全部去掉返回空。封闭环偏移后仍是封闭环。
    // 暂不处理偏移后曲线之间的全局自相交（AutoCAD 会修剪掉）。
    namespace detail
    {
        struct V2 { double x = 0, y = 0; };
        inline V2 operator+(V2 a, V2 b) { return { a.x + b.x, a.y + b.y }; }
        inline V2 operator-(V2 a, V2 b) { return { a.x - b.x, a.y - b.y }; }
        inline V2 operator*(V2 a, double k) { return { a.x * k, a.y * k }; }
        inline double Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
        inline double Cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
        inline double Len(V2 a) { return std::hypot(a.x, a.y); }
        inline V2 Xy(const Math::Point3& p) { return { p.x, p.y }; }

        struct OffSeg
        {
            bool   arc = false;
            V2     p0, p1;                // 偏移后的起 / 终点（之后会被求交的结果改写）
            V2     c;  double r = 0;      // 圆弧：圆心、偏移后的半径
            double dir = 1.0;             // 圆弧：+1 逆时针，-1 顺时针
            double bulgeSign = 1.0;       // 原 bulge 的符号
            V2     u;                     // 直线：单位方向
            double origSweep = 0.0;       // 圆弧：原来的包角（正值）
            V2     origDir;               // 直线：原方向（判断是否被挤反）
        };

        // 两条偏移曲线（无限延伸）的交点，取离 hint 最近的一个
        inline bool Meet(const OffSeg& a, const OffSeg& b, V2 hint, V2& out)
        {
            std::vector<V2> cand;
            auto lineLine = [&](const OffSeg& l1, const OffSeg& l2)
            {
                const double den = Cross(l1.u, l2.u);
                if (std::abs(den) < 1e-12) return;
                const double t = Cross(l2.p0 - l1.p0, l2.u) / den;
                cand.push_back(l1.p0 + l1.u * t);
            };
            auto lineCircle = [&](const OffSeg& l, const OffSeg& c)
            {
                const V2 f = l.p0 - c.c;
                const double bb = Dot(f, l.u);
                const double disc = bb * bb - (Dot(f, f) - c.r * c.r);
                if (disc < 0) return;
                const double sq = std::sqrt(disc);
                cand.push_back(l.p0 + l.u * (-bb + sq));
                if (sq > 1e-12) cand.push_back(l.p0 + l.u * (-bb - sq));
            };
            auto circleCircle = [&](const OffSeg& c1, const OffSeg& c2)
            {
                const V2 dv = c2.c - c1.c;
                const double d = Len(dv);
                if (d < 1e-12) return;
                const double aa = (c1.r * c1.r - c2.r * c2.r + d * d) / (2 * d);
                const double h2 = c1.r * c1.r - aa * aa;
                if (h2 < 0) return;
                const double h = std::sqrt(h2);
                const V2 uu = dv * (1.0 / d);
                const V2 mid = c1.c + uu * aa;
                const V2 n{ -uu.y, uu.x };
                cand.push_back(mid + n * h);
                if (h > 1e-12) cand.push_back(mid - n * h);
            };
            if (!a.arc && !b.arc)      lineLine(a, b);
            else if (!a.arc && b.arc)  lineCircle(a, b);
            else if (a.arc && !b.arc)  lineCircle(b, a);
            else                       circleCircle(a, b);

            if (cand.empty()) return false;
            double best = 1e300;
            for (const V2& c : cand)
            {
                const double d = Len(c - hint);
                if (d < best) { best = d; out = c; }
            }
            return true;
        }

        // 段 s 的包角（沿它自己的方向从 p0 转到 p1）
        inline double ArcSpan(const OffSeg& s)
        {
            const double a0 = std::atan2(s.p0.y - s.c.y, s.p0.x - s.c.x);
            const double a1 = std::atan2(s.p1.y - s.c.y, s.p1.x - s.c.x);
            double d = std::fmod(s.dir * (a1 - a0), Math::TwoPI);
            if (d < 0) d += Math::TwoPI;
            return d;
        }
    }

    inline Polyline Offset(const Polyline& pl, double dist)
    {
        using namespace detail;
        Polyline result;
        if (pl.SegCount() < 1 || std::abs(dist) < 1e-12)
            return result;
        const bool ring = IsRing(pl);
        const double z = pl.Points.front().z;

        // 1. 每段各自偏移
        std::vector<OffSeg> segs;
        for (int i = 0; i < pl.SegCount(); ++i)
        {
            const V2 A = Xy(pl.Points[static_cast<size_t>(i)]), B = Xy(pl.Points[static_cast<size_t>(i) + 1]);
            OffSeg s;
            if (!pl.SegIsArc(i))
            {
                const V2 d = B - A;
                const double len = Len(d);
                if (len < 1e-12) continue;                              // 零长度的段直接跳过
                s.u = d * (1.0 / len);
                s.origDir = s.u;
                const V2 n{ -s.u.y, s.u.x };
                s.p0 = A + n * dist;
                s.p1 = B + n * dist;
            }
            else
            {
                const auto g = Polyline::ComputeArc(pl.Points[static_cast<size_t>(i)], pl.Points[static_cast<size_t>(i) + 1], pl.SegBulge(i));
                if (g.Radius < 1e-12) continue;
                const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
                const double r = g.Radius - dist * dir;                 // 逆时针弧的左侧是圆心：向左偏移半径变小
                if (r < 1e-9) continue;                                 // 偏到圆心另一侧，这段没有了
                s.arc = true;
                s.c = { g.Center.x, g.Center.y };
                s.r = r;
                s.dir = dir;
                s.bulgeSign = pl.SegBulge(i) > 0 ? 1.0 : -1.0;
                s.origSweep = std::abs(g.SweepAngle);
                const double k = r / g.Radius;
                s.p0 = s.c + (A - s.c) * k;
                s.p1 = s.c + (B - s.c) * k;
            }
            segs.push_back(s);
        }

        // 2. 相邻两段求交得到新顶点；被挤反的段去掉后重新求交
        struct Joint { V2 out, in; bool joined = false; };
        for (size_t guard = 0; guard <= pl.Points.size() && !segs.empty(); ++guard)
        {
            const size_t m = segs.size();
            std::vector<OffSeg> work = segs;
            std::vector<Joint> joints(m + 1);          // joints[k] = 第 k-1 段与第 k 段之间（开放：0 和 m 是两端）
            for (size_t k = 0; k <= m; ++k)
            {
                if (!ring && (k == 0 || k == m))
                {
                    joints[k].joined = true;
                    joints[k].out = joints[k].in = (k == 0) ? work.front().p0 : work.back().p1;
                    continue;
                }
                if (ring && k == m) { joints[m] = joints[0]; continue; }
                const OffSeg& a = work[k == 0 ? m - 1 : k - 1];
                const OffSeg& b = work[k];
                V2 x;
                if (Meet(a, b, a.p1, x)) { joints[k].joined = true; joints[k].out = joints[k].in = x; }
                else { joints[k].out = a.p1; joints[k].in = b.p0; }
            }
            // 把求出的顶点写回各段
            for (size_t k = 0; k < m; ++k)
            {
                work[k].p0 = joints[k].in;
                work[k].p1 = joints[k + 1].out;
            }

            // 找第一个被挤反的段
            size_t reversed = m;
            for (size_t k = 0; k < m && reversed == m; ++k)
            {
                const OffSeg& s = work[k];
                if (!s.arc ? Dot(s.p1 - s.p0, s.origDir) < -1e-9 : ArcSpan(s) > s.origSweep + Math::PI)
                    reversed = k;
            }
            if (reversed != m)
            {
                segs.erase(segs.begin() + static_cast<std::ptrdiff_t>(reversed));
                continue;
            }

            // 3. 生成多段线
            auto P3 = [&](V2 v) { return Math::Point3{ v.x, v.y, z }; };
            for (size_t k = 0; k < m; ++k)
            {
                const OffSeg& s = work[k];
                if (result.Points.empty())
                    result.Points.push_back(P3(s.p0));
                result.Points.push_back(P3(s.p1));
                result.Bulges.push_back(s.arc ? s.bulgeSign * std::tan(ArcSpan(s) / 4.0) : 0.0);

                // 两段没有交点：用一小段直线接到下一段的起点
                const Joint& next = joints[k + 1];
                if (!next.joined && (k + 1 < m || ring))
                {
                    result.Points.push_back(P3(next.in));
                    result.Bulges.push_back(0.0);
                }
            }
            if (ring && result.Points.size() >= 2)
            {
                const auto f = result.Points.front();
                const auto l = result.Points.back();
                if (std::hypot(f.x - l.x, f.y - l.y) > 1e-9)      // 保证首尾重合
                {
                    result.Points.push_back(f);
                    result.Bulges.push_back(0.0);
                }
            }
            return result;
        }
        return result;
    }
}
