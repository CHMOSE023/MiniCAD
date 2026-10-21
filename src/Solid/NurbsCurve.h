#pragma once
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

// 空间 NURBS 曲线（docs/Solid/PLAN.md 第 11 节）：MiniCAD 自己的数据，不依赖内核——
// 求值、采样、变换在这里完成（网页版没有内核也能显示、变换）；插值、螺旋线、放样等由 ISolidKernel 完成。
namespace MiniCAD::Solid
{
    // 节点矢量完整写出（长度 = 控制点数 + 阶数 + 1，与 DXF SPLINE、MiniCAD 的 Spline 相同），一般两端各重复 阶数 + 1 次。
    // Weights 为空时是非有理曲线，否则与 ControlPoints 一一对应
    struct NurbsCurve
    {
        int                       Degree = 3;
        std::vector<Math::Point3> ControlPoints;
        std::vector<double>       Knots;
        std::vector<double>       Weights;

        bool IsValid() const
        {
            return Degree >= 1 && ControlPoints.size() >= static_cast<size_t>(Degree) + 1
                && Knots.size() == ControlPoints.size() + Degree + 1
                && (Weights.empty() || Weights.size() == ControlPoints.size());
        }
        bool   IsRational() const { return !Weights.empty(); }
        double StartParam() const { return Knots[Degree]; }
        double EndParam()   const { return Knots[ControlPoints.size()]; }

        Math::Point3 PointAt(double u) const
        {
            const int p = Degree;
            const int n = static_cast<int>(ControlPoints.size()) - 1;
            u = std::clamp(u, StartParam(), EndParam());
            // 所在区间：Knots[span] <= u < Knots[span + 1]（u 在末端时取最后一个非空区间）
            int span = p;
            while (span < n && u >= Knots[span + 1])
                ++span;
            // de Boor（齐次坐标）
            double d[32][4];
            const int m = std::min(p, 31);
            for (int j = 0; j <= m; ++j)
            {
                const int    i = span - p + j;
                const double w = IsRational() ? Weights[i] : 1.0;
                d[j][0] = ControlPoints[i].x * w;
                d[j][1] = ControlPoints[i].y * w;
                d[j][2] = ControlPoints[i].z * w;
                d[j][3] = w;
            }
            for (int r = 1; r <= m; ++r)
                for (int j = m; j >= r; --j)
                {
                    const int    i     = span - p + j;
                    const double denom = Knots[i + p - r + 1] - Knots[i];
                    const double a     = denom > 0.0 ? (u - Knots[i]) / denom : 0.0;
                    for (int c = 0; c < 4; ++c)
                        d[j][c] = (1.0 - a) * d[j - 1][c] + a * d[j][c];
                }
            const double w = d[m][3] != 0.0 ? d[m][3] : 1.0;
            return { d[m][0] / w, d[m][1] / w, d[m][2] / w };
        }

        // 一阶导数（参数为 u），中心差分
        Math::Vec3 DerivativeAt(double u) const
        {
            const double u0 = StartParam(), u1 = EndParam();
            const double h  = (u1 - u0) * 1e-6;
            const double a  = std::max(u0, u - h), b = std::min(u1, u + h);
            if (b <= a)
                return {};
            return (PointAt(b) - PointAt(a)) / (b - a);
        }

        // 按参数均匀采样：每个非空节点区间 perSpan 段
        std::vector<Math::Point3> Sample(int perSpan = 16) const
        {
            std::vector<Math::Point3> pts;
            if (!IsValid())
                return pts;
            for (size_t k = Degree; k < ControlPoints.size(); ++k)
            {
                const double a = Knots[k], b = Knots[k + 1];
                if (b <= a)
                    continue;
                for (int i = pts.empty() ? 0 : 1; i <= perSpan; ++i)
                    pts.push_back(PointAt(a + (b - a) * i / perSpan));
            }
            return pts;
        }

        double Length() const
        {
            const auto pts = Sample(64);
            double len = 0.0;
            for (size_t i = 1; i < pts.size(); ++i)
                len += (pts[i] - pts[i - 1]).Length();
            return len;
        }

        bool IsClosed(double tol = 1e-9) const
        {
            if (!IsValid())
                return false;
            const Math::Point3 a = PointAt(StartParam()), b = PointAt(EndParam());
            return (a - b).Length() <= tol * std::max(1.0, BoundsSize());
        }

        double BoundsSize() const
        {
            double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
            for (const auto& p : ControlPoints)
            {
                const double c[3] = { p.x, p.y, p.z };
                for (int i = 0; i < 3; ++i)
                {
                    lo[i] = std::min(lo[i], c[i]);
                    hi[i] = std::max(hi[i], c[i]);
                }
            }
            return ControlPoints.empty() ? 0.0 : std::hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
        }

        // 仿射变换（行向量约定，同 Math::Mat4）：NURBS 在仿射变换下只需变换控制点
        void Transform(const Math::Mat4& m)
        {
            for (auto& p : ControlPoints)
                p = m.TransformPoint(p);
        }

