// ============================================================
// MiniCAD — math/Transform.h
// 职责：仿射变换管线，组合 Model→World→View→NDC→Screen 五级矩阵
// 依赖：math/Matrix.h, math/Quaternion.h
// 约束：列向量，v' = M * v
// ============================================================
#pragma once
#include "MathDefs.hpp"
#include "Matrix.hpp" 

namespace MiniCAD {

    struct Transform {
        Mat4 model;       // Model → World
        Mat4 view;        // World → View
        Mat4 projection;  // View  → NDC

        Transform()
            : model(Mat4::Identity())
            , view(Mat4::Identity())
            , projection(Mat4::Identity())
        {
        }

        // Combined MVP matrix (column-major: P * V * M)
        Mat4 MVP() const { return projection * view * model; }
          
        // Build orthographic projection
        // left/right/bottom/top/near/far
        static Mat4 Orthographic(Real l, Real r, Real b, Real t, Real n, Real f) {
            Mat4 m = Mat4::Identity();
            m.m[0][0] = 2.f / (r - l);
            m.m[1][1] = 2.f / (t - b);
            m.m[2][2] = -2.f / (f - n);
            m.m[3][0] = -(r + l) / (r - l);
            m.m[3][1] = -(t + b) / (t - b);
            m.m[3][2] = -(f + n) / (f - n);
            return m;
        }

        // Build perspective projection (fovY in radians, aspect = w/h)
        static Mat4 Perspective(Real fovY, Real aspect, Real n, Real f) { // fovY radians
            Real tanHalf = std::tan(fovY * 0.5f);
            Mat4 m;
            m.m[0][0] = 1.f / (aspect * tanHalf);
            m.m[1][1] = 1.f / tanHalf;
            m.m[2][2] = -(f + n) / (f - n);
            m.m[2][3] = -1.f;
            m.m[3][2] = -(2.f * f * n) / (f - n);
            return m;
        }

        // Build LookAt view matrix
        static Mat4 LookAt(const Point3& eye, const Point3& center, const Vec3& up) {
            Vec3 f = (center - eye).Normalized();
            Vec3 r = f.Cross(up).Normalized();
            Vec3 u = r.Cross(f);
            Mat4 m = Mat4::Identity();
            m.m[0][0] = r.x; m.m[1][0] = r.y; m.m[2][0] = r.z;
            m.m[0][1] = u.x; m.m[1][1] = u.y; m.m[2][1] = u.z;
            m.m[0][2] = -f.x; m.m[1][2] = -f.y; m.m[2][2] = -f.z;
            m.m[3][0] = -r.x * eye.x - r.y * eye.y - r.z * eye.z;
            m.m[3][1] = -u.x * eye.x - u.y * eye.y - u.z * eye.z;
            m.m[3][2] = f.x * eye.x + f.y * eye.y + f.z * eye.z;
            return m;
        }
    };

} // namespace MiniCAD
