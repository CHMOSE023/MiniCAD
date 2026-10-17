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
            Mat4 r = {};

            r.m[0]  = 1;
            r.m[5]  = 1;
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

            r.m[0]  = s.x;
            r.m[5]  = s.y;
            r.m[10] = s.z;
            r.m[15] = 1.0;

            return r;
        }

        static Mat4 RotationX(double rad)
        {
            Mat4 r = Identity();

            double c = std::cos(rad);
            double s = std::sin(rad);

            r.m[5]  = c;
            r.m[6]  = s;
            r.m[9]  = -s;
            r.m[10] = c;

            return r;
        }

        static Mat4 RotationY(double rad)
        {
            Mat4 r = Identity();

            double c = std::cos(rad);
            double s = std::sin(rad);

            r.m[0]  = c;
            r.m[2]  = -s;
            r.m[8]  = s;
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
        // 投影矩阵构造
        // =========================

        /// 正交投影（Left-Handed，D3D 深度 [0,1]，行向量约定 P' = P * M）
        ///   x: [-w/2, w/2] → [-1, 1]
        ///   y: [-h/2, h/2] → [-1, 1]
        ///   z: [near, far] → [ 0, 1]
        static Mat4 OrthoLH(double w, double h, double nearZ, double farZ)
        {
            Mat4 r = Zero();
            r.m[0]  =  2.0 / w;
            r.m[5]  =  2.0 / h;
            r.m[10] =  1.0 / (farZ - nearZ);
            r.m[14] = -nearZ / (farZ - nearZ);
            r.m[15] =  1.0;
            return r;
        }

        // =========================
        // 逆矩阵
        // =========================

        /// 4×4 通用逆矩阵（余子式 / 伴随矩阵法）
        /// 矩阵奇异（|det| 相对 Hadamard 上界过小）时返回零矩阵
        static Mat4 Inverse(const Mat4& mat)
        {
            const double* a = mat.m;
            double inv[16];

            inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15]  + a[9]*a[7]*a[14]  + a[13]*a[6]*a[11]  - a[13]*a[7]*a[10];
            inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15]  - a[8]*a[7]*a[14]  - a[12]*a[6]*a[11]  + a[12]*a[7]*a[10];
            inv[8]  =  a[4]*a[9] *a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15]  + a[8]*a[7]*a[13]  + a[12]*a[5]*a[11]  - a[12]*a[7]*a[9];
            inv[12] = -a[4]*a[9] *a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14]  - a[8]*a[6]*a[13]  - a[12]*a[5]*a[10]  + a[12]*a[6]*a[9];

            inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15]  - a[9]*a[3]*a[14]  - a[13]*a[2]*a[11]  + a[13]*a[3]*a[10];
            inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15]  + a[8]*a[3]*a[14]  + a[12]*a[2]*a[11]  - a[12]*a[3]*a[10];
            inv[9]  = -a[0]*a[9] *a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15]  - a[8]*a[3]*a[13]  - a[12]*a[1]*a[11]  + a[12]*a[3]*a[9];
            inv[13] =  a[0]*a[9] *a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14]  + a[8]*a[2]*a[13]  + a[12]*a[1]*a[10]  - a[12]*a[2]*a[9];

            inv[2]  =  a[1]*a[6] *a[15] - a[1]*a[7] *a[14] - a[5]*a[2]*a[15]  + a[5]*a[3]*a[14]  + a[13]*a[2]*a[7]   - a[13]*a[3]*a[6];
            inv[6]  = -a[0]*a[6] *a[15] + a[0]*a[7] *a[14] + a[4]*a[2]*a[15]  - a[4]*a[3]*a[14]  - a[12]*a[2]*a[7]   + a[12]*a[3]*a[6];
            inv[10] =  a[0]*a[5] *a[15] - a[0]*a[7] *a[13] - a[4]*a[1]*a[15]  + a[4]*a[3]*a[13]  + a[12]*a[1]*a[7]   - a[12]*a[3]*a[5];
            inv[14] = -a[0]*a[5] *a[14] + a[0]*a[6] *a[13] + a[4]*a[1]*a[14]  - a[4]*a[2]*a[13]  - a[12]*a[1]*a[6]   + a[12]*a[2]*a[5];

            inv[3]  = -a[1]*a[6] *a[11] + a[1]*a[7] *a[10] + a[5]*a[2]*a[11]  - a[5]*a[3]*a[10]  - a[9]*a[2]*a[7]    + a[9]*a[3]*a[6];
            inv[7]  =  a[0]*a[6] *a[11] - a[0]*a[7] *a[10] - a[4]*a[2]*a[11]  + a[4]*a[3]*a[10]  + a[8]*a[2]*a[7]    - a[8]*a[3]*a[6];
            inv[11] = -a[0]*a[5] *a[11] + a[0]*a[7] *a[9]  + a[4]*a[1]*a[11]  - a[4]*a[3]*a[9]   - a[8]*a[1]*a[7]    + a[8]*a[3]*a[5];
            inv[15] =  a[0]*a[5] *a[10] - a[0]*a[6] *a[9]  - a[4]*a[1]*a[10]  + a[4]*a[2]*a[9]   + a[8]*a[1]*a[6]    - a[8]*a[2]*a[5];

            double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];

            // 奇异性判定不能用|det|对固定阈值——行列式是有量纲的，随矩阵元素整体缩放按 4 次方变化。
            // 正交投影的行列式 = (2/可视宽)(2/可视高)/(far-near)，随可视高度按 1/zoom² 缩小：
            // 大图纸（zoom 超过约 1.6e6）时 |det| 会低于 1e-15，矩阵其实完全良态，却被判为奇异
            // 而返回零矩阵，表现为 ScreenToWorld 恒返回 (0,0)（鼠标坐标 / 捕捉点全跳到原点）。
            //
            // 改用 Hadamard 上界做尺度无关的判定：|det| ≤ ∏‖row_i‖，两者同为 4 阶齐次，
            // 比值只反映矩阵的病态程度。良态矩阵比值接近 1，真正奇异时 det 为 0。
            double rowNormProduct = 1.0;
            for (int r = 0; r < 4; ++r)
            {
                const double* row = a + r * 4;
                const double norm = std::sqrt(row[0]*row[0] + row[1]*row[1] + row[2]*row[2] + row[3]*row[3]);
                rowNormProduct *= norm;
            }

            Mat4 result;
            if (rowNormProduct > 0.0 && std::abs(det) > 1e-12 * rowNormProduct)
            {
                double invDet = 1.0 / det;
                for (int i = 0; i < 16; ++i)
                    result.m[i] = inv[i] * invDet;
            }
            return result;
        }

        // =========================
        // 非对称正交投影（Left-Handed，屏幕空间常用）
        // 映射：x: [l, r] → [-1, 1]   y: [b, t] → [-1, 1]   z: [n, f] → [0, 1]
        // =========================
        static Mat4 OrthoOffCenterLH(double l, double r, double b, double t, double nearZ, double farZ)
        {
            Mat4 m  = {};
            m.m[0]  =  2.0 / (r - l);
            m.m[5]  =  2.0 / (t - b);
            m.m[10] =  1.0 / (farZ - nearZ);
            m.m[12] = -(r + l) / (r - l);
            m.m[13] = -(t + b) / (t - b);
            m.m[14] = -nearZ   / (farZ - nearZ);
            m.m[15] =  1.0;
            return m;
        }
        // =========================
        // GPU 兼容工具
        // =========================

        double*       Data()       { return m; }
        const double* Data() const { return m; }

        // OpenGL/D3D 使用（列主序）
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
