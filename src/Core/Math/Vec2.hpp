#pragma once
#include <cmath>

namespace MiniCAD::Math
{
    struct Vec2
    {
        double x = 0.0, y = 0.0;

        constexpr Vec2() = default;
        constexpr Vec2(double xx, double yy) : x(xx), y(yy) {}

        Vec2 operator+(const Vec2& r)     const { return { x + r.x, y + r.y }; }
        Vec2 operator-(const Vec2& r)     const { return { x - r.x, y - r.y }; }
        Vec2 operator*(double s)          const { return { x * s,   y * s };   }
        Vec2 operator/(double s)          const { return { x / s,   y / s };   }
        Vec2 operator-()                  const { return { -x, -y }; }

        Vec2& operator+=(const Vec2& r)         { x += r.x; y += r.y; return *this; }
        Vec2& operator-=(const Vec2& r)         { x -= r.x; y -= r.y; return *this; }
        Vec2& operator*=(double s)              { x *= s; y *= s;     return *this; }
        Vec2& operator/=(double s)              { x /= s; y /= s;     return *this; }

        double LengthSq() const { return x * x + y * y; }
        double Length()   const { return std::sqrt(LengthSq()); }

        void Normalize()
        {
            double l = Length();
            if (l < 1e-12) return;
            x /= l; y /= l;
        }

        Vec2 Normalized() const
        {
            double l = Length();
            return (l < 1e-12) ? Vec2{} : Vec2{ x / l, y / l };
        }
    };

    inline Vec2 operator*(double s, const Vec2& v)
    {
        return { v.x * s, v.y * s };
    }
}
