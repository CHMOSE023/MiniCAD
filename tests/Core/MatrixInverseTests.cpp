// 回归测试：Mat4::Inverse 的奇异性判定必须与矩阵量级无关
//
// 回归点（「放大图面时鼠标自动跳到原点」）：正交投影的行列式
//     |det| = 4 / (可视宽 × 可视高 × (far-near))，
// 随可视高度（m_zoom）按 1/zoom² 缩小。旧实现用固定阈值 |det| > 1e-15 判奇异，
// 大图纸 zoom ≳ 1.6e6 时 |det| 低于阈值 —— 矩阵明明良态却被判为奇异、
// 返回零矩阵，于是 Camera::ScreenToWorld 恒返回 (0,0)：鼠标坐标、捕捉点、
// 新建图元的落点全部跳到原点。改用尺度无关的 Hadamard 上界判定后恢复。
#include "Core/Math/Mat4.hpp"
#include "Viewport/Camera.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace MiniCAD;
using namespace MiniCAD::Math;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool IsZero(const Mat4& m)
    {
        for (int i = 0; i < 16; ++i)
            if (m.m[i] != 0.0)
                return false;
        return true;
    }

    double MaxAbsDiff(const Mat4& a, const Mat4& b)
    {
        double d = 0.0;
        for (int i = 0; i < 16; ++i)
            d = std::max(d, std::abs(a.m[i] - b.m[i]));
        return d;
    }

    // M * M⁻¹ ≈ I
    double InverseError(const Mat4& m)
    {
        const Mat4 inv = Mat4::Inverse(m);
        if (IsZero(inv))
            return 1e9;                 // 被判为奇异
        return MaxAbsDiff(m * inv, Mat4::Identity());
    }

    // 与 Camera 的构造方式一致：view = T(-target)，proj = OrthoLH(zoom*aspect, zoom, 0, 1000)
    Mat4 CameraViewProj(double zoom, double tx, double ty, double aspect)
    {
        return Mat4::Translation({ -tx, -ty, 0.0 }) * Mat4::OrthoLH(zoom * aspect, zoom, 0.0, 1000.0);
    }
}

int RunMatrixInverseTests()
{
    g_failures = 0;

    // ── 1. 相机矩阵在整个缩放范围内都必须是可逆的 ─────────────────────
    // 回归点：正交投影的 |det| = 4/(可视宽×可视高×1000)，随 zoom 按 1/zoom² 缩小。
    // 旧实现用固定阈值 1e-15 判奇异，zoom ≳ 1.6e6 时误判 → 返回零矩阵 →
    // Camera::ScreenToWorld 恒返回 (0,0)（大图纸放大/缩小时鼠标坐标跳到原点）
    {
        const double aspect = 1280.0 / 800.0;
        for (double zoom : { 1e-2, 1.0, 1e3, 1e6, 1.6e6, 5e6, 1e7 })
        {
            const Mat4 vp = CameraViewProj(zoom, 100.0, 200.0, aspect);
            const double err = InverseError(vp);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "相机矩阵可逆（zoom=%g，M*M⁻¹≈I 误差 %.3g）", zoom, err);
            Check(err < 1e-9, buf);
        }
    }

    // ── 2. 整体量级缩放不应改变可逆性判定 ─────────────────────────────
    {
        for (double scale : { 1e-6, 1e-3, 1.0, 1e3, 1e6 })
        {
            // 旋转 + 平移 + 均匀缩放：良态，任何一种量级都应判为可逆
            Mat4 m = Mat4::RotationZ(0.7) * Mat4::Translation({ 1.0, 2.0, 3.0 });
            Mat4 s = Mat4::Scale({ scale, scale, scale });
            const Mat4 ms = m * s;
            const bool singular = IsZero(Mat4::Inverse(ms));
            char buf[128];
            std::snprintf(buf, sizeof(buf), "均匀缩放 %g 倍的良态矩阵不被误判为奇异", scale);
            Check(!singular, buf);
        }
    }

    // ── 3. 真正奇异的矩阵仍必须返回零矩阵 ─────────────────────────────
    {
        Check(IsZero(Mat4::Inverse(Mat4::Zero())), "零矩阵 → 返回零矩阵");

        Mat4 singular = Mat4::Identity();
        singular.m[5] = 0.0;                    // 有一行全零
        Check(IsZero(Mat4::Inverse(singular)), "含全零行的矩阵 → 返回零矩阵");

        // 两行相同 → 奇异
        Mat4 dup = Mat4::Identity();
        for (int c = 0; c < 4; ++c)
            dup.m[4 + c] = dup.m[c];
        Check(IsZero(Mat4::Inverse(dup)), "两行相同的矩阵 → 返回零矩阵");

        // 零缩放（把某一维压成 0）
        Check(IsZero(Mat4::Inverse(Mat4::Identity() * Mat4::Scale({ 1.0, 0.0, 1.0 }))), "某一维零缩放 → 返回零矩阵");
    }

    // ── 4. 极小但良态的矩阵必须能求逆（旧实现的主要失效模式）─────────
    {
        // 对角元全为 1e-6：|det| = 1e-24，远低于 1e-15，但完全良态
        Mat4 tiny = Mat4::Scale({ 1e-6, 1e-6, 1e-6 });
        const Mat4 inv = Mat4::Inverse(tiny);
        Check(!IsZero(inv), "对角元 1e-6 的良态矩阵不被判为奇异");
        Check(MaxAbsDiff(tiny * inv, Mat4::Identity()) < 1e-9, "它的逆确实还原单位矩阵");

        // 极大：同样不该出问题
        Mat4 huge = Mat4::Scale({ 1e6, 1e6, 1e6 });
        Check(MaxAbsDiff(huge * Mat4::Inverse(huge), Mat4::Identity()) < 1e-9, "对角元 1e6 的矩阵求逆正确");
    }

    // ── 5. 逆矩阵的正确性（普通情况）─────────────────────────────────
    {
        const Mat4 m = Mat4::Translation({ 3.0, -4.0, 5.0 }) * Mat4::RotationX(0.3) * Mat4::Scale({ 2.0, 3.0, 4.0 });
        Check(MaxAbsDiff(m * Mat4::Inverse(m), Mat4::Identity()) < 1e-9, "一般可逆矩阵 M*M⁻¹≈I");
    }

    // ── 6. 端到端：Camera 在整个缩放范围内 ScreenToWorld 都不是 (0,0) ──
    // 直接盯住用户看到的现象：鼠标在视口中心，反算出的世界坐标必须等于观察中心，
    // 而不是恒为原点。
    {
        const double aspect = 1600.0 / 900.0;
        const double tx = 5.0e5, ty = 3.0e5;      // 大图纸（毫米制）常见的远离原点量级
        for (double zoom : { 1e2, 1e3, 1e4, 1e5, 1e6, 1.5e6, 2e6, 5e6, 1e7 })
        {
            const CameraState st{ { tx, ty, 0.0 }, zoom };
            Camera cam(1600.0, 900.0);
            cam.SetState(st);

            const Math::Point3 w = cam.ScreenToWorld(800, 450);   // 视口正中
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "ScreenToWorld 视口中心 = 观察中心（zoom=%g，得到 (%.1f, %.1f)）",
                          zoom, w.x, w.y);
            Check(std::abs(w.x - tx) < 1e-6 * std::abs(tx) + 1e-6 &&
                  std::abs(w.y - ty) < 1e-6 * std::abs(ty) + 1e-6, buf);
        }
    }

    return g_failures;
}
