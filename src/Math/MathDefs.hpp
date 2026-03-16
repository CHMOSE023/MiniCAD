#pragma once
#include <cmath>
#include <limits>

namespace MiniCAD {

    using Real = double;

    //-----------------------------------------------------
    // 常用数学常量
    //-----------------------------------------------------
    constexpr Real MINICAD_PI      = 3.1415926535897932384626433832795;
    constexpr Real MINICAD_TWO_PI  = 2.0 * MINICAD_PI;
    constexpr Real MINICAD_HALF_PI = 0.5 * MINICAD_PI;
    constexpr Real MINICAD_EPSILON = 1e-9;   // double 精度下建议用 1e-9，1e-6 偏松
    constexpr Real MINICAD_INF     = std::numeric_limits<Real>::infinity();

    //-----------------------------------------------------
    // 浮点比较内联函数（推荐在普通代码中使用）
    //-----------------------------------------------------
    inline bool RealEqual       (Real a, Real b) { return std::fabs(a - b) <= MINICAD_EPSILON; }
    inline bool RealLess        (Real a, Real b) { return (b - a) > MINICAD_EPSILON; }
    inline bool RealGreater     (Real a, Real b) { return (a - b) > MINICAD_EPSILON; }
    inline bool RealLessEqual   (Real a, Real b) { return !RealGreater(a, b); }
    inline bool RealGreaterEqual(Real a, Real b) { return !RealLess(a, b); }
    inline bool RealZero        (Real a)         { return std::fabs(a) <= MINICAD_EPSILON; }

} // namespace MiniCAD

//-----------------------------------------------------
// 浮点比较宏（用于 assert / 模板 / 宏内部）
// 注意：宏参数有副作用时需加括号保护，已处理
//-----------------------------------------------------
#define FLOAT_EQ(a, b)  (std::fabs((a) - (b)) <= ::MiniCAD::MINICAD_EPSILON)
#define FLOAT_LT(a, b)  (((b) - (a))           >  ::MiniCAD::MINICAD_EPSILON)
#define FLOAT_GT(a, b)  FLOAT_LT((b), (a))
#define FLOAT_LE(a, b)  (!FLOAT_GT((a), (b)))
#define FLOAT_GE(a, b)  (!FLOAT_LT((a), (b)))
#define FLOAT_ZERO(a)   (std::fabs(a)           <= ::MiniCAD::MINICAD_EPSILON) 