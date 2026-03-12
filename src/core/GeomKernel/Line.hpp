// ============================================================
// MiniCAD — core/GeomKernel/Line.h
// 职责：无限直线 / 线段：参数方程、最近点投影、点线距离
// 依赖：math/Point.hpp, math/Vector.hpp, math/GeometryUtils.hpp
// 约束：纯几何，无 UI / Render 依赖
// ============================================================
#pragma once
#include "math/Point.hpp"
#include "math/Vector.hpp"
#include "math/GeometryUtils.hpp"

namespace MiniCAD {

    struct Line {
        Point3 start;
        Point3 end;

        Line() = default;
        Line(const Point3& start, const Point3& end) : start(start), end(end) {}

        Vec3   Direction()    const { return (end - start).Normalized(); }
        float  Length()       const { return start.DistanceTo(end); }
        float  LengthSq()     const { return (end - start).LengthSq(); }

        Point3 Midpoint()     const {
            return { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f, (start.z + end.z) * 0.5f };
        }

        Point3 ClosestPoint(const Point3& p) const {
            return ClosestPointOnSegment(p, start, end);
        }

        float  DistanceTo(const Point3& p) const {
            return PointToSegmentDistance(p, start, end);
        }

        // Point on segment at parameter t ∈ [0,1]
        Point3 PointAt(float t) const {
            Vec3 d = end - start;
            return start + d * t;
        }
    };

} // namespace MiniCAD
