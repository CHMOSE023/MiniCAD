// ============================================================
// MiniCAD — app/Picking/Picker.h
// 职责：拾取器，Camera 依赖通过回调注入
// 依赖：app/Scene/Scene.h, app/Picking/PickResult.h,
//       math/GeometryUtils.h, math/Box.h
// 约束：不 include Camera.h；Camera 依赖通过 std::function 注入
// ============================================================
#pragma once

#include "PickResult.h"
#include "app/Scene/Scene.h"
#include "math/Ray.hpp"
#include "math/Box.hpp"
#include <functional>
#include <vector>

namespace MiniCAD {

    class Picker {
    public:
        // Camera 依赖通过回调注入，不 include Camera.h
        using ScreenToRayFn = std::function<Ray(const Vec2& screenPos)>;

        Picker(Scene& scene, ScreenToRayFn screenToRay);

        // 单点拾取：返回最近命中的实体
        PickResult PickAt(const Vec2& screenPos) const;

        // 框选：返回与屏幕矩形对应的世界包围盒相交的所有实体
        std::vector<Object::ObjectID> BoxSelect(const Box2D& screenRect) const;

        // 拾取容差（屏幕像素）
        void  SetPickTolerance(float pixels) { m_tolerance = pixels; }
        float GetPickTolerance()       const { return m_tolerance; }

    private:
    Scene&         m_scene;
        ScreenToRayFn  m_screenToRay;
        float          m_tolerance = 5.0f; // 像素容差
    };

} // namespace MiniCAD
