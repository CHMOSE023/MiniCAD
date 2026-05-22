#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    struct MTextDragState : public IGripDragState
    {
        Object::ObjectID EntityId    = Object::InvalidID;
        Math::Point3     BasePos;
        double           BaseBoxWidth = 0.0;
    };

    // ─────────────────────────────────────────────
    // MTextGripHandler
    //
    // 多行文字：
    //   Start  (SubIndex=0) — 插入点，拖动整体移动
    //   Radius (SubIndex=1) — 宽度调整点，拖动修改 BoxWidth
    //                         位于 pos + (boxWidth > 0 ? boxWidth : height*4, 0)
    // ─────────────────────────────────────────────
    class MTextGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto*       e   = static_cast<MTextEntity*>(entity);
            const auto& pos = e->GetPosition();
            double      bw  = e->GetBoxWidth();
            double      h   = e->GetHeight();

            outGrips.push_back({ entity->GetID(), Grip::Type::Start,  pos,                                    0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Radius, WidthGripPos(pos, bw, h),               1 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override
        {
            auto* e     = static_cast<MTextEntity*>(entity);
            auto  state = std::make_unique<MTextDragState>();
            state->EntityId     = entity->GetID();
            state->BasePos      = e->GetPosition();
            state->BaseBoxWidth = e->GetBoxWidth();
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* e     = static_cast<MTextEntity*>(entity);
            auto* state = static_cast<MTextDragState*>(dragState);
            const Object::ObjectID ownerId = entity->GetID();

            if (activeGrip.GripType == Grip::Type::Start)
            {
                e->SetPosition(worldPos);
                double bw = e->GetBoxWidth();
                double h  = e->GetHeight();

                for (auto& grip : grips)
                {
                    if (grip.OwnerID != ownerId) continue;
                    if (grip.GripType == Grip::Type::Start)
                        grip.WorldPos = worldPos;
                    else if (grip.GripType == Grip::Type::Radius)
                        grip.WorldPos = WidthGripPos(worldPos, bw, h);
                }
            }
            else if (activeGrip.GripType == Grip::Type::Radius)
            {
                // BoxWidth = 鼠标 X 相对于插入点的距离，最小为 0
                const auto& pos = e->GetPosition();
                double newBW = std::max(0.0, worldPos.x - pos.x);
                e->SetBoxWidth(newBW);

                for (auto& grip : grips)
                {
                    if (grip.OwnerID == ownerId && grip.GripType == Grip::Type::Radius)
                        grip.WorldPos = WidthGripPos(pos, newBW, e->GetHeight());
                }
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* e     = static_cast<MTextEntity*>(entity);
            auto* state = static_cast<MTextDragState*>(baseState);
            if (!e || !state) return false;

            const Math::Point3 afterPos = e->GetPosition();
            double             afterBW  = e->GetBoxWidth();

            bool posChanged = std::abs(afterPos.x - state->BasePos.x) > Math::LengthEPS ||
                              std::abs(afterPos.y - state->BasePos.y) > Math::LengthEPS ||
                              std::abs(afterPos.z - state->BasePos.z) > Math::LengthEPS;
            bool bwChanged  = std::abs(afterBW - state->BaseBoxWidth) > Math::LengthEPS;

            if (!posChanged && !bwChanged) return false;

            outEntry.Id         = entity->GetID();
            outEntry.Kind       = DragEntityEntry::Kind::MText;
            outEntry.BeforeMText = { state->BasePos, state->BaseBoxWidth };
            outEntry.AfterMText  = { afterPos,       afterBW };
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* e     = static_cast<MTextEntity*>(entity);
            auto* state = static_cast<MTextDragState*>(baseState);
            e->SetPosition(state->BasePos);
            e->SetBoxWidth(state->BaseBoxWidth);
        }

    private:
        // boxWidth=0 时使用 height*4 作为宽度夹点的占位距离
        static Math::Point3 WidthGripPos(const Math::Point3& pos, double bw, double h)
        {
            return { pos.x + (bw > 0.0 ? bw : h * 4.0), pos.y, pos.z };
        }
    };
}
