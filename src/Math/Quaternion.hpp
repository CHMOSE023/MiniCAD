// ============================================================
// MiniCAD — math/Quaternion.h
// 职责：四元数：slerp / 从轴角构造 / 转旋转矩阵
// 依赖：math/Vector.h, math/Matrix.h
// 约束：Real，不依赖第三方库
// ============================================================
#pragma once
#include "MathDefs.hpp"
#include "Vector.hpp"
#include "Matrix.hpp"

namespace MiniCAD {
    // 四元数结构体，表示旋转
    struct Quaternion {
        Real x, y, z, w; // w 为标量部分，(x,y,z) 为向量部分

        Quaternion() : x(0.f), y(0.f), z(0.f), w(1.f) {}  
        Quaternion(Real x, Real y, Real z, Real w) : x(x), y(y), z(z), w(w) {}

        static Quaternion Identity() { return { 0.f,0.f,0.f,1.f }; }  // 返回单位四元数

        // 根据轴向量和角度构造四元数
        // axis 必须归一化，angle 为弧度
        static Quaternion FromAxisAngle(const Vec3& axis, Real angle) { // radians
            Real half = angle * 0.5f;
            Real s = std::sin(half);
            return { axis.x * s, axis.y * s, axis.z * s, std::cos(half) };
        }

        // 四元数乘法（旋转组合）
        Quaternion operator*(const Quaternion& o) const {
            return {
                w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z
            };
        }

        // 四元数模平方
        Real LengthSq() const { return x * x + y * y + z * z + w * w; }

        // 四元数模
        Real Length()   const { return std::sqrt(LengthSq()); }

        // 返回归一化四元数
        Quaternion Normalized() const {
            Real l = Length();
            if (RealLess(0.f, l)) return { x / l, y / l, z / l, w / l };
            return Identity();
        }

        // 返回共轭四元数
        // 共轭用于反向旋转
        Quaternion Conjugate() const { return { -x,-y,-z, w }; }


        // 球面线性插值 (Slerp) 两个四元数
        // t ∈ [0,1]
        static Quaternion Slerp(const Quaternion& a, const Quaternion& b, Real t) {
            Real dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            Quaternion bb = b;
            if (dot < 0.f) { bb = { -b.x,-b.y,-b.z,-b.w }; dot = -dot; }
            if (dot > 0.9995f) {
                // linear interp
                Quaternion r{ a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t,
                             a.z + (bb.z - a.z) * t, a.w + (bb.w - a.w) * t };
                return r.Normalized();
            }
            Real theta0 = std::acos(dot);
            Real theta = theta0 * t;
            Real sinT = std::sin(theta);
            Real sinT0 = std::sin(theta0);
            Real s1 = std::cos(theta) - dot * sinT / sinT0;
            Real s2 = sinT / sinT0;
            return { s1 * a.x + s2 * bb.x, s1 * a.y + s2 * bb.y,
                     s1 * a.z + s2 * bb.z, s1 * a.w + s2 * bb.w };
        }

        // 将四元数转换为 4x4 旋转矩阵
        Mat4 ToMatrix() const {
            Real xx = x * x, yy = y * y, zz = z * z;
            Real xy = x * y, xz = x * z, yz = y * z;
            Real wx = w * x, wy = w * y, wz = w * z;
            Mat4 r = Mat4::Identity();
            r.m[0][0] = 1 - 2 * (yy + zz); r.m[1][0] = 2 * (xy - wz);   r.m[2][0] = 2 * (xz + wy);
            r.m[0][1] = 2 * (xy + wz);   r.m[1][1] = 1 - 2 * (xx + zz); r.m[2][1] = 2 * (yz - wx);
            r.m[0][2] = 2 * (xz - wy);   r.m[1][2] = 2 * (yz + wx);   r.m[2][2] = 1 - 2 * (xx + yy);
            return r;
        }
    };

} // namespace MiniCAD
