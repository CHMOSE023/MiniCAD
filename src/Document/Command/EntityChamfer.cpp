#include "EntityChamfer.h"

#include <cmath>

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
        Math::Point3 P3(V2 v, double z) { return { v.x, v.y, z }; }

        // 一条直线在交点 x 处的处理：away 是从交点指向保留一侧的单位向量
        struct Side
        {
            const LineEntity* line = nullptr;
            V2 away;
            V2 keep;         // 保留的端点
            double keepDist = 0;   // 保留端点到交点的距离（沿 away 方向）
        };

        bool MakeSide(const LineEntity& e, V2 x, V2 pick, Side& out)
        {
            const Line& l = e.GetLine();
            const V2 s = Xy(l.Start), t = Xy(l.End);
            const V2 d = t - s;
            const double len = Len(d);
            if (len < kEps) return false;
            const V2 u = d * (1.0 / len);

            const double side = Dot(pick - x, u);
            if (std::abs(side) < kEps) return false;              // 点选恰好在交点上，分不出保留哪一侧
            const V2 away = u * (side > 0 ? 1.0 : -1.0);

            // 保留点选一侧：沿 away 方向更远的那个端点
            const bool startFarther = Dot(s - x, away) > Dot(t - x, away);
            out.line     = &e;
            out.away     = away;
            out.keep     = startFarther ? s : t;
            out.keepDist = Dot(out.keep - x, away);
            return true;
        }

        std::unique_ptr<LineEntity> Fit(const Side& side, V2 p)
        {
            const Line& l = side.line->GetLine();
            Line n = l;
            const bool keepIsStart = Len(Xy(l.Start) - side.keep) < Len(Xy(l.End) - side.keep);
            if (keepIsStart) n.End   = P3(p, l.End.z);
            else             n.Start = P3(p, l.Start.z);
            auto e = std::make_unique<LineEntity>(side.line->GetID(), n.Start, n.End);
            e->SetAttr(side.line->GetAttr());
            return e;
        }
    }

    bool ComputeChamfer(const Entity& a, const Math::Point3& pickA,
                        const Entity& b, const Math::Point3& pickB,
                        double d1, double d2, ChamferResult& out)
    {
        if (a.GetID() == b.GetID() || !a.IsKindOf<LineEntity>() || !b.IsKindOf<LineEntity>())
            return false;
        if (!(d1 >= 0.0) || !(d2 >= 0.0) || !std::isfinite(d1) || !std::isfinite(d2))
            return false;
        const bool zero1 = d1 < kEps, zero2 = d2 < kEps;
        if (zero1 != zero2)
            return false;                                         // 一个为 0 一个不为 0：倒角线会和某条线重合

        const auto& la = static_cast<const LineEntity&>(a);
        const auto& lb = static_cast<const LineEntity&>(b);
        const Line& A = la.GetLine();
        const Line& B = lb.GetLine();

        // 两条线（延长线）的交点
        const V2 pa = Xy(A.Start), da = Xy(A.End) - pa;
        const V2 pb = Xy(B.Start), db = Xy(B.End) - pb;
        const double den = Cross(da, db);
        if (std::abs(den) < 1e-12 * (Len(da) * Len(db) + 1.0))
            return false;                                         // 平行（或零长度）
        const V2 x = pa + da * (Cross(pb - pa, db) / den);

        Side sa, sb;
        if (!MakeSide(la, x, Xy(pickA), sa) || !MakeSide(lb, x, Xy(pickB), sb))
            return false;

        const V2 p1 = x + sa.away * d1;
        const V2 p2 = x + sb.away * d2;
        if (!zero1 && (d1 > sa.keepDist + kEps || d2 > sb.keepDist + kEps))
            return false;                                         // 距离超过保留一侧的长度
        if (zero1 && (sa.keepDist < -kEps || sb.keepDist < -kEps))
            return false;

        ChamferResult r;
        r.first  = Fit(sa, p1);
        r.second = Fit(sb, p2);
        if (!zero1)
        {
            r.line = std::make_unique<LineEntity>(0, P3(p1, A.Start.z), P3(p2, A.Start.z));
            r.line->SetAttr(a.GetAttr());
        }
        out = std::move(r);
        return true;
    }
}
