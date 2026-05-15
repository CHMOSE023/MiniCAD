#pragma once

namespace MiniCAD::Math
{
	inline constexpr double LengthEPS = 1e-9;   // 用于长度比较的容差
    inline constexpr double AngleEPS  = 1e-12;  // 用于角度比较的容差（弧度）
    inline constexpr double AreaEPS   = 1e-12;  // 用于面积比较的容差
    inline constexpr double PI        = 3.14159265358979323846;
    inline constexpr double HalfPI    = PI * 0.5;
    inline constexpr double TwoPI     = PI * 2.0;
}