        // 方向相反的同一条曲线（参数区间不变）
        NurbsCurve Reversed() const
        {
            NurbsCurve r = *this;
            std::reverse(r.ControlPoints.begin(), r.ControlPoints.end());
            std::reverse(r.Weights.begin(), r.Weights.end());
            const double a = Knots.front(), b = Knots.back();
            for (size_t i = 0; i < Knots.size(); ++i)
                r.Knots[i] = a + b - Knots[Knots.size() - 1 - i];
            return r;
        }

        // ── 构造 ─────────────────────────────────────────────────────

        // 控制点 + 阶数，两端夹持的均匀节点矢量；控制点不够时阶数自动降低
        static NurbsCurve FromControlPoints(std::vector<Math::Point3> points, int degree)
        {
            NurbsCurve c;
            if (points.size() < 2 || degree < 1)
                return c;
            c.Degree        = std::min(degree, static_cast<int>(points.size()) - 1);
            c.ControlPoints = std::move(points);
            const int n = static_cast<int>(c.ControlPoints.size());
            const int spans = n - c.Degree;
            for (int i = 0; i <= c.Degree; ++i)
                c.Knots.push_back(0.0);
            for (int i = 1; i < spans; ++i)
                c.Knots.push_back(static_cast<double>(i) / spans);
            for (int i = 0; i <= c.Degree; ++i)
                c.Knots.push_back(1.0);
            return c;
        }

        // 首尾相接的直线 / 圆弧段 → 二次有理曲线（精确表示圆弧）。
        // points[i] → points[i + 1] 一段，bulges[i] 为该段的凸度（tan(圆心角 / 4)，正值为逆时针，同 Solid::ProfileVertex，
        // 圆弧在 z = points[i].z 的水平面内）；closed 时最后一个点连回第一个（不要重复首点）。
        // 参数：每段（圆弧再按不超过 90° 分成几段）占一个单位长度的参数区间
        static NurbsCurve FromSegments(const std::vector<Math::Point3>& points, const std::vector<double>& bulges, bool closed)
        {
            NurbsCurve c;
            const size_t n = points.size();
            const size_t segments = closed ? n : (n == 0 ? 0 : n - 1);
            if (segments == 0)
                return c;
            c.Degree = 2;
            auto addPiece = [&](const Math::Point3& a, const Math::Point3& mid, double w, const Math::Point3& b) {
                if (c.ControlPoints.empty())
                {
                    c.ControlPoints.push_back(a);
                    c.Weights.push_back(1.0);
                }
                c.ControlPoints.push_back(mid);
                c.Weights.push_back(w);
                c.ControlPoints.push_back(b);
                c.Weights.push_back(1.0);
            };
            for (size_t s = 0; s < segments; ++s)
            {
                const Math::Point3& a = points[s];
                const Math::Point3& b = points[(s + 1) % n];
                const double bulge = s < bulges.size() ? bulges[s] : 0.0;
                const double chord = std::hypot(b.x - a.x, b.y - a.y);
                if (std::abs(bulge) < 1e-12 || chord < 1e-12)
                {
                    addPiece(a, Math::Point3{ (a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2 }, 1.0, b);
                    continue;
                }
                // 圆心、半径与起始角（凸度约定同 SMLibKernel 的 ArcOf：圆心在弦中点的左法线方向上偏移 h）
                const double sweep = 4.0 * std::atan(bulge);   // 带符号，正值逆时针
                const double h  = chord * (1.0 - bulge * bulge) / (4.0 * bulge);
                const double cx = (a.x + b.x) * 0.5 - (b.y - a.y) / chord * h;
                const double cy = (a.y + b.y) * 0.5 + (b.x - a.x) / chord * h;
                const double r  = std::hypot(a.x - cx, a.y - cy);
                const double a0 = std::atan2(a.y - cy, a.x - cx);
                const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2) - 1e-9)));
                const double step = sweep / pieces;
                Math::Point3 prev = a;
                for (int k = 1; k <= pieces; ++k)
                {
                    const double t0 = a0 + step * (k - 1), t1 = a0 + step * k;
                    const Math::Point3 next = k == pieces ? b : Math::Point3{ cx + r * std::cos(t1), cy + r * std::sin(t1), a.z };
                    const double tm = (t0 + t1) / 2, rm = r / std::cos(step / 2);   // 两端切线的交点
                    const double z  = a.z + (b.z - a.z) * (k - 0.5) / pieces;
                    addPiece(prev, Math::Point3{ cx + rm * std::cos(tm), cy + rm * std::sin(tm), z }, std::cos(step / 2), next);
                    prev = next;
                }
            }
            // 节点：每段二次贝塞尔一个单位区间，内部节点重复 2 次
            const size_t pieces = (c.ControlPoints.size() - 1) / 2;
            c.Knots = { 0.0, 0.0, 0.0 };
            for (size_t k = 1; k < pieces; ++k)
            {
                c.Knots.push_back(static_cast<double>(k));
                c.Knots.push_back(static_cast<double>(k));
            }
            c.Knots.insert(c.Knots.end(), 3, static_cast<double>(pieces));
            bool rational = false;
            for (double w : c.Weights)
                rational = rational || std::abs(w - 1.0) > 1e-15;
            if (!rational)
                c.Weights.clear();
            return c;
        }
    };
}
