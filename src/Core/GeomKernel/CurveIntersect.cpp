#include "CurveIntersect.hpp"
#include "../Math/MathUtils.hpp"
#include <cmath>

namespace MiniCAD::Geom
{
    namespace
    {
        // 线段-线段求交（XY 平面）。
        //   A(t) = a0 + t·(a1-a0),  B(s) = b0 + s·(b1-b0),  t,s ∈ [0,1]
        // 命中时把交点写入 out 并返回 true；平行 / 不相交返回 false。
        bool SegSeg2D(const Math::Point3& a0, const Math::Point3& a1,
                      const Math::Point3& b0, const Math::Point3& b1,
                      Math::Point3& out)
        {
            const double d1x = a1.x - a0.x, d1y = a1.y - a0.y;
            const double d2x = b1.x - b0.x, d2y = b1.y - b0.y;

            const double denom = d1x * d2y - d1y * d2x;          // cross(d1, d2)
            if (std::abs(denom) < 1e-12) return false;            // 平行 / 退化

            const double ox = b0.x - a0.x, oy = b0.y - a0.y;
            const double t = (ox * d2y - oy * d2x) / denom;       // cross(o, d2)/denom
            const double s = (ox * d1y - oy * d1x) / denom;       // cross(o, d1)/denom

            if (t < 0.0 || t > 1.0 || s < 0.0 || s > 1.0) return false;

            out = { a0.x + t * d1x, a0.y + t * d1y, a0.z + t * (a1.z - a0.z) };
            return true;
        }
    }

    std::vector<Math::Point3> IntersectCurves(const ICurve& a, const ICurve& b,
                                              double tol, int segHint)
    {
        std::vector<Math::Point3> pa, pb;
        a.Tessellate(pa, segHint);
        b.Tessellate(pb, segHint);

        std::vector<Math::Point3> result;
        if (pa.size() < 2 || pb.size() < 2) return result;

        const double tol2 = tol * tol;
        auto pushUnique = [&](const Math::Point3& p)
        {
            for (const auto& q : result)
                if (Math::DistanceSq(p, q) < tol2) return;        // 合并近邻交点
            result.push_back(p);
        };

        Math::Point3 hit;
        for (size_t i = 0; i + 1 < pa.size(); ++i)
            for (size_t j = 0; j + 1 < pb.size(); ++j)
                if (SegSeg2D(pa[i], pa[i + 1], pb[j], pb[j + 1], hit))
                    pushUnique(hit);

        return result;
    }
}
