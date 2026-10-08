#pragma once
#include <cmath>

namespace MiniGUI
{
    // 界面坐标统一使用 float（逻辑像素 DIP），与 MiniCAD 几何内核的 double 类型区分开
    struct Vec2
    {
        float x = 0.0f, y = 0.0f;

        constexpr Vec2() = default;
        constexpr Vec2(float xx, float yy) : x(xx), y(yy) {}

        constexpr Vec2 operator+(const Vec2& r) const { return { x + r.x, y + r.y }; }
        constexpr Vec2 operator-(const Vec2& r) const { return { x - r.x, y - r.y }; }
        constexpr Vec2 operator*(float s)       const { return { x * s,   y * s   }; }
        constexpr Vec2 operator/(float s)       const { return { x / s,   y / s   }; }
        constexpr Vec2 operator-()              const { return { -x, -y }; }

        Vec2& operator+=(const Vec2& r) { x += r.x; y += r.y; return *this; }
        Vec2& operator-=(const Vec2& r) { x -= r.x; y -= r.y; return *this; }
        Vec2& operator*=(float s)       { x *= s;   y *= s;   return *this; }
        Vec2& operator/=(float s)       { x /= s;   y /= s;   return *this; }

        constexpr bool operator==(const Vec2& r) const { return x == r.x && y == r.y; }
        constexpr bool operator!=(const Vec2& r) const { return !(*this == r); }

        float LengthSq() const { return x * x + y * y; }
        float Length()   const { return std::sqrt(LengthSq()); }

        Vec2 Normalized() const
        {
            float l = Length();
            return (l < 1e-12f) ? Vec2{} : Vec2{ x / l, y / l };
        }
    };

    inline constexpr Vec2 operator*(float s, const Vec2& v)
    {
        return { v.x * s, v.y * s };
    }
}
