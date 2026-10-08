#pragma once
#include "Vec3.hpp"
#include "Point3.hpp"
#include "MathUtils.hpp"
#include <cmath>

namespace MiniCAD::Math
{
    // 物体坐标系(Object Coordinate System)。以挤出方向(法向 N)为 Z 轴,
    // 通过 DXF「任意轴算法」(Arbitrary Axis Algorithm)确定 X/Y 轴,实现 OCS<->WCS 变换。
    struct OCS
    {
        Vec3 Ux{ 1, 0, 0 };   // OCS X 轴(WCS 表示)
        Vec3 Uy{ 0, 1, 0 };   // OCS Y 轴(WCS 表示)
        Vec3 Uz{ 0, 0, 1 };   // OCS Z 轴 = 法向
    };

    // DXF 任意轴算法:给定法向 N(应已归一化),构造稳定的 OCS 基。
    inline OCS BuildOCS(const Vec3& normal)
    {
        Vec3 n = normal.Normalized();
        if (n.LengthSq() < 1e-24) n = { 0, 0, 1 };

        constexpr double kBound = 1.0 / 64.0;
        Vec3 ax;
        if (std::abs(n.x) < kBound && std::abs(n.y) < kBound)
            ax = Cross(Vec3{ 0, 1, 0 }, n);   // 接近 ±Z 时用世界 Y
        else
            ax = Cross(Vec3{ 0, 0, 1 }, n);   // 否则用世界 Z
        ax.Normalize();
        Vec3 ay = Cross(n, ax).Normalized();

        return { ax, ay, n };
    }

    // OCS 坐标 → WCS 坐标。
    inline Point3 OcsToWcs(const OCS& ocs, const Point3& p)
    {
        return {
            ocs.Ux.x * p.x + ocs.Uy.x * p.y + ocs.Uz.x * p.z,
            ocs.Ux.y * p.x + ocs.Uy.y * p.y + ocs.Uz.y * p.z,
            ocs.Ux.z * p.x + ocs.Uy.z * p.y + ocs.Uz.z * p.z,
        };
    }

    // WCS 坐标 → OCS 坐标(基为标准正交,转置即逆)。
    inline Point3 WcsToOcs(const OCS& ocs, const Point3& p)
    {
        return {
            ocs.Ux.x * p.x + ocs.Ux.y * p.y + ocs.Ux.z * p.z,
            ocs.Uy.x * p.x + ocs.Uy.y * p.y + ocs.Uy.z * p.z,
            ocs.Uz.x * p.x + ocs.Uz.y * p.y + ocs.Uz.z * p.z,
        };
    }

    // 法向是否为默认 +Z(即纯 2D,无需 OCS 变换)。
    inline bool IsDefaultExtrusion(const Vec3& n)
    {
        return std::abs(n.x) < 1e-12 && std::abs(n.y) < 1e-12 && n.z > 0.0;
    }
}
