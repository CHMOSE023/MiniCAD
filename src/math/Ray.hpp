// ============================================================
// MiniCAD — math/Ray.hpp
// 职责：射线：原点 + 方向
// 依赖：math/Point.hpp, math/Vector.hpp
// ============================================================
#pragma once
#include "math/Point.hpp"
#include "math/Vector.hpp"

namespace MiniCAD {

    struct Ray {
        Point3 origin;
        Vec3   dir;     // 应为单位向量

        Ray() = default;
        Ray(const Point3& origin, const Vec3& dir)
            : origin(origin), dir(dir) {
        }

        // 沿射线取参数 t 处的点
        Point3 At(float t) const {
            return origin + dir * t;
        }
    };

} // namespace MiniCAD
