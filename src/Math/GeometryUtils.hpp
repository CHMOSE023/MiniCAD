// ============================================================
// MiniCAD — math/GeometryUtils.h
// 职责：header-only 几何工具函数
// 依赖：math/Point.h, math/Vector.h, math/MathDefs.h
// 约束：纯函数，无副作用，header-only
// ============================================================
#pragma once
#include "Point.hpp"
#include "Vector.hpp"
#include "MathDefs.hpp" 

namespace MiniCAD {

    // 返回点 p 到线段 (a,b) 的最近点
    inline Point3 ClosestPointOnSegment(const Point3& p, const Point3& a, const Point3& b) {
        Vec3 ab = b - a;
        float len2 = ab.LengthSq();
        if (RealEqual(len2, 0.f)) return a;
        float t = (p - a).Dot(ab) / len2;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        return a + ab * t;
    }

    // 点到线段距离
    inline float PointToSegmentDistance(const Point3& p, const Point3& a, const Point3& b) {
        Point3 closest = ClosestPointOnSegment(p, a, b);
        return p.DistanceTo(closest);
    }

    // 射线与平面求交，返回 false 表示平行或无交
    // outT 为射线参数（p = rayOrigin + outT * rayDir）
    inline bool RayPlaneIntersect(const Point3& rayOrigin, const Vec3& rayDir,
        const Point3& planePoint, const Vec3& planeNormal,
        float& outT)
    {
        float denom = planeNormal.Dot(rayDir);
        if (RealEqual(denom, 0.f)) return false;
        outT = planeNormal.Dot(planePoint - rayOrigin) / denom;
        return RealGreaterEqual(outT, 0.f);
    }
 

} // namespace MiniCAD
