#pragma once
#include "Point.hpp"
#include "Matrix.hpp"
#include <algorithm> // std::min, std::max

namespace MiniCAD {

    // ============================================================
    // Axis-Aligned Bounding Box（AABB）
    // 3D 包围盒
    // ============================================================
    struct Box {
        Point3 min; // 最小角点
        Point3 max; // 最大角点

        // 默认构造：无效盒子
        Box()
            : min{ MINICAD_INF,  MINICAD_INF,  MINICAD_INF }
            , max{ -MINICAD_INF, -MINICAD_INF, -MINICAD_INF }
        {
        }

        Box(const Point3& min, const Point3& max) : min(min), max(max) {}

        // 是否有效（min ≤ max）
        bool IsValid() const {
            return RealLessEqual(min.x, max.x) &&
                RealLessEqual(min.y, max.y) &&
                RealLessEqual(min.z, max.z);
        }

        // 扩展包围盒以包含点 p
        void Expand(const Point3& p) {
            if (RealLess(p.x, min.x)) min.x = p.x;
            if (RealGreater(p.x, max.x)) max.x = p.x;
            if (RealLess(p.y, min.y)) min.y = p.y;
            if (RealGreater(p.y, max.y)) max.y = p.y;
            if (RealLess(p.z, min.z)) min.z = p.z;
            if (RealGreater(p.z, max.z)) max.z = p.z;
        }

        // 合并两个盒子，返回包含两个盒子的最小盒
        Box Merge(const Box& o) const {
            return {
                { std::min(min.x, o.min.x), std::min(min.y, o.min.y), std::min(min.z, o.min.z) },
                { std::max(max.x, o.max.x), std::max(max.y, o.max.y), std::max(max.z, o.max.z) }
            };
        }

        // 判断是否包含点 p
        bool Contains(const Point3& p) const {
            return RealGreaterEqual(p.x, min.x) && RealLessEqual(p.x, max.x) &&
                RealGreaterEqual(p.y, min.y) && RealLessEqual(p.y, max.y) &&
                RealGreaterEqual(p.z, min.z) && RealLessEqual(p.z, max.z);
        }

        // 判断是否与另一个盒子相交
        bool Intersects(const Box& o) const {
            return RealLessEqual(min.x, o.max.x) && RealGreaterEqual(max.x, o.min.x) &&
                RealLessEqual(min.y, o.max.y) && RealGreaterEqual(max.y, o.min.y) &&
                RealLessEqual(min.z, o.max.z) && RealGreaterEqual(max.z, o.min.z);
        }

        // 中心点
        Point3 Center() const {
            return { (min.x + max.x) * 0.5,
                     (min.y + max.y) * 0.5,
                     (min.z + max.z) * 0.5 };
        }
    };

} // namespace MiniCAD
