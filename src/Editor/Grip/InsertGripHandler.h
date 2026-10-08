#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    struct InsertDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Math::Point3     Base;      // 插入点快照
    };

    // ─────────────────────────────────────────────
    // InsertGripHandler
    //
    // 块引用(InsertEntity):一个插入点夹点,拖动即整体移动
    // ─────────────────────────────────────────────
    class InsertGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* e = static_cast<InsertEntity*>(entity);
            outGrips.push_back({ entity->GetID(), Grip::Type::Start, e->GetPosition(), 0 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto  state = std::make_unique<InsertDragState>();
            state->EntityId = entity->GetID();
            state->Base     = e->GetPosition();
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState*, const Grip&,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* e = static_cast<InsertEntity*>(entity);
            e->SetPosition(worldPos);

            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
                if (grip.OwnerID == ownerId)
                    grip.WorldPos = worldPos;
        }

        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto* state = static_cast<InsertDragState*>(baseState);
            if (!e || !state) return false;

            const Math::Point3 after = e->GetPosition();
            if (std::abs(after.x - state->Base.x) < Math::LengthEPS &&
                std::abs(after.y - state->Base.y) < Math::LengthEPS &&
                std::abs(after.z - state->Base.z) < Math::LengthEPS)
                return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Insert;
            outEntry.BeforePoint = state->Base;
            outEntry.AfterPoint  = after;
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto* state = static_cast<InsertDragState*>(baseState);
            e->SetPosition(state->Base);
        }
    };
}
