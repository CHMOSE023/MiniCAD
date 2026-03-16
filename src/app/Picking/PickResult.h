// ============================================================
// MiniCAD — app/Picking/PickResult.h
// 职责：拾取结果结构体
// 依赖：core/Object/Object.h, math/Point.h
// 约束：纯数据结构
// ============================================================
#pragma once

#include "core/Object/Object.hpp"
#include "math/Point.hpp"

namespace MiniCAD {

    struct PickResult {
        Object::ObjectID entityId = Object::INVALID_ID;
        Point3           hitPoint;
        float            hitDist = 1e38f;
        bool             hit = false;
    };

} // namespace MiniCAD
