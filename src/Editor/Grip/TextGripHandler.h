#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    struct TextDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Math::Point3     Base;      // 插入点快照
    };

    // ─────────────────────────────────────────────
    // TextGripHandler
    //
    // 单行文字：一个插入点夹点，拖动即整体移动
    // ─────────────────────────────────────────────
    class TextGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* e = static_cast<TextEntity*>(entity);
            outGrips.push_back({ entity->GetID(), Grip::Type::Start, e->GetPosition(), 0 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override
        {
            auto* e     = static_cast<TextEntity*>(entity);
            auto  state = std::make_unique<TextDragState>();
            state->EntityId = entity->GetID();
            state->Base     = e->GetPosition();
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip&,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* e = static_cast<TextEntity*>(entity);
            e->SetPosition(worldPos);

            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
                if (grip.OwnerID == ownerId)
                    grip.WorldPos = worldPos;
        }

        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* e     = static_cast<TextEntity*>(entity);
            auto* state = static_cast<TextDragState*>(baseState);
            if (!e || !state) return false;

            const Math::Point3 after = e->GetPosition();
            if (std::abs(after.x - state->Base.x) < Math::LengthEPS &&
                std::abs(after.y - state->Base.y) < Math::LengthEPS &&
                std::abs(after.z - state->Base.z) < Math::LengthEPS)
                return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Text;
            outEntry.BeforePoint = state->Base;
            outEntry.AfterPoint  = after;
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* e     = static_cast<TextEntity*>(entity);
            auto* state = static_cast<TextDragState*>(baseState);
            e->SetPosition(state->Base);
        }
    };
}
