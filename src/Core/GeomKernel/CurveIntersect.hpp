#pragma once
#include "ICurve.hpp"
#include <vector>

namespace MiniCAD::Geom
{
    // =========================================================================
    // 曲线求交（通用）
    //
    // 两条曲线各自折线化后逐段做线段-线段求交，再按 tol 合并近邻交点。对任意
    // ICurve 组合一致工作 —— 这正是引入 ICurve 抽象后「求交不再按类型两两分派」
    // 的直接体现（Trim / Extend / 交点捕捉均可在其上构建）。
    //
    // 结果为 XY 平面上的交点世界坐标；z 取自曲线 a 对应线段的插值。
    // segHint 控制离散精度（曲线段数，默认 64）；tol 为交点去重的世界容差。
    // =========================================================================
    std::vector<Math::Point3> IntersectCurves(const ICurve& a, const ICurve& b,
                                              double tol = 1e-6, int segHint = 64);
}
