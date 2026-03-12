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
