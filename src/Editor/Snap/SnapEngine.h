#pragma once
#include "SnapResult.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include "Editor/Viewport/Camera.h"
#include <unordered_set>

namespace MiniCAD
{
	// 捕捉引擎：提供捕捉查询接口，支持多种捕捉类型（端点、中心点、最近点、网格等）
    class SnapEngine
    {
    public:
        // ─── 开关 ───────────────────────────────
        bool   EnableEndpoint = true;
        bool   EnableMidpoint = true;
        bool   EnableNearest  = true;
        bool   EnableGrid     = false;
        double GridSize       = 1.0;    // 世界单位
        double SnapRadiusPx   = 12.0;   // 屏幕像素捕捉半径

        // ─── 主接口 ─────────────────────────────
        // exclude: 需要跳过的对象（夹点拖拽时传入当前选中集合，避免捕捉自身）
        SnapResult Query(const Math::Point2&                             screenPt,
                         const Scene&                                    scene,
                         const Camera&                                   cam,
                         const std::unordered_set<Object::ObjectID>&     exclude = {}) const;

    private:
        SnapResult TryEndpoint(const Math::Point2& sp, const Scene&, const Camera&,
                               const std::unordered_set<Object::ObjectID>& exclude) const;
        SnapResult TryMidpoint(const Math::Point2& sp, const Scene&, const Camera&,
                               const std::unordered_set<Object::ObjectID>& exclude) const;
        SnapResult TryNearest (const Math::Point2& sp, const Scene&, const Camera&,
                               const std::unordered_set<Object::ObjectID>& exclude) const;
        SnapResult TryGrid    (const Math::Point2& sp, const Camera&) const;
    };
}
