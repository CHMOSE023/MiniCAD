#pragma once
#include "Vec2.hpp"

namespace MiniCAD::Math
{
    struct Point2
    {
        double x = 0.0;
        double y = 0.0;
        constexpr Point2() = default;
        constexpr Point2(double xx, double yy) : x(xx) , y(yy) {}
    };
       
    inline Vec2   operator-(const Point2& a, const Point2& b) { return { a.x - b.x,  a.y - b.y }; }
    inline Point2 operator+(const Point2& p, const Vec2& v)   { return { p.x + v.x,   p.y + v.y }; } 
    inline Point2 operator-(const Point2& p, const Vec2& v)   { return { p.x - v.x,   p.y - v.y }; }     

    inline Point2& operator+=(Point2& p, const Vec2& v) { p.x += v.x;  p.y += v.y; return p; }
    inline Point2& operator-=(Point2& p, const Vec2& v) { p.x -= v.x;  p.y -= v.y; return p; }

   
}
