#include "EntityFillet.h"

#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Math/Constants.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace MiniCAD
{
    namespace
    {
        constexpr double kEps = 1e-9;

        struct V2 { double x = 0, y = 0; };
        V2     operator+(V2 a, V2 b) { return { a.x + b.x, a.y + b.y }; }
        V2     operator-(V2 a, V2 b) { return { a.x - b.x, a.y - b.y }; }
        V2     operator*(V2 a, double k) { return { a.x * k, a.y * k }; }
        double Dot(V2 a, V2 b)   { return a.x * b.x + a.y * b.y; }
        double Cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
        double Len(V2 a)         { return std::hypot(a.x, a.y); }
        V2     Xy(const Math::Point3& p) { return { p.x, p.y }; }

        // 圆角计算用到的曲线：无限直线（点 + 单位方向）或圆（圆心 + 半径，圆弧按所在圆处理）
        struct Curve
        {
            bool isLine = false;
            V2   p, d;          // 直线
            V2   c;             // 圆
            double R = 0;
        };

        std::optional<Curve> CurveOf(const Entity& e)
        {
            Curve k;
            if (e.IsKindOf<LineEntity>())
            {
                const Line& l = static_cast<const LineEntity&>(e).GetLine();
                const V2 d = Xy(l.End) - Xy(l.Start);
                const double len = Len(d);
                if (len < kEps) return std::nullopt;
                k.isLine = true; k.p = Xy(l.Start); k.d = d * (1.0 / len);
                return k;
            }
            if (e.IsKindOf<ArcEntity>())
            {
                const Arc& a = static_cast<const ArcEntity&>(e).GetArc();
                if (a.Radius < kEps) return std::nullopt;
                k.c = Xy(a.Center); k.R = a.Radius;
                return k;
            }
            if (e.IsKindOf<CircleEntity>())
            {
                const Circle& c = static_cast<const CircleEntity&>(e).GetCircle();
                if (c.Radius < kEps) return std::nullopt;
                k.c = Xy(c.Center); k.R = c.Radius;
                return k;
            }
            return std::nullopt;
        }

        // ── 求交 ─────────────────────────────────────────────────────────
        bool LineLine(V2 p1, V2 d1, V2 p2, V2 d2, V2& out)
        {
            const double den = Cross(d1, d2);
            if (std::abs(den) < 1e-12) return false;
            const double t = Cross(p2 - p1, d2) / den;
            out = p1 + d1 * t;
            return true;
        }

        void LineCircle(V2 p, V2 d, V2 c, double r, std::vector<V2>& out)
        {
            const V2 f = p - c;
            const double b = Dot(f, d);
            const double disc = b * b - (Dot(f, f) - r * r);
            if (disc < 0) return;
            const double s = std::sqrt(disc);
            out.push_back(p + d * (-b + s));
            if (s > 1e-12) out.push_back(p + d * (-b - s));
        }

        void CircleCircle(V2 c1, double r1, V2 c2, double r2, std::vector<V2>& out)
        {
            const V2 dv = c2 - c1;
            const double d = Len(dv);
            if (d < kEps) return;
            const double a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
            const double h2 = r1 * r1 - a * a;
            if (h2 < 0) return;
            const double h = std::sqrt(h2);
            const V2 u = dv * (1.0 / d);
            const V2 mid = c1 + u * a;
            const V2 n{ -u.y, u.x };
            out.push_back(mid + n * h);
            if (h > 1e-12) out.push_back(mid - n * h);
        }

        // 与曲线 k 相切、半径 r 的圆的圆心应满足的「等距曲线」：直线 → 两条平行线；圆 → 半径 R+r 和 |R-r| 两个圆
        void Centers(const Curve& k1, const Curve& k2, double r, std::vector<V2>& out)
        {
            auto offsetLines = [&](const Curve& k) -> std::vector<std::pair<V2, V2>>
            {
                const V2 n{ -k.d.y, k.d.x };
                return { { k.p + n * r, k.d }, { k.p - n * r, k.d } };
            };
            auto offsetRadii = [&](const Curve& k) -> std::vector<double>
            {
                std::vector<double> v{ k.R + r };
                if (std::abs(k.R - r) > kEps) v.push_back(std::abs(k.R - r));
                return v;
            };

            if (k1.isLine && k2.isLine)
            {
                for (auto& l1 : offsetLines(k1))
                    for (auto& l2 : offsetLines(k2))
                        if (V2 x; LineLine(l1.first, l1.second, l2.first, l2.second, x)) out.push_back(x);
            }
            else if (k1.isLine != k2.isLine)
            {
                const Curve& line = k1.isLine ? k1 : k2;
                const Curve& circ = k1.isLine ? k2 : k1;
                for (auto& l : offsetLines(line))
                    for (double rr : offsetRadii(circ))
                        LineCircle(l.first, l.second, circ.c, rr, out);
            }
            else
            {
                for (double r1 : offsetRadii(k1))
                    for (double r2 : offsetRadii(k2))
                        CircleCircle(k1.c, r1, k2.c, r2, out);
            }
        }

        // 圆心 x 在曲线上的切点
        std::optional<V2> Tangent(const Curve& k, V2 x)
        {
            if (k.isLine)
                return k.p + k.d * Dot(x - k.p, k.d);
            const V2 v = x - k.c;
            const double l = Len(v);
            if (l < kEps) return std::nullopt;
            return k.c + v * (k.R / l);
        }

        Math::Point3 P3(V2 v, double z) { return { v.x, v.y, z }; }

        // 修剪 / 延伸直线：切点 t 把直线分成两侧，点选位置所在的一侧保留，另一侧的端点移到 t。
        // （不能按「离哪个端点近」判断：点选在线段中间时两端一样近）
        std::unique_ptr<Entity> FitLine(const LineEntity& src, V2 pick, V2 t)
        {
            Line l = src.GetLine();
            const V2 d = Xy(l.End) - Xy(l.Start);
            const bool pickOnEndSide = Dot(pick - t, d) > 0;
            if (pickOnEndSide) l.Start = P3(t, l.Start.z);
            else               l.End   = P3(t, l.End.z);
            auto e = src.Clone(src.GetID());
            static_cast<LineEntity&>(*e).SetLine(l);
            return e;
        }

        // 修剪 / 延伸圆弧：切点 t 把所在的圆分成两侧，点选位置在 t 的逆时针一侧则保留终点侧（起点改到 t），否则保留起点侧
        std::unique_ptr<Entity> FitArc(const ArcEntity& src, V2 pick, V2 t)
        {
            Arc a = src.GetArc();
            const double ang  = std::atan2(t.y - a.Center.y, t.x - a.Center.x);
            const double pAng = std::atan2(pick.y - a.Center.y, pick.x - a.Center.x);
            double diff = std::fmod(pAng - ang, Math::TwoPI);
            if (diff > Math::PI)   diff -= Math::TwoPI;
            if (diff < -Math::PI)  diff += Math::TwoPI;
            if (diff > 0) a.StartAngle = ang;
            else          a.EndAngle   = ang;
            auto e = src.Clone(src.GetID());
            static_cast<ArcEntity&>(*e).SetArc(a);
            return e;
        }

        std::unique_ptr<Entity> Fit(const Entity& src, V2 pick, V2 t)
        {
            if (src.IsKindOf<LineEntity>()) return FitLine(static_cast<const LineEntity&>(src), pick, t);
            if (src.IsKindOf<ArcEntity>())  return FitArc(static_cast<const ArcEntity&>(src), pick, t);
            return nullptr;                     // 圆不修剪
        }
    }

    bool ComputeFillet(const Entity& a, const Math::Point3& pickA,
                       const Entity& b, const Math::Point3& pickB,
                       double radius, FilletResult& out)
    {
        if (&a == &b || a.GetID() == b.GetID() || radius < 0.0 || !std::isfinite(radius))
            return false;

        const auto ka = CurveOf(a), kb = CurveOf(b);
        if (!ka || !kb)
            return false;

        const V2 pa = Xy(pickA), pb = Xy(pickB);
        const double z = pickA.z;

        // ── 半径 0：两条直线延伸 / 修剪到交点 ─────────────────────────
        if (radius < kEps)
        {
            if (!ka->isLine || !kb->isLine)
                return false;
            V2 x;
            if (!LineLine(ka->p, ka->d, kb->p, kb->d, x))
                return false;
            FilletResult r;
            r.first  = Fit(a, pa, x);
            r.second = Fit(b, pb, x);
            out = std::move(r);
            return true;
        }

        // ── 半径 > 0：找与两条曲线都相切的圆，取切点离点选位置最近的那个 ──
        std::vector<V2> centers;
        Centers(*ka, *kb, radius, centers);

        double best = std::numeric_limits<double>::infinity();
        V2 bc, bt1, bt2;
        for (const V2& c : centers)
        {
            const auto t1 = Tangent(*ka, c), t2 = Tangent(*kb, c);
            if (!t1 || !t2) continue;
            if (Len(*t1 - *t2) < 1e-9) continue;                // 两个切点重合：退化
            const double score = Len(*t1 - pa) + Len(*t2 - pb);
            if (score < best) { best = score; bc = c; bt1 = *t1; bt2 = *t2; }
        }
        if (!std::isfinite(best))
            return false;

        FilletResult r;
        r.first  = Fit(a, pa, bt1);
        r.second = Fit(b, pb, bt2);

        // 圆角弧：取两个切点之间较短的一段（逆时针）
        double a1 = std::atan2(bt1.y - bc.y, bt1.x - bc.x);
        double a2 = std::atan2(bt2.y - bc.y, bt2.x - bc.x);
        double span = std::fmod(a2 - a1, Math::TwoPI);
        if (span < 0) span += Math::TwoPI;
        if (span > Math::PI) std::swap(a1, a2);

        r.arc = std::make_unique<ArcEntity>(0, P3(bc, z), radius, a1, a2);
        r.arc->SetAttr(a.GetAttr());
        out = std::move(r);
        return true;
    }
}
