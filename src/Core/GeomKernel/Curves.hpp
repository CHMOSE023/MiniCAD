#pragma once
#include "ICurve.hpp"
#include "Line.hpp"
#include "XLine.hpp"
#include "Arc.hpp"
#include "Circle.hpp"
#include "Ellipse.hpp"
#include "Polyline.hpp"
#include "PolylineOps.hpp"
#include "../Math/MathUtils.hpp"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    // =========================================================================
    // 具体参数曲线适配器
    //
    // 每个适配器按值持有对应的几何结构（结构都很小），复用其已有的几何计算，
    // 仅补齐 ICurve 要求的统一参数接口。值持有保证适配器可独立于源实体存活
    // （Trim / Extend 会就地修改副本后再写回）。
    //
    // 覆盖：直线 / 射线 / 构造线 / 圆弧 / 圆 / 椭圆 / 多段线。样条的适配器留待后续。
    // =========================================================================

    namespace detail
    {
        // 由两端点构造已规范化（Min ≤ Max 逐分量）的包围盒。
        inline AABB MakeAABB(const Math::Point3& a, const Math::Point3& b)
        {
            return
            {
                { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) },
                { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }
            };
        }
    }
    using detail::MakeAABB;

    // ── 直线段 ───────────────────────────────────────────────────────────────
    class LineCurve final : public ICurve
    {
    public:
        explicit LineCurve(const Line& l) : m_line(l) {}

        double TMin() const override { return 0.0; }
        double TMax() const override { return 1.0; }

        Math::Point3 PointAt(double t)   const override { return m_line.PointAt(t); }
        Math::Vec3   TangentAt(double)   const override { return m_line.Vector(); }
        double       ProjectParam(const Math::Point3& p) const override { return m_line.ProjectParam(p); }
        AABB         Bounds()            const override { return m_line.GetBounds(); }

        void Tessellate(std::vector<Math::Point3>& out, int) const override
        {
            out.push_back(m_line.Start);
            out.push_back(m_line.End);
        }

    private:
        Line m_line;
    };

    // ── 射线（半无限直线，t ∈ [0, +∞)，t 为沿单位方向的世界距离）──────────────
    class RayCurve final : public ICurve
    {
    public:
        explicit RayCurve(const XLine& r)
            : m_origin(r.Origin), m_dir(r.UnitDirection()) {}

        double TMin()      const override { return 0.0; }
        double TMax()      const override { return kCurveInf; }
        bool   IsBounded() const override { return false; }

        Math::Point3 PointAt(double t)   const override { return m_origin + m_dir * t; }
        Math::Vec3   TangentAt(double)   const override { return m_dir; }
        double       ProjectParam(const Math::Point3& p) const override { return Math::Dot(p - m_origin, m_dir); }
        AABB         Bounds()            const override { return MakeAABB(m_origin, m_origin + m_dir * kCurveHalfLength); }

        void Tessellate(std::vector<Math::Point3>& out, int) const override
        {
            out.push_back(m_origin);
            out.push_back(m_origin + m_dir * kCurveHalfLength);
        }

    private:
        Math::Point3 m_origin;
        Math::Vec3   m_dir;
    };

    // ── 构造线（无限直线，t ∈ (-∞, +∞)）──────────────────────────────────────
    class XLineCurve final : public ICurve
    {
    public:
        explicit XLineCurve(const XLine& xl)
            : m_origin(xl.Origin), m_dir(xl.UnitDirection()) {}

        double TMin()      const override { return -kCurveInf; }
        double TMax()      const override { return  kCurveInf; }
        bool   IsBounded() const override { return false; }

        Math::Point3 PointAt(double t)   const override { return m_origin + m_dir * t; }
        Math::Vec3   TangentAt(double)   const override { return m_dir; }
        double       ProjectParam(const Math::Point3& p) const override { return Math::Dot(p - m_origin, m_dir); }
        AABB         Bounds()            const override
        {
            return MakeAABB(m_origin - m_dir * kCurveHalfLength, m_origin + m_dir * kCurveHalfLength);
        }

        void Tessellate(std::vector<Math::Point3>& out, int) const override
        {
            out.push_back(m_origin - m_dir * kCurveHalfLength);
            out.push_back(m_origin + m_dir * kCurveHalfLength);
        }

    private:
        Math::Point3 m_origin;
        Math::Vec3   m_dir;
    };

    // ── 圆弧（t = 角度 ∈ [StartAngle, StartAngle+Sweep]）──────────────────────
    class ArcCurve final : public ICurve
    {
    public:
        explicit ArcCurve(const Arc& a) : m_arc(a) {}

        double TMin() const override { return m_arc.StartAngle; }
        double TMax() const override { return m_arc.StartAngle + m_arc.SweepAngle(); }

        Math::Point3 PointAt(double angle) const override { return m_arc.PointAt(angle); }
        Math::Vec3   TangentAt(double angle) const override
        {
            // d/dθ [Center + R·(cosθ, sinθ)] = R·(-sinθ, cosθ)（逆时针正向）
            return { -std::sin(angle) * m_arc.Radius, std::cos(angle) * m_arc.Radius, 0.0 };
        }
        double ProjectParam(const Math::Point3& p) const override
        {
            return std::atan2(p.y - m_arc.Center.y, p.x - m_arc.Center.x);
        }
        AABB Bounds() const override { return m_arc.GetBounds(); }

        // 角度环绕：交给 Arc 自身的范围判断与最近点。
        bool ContainsParam(double angle) const override { return m_arc.ContainsAngle(angle); }
        Math::Point3 ClosestPoint(const Math::Point3& p) const override { return m_arc.ClosestPoint(p); }

        void Tessellate(std::vector<Math::Point3>& out, int segHint) const override
        {
            const int segs = std::max(2, segHint);
            const double sweep = m_arc.SweepAngle();
            for (int i = 0; i <= segs; ++i)
                out.push_back(m_arc.PointAt(m_arc.StartAngle + sweep * i / segs));
        }

    private:
        Arc m_arc;
    };

    // ── 多段线（t = u ∈ [0, 段数]：整数部分是段号，小数部分是沿该段的进度，见 PolylineOps）──
    // 首尾重合的封闭多段线仍按开放曲线处理（IsClosed 为 false），参数 0 与段数是同一个点，
    // 需要绕过接缝的编辑（修剪 / 打断）用 PolylineOps::IsRing / RemoveBetween 单独处理。
    class PolylineCurve final : public ICurve
    {
    public:
        explicit PolylineCurve(const Polyline& pl) : m_pl(pl) {}

        double TMin() const override { return 0.0; }
        double TMax() const override { return PolylineOps::ParamCount(m_pl); }

        Math::Point3 PointAt(double u)   const override { return PolylineOps::PointAt(m_pl, u); }
        Math::Vec3   TangentAt(double u) const override { return PolylineOps::TangentAt(m_pl, u); }
        double       ProjectParam(const Math::Point3& p) const override { return PolylineOps::ParamOf(m_pl, p); }
        AABB         Bounds() const override { return m_pl.GetBounds(); }

        void Tessellate(std::vector<Math::Point3>& out, int) const override
        {
            const auto pts = m_pl.Tessellate(Math::PI / 90.0);       // 2°：求交精度比绘制高
            out.insert(out.end(), pts.begin(), pts.end());
        }

        const Polyline& Source() const { return m_pl; }

    private:
        Polyline m_pl;
    };

    // ── 圆（闭合，t = 角度 ∈ [0, 2π)）─────────────────────────────────────────
    class CircleCurve final : public ICurve
    {
    public:
        explicit CircleCurve(const Circle& c) : m_circle(c) {}

        double TMin()     const override { return 0.0; }
        double TMax()     const override { return Math::TwoPI; }
        bool   IsClosed() const override { return true; }

        Math::Point3 PointAt(double angle) const override { return m_circle.PointAt(angle); }
        Math::Vec3   TangentAt(double angle) const override
        {
            return { -std::sin(angle) * m_circle.Radius, std::cos(angle) * m_circle.Radius, 0.0 };
        }
        double ProjectParam(const Math::Point3& p) const override
        {
            return std::atan2(p.y - m_circle.Center.y, p.x - m_circle.Center.x);
        }
        AABB Bounds() const override { return m_circle.GetBounds(); }
        Math::Point3 ClosestPoint(const Math::Point3& p) const override { return m_circle.ClosestPoint(p); }

        void Tessellate(std::vector<Math::Point3>& out, int segHint) const override
        {
            const int segs = std::max(8, segHint);
            for (int i = 0; i <= segs; ++i)
                out.push_back(m_circle.PointAt(Math::TwoPI * i / segs));
        }

    private:
        Circle m_circle;
    };

    // ── 椭圆 / 椭圆弧（t = 参数角；整椭圆闭合 ∈ [0, 2π)，椭圆弧 ∈ [StartParam, StartParam+Sweep]）──
    class EllipseCurve final : public ICurve
    {
    public:
        explicit EllipseCurve(const Ellipse& e) : m_ellipse(e) {}

        double TMin()     const override { return m_ellipse.IsFull() ? 0.0 : m_ellipse.StartParam; }
        double TMax()     const override { return m_ellipse.IsFull() ? Math::TwoPI : m_ellipse.StartParam + m_ellipse.SweepParam(); }
        bool   IsClosed() const override { return m_ellipse.IsFull(); }

        Math::Point3 PointAt(double t) const override { return m_ellipse.PointAt(t); }
        Math::Vec3   TangentAt(double t) const override
        {
            // d/dt P(t) = -Rx·sin(t)·[cosR, sinR] + Ry·cos(t)·[-sinR, cosR]
            const double cosR = std::cos(m_ellipse.Rotation);
            const double sinR = std::sin(m_ellipse.Rotation);
            const double cosT = std::cos(t);
            const double sinT = std::sin(t);
            return
            {
                -m_ellipse.RadiusX * sinT * cosR - m_ellipse.RadiusY * cosT * sinR,
                -m_ellipse.RadiusX * sinT * sinR + m_ellipse.RadiusY * cosT * cosR,
                0.0
            };
        }
        // 整椭圆上最近点的参数角（投影到无界母曲线，即整椭圆）。
        double ProjectParam(const Math::Point3& p) const override
        {
            return Ellipse::WrapParam(m_ellipse.ClosestParamFull(p));
        }
        AABB Bounds() const override { return m_ellipse.GetBounds(); }

        // 参数环绕：交给 Ellipse 自身的范围判断与最近点。
        bool ContainsParam(double t) const override { return m_ellipse.ContainsParam(t); }
        Math::Point3 ClosestPoint(const Math::Point3& p) const override { return m_ellipse.ClosestPoint(p); }

        void Tessellate(std::vector<Math::Point3>& out, int segHint) const override
        {
            const int    segs  = std::max(m_ellipse.IsFull() ? 8 : 2, segHint);
            const double t0    = TMin();
            const double sweep = TMax() - t0;
            for (int i = 0; i <= segs; ++i)
                out.push_back(m_ellipse.PointAt(t0 + sweep * i / segs));
        }

    private:
        Ellipse m_ellipse;
    };
}
