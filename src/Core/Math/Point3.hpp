#pragma once
#include "Vec3.hpp"

namespace MiniCAD::Math
{
    struct Point3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        constexpr Point3() = default;
        constexpr Point3(double xx, double yy, double zz) : x(xx), y(yy), z(zz) { }
    };

    inline Vec3    operator-(const Point3& a, const Point3& b) { return { a.x - b.x,  a.y - b.y,  a.z - b.z }; }
    inline Point3  operator+(const Point3& p, const Vec3& v)   { return { p.x + v.x,  p.y + v.y,  p.z + v.z }; }
    inline Point3  operator+(const Vec3& v, const Point3& p)   { return p + v; }
    inline Point3  operator-(const Point3& p, const Vec3& v)   { return { p.x - v.x, p.y - v.y,  p.z - v.z };  }

    inline Point3& operator+=(Point3& p, const Vec3& v)        { p.x += v.x;  p.y += v.y; p.z += v.z; return p;}
    inline Point3& operator-=(Point3& p, const Vec3& v)        { p.x -= v.x;p.y -= v.y;   p.z -= v.z; return p;} 
}
