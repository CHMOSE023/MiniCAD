#pragma once
#include <cmath>
#include "Vec3.hpp"
#include "Point3.hpp"

namespace MiniCAD::Math
{
    // =========================================================
    // 约定（非常重要）
    // =========================================================
    // CPU 侧统一：
    // 1. Row-major 存储
    // 2. Row-vector 语义（逻辑上）
    // 3. 变换表达：P' = P * M（概念上）
    // m[0]  m[1]  m[2]  m[3]
    // m[4]  m[5]  m[6]  m[7]
    // m[8]  m[9]  m[10] m[11]
    // m[12] m[13] m[14] m[15]
    //
    // GPU 侧：
    // - OpenGL: 需要 transpose
    // - D3D:    可直接或 row_major

    // =========================================================

    struct Mat4
    {
        double m[16] =
        {
            0,0,0,0,
            0,0,0,0,
            0,0,0,0,
            0,0,0,0
        };

        // =========================
        // Identity
        // =========================
        static Mat4 Identity()
        {
            Mat4 r;

            r.m[0] = 1;
            r.m[5] = 1;
            r.m[10] = 1;
            r.m[15] = 1;

            return r;
        }

        static Mat4 Zero()
        {
            return Mat4{};
        }

        // =========================
        // Transform 构造
        // =========================
        static Mat4 Translation(const Vec3& t)
        {
            Mat4 r = Identity();

            r.m[12] = t.x;
            r.m[13] = t.y;
            r.m[14] = t.z;

            return r;
        }

        static Mat4 Scale(const Vec3& s)
        {
            Mat4 r = {};

            r.m[0] = s.x;
            r.m[5] = s.y;
            r.m[10] = s.z;
            r.m[15] = 1.0;

            return r;
        }

        static Mat4 RotationX(double rad)
        {
            Mat4 r = Identity();

            double c = std::cos(rad);
            double s = std::sin(rad);

            r.m[5] = c;
            r.m[6] = s;
            r.m[9] = -s;
            r.m[10] = c;

            return r;
        }

        static Mat4 RotationY(double rad)
        {
            Mat4 r = Identity();

            double c = std::cos(rad);
            double s = std::sin(rad);

            r.m[0] = c;
            r.m[2] = -s;
            r.m[8] = s;
            r.m[10] = c;

            return r;
        }

        static Mat4 RotationZ(double rad)
        {
            Mat4 r = Identity();

            double c = std::cos(rad);
            double s = std::sin(rad);

            r.m[0] = c;
            r.m[1] = s;

            r.m[4] = -s;
            r.m[5] = c;

            return r;
        }

        // =========================
        // 矩阵乘法（组合变换）
        // =========================
        Mat4 operator*(const Mat4& rhs) const
        {
            Mat4 r = {};

            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    double sum = 0.0;

                    for (int k = 0; k < 4; ++k)
                    {
                        sum += m[row * 4 + k] * rhs.m[k * 4 + col];
                    }

                    r.m[row * 4 + col] = sum;
                }
            }

            return r;
        }

        // =========================
        // 向量 / 点变换
        // =========================

        Vec3 TransformVector(const Vec3& v) const
        {
            return {
                v.x * m[0] + v.y * m[4] + v.z * m[8],
                v.x * m[1] + v.y * m[5] + v.z * m[9],
                v.x * m[2] + v.y * m[6] + v.z * m[10]
            };
        }

        Point3 TransformPoint(const Point3& p) const
        {
            return {
                p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
                p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
                p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]
            };
        }

        // =========================
        // 语法糖
        // =========================

        Vec3 operator*(const Vec3& v) const
        {
            return TransformVector(v);
        }

        Point3 operator*(const Point3& p) const
        {
            return TransformPoint(p);
        }

        // =========================
        // GPU 兼容工具
        // =========================

        double*       Data()       { return m; }
        const double* Data() const { return m; }

        // OpenGL 使用（列主序）
        Mat4 Transposed() const
        {
            Mat4 r;

            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    r.m[col * 4 + row] = m[row * 4 + col];
                }
            }

            return r;
        }
    };
}
