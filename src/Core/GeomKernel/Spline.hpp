#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Constants.hpp"
#include "AABB.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace MiniCAD
{
    // =========================================================================
    // Spline —— 通过拟合点的三次插值样条（AutoCAD Spline 风格）
    //
    // 数学模型：
    //   每两个相邻拟合点之间用一段三次多项式连接：
    //     P(t) = A + B·t + C·t² + D·t³,   t ∈ [0, 1]
    //
    //   约束：
    //     · C0：曲线通过所有拟合点
    //     · C1：相邻段在连接点处一阶导数连续（切线连续）
    //     · C2：相邻段在连接点处二阶导数连续（曲率连续）
    //
    //   边界条件（可选）：
    //     · 自然（Natural）：两端二阶导数为 0
    //     · 夹持（Clamped）：两端一阶导数由用户指定
    //     · 闭合（Closed） ：首尾相连，全程 C2
    //
    // 参数化：弦长参数化（Chord-length），与 AutoCAD 行为一致
    // =========================================================================

    struct SplineSegment
    {
        Math::Point3 A, B, C, D;   // P(t) = A + B·t + C·t² + D·t³，t∈[0,1]

        Math::Point3 Evaluate(double t) const
        {
            return
            {
                A.x + t * (B.x + t * (C.x + t * D.x)),
                A.y + t * (B.y + t * (C.y + t * D.y)),
                A.z + t * (B.z + t * (C.z + t * D.z)),
            };
        }

        // 一阶导数（切线方向，未归一化）
        Math::Point3 Derivative(double t) const
        {
            return
            {
                B.x + t * (2.0 * C.x + t * 3.0 * D.x),
                B.y + t * (2.0 * C.y + t * 3.0 * D.y),
                B.z + t * (2.0 * C.z + t * 3.0 * D.z),
            };
        }
    };

    // ── 边界条件 ──────────────────────────────────────────────────────────────
    enum class SplineBoundary
    {
        Natural,    // 两端二阶导数 = 0（最常用）
        Clamped,    // 两端一阶导数由 StartTangent / EndTangent 指定
        Closed,     // 闭合曲线，首尾 C2 连续
    };

    struct Spline
    {
        std::vector<Math::Point3> FitPoints;     // 拟合点（用户点击的点）
        SplineBoundary            Boundary = SplineBoundary::Natural;
        Math::Point3              StartTangent{};  // 仅 Clamped 模式有效
        Math::Point3              EndTangent{};    // 仅 Clamped 模式有效

        // 内部计算结果（Build() 后有效）
        std::vector<SplineSegment> Segments;       // 段数 = FitPoints.size()-1（非闭合）
        std::vector<double>        Params;         // 各拟合点对应的弦长参数值

        Spline() = default;

        explicit Spline(std::vector<Math::Point3> pts, SplineBoundary boundary = SplineBoundary::Natural)
            : FitPoints(std::move(pts))
            , Boundary(boundary)
        {
            Build();
        }

        // ── 有效性 ────────────────────────────────────────────────────────────
        bool IsValid() const
        {
            return FitPoints.size() >= 2 && !Segments.empty();
        }

        bool IsClosed() const { return Boundary == SplineBoundary::Closed; }

        // ── 重新计算所有段 ────────────────────────────────────────────────────
        void Build()
        {
            Segments.clear();
            Params.clear();

            int n = static_cast<int>(FitPoints.size());
            if (n < 2) return;

            // ── 弦长参数化 ────────────────────────────────────────────────
            ComputeChordParams();

            if (IsClosed() && n >= 3)
                BuildClosed();
            else
                BuildOpen();
        }

    private:

        // ── 弦长参数化：t[i] ∝ 累计弦长 ─────────────────────────────────────
        void ComputeChordParams()
        {
            int n = static_cast<int>(FitPoints.size());
            Params.resize(n);
            Params[0] = 0.0;

            for (int i = 1; i < n; ++i)
            {
                double dx = FitPoints[i].x - FitPoints[i - 1].x;
                double dy = FitPoints[i].y - FitPoints[i - 1].y;
                double dz = FitPoints[i].z - FitPoints[i - 1].z;
                Params[i] = Params[i - 1] + std::sqrt(dx * dx + dy * dy + dz * dz);
            }

            // 归一化到 [0, 1]
            double total = Params.back();
            if (total > Math::LengthEPS)
                for (auto& p : Params) p /= total;
        }

        // ── 开放曲线：解三对角线性方程组 ─────────────────────────────────────
        //
        // 设各拟合点处的二阶导数为 M[i]（矩参数），
        // 由 C2 连续性推导出三对角方程：
        //
        //   μ[i]·M[i-1] + 2·M[i] + λ[i]·M[i+1] = d[i]
        //
        // 边界条件决定首尾方程（Natural / Clamped）。
        //
        void BuildOpen()
        {
            int n = static_cast<int>(FitPoints.size());
            int N = n - 1;   // 段数

            // h[i] = Params[i+1] - Params[i]（局部步长）
            std::vector<double> h(N);
            for (int i = 0; i < N; ++i)
                h[i] = Params[i + 1] - Params[i];

            // 对 x / y / z 分量分别求解
            auto solve = [&](auto getCoord) -> std::vector<double>
                {
                    // 构建方程组右端项 d（内部点，i=1..n-2）
                    std::vector<double> lower(n, 0), diag(n, 0), upper(n, 0), rhs(n, 0);

                    // 内部点
                    for (int i = 1; i < n - 1; ++i)
                    {
                        double hi1 = h[i - 1], hi = h[i];
                        lower[i] = hi1;
                        diag[i] = 2.0 * (hi1 + hi);
                        upper[i] = hi;
                        rhs[i] = 6.0 * ((getCoord(FitPoints[i + 1]) - getCoord(FitPoints[i])) / hi
                            - (getCoord(FitPoints[i]) - getCoord(FitPoints[i - 1])) / hi1);
                    }

                    // 边界条件
                    if (Boundary == SplineBoundary::Natural)
                    {
                        diag[0] = 1.0;  upper[0] = 0.0;  rhs[0] = 0.0;
                        diag[n - 1] = 1.0;  lower[n - 1] = 0.0; rhs[n - 1] = 0.0;
                    }
                    else  // Clamped
                    {
                        // 一阶导数边界 → 转换为二阶导数方程
                        diag[0] = 2.0 * h[0];
                        upper[0] = h[0];
                        rhs[0] = 6.0 * ((getCoord(FitPoints[1]) - getCoord(FitPoints[0])) / h[0]
                            - getCoord(StartTangent));

                        diag[n - 1] = 2.0 * h[N - 1];
                        lower[n - 1] = h[N - 1];
                        rhs[n - 1] = 6.0 * (getCoord(EndTangent)
                            - (getCoord(FitPoints[n - 1]) - getCoord(FitPoints[n - 2])) / h[N - 1]);
                    }

                    // Thomas 追赶法（三对角方程组求解，O(n)）
                    return ThomasSolve(lower, diag, upper, rhs);
                };

            std::vector<double> Mx = solve([](const Math::Point3& p) { return p.x; });
            std::vector<double> My = solve([](const Math::Point3& p) { return p.y; });
            std::vector<double> Mz = solve([](const Math::Point3& p) { return p.z; });

            // ── 由 M 值组装各段三次多项式系数 ────────────────────────────
            Segments.resize(N);
            for (int i = 0; i < N; ++i)
            {
                double hi = h[i];
                auto makeCoeffs = [&](int coord)
                    {
                        double p0 = (coord == 0) ? FitPoints[i].x : (coord == 1) ? FitPoints[i].y : FitPoints[i].z;
                        double p1 = (coord == 0) ? FitPoints[i + 1].x : (coord == 1) ? FitPoints[i + 1].y : FitPoints[i + 1].z;
                        double m0 = (coord == 0) ? Mx[i] : (coord == 1) ? My[i] : Mz[i];
                        double m1 = (coord == 0) ? Mx[i + 1] : (coord == 1) ? My[i + 1] : Mz[i + 1];

                        // 三次 Hermite 形式（从弦长参数转为局部 t∈[0,1]）
                        double a = p0;
                        double b = (p1 - p0) / hi - hi * (2.0 * m0 + m1) / 6.0;
                        double c = m0 / 2.0;
                        double d = (m1 - m0) / (6.0 * hi);
                        return std::make_tuple(a, b, c, d);
                    };

                auto [ax, bx, cx, dx] = makeCoeffs(0);
                auto [ay, by, cy, dy] = makeCoeffs(1);
                auto [az, bz, cz, dz] = makeCoeffs(2);

                // 将 t∈[0,hi] 变换为 u∈[0,1]：t = u·hi
                double h1 = hi, h2 = hi * hi, h3 = hi * hi * hi;
                Segments[i].A = { ax,            ay,            az };
                Segments[i].B = { bx * h1,         by * h1,         bz * h1 };
                Segments[i].C = { cx * h2,         cy * h2,         cz * h2 };
                Segments[i].D = { dx * h3,         dy * h3,         dz * h3 };
            }
        }

        // ── 闭合曲线：循环三对角方程组（Sherman-Morrison） ────────────────────
        void BuildClosed()
        {
            int n = static_cast<int>(FitPoints.size());

            // 闭合：在末尾虚拟追加起点
            std::vector<double> h(n);
            for (int i = 0; i < n - 1; ++i)
            {
                double dx = FitPoints[i + 1].x - FitPoints[i].x;
                double dy = FitPoints[i + 1].y - FitPoints[i].y;
                double dz = FitPoints[i + 1].z - FitPoints[i].z;
                h[i] = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), Math::LengthEPS);
            }
            // 末端→首端
            {
                double dx = FitPoints[0].x - FitPoints[n - 1].x;
                double dy = FitPoints[0].y - FitPoints[n - 1].y;
                double dz = FitPoints[0].z - FitPoints[n - 1].z;
                h[n - 1] = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), Math::LengthEPS);
            }

            auto solve = [&](auto getCoord) -> std::vector<double>
                {
                    std::vector<double> diag(n), upper(n), lower(n), rhs(n);
                    for (int i = 0; i < n; ++i)
                    {
                        int prev = (i + n - 1) % n;
                        int next = (i + 1) % n;
                        double hi1 = h[prev], hi = h[i];
                        diag[i] = 2.0 * (hi1 + hi);
                        upper[i] = hi;
                        lower[i] = hi1;
                        double fp = getCoord(FitPoints[prev]);
                        double fi = getCoord(FitPoints[i]);
                        double fnxt = getCoord(FitPoints[next]);
                        rhs[i] = 6.0 * ((fnxt - fi) / hi - (fi - fp) / hi1);
                    }
                    return CyclicThomasSolve(lower, diag, upper, rhs);
                };

            std::vector<double> Mx = solve([](const Math::Point3& p) { return p.x; });
            std::vector<double> My = solve([](const Math::Point3& p) { return p.y; });
            std::vector<double> Mz = solve([](const Math::Point3& p) { return p.z; });

            Segments.resize(n);
            for (int i = 0; i < n; ++i)
            {
                int next = (i + 1) % n;
                double hi = h[i];

                auto makeCoeffs = [&](int coord)
                    {
                        double p0 = (coord == 0) ? FitPoints[i].x : (coord == 1) ? FitPoints[i].y : FitPoints[i].z;
                        double p1 = (coord == 0) ? FitPoints[next].x : (coord == 1) ? FitPoints[next].y : FitPoints[next].z;
                        double m0 = (coord == 0) ? Mx[i] : (coord == 1) ? My[i] : Mz[i];
                        double m1 = (coord == 0) ? Mx[next] : (coord == 1) ? My[next] : Mz[next];
                        double a = p0;
                        double b = (p1 - p0) / hi - hi * (2.0 * m0 + m1) / 6.0;
                        double c = m0 / 2.0;
                        double d = (m1 - m0) / (6.0 * hi);
                        return std::make_tuple(a, b, c, d);
                    };

                auto [ax, bx, cx, dx_] = makeCoeffs(0);
                auto [ay, by, cy, dy_] = makeCoeffs(1);
                auto [az, bz, cz, dz_] = makeCoeffs(2);

                double h1 = hi, h2 = hi * hi, h3 = hi * hi * hi;
                Segments[i].A = { ax,    ay,    az };
                Segments[i].B = { bx * h1, by * h1, bz * h1 };
                Segments[i].C = { cx * h2, cy * h2, cz * h2 };
                Segments[i].D = { dx_ * h3,dy_ * h3,dz_ * h3 };
            }
        }

        // ── Thomas 追赶法（三对角系统）─────────────────────────────────────
        static std::vector<double> ThomasSolve(
            const std::vector<double>& l,
            const std::vector<double>& d,
            const std::vector<double>& u,
            const std::vector<double>& r)
        {
            int n = static_cast<int>(d.size());
            std::vector<double> c(n), x(n), dd(d), rr(r);

            // 前向消元
            c[0] = u[0] / dd[0];
            rr[0] = rr[0] / dd[0];
            for (int i = 1; i < n; ++i)
            {
                double m = l[i] / (dd[i] - l[i] * c[i - 1]);
                dd[i] = dd[i] - l[i] * c[i - 1];
                c[i] = u[i] / dd[i];
                rr[i] = (rr[i] - l[i] * rr[i - 1]) / dd[i];
            }
            // 回代
            x[n - 1] = rr[n - 1];
            for (int i = n - 2; i >= 0; --i)
                x[i] = rr[i] - c[i] * x[i + 1];
            return x;
        }

        // ── 循环三对角（Sherman-Morrison + Thomas）────────────────────────────
        static std::vector<double> CyclicThomasSolve(
            const std::vector<double>& l,
            const std::vector<double>& d,
            const std::vector<double>& u,
            const std::vector<double>& r)
        {
            int n = static_cast<int>(d.size());
            if (n == 1) return { r[0] / d[0] };

            double alpha = u[n - 1];  // 右上角
            double beta = l[0];    // 左下角
            double gamma = -d[0];

            // 修改对角线：d'[0] = d[0] - γ, d'[n-1] = d[n-1] - α·β/γ
            std::vector<double> dd(d);
            dd[0] -= gamma;
            dd[n - 1] -= alpha * beta / gamma;

            // 求解两个普通三对角系统
            std::vector<double> u_vec(n, 0.0);
            u_vec[0] = gamma;
            u_vec[n - 1] = alpha;

            auto y = ThomasSolve(l, dd, u, r);
            auto q = ThomasSolve(l, dd, u, u_vec);

            double factor = (y[0] + beta * y[n - 1] / gamma)
                / (1.0 + q[0] + beta * q[n - 1] / gamma);

            std::vector<double> x(n);
            for (int i = 0; i < n; ++i)
                x[i] = y[i] - factor * q[i];
            return x;
        }

    public:
        // ── 采样（用于渲染）──────────────────────────────────────────────────
        // samplesPerSeg：每段插值点数（不含末端）
        std::vector<Math::Point3> Tessellate(int samplesPerSeg = 32) const
        {
            std::vector<Math::Point3> pts;
            if (!IsValid()) return pts;

            pts.reserve(Segments.size() * samplesPerSeg + 1);

            for (const auto& seg : Segments)
            {
                for (int k = 0; k < samplesPerSeg; ++k)
                {
                    double t = static_cast<double>(k) / samplesPerSeg;
                    pts.push_back(seg.Evaluate(t));
                }
            }
            pts.push_back(Segments.back().Evaluate(1.0));   // 末端点
            return pts;
        }

        // ── 最近点（逐段二分法，快速近似）────────────────────────────────────
        Math::Point3 ClosestPoint(const Math::Point3& p, int samples = 128) const
        {
            if (!IsValid()) return p;

            Math::Point3 best = Segments[0].Evaluate(0.0);
            double bestD2 = Dist2(p, best);

            for (const auto& seg : Segments)
            {
                for (int k = 0; k <= samples; ++k)
                {
                    double t = static_cast<double>(k) / samples;
                    Math::Point3 q = seg.Evaluate(t);
                    double d2 = Dist2(p, q);
                    if (d2 < bestD2) { bestD2 = d2; best = q; }
                }
            }
            return best;
        }

        double DistanceToPoint(const Math::Point3& p) const
        {
            return std::sqrt(Dist2(p, ClosestPoint(p)));
        }

        // ── 包围盒（对每段采样取极值）────────────────────────────────────────
        AABB GetBounds() const
        {
            if (!IsValid()) return { {0,0,0},{0,0,0} };

            auto pts = Tessellate(32);
            double minX = pts[0].x, maxX = pts[0].x;
            double minY = pts[0].y, maxY = pts[0].y;
            double minZ = pts[0].z, maxZ = pts[0].z;

            for (const auto& q : pts)
            {
                minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
                minY = std::min(minY, q.y); maxY = std::max(maxY, q.y);
                minZ = std::min(minZ, q.z); maxZ = std::max(maxZ, q.z);
            }
            return { {minX,minY,minZ},{maxX,maxY,maxZ} };
        }

    private:
        static double Dist2(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            return dx * dx + dy * dy + dz * dz;
        }
    };
}