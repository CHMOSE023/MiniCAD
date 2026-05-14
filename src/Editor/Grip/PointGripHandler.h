#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/PointEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "Core/Math/Constants.hpp"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // PointDragState
    // ─────────────────────────────────────────────
    struct PointDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Math::Point3     Base;      // 拖拽开始时的位置快照
    };

    // ─────────────────────────────────────────────
    // PointGripHandler
    //
    // 点实体只有单个夹点，拖拽即整体移动
    // ─────────────────────────────────────────────
    class PointGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────
        // BuildGrips — 单个 Start 夹点
        // ─────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* pointEntity = static_cast<PointEntity*>(entity);
            const auto& p = pointEntity->GetPoint();

            outGrips.push_back({ entity->GetID(), Grip::Type::Start, p.Position, 0 });
        }

        // ─────────────────────────────────────────
        // BeginDrag — 存快照
        // ─────────────────────────────────────────
        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* pointEntity = static_cast<PointEntity*>(entity);
            const auto& p = pointEntity->GetPoint();

            auto state      = std::make_unique<PointDragState>();
            state->EntityId = entity->GetID();
            state->Base     = p.Position;

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag — 移动点 + 同步夹点坐标（预览）
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* baseState, const Grip& /*activeGrip*/, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* pointEntity = static_cast<PointEntity*>(entity);
            pointEntity->SetPoint({ worldPos });

            // 同步夹点坐标，使夹点跟手（预览）
            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
            {
                if (grip.OwnerID == ownerId)
                    grip.WorldPos = worldPos;
            }
        }

        // ─────────────────────────────────────────
        // EndDrag — 推入 CommandStack
        // ─────────────────────────────────────────
        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* pointEntity = static_cast<PointEntity*>(entity);
            auto* state       = static_cast<PointDragState*>(baseState);

            if (!pointEntity || !state)
                return false;

            const Math::Point3 after = pointEntity->GetPoint().Position;

            // 未移动则不产生命令 
            if (std::abs(after.x - state->Base.x) < Math::LengthEPS &&
                std::abs(after.y - state->Base.y) < Math::LengthEPS &&
                std::abs(after.z - state->Base.z) < Math::LengthEPS)
                return false;
             
            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Point;
            outEntry.BeforePoint = state->Base;
            outEntry.AfterPoint  = after;

			return true;
        }

        // ─────────────────────────────────────────
        // CancelDrag — 还原到 Base 快照
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* pointEntity = static_cast<PointEntity*>(entity);
            auto* state       = static_cast<PointDragState*>(baseState);

            pointEntity->SetPoint({ state->Base });
        }
    };
}