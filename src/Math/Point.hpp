// ============================================================
// MiniCAD — math/Point.h
// 职责：Point3 点类型，支持齐次坐标转换
// 依赖：math/Vector.h
// 约束：不依赖任何第三方库
// ============================================================
#pragma once
#include "MathDefs.hpp"
#include "Vector.hpp"

namespace MiniCAD { 

    struct Point2 {
        Real x, y;

        Point2() : x(0.f), y(0.f) {}
        Point2(Real x, Real y) : x(x), y(y) {}

        static Point2 Origin() { return { 0.f, 0.f }; }

        Vec2    operator-(const Point2& o) const { return { x - o.x, y - o.y }; }
        Point2  operator+(const Vec2& v)   const { return { x + v.x, y + v.y }; }
        Point2  operator-(const Vec2& v)   const { return { x - v.x, y - v.y }; }
        Point2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }

        bool operator==(const Point2& o) const { return RealEqual(x, o.x) && RealEqual(y, o.y); }
        bool operator!=(const Point2& o) const { return !(*this == o); }

        Vec3 ToHomogeneous() const { return { x, y, 1.f }; }
        Vec2 ToVec2()        const { return { x, y }; }
    };

    struct Point3 {
        Real x, y, z;

        Point3() : x(0.f), y(0.f), z(0.f) {}
        Point3(Real x, Real y, Real z) : x(x), y(y), z(z) {}

        static Point3 Origin() { return { 0.f, 0.f, 0.f }; }

        Vec3    operator-(const Point3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Point3  operator+(const Vec3& v)   const { return { x + v.x, y + v.y, z + v.z }; }
        Point3  operator-(const Vec3& v)   const { return { x - v.x, y - v.y, z - v.z }; }
        Point3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }

        bool operator==(const Point3& o) const {
            return RealEqual(x, o.x) && RealEqual(y, o.y) && RealEqual(z, o.z);
        }
        bool operator!=(const Point3& o) const { return !(*this == o); }
         
        Vec3 ToVec3()        const { return { x, y, z }; }

        Real DistanceTo(const Point3& o) const { return (*this - o).Length(); }
    };

} // namespace MiniCAD
