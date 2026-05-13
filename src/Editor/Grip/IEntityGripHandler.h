#pragma once

#include "Core/Entity/Entity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"

#include <memory>
#include <vector>

namespace MiniCAD
{ 
    struct Grip;
 
    class IGripDragState
    {
    public: 
        virtual ~IGripDragState() = default;
    };
 
    class IEntityGripHandler
    {
    public:

        virtual ~IEntityGripHandler() = default;

    public:

        // ─────────────────────────────────────────
        // 构建实体夹点
        // ─────────────────────────────────────────
        virtual void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) = 0;

        // ─────────────────────────────────────────
        // 开始拖拽
        //
        // 返回：
        //   每次 drag 独立状态
        // ─────────────────────────────────────────
        virtual std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& activeGrip) = 0;

        // ─────────────────────────────────────────
        // 拖拽更新
        // ─────────────────────────────────────────
        virtual void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) = 0;

        // ─────────────────────────────────────────
        // 拖拽结束
        // ─────────────────────────────────────────
        virtual void EndDrag(Entity* entity, IGripDragState* dragState) {}

        // ─────────────────────────────────────────
        // 取消拖拽
        //
        // ESC 等情况恢复初始状态
        // ─────────────────────────────────────────
        virtual void CancelDrag(Entity* entity, IGripDragState* dragState) = 0;
    };
}