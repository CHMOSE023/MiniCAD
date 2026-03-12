// ============================================================
// MiniCAD — app/Picking/Picker.cpp
// 职责：Picker 实现
// 依赖：app/Picking/Picker.h, core/Entity/LineEntity.h
// 约束：不 include Camera.h
// ============================================================
#include "Picker.h"
#include "core/Entity/LineEntity.h"
#include <math/Box.hpp>
#include <core/GeomKernel/Line.hpp>
#include <math/Point.hpp>

namespace MiniCAD {

Picker::Picker(Scene& scene, ScreenToRayFn screenToRay)
    : m_scene(scene), m_screenToRay(std::move(screenToRay)) {}

PickResult Picker::PickAt(const Vec2& screenPos) const {
    Ray ray = m_screenToRay(screenPos);
    PickResult best;

    for (auto id : m_scene.GetAllIDs()) {
        const Object* obj = m_scene.GetEntity(id);
        if (!obj) continue;

        // 先做 AABB 快速测试
        if (obj->IsKindOf<LineEntity>()) {
            const LineEntity* le = static_cast<const LineEntity*>(obj);
            Box bbox = le->GetBoundingBox();
            float tMin, tMax;
            if (!bbox.IntersectsRay(ray.origin, ray.dir, tMin, tMax)) continue;

            // 精确：点到线段的距离（在射线方向取最近点参数）
            const Line& line = le->GetLine();
            float dist = le->GetLine().DistanceTo(ray.origin + ray.dir * tMin);
            // 简化：取射线上距离线段最近的点
            float tBest = tMin;
            Point3 closest = ClosestPointOnSegment(ray.origin, line.start, line.end);
            float d = closest.DistanceTo(ray.origin);
            if (d < best.hitDist) {
                best.hit      = true;
                best.entityId = id;
                best.hitPoint = closest;
                best.hitDist  = d;
            }
            (void)dist;
        }
    }
    return best;
}

std::vector<Object::ObjectID> Picker::BoxSelect(const Box2D& screenRect) const {
    // 简化：将 2D 屏幕矩形视为 XY 平面上的世界 AABB
    Box worldBox(
        Point3(screenRect.min.x, screenRect.min.y, -1e6f),
        Point3(screenRect.max.x, screenRect.max.y,  1e6f)
    );
    return m_scene.QueryByBox(worldBox);
}

} // namespace MiniCAD
