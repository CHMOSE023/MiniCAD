#include "NodeGraph/CurveOps.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniCAD::Nodes::CurveOps
{
    using Solid::NurbsCurve;

    namespace
    {
        constexpr double kPi    = std::numbers::pi;
        constexpr double kTwoPi = 2.0 * kPi;

        // 齐次坐标的控制点（有理曲线的插入节点、细分都在齐次坐标里做）
        struct H
        {
            double x = 0, y = 0, z = 0, w = 1;
        };

        H Mix(const H& a, const H& b, double t)   // (1 − t)·a + t·b
        {
            return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
        }

        std::vector<H> ToH(const NurbsCurve& c)
        {
            std::vector<H> h(c.ControlPoints.size());
            for (size_t i = 0; i < h.size(); ++i)
            {
                const double w = c.IsRational() ? c.Weights[i] : 1.0;
                const auto&  p = c.ControlPoints[i];
                h[i] = { p.x * w, p.y * w, p.z * w, w };
            }
            return h;
        }

        void FromH(NurbsCurve& c, const std::vector<H>& h, bool rational)
        {
            c.ControlPoints.resize(h.size());
            c.Weights.clear();
            for (size_t i = 0; i < h.size(); ++i)
            {
                const double w = h[i].w != 0.0 ? h[i].w : 1.0;
                c.ControlPoints[i] = { h[i].x / w, h[i].y / w, h[i].z / w };
                if (rational)
                    c.Weights.push_back(h[i].w);
            }
        }

        double KnotEps(const NurbsCurve& c) { return std::max(1e-14, (c.EndParam() - c.StartParam()) * 1e-12); }

        bool IsClamped(const NurbsCurve& c)
        {
            const size_t p = static_cast<size_t>(c.Degree);
            for (size_t i = 1; i <= p; ++i)
                if (c.Knots[i] != c.Knots[0] || c.Knots[c.Knots.size() - 1 - i] != c.Knots.back())
                    return false;
            return true;
        }

        // 节点区间与非零基函数（Piegl & Tiller A2.1 / A2.2）
        int FindSpan(int n, int p, double u, const std::vector<double>& U)
        {
            if (u >= U[n + 1])
                return n;
            if (u <= U[p])
                return p;
            int lo = p, hi = n + 1, mid = (lo + hi) / 2;
            while (u < U[mid] || u >= U[mid + 1])
            {
                if (u < U[mid])
                    hi = mid;
                else
                    lo = mid;
                mid = (lo + hi) / 2;
            }
            return mid;
        }

        void BasisFuns(int i, double u, int p, const std::vector<double>& U, double* N)
        {
            double left[16], right[16];
            N[0] = 1.0;
            for (int j = 1; j <= p; ++j)
            {
                left[j]  = u - U[i + 1 - j];
                right[j] = U[i + j] - u;
                double saved = 0.0;
                for (int r = 0; r < j; ++r)
                {
                    const double denom = right[r + 1] + left[j - r];
                    const double temp  = denom != 0.0 ? N[r] / denom : 0.0;
                    N[r]  = saved + right[r + 1] * temp;
                    saved = left[j - r] * temp;
                }
                N[j] = saved;
            }
        }

        Math::Point3 Mid(const Math::Point3& a, const Math::Point3& b) { return { (a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2 }; }
    }

    // ── 曲线实体 ↔ 多段线 ─────────────────────────────────────────────

    std::optional<Polyline> FlatPolyline(const Entity& e)
    {
        if (!e.HasDefaultExtrusion())
            return std::nullopt;
        if (e.IsKindOf<LineEntity>())
        {
            const Line& l = static_cast<const LineEntity&>(e).GetLine();
            if (std::abs(l.Start.z - l.End.z) > 1e-9 || (l.End - l.Start).Length() < 1e-12)
                return std::nullopt;
            return Polyline({ l.Start, l.End });
        }
        if (e.IsKindOf<ArcEntity>())
        {
            const Arc& a = static_cast<const ArcEntity&>(e).GetArc();
            double sweep = std::fmod(a.EndAngle - a.StartAngle, kTwoPi);
            if (sweep <= 1e-12)
                sweep += kTwoPi;
            if (a.Radius <= 1e-12 || sweep >= kTwoPi - 1e-12)
                return std::nullopt;
            const Math::Point3 p0{ a.Center.x + a.Radius * std::cos(a.StartAngle), a.Center.y + a.Radius * std::sin(a.StartAngle), a.Center.z };
            const Math::Point3 p1{ a.Center.x + a.Radius * std::cos(a.StartAngle + sweep), a.Center.y + a.Radius * std::sin(a.StartAngle + sweep), a.Center.z };
            return Polyline({ p0, p1 }, { std::tan(sweep / 4.0) });
        }
        if (e.IsKindOf<CircleEntity>())
        {
            const Circle& c = static_cast<const CircleEntity&>(e).GetCircle();
            if (c.Radius <= 1e-12)
                return std::nullopt;
            const Math::Point3 a{ c.Center.x + c.Radius, c.Center.y, c.Center.z }, b{ c.Center.x - c.Radius, c.Center.y, c.Center.z };
            return Polyline({ a, b, a }, { 1.0, 1.0 });
        }
        if (e.IsKindOf<PolylineEntity>())
        {
            Polyline pl = static_cast<const PolylineEntity&>(e).GetPolyline();
            if (!pl.IsValid())
                return std::nullopt;
            for (const auto& p : pl.Points)
                if (std::abs(p.z - pl.Points.front().z) > 1e-9)
                    return std::nullopt;
            pl.Bulges.resize(pl.Points.size() - 1, 0.0);
            return pl;
        }
        return std::nullopt;
    }

    CurvePtr FromPolyline(Polyline pl, const Entity* attr)
    {
        if (pl.Points.size() < 2)
            return nullptr;
        pl.Bulges.resize(pl.Points.size() - 1, 0.0);
        // 去掉长度为零的段
        for (size_t i = 0; i + 1 < pl.Points.size();)
        {
            if (pl.Points.size() > 2 && std::hypot(pl.Points[i + 1].x - pl.Points[i].x, pl.Points[i + 1].y - pl.Points[i].y) < 1e-12)
            {
                pl.Points.erase(pl.Points.begin() + static_cast<std::ptrdiff_t>(i + 1));
                pl.Bulges.erase(pl.Bulges.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
        // 合并相邻的同圆圆弧（同向、合起来不到整圈）
        for (size_t i = 0; i + 2 < pl.Points.size();)
        {
            const double b1 = pl.Bulges[i], b2 = pl.Bulges[i + 1];
            if (std::abs(b1) > 1e-12 && std::abs(b2) > 1e-12 && (b1 > 0) == (b2 > 0))
            {
                const auto g1 = Polyline::ComputeArc(pl.Points[i], pl.Points[i + 1], b1);
                const auto g2 = Polyline::ComputeArc(pl.Points[i + 1], pl.Points[i + 2], b2);
                const double tol = 1e-9 * std::max(1.0, g1.Radius);
                const double sweep = std::abs(g1.SweepAngle) + std::abs(g2.SweepAngle);
                if (std::hypot(g1.Center.x - g2.Center.x, g1.Center.y - g2.Center.y) < tol && std::abs(g1.Radius - g2.Radius) < tol && sweep < kTwoPi - 1e-6)
                {
                    pl.Bulges[i] = (b1 > 0 ? 1.0 : -1.0) * std::tan(sweep / 4.0);
                    pl.Points.erase(pl.Points.begin() + static_cast<std::ptrdiff_t>(i + 1));
                    pl.Bulges.erase(pl.Bulges.begin() + static_cast<std::ptrdiff_t>(i + 1));
                    continue;
                }
            }
            ++i;
        }

        std::shared_ptr<Entity> out;
        const int segs = pl.SegCount();
        if (segs == 1 && !pl.SegIsArc(0))
            out = std::make_shared<LineEntity>(Object::InvalidID, pl.Points[0], pl.Points[1]);
        else if (segs == 1 && pl.Bulges[0] > 0)
        {
            const auto g = Polyline::ComputeArc(pl.Points[0], pl.Points[1], pl.Bulges[0]);
            out = std::make_shared<ArcEntity>(Object::InvalidID, Math::Point3{ g.Center.x, g.Center.y, pl.Points[0].z }, g.Radius,
                                              g.StartAngle, g.StartAngle + g.SweepAngle);
        }
        else if (segs == 2 && PolylineOps::IsRing(pl) && pl.Bulges[0] > 0 && pl.Bulges[1] > 0)
        {
            const auto g1 = Polyline::ComputeArc(pl.Points[0], pl.Points[1], pl.Bulges[0]);
            const auto g2 = Polyline::ComputeArc(pl.Points[1], pl.Points[2], pl.Bulges[1]);
            const double tol = 1e-9 * std::max(1.0, g1.Radius);
            if (std::hypot(g1.Center.x - g2.Center.x, g1.Center.y - g2.Center.y) < tol && std::abs(g1.Radius - g2.Radius) < tol)
                out = std::make_shared<CircleEntity>(Object::InvalidID, Math::Point3{ g1.Center.x, g1.Center.y, pl.Points[0].z }, g1.Radius);
        }
        if (!out)
            out = std::make_shared<PolylineEntity>(Object::InvalidID, std::move(pl));
        if (attr)
        {
            EntityAttr a = attr->GetAttr();
            a.Extrusion = { 0, 0, 1 };
            out->SetAttr(a);
        }
        return out;
    }

    Polyline Reversed(const Polyline& pl)
    {
        Polyline r;
        r.Points.assign(pl.Points.rbegin(), pl.Points.rend());
        for (int i = pl.SegCount() - 1; i >= 0; --i)
            r.Bulges.push_back(-pl.SegBulge(i));
        return r;
    }

    double SignedArea(const Polyline& pl)
    {
        double area = 0.0;
        for (int i = 0; i < pl.SegCount(); ++i)
        {
            const auto& a = pl.Points[static_cast<size_t>(i)];
            const auto& b = pl.Points[static_cast<size_t>(i) + 1];
            area += (a.x * b.y - b.x * a.y) * 0.5;
            if (pl.SegIsArc(i))
            {
                // 弓形面积：凸度为正（逆时针弧）时弧在弦的右侧，逆时针环的右侧是外面，面积增加
                const auto g = Polyline::ComputeArc(a, b, pl.SegBulge(i));
                const double th = std::abs(g.SweepAngle);
                area += (pl.SegBulge(i) > 0 ? 1.0 : -1.0) * g.Radius * g.Radius * 0.5 * (th - std::sin(th));
            }
        }
        return area;
    }

    // ── 空间曲线：分析 ───────────────────────────────────────────────

    double Lerp(const NurbsCurve& c, double t) { return c.StartParam() + (c.EndParam() - c.StartParam()) * t; }

    double Unlerp(const NurbsCurve& c, double u)
    {
        const double span = c.EndParam() - c.StartParam();
        return span > 0 ? (u - c.StartParam()) / span : 0.0;
    }

    // 先密集采样，再在相邻区间里三分搜索
    double ClosestParam(const NurbsCurve& c, const Math::Point3& p)
    {
        constexpr int kSamples = 512;
        const double u0 = c.StartParam(), u1 = c.EndParam();
        int best = 0;
        double bestD = 1e300;
        for (int i = 0; i <= kSamples; ++i)
        {
            const double d = (c.PointAt(u0 + (u1 - u0) * i / kSamples) - p).LengthSq();
            if (d < bestD)
                bestD = d, best = i;
        }
        double lo = u0 + (u1 - u0) * std::max(best - 1, 0) / kSamples;
        double hi = u0 + (u1 - u0) * std::min(best + 1, kSamples) / kSamples;
        for (int it = 0; it < 80; ++it)
        {
            const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
            if ((c.PointAt(m1) - p).LengthSq() < (c.PointAt(m2) - p).LengthSq())
                hi = m2;
            else
                lo = m1;
        }
        return (lo + hi) * 0.5;
    }

    // 折线段两两求最近点，距离在容差内的再用牛顿法在 (u, v) 上细化
    std::vector<std::pair<double, double>> IntersectCurves(const NurbsCurve& a, const NurbsCurve& b, double tol)
    {
        constexpr int kSeg = 256;
        auto sample = [](const NurbsCurve& c)
        {
            std::vector<std::pair<double, Math::Point3>> pts;
            for (int i = 0; i <= kSeg; ++i)
            {
                const double u = c.StartParam() + (c.EndParam() - c.StartParam()) * i / kSeg;
                pts.push_back({ u, c.PointAt(u) });
            }
            return pts;
        };
        const auto pa = sample(a), pb = sample(b);
        std::vector<std::pair<double, double>> hits;
        for (int i = 0; i < kSeg; ++i)
            for (int j = 0; j < kSeg; ++j)
            {
                // 两条线段的最近点
                const Math::Vec3 d1 = pa[i + 1].second - pa[i].second, d2 = pb[j + 1].second - pb[j].second, r = pa[i].second - pb[j].second;
                const double aa = Math::Dot(d1, d1), ee = Math::Dot(d2, d2), f = Math::Dot(d2, r), c = Math::Dot(d1, r), bb = Math::Dot(d1, d2);
                const double denom = aa * ee - bb * bb;
                double s = denom > 1e-18 ? std::clamp((bb * f - c * ee) / denom, 0.0, 1.0) : 0.0;
                double t = ee > 1e-18 ? std::clamp((bb * s + f) / ee, 0.0, 1.0) : 0.0;
                s = aa > 1e-18 ? std::clamp((bb * t - c) / aa, 0.0, 1.0) : 0.0;
                const Math::Vec3 gap = (pa[i].second + d1 * s) - (pb[j].second + d2 * t);
                const double seg = std::max(std::sqrt(aa), std::sqrt(ee));
                if (gap.Length() > tol + seg * 0.5)
                    continue;
                // 牛顿法：最小化 |A(u) − B(v)|²
                double u = pa[i].first + (pa[i + 1].first - pa[i].first) * s;
                double v = pb[j].first + (pb[j + 1].first - pb[j].first) * t;
                for (int it = 0; it < 30; ++it)
                {
                    const Math::Vec3 e = a.PointAt(u) - b.PointAt(v);
                    const Math::Vec3 du = a.DerivativeAt(u), dv = b.DerivativeAt(v);
                    // 正规方程 J^T J Δ = −J^T e，J = [du, −dv]
                    const double m11 = Math::Dot(du, du), m12 = -Math::Dot(du, dv), m22 = Math::Dot(dv, dv);
                    const double r1 = -Math::Dot(du, e), r2 = Math::Dot(dv, e);
                    const double det = m11 * m22 - m12 * m12;
                    if (std::abs(det) < 1e-20)
                        break;
                    const double du_ = (r1 * m22 - m12 * r2) / det, dv_ = (m11 * r2 - m12 * r1) / det;
                    u = std::clamp(u + du_, a.StartParam(), a.EndParam());
                    v = std::clamp(v + dv_, b.StartParam(), b.EndParam());
                    if (std::abs(du_) + std::abs(dv_) < 1e-14)
                        break;
                }
                if ((a.PointAt(u) - b.PointAt(v)).Length() > tol)
                    continue;
                const Math::Point3 hp = a.PointAt(u);
                const bool dup = std::any_of(hits.begin(), hits.end(), [&](const auto& h) { return (a.PointAt(h.first) - hp).Length() <= tol * 10; });
                if (!dup)
                    hits.push_back({ u, v });
            }
        std::sort(hits.begin(), hits.end());
        return hits;
    }

    // ── 空间曲线：修改 ───────────────────────────────────────────────

    // Boehm 插入节点（Piegl & Tiller A5.1，每次插入一个）
    void InsertKnot(NurbsCurve& c, double u, int times)
    {
        const bool rational = c.IsRational();
        std::vector<H> P = ToH(c);
        const int p = c.Degree;
        for (int t = 0; t < times; ++t)
        {
            const int n = static_cast<int>(P.size());
            int k = p;   // 最后一个满足 U[k] ≤ u 的节点（限制在 [p, n − 1]）
            while (k < n - 1 && c.Knots[k + 1] <= u)
                ++k;
            std::vector<H> Q(n + 1);
            for (int i = 0; i <= n; ++i)
            {
                if (i <= k - p)
                    Q[i] = P[i];
                else if (i >= k + 1)
                    Q[i] = P[i - 1];
                else
                {
                    const double denom = c.Knots[i + p] - c.Knots[i];
                    const double a = denom > 0.0 ? (u - c.Knots[i]) / denom : 0.0;
                    Q[i] = Mix(P[i - 1], P[i], a);
                }
            }
            c.Knots.insert(c.Knots.begin() + k + 1, u);
            P = std::move(Q);
        }
        FromH(c, P, rational);
    }

    std::pair<NurbsCurve, NurbsCurve> SplitAt(const NurbsCurve& c, double u)
    {
        const double eps = KnotEps(c);
        if (!c.IsValid() || u <= c.StartParam() + eps)
            return { NurbsCurve{}, c };
        if (u >= c.EndParam() - eps)
            return { c, NurbsCurve{} };
        // 靠得很近的已有节点：直接用它
        int mult = 0;
        for (double k : c.Knots)
            if (std::abs(k - u) <= eps)
            {
                u = k;
                ++mult;
            }
        NurbsCurve w = c;
        const int p = c.Degree;
        if (mult < p + 1)
            InsertKnot(w, u, p + 1 - mult);
        size_t first = 0;
        while (w.Knots[first] != u)
            ++first;
        NurbsCurve left, right;
        left.Degree = right.Degree = p;
        left.Knots.assign(w.Knots.begin(), w.Knots.begin() + static_cast<std::ptrdiff_t>(first + p + 1));
        left.ControlPoints.assign(w.ControlPoints.begin(), w.ControlPoints.begin() + static_cast<std::ptrdiff_t>(first));
        right.Knots.assign(w.Knots.begin() + static_cast<std::ptrdiff_t>(first), w.Knots.end());
        right.ControlPoints.assign(w.ControlPoints.begin() + static_cast<std::ptrdiff_t>(first), w.ControlPoints.end());
        if (w.IsRational())
        {
            left.Weights.assign(w.Weights.begin(), w.Weights.begin() + static_cast<std::ptrdiff_t>(first));
            right.Weights.assign(w.Weights.begin() + static_cast<std::ptrdiff_t>(first), w.Weights.end());
        }
        return { left.IsValid() ? left : NurbsCurve{}, right.IsValid() ? right : NurbsCurve{} };
    }

    NurbsCurve SubCurve(const NurbsCurve& c, double u0, double u1)
    {
        if (!c.IsValid() || !(u1 > u0))
            return {};
        NurbsCurve r = SplitAt(c, u0).second;
        if (!r.IsValid())
            return {};
        if (u1 < r.EndParam())
            r = SplitAt(r, u1).first;
        return r;
    }

    NurbsCurve ElevateLinear(const NurbsCurve& c, int degree)
    {
        if (!c.IsValid() || c.Degree != 1 || degree < 1)
            return {};
        if (degree == 1)
            return c;
        NurbsCurve r;
        r.Degree = degree;
        // 一次曲线的第 i 个控制点在参数 Knots[i + 1] 处；每段直线写成 degree 次贝塞尔（控制点等分）
        const size_t n = c.ControlPoints.size();
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const double a = c.Knots[i + 1], b = c.Knots[i + 2];
            if (b <= a)
                continue;
            const auto& A = c.ControlPoints[i];
            const auto& B = c.ControlPoints[i + 1];
            if (r.ControlPoints.empty())
            {
                r.ControlPoints.push_back(A);
                r.Knots.insert(r.Knots.end(), degree + 1, a);
            }
            else
                r.Knots.insert(r.Knots.end(), degree, a);
            for (int k = 1; k <= degree; ++k)
            {
                const double t = static_cast<double>(k) / degree;
                r.ControlPoints.push_back({ A.x + (B.x - A.x) * t, A.y + (B.y - A.y) * t, A.z + (B.z - A.z) * t });
            }
        }
        if (r.ControlPoints.empty())
            return {};
        r.Knots.push_back(c.EndParam());
        return r.IsValid() ? r : NurbsCurve{};
    }

    NurbsCurve Concatenate(const NurbsCurve& a0, const NurbsCurve& b0)
    {
        if (!a0.IsValid() || !b0.IsValid() || !IsClamped(a0) || !IsClamped(b0))
            return {};
        NurbsCurve a = a0, b = b0;
        if (a.Degree != b.Degree)
        {
            if (a.Degree == 1)
                a = ElevateLinear(a, b.Degree);
            else if (b.Degree == 1)
                b = ElevateLinear(b, a.Degree);
            else
                return {};
            if (!a.IsValid() || !b.IsValid())
                return {};
        }
        const int p = a.Degree;
        NurbsCurve r;
        r.Degree        = p;
        r.ControlPoints = a.ControlPoints;
        r.ControlPoints.back() = Mid(a.ControlPoints.back(), b.ControlPoints.front());
        r.ControlPoints.insert(r.ControlPoints.end(), b.ControlPoints.begin() + 1, b.ControlPoints.end());
        if (a.IsRational() || b.IsRational())
        {
            std::vector<double> wa = a.IsRational() ? a.Weights : std::vector<double>(a.ControlPoints.size(), 1.0);
            std::vector<double> wb = b.IsRational() ? b.Weights : std::vector<double>(b.ControlPoints.size(), 1.0);
            // b 的齐次坐标整体缩放不改变曲线：让接点处的权重相同
            const double k = wa.back() / wb.front();
            r.Weights = wa;
            for (size_t i = 1; i < wb.size(); ++i)
                r.Weights.push_back(wb[i] * k);
        }
        r.Knots.assign(a.Knots.begin(), a.Knots.end() - 1);
        const double shift = a.Knots.back() - b.Knots.front();
        for (size_t j = static_cast<size_t>(p) + 1; j < b.Knots.size(); ++j)
            r.Knots.push_back(b.Knots[j] + shift);
        return r.IsValid() ? r : NurbsCurve{};
    }

    NurbsCurve Extend(const NurbsCurve& c, double length, bool atEnd, bool smooth)
    {
        if (!c.IsValid() || !IsClamped(c))
            return {};
        if (length <= 0.0)
            return c;
        if (!atEnd)
        {
            NurbsCurve r = Extend(c.Reversed(), length, true, smooth);
            return r.IsValid() ? r.Reversed() : r;
        }
        const int p = c.Degree;
        const Math::Point3 end = c.PointAt(c.EndParam());
        if (!smooth)
        {
            // 沿端点切线：最后一段控制多边形的方向（夹持曲线的端点切线方向）
            Math::Vec3 dir;
            for (size_t i = c.ControlPoints.size() - 1; i > 0 && dir.Length() < 1e-12; --i)
                dir = c.ControlPoints.back() - c.ControlPoints[i - 1];
            if (dir.Length() < 1e-12)
                return {};
            dir = dir.Normalized();
            std::vector<Math::Point3> pts;
            for (int k = 0; k <= p; ++k)
                pts.push_back(end + dir * (length * k / p));
            return Concatenate(c, NurbsCurve::FromControlPoints(std::move(pts), p));
        }
        // 最后一个节点区间是一段贝塞尔：在 s > 1 处做 de Casteljau，右半段反过来就是 [1, s] 的延伸
        const NurbsCurve bez = SubCurve(c, c.Knots[c.ControlPoints.size() - 1], c.EndParam());
        if (!bez.IsValid() || static_cast<int>(bez.ControlPoints.size()) != p + 1)
            return {};
        const std::vector<H> Pw = ToH(bez);
        auto extension = [&](double s)
        {
            std::vector<H> b = Pw, ext(p + 1);
            ext[0] = b[p];
            for (int r = 1; r <= p; ++r)
            {
                for (int i = 0; i <= p - r; ++i)
                    b[i] = Mix(b[i], b[i + 1], s);
                ext[r] = b[p - r];
            }
            NurbsCurve e;
            e.Degree = p;
            e.Knots.assign(p + 1, 0.0);
            e.Knots.insert(e.Knots.end(), p + 1, 1.0);
            FromH(e, ext, bez.IsRational());
            return e;
        };
        double lo = 1.0, hi = 2.0;
        while (extension(hi).Length() < length && hi < 1e6)
            hi *= 2.0;
        for (int it = 0; it < 60; ++it)
        {
            const double mid = (lo + hi) * 0.5;
            (extension(mid).Length() < length ? lo : hi) = mid;
        }
        return Concatenate(c, extension((lo + hi) * 0.5));
    }

    NurbsCurve Rebuild(const NurbsCurve& c, int count, int degree, double* maxDeviation)
    {
        if (!c.IsValid() || degree < 1 || degree > 11 || count < degree + 1)
            return {};
        // 采样点按弦长分配参数
        const int M = std::max(200, count * 10);
        std::vector<Math::Point3> Q(M + 1);
        for (int k = 0; k <= M; ++k)
            Q[k] = c.PointAt(Lerp(c, static_cast<double>(k) / M));
        std::vector<double> t(M + 1, 0.0);
        for (int k = 1; k <= M; ++k)
            t[k] = t[k - 1] + (Q[k] - Q[k - 1]).Length();
        if (t[M] < 1e-12)
            return {};
        for (double& v : t)
            v /= t[M];
        t[M] = 1.0;

        NurbsCurve r;
        r.Degree = degree;
        const int n = count - 1, spans = count - degree;
        r.Knots.assign(degree + 1, 0.0);
        for (int j = 1; j < spans; ++j)
            r.Knots.push_back(static_cast<double>(j) / spans);
        r.Knots.insert(r.Knots.end(), degree + 1, 1.0);
        r.ControlPoints.assign(count, Q[0]);
        r.ControlPoints.back() = Q[M];

        // 两端点固定，其余控制点最小二乘（Piegl & Tiller 9.4.1）
        const int m = n - 1;
        if (m > 0)
        {
            std::vector<double> A(static_cast<size_t>(m) * m, 0.0), B(static_cast<size_t>(m) * 3, 0.0);
            double N[16];
            for (int k = 1; k < M; ++k)
            {
                const int span = FindSpan(n, degree, t[k], r.Knots);
                BasisFuns(span, t[k], degree, r.Knots, N);
                const double n0 = span - degree == 0 ? N[0] : 0.0, nn = span == n ? N[degree] : 0.0;
                const Math::Vec3 R = Math::Vec3{ Q[k].x, Q[k].y, Q[k].z } - Math::Vec3{ Q[0].x, Q[0].y, Q[0].z } * n0 - Math::Vec3{ Q[M].x, Q[M].y, Q[M].z } * nn;
                for (int a = 0; a <= degree; ++a)
                {
                    const int ia = span - degree + a;
                    if (ia < 1 || ia > n - 1)
                        continue;
                    B[(ia - 1) * 3 + 0] += N[a] * R.x;
                    B[(ia - 1) * 3 + 1] += N[a] * R.y;
                    B[(ia - 1) * 3 + 2] += N[a] * R.z;
                    for (int b = 0; b <= degree; ++b)
                    {
                        const int ib = span - degree + b;
                        if (ib >= 1 && ib <= n - 1)
                            A[static_cast<size_t>(ia - 1) * m + (ib - 1)] += N[a] * N[b];
                    }
                }
            }
            // 高斯消元（列主元）
            for (int col = 0; col < m; ++col)
            {
                int piv = col;
                for (int row = col + 1; row < m; ++row)
                    if (std::abs(A[static_cast<size_t>(row) * m + col]) > std::abs(A[static_cast<size_t>(piv) * m + col]))
                        piv = row;
                if (std::abs(A[static_cast<size_t>(piv) * m + col]) < 1e-14)
                    return {};
                if (piv != col)
                {
                    for (int j = 0; j < m; ++j)
                        std::swap(A[static_cast<size_t>(piv) * m + j], A[static_cast<size_t>(col) * m + j]);
                    for (int j = 0; j < 3; ++j)
                        std::swap(B[piv * 3 + j], B[col * 3 + j]);
                }
                for (int row = col + 1; row < m; ++row)
                {
                    const double f = A[static_cast<size_t>(row) * m + col] / A[static_cast<size_t>(col) * m + col];
                    if (f == 0.0)
                        continue;
                    for (int j = col; j < m; ++j)
                        A[static_cast<size_t>(row) * m + j] -= f * A[static_cast<size_t>(col) * m + j];
                    for (int j = 0; j < 3; ++j)
                        B[row * 3 + j] -= f * B[col * 3 + j];
                }
            }
            for (int row = m - 1; row >= 0; --row)
            {
                double x[3] = { B[row * 3], B[row * 3 + 1], B[row * 3 + 2] };
                for (int j = row + 1; j < m; ++j)
                {
                    const auto& pj = r.ControlPoints[static_cast<size_t>(j) + 1];
                    const double a = A[static_cast<size_t>(row) * m + j];
                    x[0] -= a * pj.x;
                    x[1] -= a * pj.y;
                    x[2] -= a * pj.z;
                }
                const double d = A[static_cast<size_t>(row) * m + row];
                r.ControlPoints[static_cast<size_t>(row) + 1] = { x[0] / d, x[1] / d, x[2] / d };
            }
        }
        if (maxDeviation)
        {
            double worst = 0.0;
            for (int k = 0; k <= M; ++k)
                worst = std::max(worst, (r.PointAt(t[k]) - Q[k]).Length());
            *maxDeviation = worst;
        }
        return r;
    }

    std::vector<double> Kinks(const NurbsCurve& c)
    {
        std::vector<double> out;
        if (!c.IsValid())
            return out;
        const double eps = KnotEps(c);
        const double h = (c.EndParam() - c.StartParam()) * 1e-7;
        for (size_t i = static_cast<size_t>(c.Degree) + 1; i < c.ControlPoints.size(); ++i)
        {
            const double u = c.Knots[i];
            if (u <= c.StartParam() + eps || u >= c.EndParam() - eps || (!out.empty() && std::abs(out.back() - u) <= eps))
                continue;
            int mult = 0;
            for (double k : c.Knots)
                mult += std::abs(k - u) <= eps ? 1 : 0;
            if (mult < c.Degree)
                continue;
            if (c.Degree > 1)
            {
                // 两侧的切线方向与曲率（单侧差分）：都连续时不拆（如椭圆、整圆的各段之间）
                // 曲率的差分步长取相邻节点区间的千分之一（太小时二阶差分被舍入误差淹没）
                double gap = c.EndParam() - c.StartParam();
                for (double k : c.Knots)
                    if (std::abs(k - u) > eps)
                        gap = std::min(gap, std::abs(k - u));
                const double hc = gap * 1e-3;
                const Math::Point3 pu = c.PointAt(u), l1 = c.PointAt(u - hc), l2 = c.PointAt(u - 2 * hc), r1 = c.PointAt(u + hc), r2 = c.PointAt(u + 2 * hc);
                const Math::Vec3 tl = pu - c.PointAt(u - h), tr = c.PointAt(u + h) - pu;
                if (tl.Length() < 1e-15 || tr.Length() < 1e-15)
                    continue;
                if (Math::Dot(tl.Normalized(), tr.Normalized()) > 1.0 - 1e-9)
                {
                    auto curvature = [](const Math::Vec3& d1, const Math::Vec3& d2) { return Math::Cross(d1, d2).Length() / std::pow(d1.Length(), 3); };
                    const double kl = curvature(pu - l1, (pu - l1) - (l1 - l2)), kr = curvature(r1 - pu, (r2 - r1) - (r1 - pu));
                    const double scale = std::max({ kl, kr, 1e-6 / std::max(c.BoundsSize(), 1e-9) });
                    if (std::abs(kl - kr) <= 1e-2 * scale)
                        continue;
                }
            }
            out.push_back(u);
        }
        return out;
    }

    std::optional<Math::Vec3> PlaneNormal(const NurbsCurve& c)
    {
        const auto& cp = c.ControlPoints;
        if (cp.empty())
            return std::nullopt;
        double size = 0.0;
        for (const auto& p : cp)
            size = std::max(size, (p - cp.front()).Length());
        const double tol = std::max(1e-9, size * 1e-9);
        // 找到不共线的三个控制点定平面（取叉积最大的一组，数值上最稳）
        Math::Vec3 normal;
        for (size_t i = 1; i < cp.size(); ++i)
            for (size_t j = i + 1; j < cp.size(); ++j)
            {
                const Math::Vec3 n = Math::Cross(cp[i] - cp[0], cp[j] - cp[0]);
                if (n.Length() > normal.Length())
                    normal = n;
            }
        if (normal.Length() < tol * std::max(1.0, size))
            return Math::Vec3{};
        normal = normal.Normalized();
        for (const auto& p : cp)
            if (std::abs(Math::Dot(p - cp[0], normal)) > tol * 10)
                return std::nullopt;
        return normal;
    }

    NurbsCurve FromConicPieces(const std::vector<ConicPiece>& pieces)
    {
        NurbsCurve c;
        if (pieces.empty())
            return c;
        c.Degree = 2;
        bool rational = false;
        for (const auto& pc : pieces)
        {
            if (c.ControlPoints.empty())
            {
                c.ControlPoints.push_back(pc.A);
                c.Weights.push_back(1.0);
            }
            c.ControlPoints.push_back(pc.Mid);
            c.Weights.push_back(pc.W);
            c.ControlPoints.push_back(pc.B);
            c.Weights.push_back(1.0);
            rational = rational || std::abs(pc.W - 1.0) > 1e-15;
        }
        c.Knots = { 0.0, 0.0, 0.0 };
        for (size_t k = 1; k < pieces.size(); ++k)
            c.Knots.insert(c.Knots.end(), 2, static_cast<double>(k));
        c.Knots.insert(c.Knots.end(), 3, static_cast<double>(pieces.size()));
        if (!rational)
            c.Weights.clear();
        return c;
    }

    std::vector<ConicPiece> FilletCorners(const std::vector<Math::Point3>& vertices, bool closed, double radius, int& skipped)
    {
        skipped = 0;
        std::vector<Math::Point3> v;
        for (const auto& p : vertices)
            if (v.empty() || (p - v.back()).Length() > 1e-12)
                v.push_back(p);
        if (closed && v.size() > 1 && (v.front() - v.back()).Length() <= 1e-12)
            v.pop_back();
        std::vector<ConicPiece> out;
        const size_t m = v.size();
        if (m < 2 || (closed && m < 3))
            return out;
        auto P = [&](size_t i) { return v[i % m]; };

        struct Corner
        {
            bool         On = false;
            double       T = 0.0, Phi = 0.0;
            Math::Point3 T1, T2;
        };
        std::vector<Corner> cs(m);
        auto hasCorner = [&](size_t i) { return closed || (i > 0 && i + 1 < m); };
        for (size_t i = 0; i < m; ++i)
        {
            if (!hasCorner(i))
                continue;
            const Math::Vec3 d1 = (P(i) - P(i + m - 1)).Normalized(), d2 = (P(i + 1) - P(i)).Normalized();
            const double phi = std::acos(std::clamp(Math::Dot(d1, d2), -1.0, 1.0));   // 转角 = 圆弧的包角
            if (phi < 1e-9)
                continue;   // 共线，没有拐角
            if (phi > kPi - 1e-9)
            {
                ++skipped;   // 折返，无法倒圆角
                continue;
            }
            Corner& c = cs[i];
            c.On  = true;
            c.Phi = phi;
            c.T   = radius * std::tan(phi / 2.0);
            c.T1  = P(i) - d1 * c.T;
            c.T2  = P(i) + d2 * c.T;
        }
        // 圆弧占用的长度不能超过相邻边的可用长度（另一端也有圆角时各占一半）
        std::vector<bool> candidate(m);
        for (size_t i = 0; i < m; ++i)
            candidate[i] = cs[i].On;
        for (size_t i = 0; i < m; ++i)
        {
            if (!cs[i].On)
                continue;
            const size_t prev = (i + m - 1) % m, next = (i + 1) % m;
            const double lenPrev = (P(i) - P(prev)).Length(), lenNext = (P(next) - P(i)).Length();
            const double availPrev = candidate[prev] ? lenPrev / 2 : lenPrev;
            const double availNext = candidate[next] ? lenNext / 2 : lenNext;
            if (cs[i].T > std::min(availPrev, availNext) * (1.0 + 1e-9))
            {
                cs[i].On = false;
                ++skipped;
            }
        }

        auto line = [&](const Math::Point3& a, const Math::Point3& b)
        {
            if ((b - a).Length() > 1e-12)
                out.push_back({ a, Mid(a, b), b, 1.0, false, 0.0 });
        };
        auto corner = [&](size_t i, Math::Point3& cur)
        {
            const Corner& c = cs[i % m];
            if (!c.On)
            {
                line(cur, P(i));
                cur = P(i);
                return;
            }
            line(cur, c.T1);
            out.push_back({ c.T1, P(i), c.T2, std::cos(c.Phi / 2.0), true, c.Phi });
            cur = c.T2;
        };
        if (!closed)
        {
            Math::Point3 cur = v[0];
            for (size_t i = 1; i + 1 < m; ++i)
                corner(i, cur);
            line(cur, v[m - 1]);
            return out;
        }
        const Math::Point3 start = cs[0].On ? cs[0].T2 : v[0];
        Math::Point3 cur = start;
        for (size_t i = 1; i < m; ++i)
            corner(i, cur);
        if (cs[0].On)
        {
            line(cur, cs[0].T1);
            out.push_back({ cs[0].T1, v[0], start, std::cos(cs[0].Phi / 2.0), true, cs[0].Phi });
        }
        else
            line(cur, v[0]);
        return out;
    }
}
