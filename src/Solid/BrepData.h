#pragma once
#include "Solid/NurbsCurve.h"
#include <vector>

// 中性 B-rep：ACIS 等外部数据 → ISolidKernel::FromBrep（docs/Solid/PLAN.md 第 16 节）
namespace MiniCAD::Solid
{
    // 控制点下标 u * CountV + v；节点矢量完整写出
    struct NurbsSurface
    {
        int                       DegreeU = 3, DegreeV = 3;
        int                       CountU = 0, CountV = 0;
        std::vector<double>       KnotsU, KnotsV;
        std::vector<Math::Point3> ControlPoints;
        std::vector<double>       Weights;

        bool IsValid() const
        {
            return DegreeU >= 1 && DegreeV >= 1 && CountU > DegreeU && CountV > DegreeV
                && ControlPoints.size() == static_cast<size_t>(CountU) * CountV
                && KnotsU.size() == static_cast<size_t>(CountU + DegreeU + 1)
                && KnotsV.size() == static_cast<size_t>(CountV + DegreeV + 1)
                && (Weights.empty() || Weights.size() == ControlPoints.size());
        }

        Math::Point3 PointAt(double u, double v) const
        {
            std::vector<double> bu, bv;
            Basis(KnotsU, DegreeU, CountU, u, bu);
            Basis(KnotsV, DegreeV, CountV, v, bv);
            double x = 0, y = 0, z = 0, w = 0;
            for (int i = 0; i < CountU; ++i)
            {
                if (bu[i] == 0.0)
                    continue;
                for (int j = 0; j < CountV; ++j)
                {
                    const size_t k = static_cast<size_t>(i) * CountV + j;
                    const double b = bu[i] * bv[j] * (Weights.empty() ? 1.0 : Weights[k]);
                    x += b * ControlPoints[k].x;
                    y += b * ControlPoints[k].y;
                    z += b * ControlPoints[k].z;
                    w += b;
                }
            }
            return w != 0.0 ? Math::Point3{ x / w, y / w, z / w } : Math::Point3{ x, y, z };
        }

        static void Basis(const std::vector<double>& knots, int degree, int count, double t, std::vector<double>& out)
        {
            out.assign(count, 0.0);
            t = std::clamp(t, knots[degree], knots[count]);
            int span = degree;
            while (span < count - 1 && t >= knots[span + 1])
                ++span;
            std::vector<double> n(degree + 1, 0.0), left(degree + 1), right(degree + 1);
            n[0] = 1.0;
            for (int j = 1; j <= degree; ++j)
            {
                left[j]  = t - knots[span + 1 - j];
                right[j] = knots[span + j] - t;
                double saved = 0.0;
                for (int r = 0; r < j; ++r)
                {
                    const double denom = right[r + 1] + left[j - r];
                    const double tmp   = denom != 0.0 ? n[r] / denom : 0.0;
                    n[r]  = saved + right[r + 1] * tmp;
                    saved = left[j - r] * tmp;
                }
                n[j] = saved;
            }
            for (int j = 0; j <= degree; ++j)
                out[span - degree + j] = n[j];
        }
    };

    enum class BrepSurfaceType { Plane, Cone, Sphere, Torus, Nurbs };

    // 法线：平面为 Normal，其余背离轴线 / 球心。圆锥沿 Normal 每走 1 半径增加 Sin / Cos，圆柱 Sin = 0
    struct BrepSurface
    {
        BrepSurfaceType Type = BrepSurfaceType::Plane;
        Math::Point3    Origin;
        Math::Vec3      Normal{ 0.0, 0.0, 1.0 };
        Math::Vec3      XAxis{ 1.0, 0.0, 0.0 };
        double          Radius       = 0.0;
        double          MinorRadius  = 0.0;
        double          SinHalfAngle = 0.0;
        double          CosHalfAngle = 1.0;
        NurbsSurface    Nurbs;
    };

    // 环：首尾相接的三维曲线，面在环的左边（从外侧看）；没有环 = 整张曲面；PointLoops = 退化成一点的环（锥顶）
    struct BrepFace
    {
        BrepSurface                          Surface;
        bool                                 Reversed = false;
        std::vector<std::vector<NurbsCurve>> Loops;
        std::vector<Math::Point3>            PointLoops;
    };

    struct BrepModel
    {
        std::vector<BrepFace> Faces;
    };
}
