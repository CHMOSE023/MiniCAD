#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    struct Solid3DDragState : public IGripDragState
    {
        Math::Point3 Base;        // 夹点起始位置
        Math::Mat4   Transform;   // 变换矩阵快照
    };

    // ─────────────────────────────────────────────
    // Solid3DGripHandler
    //
    // 三维实体(Solid3DEntity):包围盒中心一个夹点,拖动即整体移动(只改变换矩阵)
    // ─────────────────────────────────────────────
    class Solid3DGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            outGrips.push_back({ entity->GetID(), Grip::Type::Start, entity->GetBoundingBox().Center(), 0 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& grip) override
        {
            auto* e     = static_cast<Solid3DEntity*>(entity);
            auto  state = std::make_unique<Solid3DDragState>();
            state->Base      = grip.WorldPos;
            state->Transform = e->GetTransform();
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* baseState, const Grip&,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* e     = static_cast<Solid3DEntity*>(entity);
            auto* state = static_cast<Solid3DDragState*>(baseState);
            e->SetTransform(state->Transform * Solid3DEntity::MoveMatrix(worldPos - state->Base));

            const Object::ObjectID ownerId = entity->GetID();
            for (auto& g : grips)
                if (g.OwnerID == ownerId)
                    g.WorldPos = worldPos;
        }

        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* e     = static_cast<Solid3DEntity*>(entity);
            auto* state = static_cast<Solid3DDragState*>(baseState);
            if (!e || !state) return false;

            const Math::Mat4& after = e->GetTransform();
            if (std::abs(after.m[12] - state->Transform.m[12]) < Math::LengthEPS &&
                std::abs(after.m[13] - state->Transform.m[13]) < Math::LengthEPS &&
                std::abs(after.m[14] - state->Transform.m[14]) < Math::LengthEPS)
                return false;

            outEntry.Id              = entity->GetID();
            outEntry.Kind            = DragEntityEntry::Kind::Transform;
            outEntry.BeforeTransform = state->Transform;
            outEntry.AfterTransform  = after;
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* e     = static_cast<Solid3DEntity*>(entity);
            auto* state = static_cast<Solid3DDragState*>(baseState);
            e->SetTransform(state->Transform);
        }
    };
}
