#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Constants.hpp"
#include "AABB.hpp"
#include <vector>
#include <limits>

namespace MiniCAD
{
    // =========================================================================
    // ICurve —— 参数曲线统一抽象
    //
    // 把所有「线形」几何（Line / Ray / XLine / Arc / Circle / Ellipse …）以同一
    // 套参数接口暴露出来，让裁剪(Trim)、延伸(Extend)、求交(Intersect)、捕捉(Snap)
    // 等编辑算法不必再对具体类型写 if / switch 两两分派 —— 这是「直线、射线、圆、
    // 椭圆、圆弧没有公共基类导致编辑麻烦」问题的根。
    //
    // 参数 t 的语义随曲线而异，但对外统一为「沿曲线单调推进的参数」：
    //   · Line   : t ∈ [0, 1]          PointAt(t) = Start + t·(End-Start)
    //   · Ray    : t ∈ [0, +∞)         PointAt(t) = Origin + t·UnitDir   (t 即世界距离)
    //   · XLine  : t ∈ (-∞, +∞)        PointAt(t) = Origin + t·UnitDir
    //   · Arc    : t = 角度 ∈ [StartAngle, StartAngle+Sweep]
    //   · Circle : t = 角度 ∈ [0, 2π)，闭合
    //   · Ellipse: t = 参数角 ∈ [0, 2π)，闭合
    //
    // 实现者只需提供少量纯虚原语（PointAt / TangentAt / ProjectParam / Tessellate
    // / Bounds + 参数域），ClosestPoint / DistanceToPoint 等由基类派生。需要更精确
    // 实现的曲线（如 Ellipse）可覆写 ClosestPoint。
    // =========================================================================
    class ICurve
    {
    public:
        virtual ~ICurve() = default;

        // ── 参数域 ─────────────────────────────────────────────────────────
        virtual double TMin() const = 0;
        virtual double TMax() const = 0;
        virtual bool   IsClosed()  const { return false; }  // 圆 / 椭圆：首尾相接
        virtual bool   IsBounded() const { return true;  }  // false：Ray / XLine 向无限延伸

        // ── 求值原语 ───────────────────────────────────────────────────────
        // 参数点
        virtual Math::Point3 PointAt(double t)   const = 0;
        // 切向（未归一化，方向沿参数 t 增大）
        virtual Math::Vec3   TangentAt(double t) const = 0;
        // 点 p 投影到「无界母曲线」上的参数（可能落在 [TMin,TMax] 之外）。
        // 圆 / 椭圆返回域内角度。
        virtual double       ProjectParam(const Math::Point3& p) const = 0;
        // 折线化（世界坐标），通用算法（求交 / 渲染）的兜底离散表示。
        // 无界曲线以一段足够长的近似区间离散。
        virtual void         Tessellate(std::vector<Math::Point3>& out, int segHint = 64) const = 0;
        // 轴对齐包围盒（无界曲线为其离散近似区间的包围盒）。
        virtual AABB         Bounds() const = 0;

        // ── 派生便捷（基于上面的原语，子类一般无需覆写）──────────────────────
        Math::Point3 StartPoint() const { return PointAt(TMin()); }
        Math::Point3 EndPoint()   const { return PointAt(TMax()); }

        // 把参数夹到有效域内（闭合曲线按周期处理，不夹）。
        double ClampParam(double t) const
        {
            if (IsClosed()) return t;
            if (t < TMin()) return TMin();
            if (t > TMax()) return TMax();
            return t;
        }

        // 参数是否落在有效域内。Arc 等需角度环绕判断者应覆写。
        virtual bool ContainsParam(double t) const
        {
            if (IsClosed()) return true;
            return t >= TMin() - Math::LengthEPS && t <= TMax() + Math::LengthEPS;
        }

        // 域内最近点。默认：投影参数 → 夹取 → 求值。
        // 对线段 / 射线 / 构造线 / 圆 / 椭圆均正确；Arc 因角度环绕需覆写。
        virtual Math::Point3 ClosestPoint(const Math::Point3& p) const
        {
            return PointAt(ClampParam(ProjectParam(p)));
        }

        double DistanceToPoint(const Math::Point3& p) const
        {
            return (p - ClosestPoint(p)).Length();
        }
    };

    // 无界曲线的参数极值（Ray 的 TMax / XLine 的 TMin、TMax）。
    inline constexpr double kCurveInf = std::numeric_limits<double>::infinity();

    // 无界曲线折线化时单侧近似「无限」的世界半长（与实体绘制保持量级一致）。
    inline constexpr double kCurveHalfLength = 1.0e6;
}
