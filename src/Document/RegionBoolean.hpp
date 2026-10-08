#pragma once
#include "Core/Entity/HatchEntity.hpp"
#include "Core/GeomKernel/PolygonBoolean.hpp"
#include <vector>

namespace MiniCAD::RegionBoolean
{
    // 面域的布尔运算（并 / 交 / 差），在边界环（HatchLoop）上进行。
    //
    // 运算在离散多边形上做：直线、圆弧、椭圆弧、样条先按 2° 一段离散，所以和对方相交、被切开的边界
    // 会变成折线（圆弧不再是精确的圆弧）；没有和对方接触的整个环保持原样，精确的圆 / 椭圆不会被离散。
    using Op = PolygonBoolean::Op;

    // 把一个环按 2° 的圆心角步长离散为闭合多边形（不重复首点由 Compute 内部清理）
    inline PolygonBoolean::Polygon TessellateFine(const HatchLoop& loop)
    {
        constexpr double kStep = Math::PI / 90.0;
        PolygonBoolean::Polygon out;
        for (size_t i = 0; i < loop.Edges.size(); ++i)
        {
            const HatchEdge& e = loop.Edges[i];
            std::vector<Math::Point3> pts;
            switch (e.Type)
            {
            case HatchEdge::Kind::Poly:
                pts = e.Poly.IsValid() ? e.Poly.Tessellate(kStep) : e.Poly.Points;
                break;
            case HatchEdge::Kind::EllipseArc:
            {
                double span = e.A1 - e.A0;
                if (span <= 0.0) span += Math::TwoPI;
                const int n = std::max(16, static_cast<int>(std::ceil(span / kStep)));
                for (int k = 0; k <= n; ++k)
                    pts.push_back(e.Ell.PointAt(e.A0 + span * (static_cast<double>(k) / n)));
                break;
            }
            case HatchEdge::Kind::Spline:
                pts = e.Spl.Tessellate(48);
                break;
            }
            const size_t count = (i + 1 == loop.Edges.size()) ? pts.size() : (pts.empty() ? 0 : pts.size() - 1);
            out.insert(out.end(), pts.begin(), pts.begin() + count);
        }
        return out;
    }

    // a op b。返回的环里，与对方无接触的环是输入环的原样拷贝，其余是新生成的多段线环。
    inline std::vector<HatchLoop> Apply(const std::vector<HatchLoop>& a, const std::vector<HatchLoop>& b, Op op)
    {
        PolygonBoolean::Region ra, rb;
        for (const auto& l : a) ra.push_back(TessellateFine(l));
        for (const auto& l : b) rb.push_back(TessellateFine(l));

        const auto res = PolygonBoolean::Compute(ra, rb, op);

        std::vector<HatchLoop> out;
        for (const auto& lp : res)
        {
            if (lp.Source >= 0)
            {
                const size_t s = static_cast<size_t>(lp.Source);
                out.push_back(s < a.size() ? a[s] : b[s - a.size()]);
                continue;
            }
            std::vector<Math::Point3> pts = lp.Points;
            pts.push_back(pts.front());                         // 闭合
            out.push_back(HatchLoop::FromPolyline(Polyline(std::move(pts))));
        }
        return out;
    }

    // 多个操作数依次运算：operands[0] op operands[1] op operands[2] …
    //   并、交可交换；差是 operands[0] 依次减去其余。
    inline std::vector<HatchLoop> Combine(const std::vector<std::vector<HatchLoop>>& operands, Op op)
    {
        if (operands.empty()) return {};
        std::vector<HatchLoop> acc = operands[0];
        for (size_t i = 1; i < operands.size(); ++i)
            acc = Apply(acc, operands[i], op);
        return acc;
    }
}
