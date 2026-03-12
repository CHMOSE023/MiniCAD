#pragma once
#include "Point.hpp"
#include "Matrix.hpp"
#include <algorithm> // std::min, std::max

namespace MiniCAD {

    // ============================================================
    // Box2D — 2D Axis-Aligned Bounding Box（屏幕坐标 / UV）
    // ============================================================
    struct Box2D {
        Vec2 min;
        Vec2 max;

        // 默认构造：无效盒子
        Box2D()
            : min{ MINICAD_INF,  MINICAD_INF }
            , max{ -MINICAD_INF, -MINICAD_INF }
        {
        }

        Box2D(const Vec2& min, const Vec2& max) : min(min), max(max) {}

        bool IsValid() const {
            return RealLessEqual(min.x, max.x) &&
                RealLessEqual(min.y, max.y);
        }

        void Expand(const Vec2& p) {
            if (RealLess(p.x, min.x))    min.x = p.x;
            if (RealGreater(p.x, max.x)) max.x = p.x;
            if (RealLess(p.y, min.y))    min.y = p.y;
            if (RealGreater(p.y, max.y)) max.y = p.y;
        }

        Box2D Merge(const Box2D& o) const {
            return {
                { std::min(min.x, o.min.x), std::min(min.y, o.min.y) },
                { std::max(max.x, o.max.x), std::max(max.y, o.max.y) }
            };
        }

        bool Contains(const Vec2& p) const {
            return RealGreaterEqual(p.x, min.x) && RealLessEqual(p.x, max.x) &&
                RealGreaterEqual(p.y, min.y) && RealLessEqual(p.y, max.y);
        }

        bool Intersects(const Box2D& o) const {
            return RealLessEqual(min.x, o.max.x) && RealGreaterEqual(max.x, o.min.x) &&
                RealLessEqual(min.y, o.max.y) && RealGreaterEqual(max.y, o.min.y);
        }

        Vec2 Center() const {
            return { (min.x + max.x) * Real(0.5),
                     (min.y + max.y) * Real(0.5) };
        }

        Vec2 Size() const {
            return { max.x - min.x, max.y - min.y };
        }
    };

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

        // 射线与 AABB 求交（slab method）
        // 返回 false 表示不相交；相交时 outTMin / outTMax 为射线进出参数
        bool IntersectsRay(const Point3& origin, const Vec3& dir,
            float& outTMin, float& outTMax) const
        {
            outTMin = -MINICAD_INF;
            outTMax = MINICAD_INF;

            // 对 X / Y / Z 三轴分别做 slab 测试
            auto testAxis = [&](float o, float d, float bMin, float bMax) -> bool {
                if (RealEqual(d, 0.f)) {
                    // 射线与该轴平行：原点必须在 slab 内
                    return RealGreaterEqual(o, bMin) && RealLessEqual(o, bMax);
                }
                float invD = 1.f / d;
                float t0 = (bMin - o) * invD;
                float t1 = (bMax - o) * invD;
                if (t0 > t1) std::swap(t0, t1);
                outTMin = std::max(outTMin, t0);
                outTMax = std::min(outTMax, t1);
                return outTMin <= outTMax;
                };

            if (!testAxis((float)origin.x, (float)dir.x, (float)min.x, (float)max.x)) return false;
            if (!testAxis((float)origin.y, (float)dir.y, (float)min.y, (float)max.y)) return false;
            if (!testAxis((float)origin.z, (float)dir.z, (float)min.z, (float)max.z)) return false;

            return outTMax >= 0.f; // 排除射线起点在盒子后方的情况
        }

        // 中心点
        Point3 Center() const {
            return { (min.x + max.x) * 0.5,
                     (min.y + max.y) * 0.5,
                     (min.z + max.z) * 0.5 };
        }
    };

} // namespace MiniCAD
