// ============================================================
// MiniCAD — math/Vector.h
// 职责：Vec2 / Vec3 / Vec4 向量类型
// 依赖：math/MathDefs.h
// 约束：不依赖任何第三方库，使用 Real
// ============================================================
#pragma once
#include "MathDefs.hpp"
#include <cmath>

namespace MiniCAD {

    // ─────────────────────────────────────────────────────────
    // Vec2 — 2D 向量（屏幕坐标 / UV）
    // ─────────────────────────────────────────────────────────
    struct Vec2 {
        Real x, y;

        Vec2() : x(0), y(0) {}
        Vec2(Real x, Real y) : x(x), y(y) {}

        static Vec2 Zero() { return { 0, 0 }; }
        static Vec2 UnitX() { return { 1, 0 }; }
        static Vec2 UnitY() { return { 0, 1 }; }

        Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
        Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
        Vec2 operator*(Real s)        const { return { x * s,   y * s }; }
        Vec2 operator/(Real s)        const { return { x / s,   y / s }; }

        Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
        Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
        Vec2& operator*=(Real s) { x *= s;   y *= s;   return *this; }

        bool operator==(const Vec2& o) const {
            return RealEqual(x, o.x) && RealEqual(y, o.y);
        }
        bool operator!=(const Vec2& o) const { return !(*this == o); }

        Real Dot(const Vec2& o)  const { return x * o.x + y * o.y; }
        Real LengthSq()          const { return Dot(*this); }
        Real Length()            const { return std::sqrt(LengthSq()); }

        Vec2 Normalized() const {
            Real l = Length();
            return RealLess(0, l) ? (*this) / l : Zero();
        }
    };

    inline Vec2 operator*(Real s, const Vec2& v) { return v * s; }

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

    inline Vec3 operator*(Real s, const Vec3& v) { return v * s; }


    // -----------------------------------------------------------------
    struct Vec4 {
        Real x, y, z, w;

        Vec4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
        Vec4(Real x, Real y, Real z, Real w) : x(x), y(y), z(z), w(w) {}
        Vec4(const Vec3& v, Real w) : x(v.x), y(v.y), z(v.z), w(w) {}

        static Vec4 Zero() { return { 0.f,0.f,0.f,0.f }; }

        Vec4 operator+(const Vec4& o) const { return { x + o.x, y + o.y, z + o.z, w + o.w }; }
        Vec4 operator-(const Vec4& o) const { return { x - o.x, y - o.y, z - o.z, w - o.w }; }
        Vec4 operator*(Real s)       const { return { x * s,   y * s,   z * s,   w * s }; }
        Vec4 operator/(Real s)       const { return { x / s,   y / s,   z / s,   w / s }; }

        Vec4& operator+=(const Vec4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
        Vec4& operator-=(const Vec4& o) { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
        Vec4& operator*=(Real s) { x *= s;   y *= s;   z *= s;   w *= s;   return *this; }

        bool operator==(const Vec4& o) const {
            return RealEqual(x, o.x) && RealEqual(y, o.y) && RealEqual(z, o.z) && RealEqual(w, o.w);
        }
        bool operator!=(const Vec4& o) const { return !(*this == o); }

        Real Dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
        Vec3  XYZ() const { return { x, y, z }; }
    };

    inline Vec4 operator*(Real s, const Vec4& v) { return v * s; }

} // namespace MiniCAD 
