#pragma once
#include <cmath>
#include <limits>

namespace MiniCAD {

    // 数学库使用的基础数值类型
    using Real = double;

    //-----------------------------------------------------
    // 常用数学常量
    //-----------------------------------------------------

    constexpr Real MINICAD_PI = 3.1415926535897932384626433832795;       // π
    constexpr Real MINICAD_TWO_PI = 2.0 * MINICAD_PI;                    // 2π
    constexpr Real MINICAD_HALF_PI = 0.5 * MINICAD_PI;                   // π/2

    // 浮点比较容差
    constexpr Real MINICAD_EPSILON = 1e-9;

    // 正无穷
    constexpr Real MINICAD_INF = std::numeric_limits<Real>::infinity();

    //-----------------------------------------------------
    // 浮点比较辅助函数
    //-----------------------------------------------------

    // 判断 a ≈ b
    inline bool RealEqual(Real a, Real b)
    {
        return std::fabs(a - b) <= MINICAD_EPSILON;
    }

    // 判断 a < b（带容差）
    inline bool RealLess(Real a, Real b)
    {
        return (b - a) > MINICAD_EPSILON;
    }

    // 判断 a > b（带容差）
    inline bool RealGreater(Real a, Real b)
    {
        return (a - b) > MINICAD_EPSILON;
    }

    // 判断 a ≤ b（带容差）
    inline bool RealLessEqual(Real a, Real b)
    {
        return !RealGreater(a, b);
    }

    // 判断 a ≥ b（带容差）
    inline bool RealGreaterEqual(Real a, Real b)
    {
        return !RealLess(a, b);
    }

    // 判断 a ≈ 0
    inline bool RealZero(Real a)
    {
        return std::fabs(a) <= MINICAD_EPSILON;
    }

} // namespace MiniCAD
