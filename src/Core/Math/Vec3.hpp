#pragma once
#include <cmath>

namespace MiniCAD::Math
{
    struct Vec3
    {
        double x = 0.0, y = 0.0, z = 0.0;

        constexpr Vec3() = default;
        constexpr Vec3(double xx, double yy, double zz) : x(xx), y(yy), z(zz) {}

        Vec3 operator+(const Vec3& r)   const { return { x + r.x,  y + r.y,  z + r.z }; }
        Vec3 operator-(const Vec3& r)   const { return { x - r.x,  y - r.y,  z - r.z }; }
        Vec3 operator*(double s)        const { return { x * s,    y * s,    z * s   }; }
        Vec3 operator/(double s)        const { return { x / s,    y / s,    z / s   }; }
        Vec3 operator-()                const { return { -x,       -y,       -z      }; }

        Vec3& operator+=(const Vec3& r)       { x += r.x; y += r.y;  z += r.z;  return *this; }
        Vec3& operator-=(const Vec3& r)       { x -= r.x; y -= r.y;  z -= r.z;  return *this; }
        Vec3& operator*=(double s)            { x *= s;    y *= s;   z *= s;    return *this; }
        Vec3& operator/=(double s)            { x /= s;    y /= s;   z /= s;    return *this; }

        double LengthSq()   const { return x * x + y * y + z * z; }
        double Length()     const { return std::sqrt(LengthSq()); }
        Vec3   Normalized() const { double l = Length(); return (l < 1e-12) ? Vec3{} : (*this / l); }
        void   Normalize()        { double l = Length(); if (l < 1e-12) return; x /= l; y /= l; z /= l; }
    };

    inline Vec3 operator*(double s, const Vec3& v) { return { v.x * s, v.y * s, v.z * s }; }
}
