#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/RayEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "Core/Math/Constants.hpp"

#include "GripType.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // RayDragState — 拖拽开始时的几何快照
    // ─────────────────────────────────────────────
    struct RayDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        XLine            Base;   // 射线复用 XLine(起点 + 方向)
    };

    // ─────────────────────────────────────────────
    // RayGripHandler
    //
    // 射线两个夹点:
    //   Start(起点)   — 拖拽平移整条射线(方向不变)
    //   End (通过点)  — 拖拽改变方向(起点不变),通过点 = Origin + Direction
    // ─────────────────────────────────────────────
    class RayGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* rayEntity = static_cast<RayEntity*>(entity);
            const XLine& r  = rayEntity->GetRay();

            outGrips.push_back({ entity->GetID(), Grip::Type::Start, r.Origin,             0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::End,   r.Origin + r.Direction, 1 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* rayEntity = static_cast<RayEntity*>(entity);

            auto state      = std::make_unique<RayDragState>();
            state->EntityId = entity->GetID();
            state->Base     = rayEntity->GetRay();

            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* rayEntity = static_cast<RayEntity*>(entity);
            auto* state     = static_cast<RayDragState*>(dragState);

            XLine r = state->Base;

            switch (activeGrip.GripType)
            {
            case Grip::Type::Start:
                // 平移整条射线:方向不变,起点移到光标
                r.Origin = worldPos;
                break;

            case Grip::Type::End:
                // 改变方向:起点不变,方向 = 通过点 - 起点
                r.Direction = worldPos - r.Origin;
                break;

            default:
                break;
            }

            rayEntity->SetRay(r);

            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Start: grip.WorldPos = r.Origin;              break;
                case Grip::Type::End:   grip.WorldPos = r.Origin + r.Direction; break;
                default:                break;
                }
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* rayEntity = static_cast<RayEntity*>(entity);
            auto* state     = static_cast<RayDragState*>(dragState);

            if (!rayEntity || !state)
                return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Ray;
            outEntry.BeforeXLine = state->Base;
            outEntry.AfterXLine  = rayEntity->GetRay();

            return true;
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<RayDragState*>(dragState);
            if (!state) return;

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };   // 灰,半透明

            // Ghost:原始射线(仅沿 +Direction 单侧延伸)
            if (state->Base.IsValid())
            {
                const Math::Vec3   dir = state->Base.UnitDirection();
                const Math::Point3 end = state->Base.Origin + dir * kPreviewHalfLength;
                overlay.AddLine(state->Base.Origin, end, kGhost);
            }
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* rayEntity = static_cast<RayEntity*>(entity);
            auto* state     = static_cast<RayDragState*>(dragState);

            rayEntity->SetRay(state->Base);
        }

    private:
        static constexpr double kPreviewHalfLength = 1.0e6;
    };
}
