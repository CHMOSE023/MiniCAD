// ============================================================
// MiniCAD — math/Vector.h
// 职责：Vec2 / Vec3 / Vec4 向量类型
// 依赖：math/MathDefs.h
// 约束：不依赖任何第三方库，使用 float
// ============================================================
#pragma once
#include "MathDefs.hpp"
#include <cmath>

namespace MiniCAD {

    // -----------------------------------------------------------------
    struct Vec3 {
        Real x, y, z;

        Vec3() : x(0), y(0), z(0) {}
        Vec3(Real x, Real y, Real z) : x(x), y(y), z(z) {}

        static Vec3 Zero() { return { 0,0,0 }; }
        static Vec3 UnitX() { return { 1,0,0 }; }
        static Vec3 UnitY() { return { 0,1,0 }; }
        static Vec3 UnitZ() { return { 0,0,1 }; }

        Vec3 operator+(const Vec3& o) const { return { x + o.x,y + o.y,z + o.z }; }
        Vec3 operator-(const Vec3& o) const { return { x - o.x,y - o.y,z - o.z }; }
        Vec3 operator*(Real s) const { return { x * s,y * s,z * s }; }
        Vec3 operator/(Real s) const { return { x / s,y / s,z / s }; }

        Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        Vec3& operator*=(Real s) { x *= s; y *= s; z *= s; return *this; }

        bool operator==(const Vec3& o) const {
            return RealEqual(x, o.x) && RealEqual(y, o.y) && RealEqual(z, o.z);
        }

        bool operator!=(const Vec3& o) const { return !(*this == o); }

        Real Dot(const Vec3& o) const {
            return x * o.x + y * o.y + z * o.z;
        }

        Vec3 Cross(const Vec3& o) const {
            return {
                y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x
            };
        }

        Real LengthSq() const { return Dot(*this); }

        Real Length() const { return std::sqrt(LengthSq()); }

        Vec3 Normalized() const {
            Real l = Length();
            return RealLess(0, l) ? (*this) / l : Zero();
        }
    };

    inline Vec3 operator*(float s, const Vec3& v) { return v * s; } 

} // namespace MiniCAD
