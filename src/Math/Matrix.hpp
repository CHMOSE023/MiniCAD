// ============================================================
// MiniCAD — math/Matrix.h
// 职责：Mat4 矩阵类型，列向量右手系
// 依赖：math/Vector.h, math/Point.h
// 约束：列向量，v' = M * v
// ============================================================
#pragma once
#include "Vector.hpp"
#include "Point.hpp"
#include <cmath>

namespace MiniCAD { 

    // Mat4: column-major
    struct Mat4 {
        Real m[4][4]; // m[col][row]

        Mat4() { for (auto& c : m) for (auto& r : c) r = 0.f; }

        static Mat4 Identity() {
            Mat4 r;
            r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.f;
            return r;
        }

        static Mat4 Translation(Real tx, Real ty, Real tz) {
            Mat4 r = Identity();
            r.m[3][0] = tx; r.m[3][1] = ty; r.m[3][2] = tz;
            return r;
        }

        // axis must be normalized, angle in radians
        static Mat4 Rotation(const Vec3& axis, Real angle) { // radians
            Real c = std::cos(angle), s = std::sin(angle), t = 1.f - c;
            Real x = axis.x, y = axis.y, z = axis.z;
            Mat4 r = Identity();
            r.m[0][0] = t * x * x + c;   r.m[0][1] = t * x * y + s * z; r.m[0][2] = t * x * z - s * y;
            r.m[1][0] = t * x * y - s * z; r.m[1][1] = t * y * y + c;   r.m[1][2] = t * y * z + s * x;
            r.m[2][0] = t * x * z + s * y; r.m[2][1] = t * y * z - s * x; r.m[2][2] = t * z * z + c;
            return r;
        }

        static Mat4 Scale(Real sx, Real sy, Real sz) {
            Mat4 r = Identity();
            r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
            return r;
        }

        Real& At(int col, int row) { return m[col][row]; }
        Real  At(int col, int row) const { return m[col][row]; }

        Mat4 operator*(const Mat4& o) const {
            Mat4 r;
            for (int c = 0; c < 4; c++)
                for (int row = 0; row < 4; row++)
                    for (int k = 0; k < 4; k++)
                        r.m[c][row] += m[k][row] * o.m[c][k];
            return r;
        }
         
        Mat4 Transpose() const {
            Mat4 r;
            for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) r.m[row][c] = m[c][row];
            return r;
        }

        Real Determinant() const {
            // Expand along first column
            const auto& a = m;
            Real det = 0.f;
            for (int i = 0; i < 4; i++) {
                // cofactor
                Real sub[3][3];
                for (int c2 = 1; c2 < 4; c2++) {
                    int sc = c2 - 1;
                    int sr = 0;
                    for (int r2 = 0; r2 < 4; r2++) {
                        if (r2 == i) continue;
                        sub[sc][sr++] = a[c2][r2];
                    }
                }
                Real minor = sub[0][0] * (sub[1][1] * sub[2][2] - sub[2][1] * sub[1][2])
                    - sub[1][0] * (sub[0][1] * sub[2][2] - sub[2][1] * sub[0][2])
                    + sub[2][0] * (sub[0][1] * sub[1][2] - sub[1][1] * sub[0][2]);
                det += ((i % 2 == 0) ? 1.f : -1.f) * a[0][i] * minor;
            }
            return det;
        }

        Mat4  Inverse() const {
            // Gauss-Jordan elimination
            Real aug[4][8];
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 4; col++) aug[row][col] = m[col][row];
                for (int col = 0; col < 4; col++) aug[row][col + 4] = (row == col) ? 1.f : 0.f;
            }
            for (int col = 0; col < 4; col++) {
                // pivot
                int pivot = col;
                for (int row = col + 1; row < 4; row++)
                    if (std::abs(aug[row][col]) > std::abs(aug[pivot][col])) pivot = row;
                if (pivot != col)
                    for (int k = 0; k < 8; k++) { Real t = aug[col][k]; aug[col][k] = aug[pivot][k]; aug[pivot][k] = t; }
                Real inv = 1.f / aug[col][col];
                for (int k = 0; k < 8; k++) aug[col][k] *= inv;
                for (int row = 0; row < 4; row++) {
                    if (row == col) continue;
                    Real f = aug[row][col];
                    for (int k = 0; k < 8; k++) aug[row][k] -= f * aug[col][k];
                }
            }
            Mat4 result;
            for (int row = 0; row < 4; row++)
                for (int col = 0; col < 4; col++)
                    result.m[col][row] = aug[row][col + 4];
            return result;
        };
    };

} // namespace MiniCAD
