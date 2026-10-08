#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/XLineEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "Core/Math/Constants.hpp"

#include "GripType.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // XLineDragState — 拖拽开始时的几何快照
    // ─────────────────────────────────────────────
    struct XLineDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        XLine            Base;   // UpdateDrag 基于快照增量;CancelDrag / EndDrag 还原 / before
    };

    // ─────────────────────────────────────────────
    // XLineGripHandler
    //
    // 无限直线两个夹点:
    //   Start(基点)   — 拖拽平移整条线(方向不变)
    //   End (通过点)  — 拖拽改变方向(基点不变),通过点 = Origin + Direction
    // ─────────────────────────────────────────────
    class XLineGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* xlineEntity = static_cast<XLineEntity*>(entity);
            const XLine& xl   = xlineEntity->GetXLine();

            outGrips.push_back({ entity->GetID(), Grip::Type::Start, xl.Origin,               0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::End,   xl.Origin + xl.Direction, 1 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* xlineEntity = static_cast<XLineEntity*>(entity);

            auto state      = std::make_unique<XLineDragState>();
            state->EntityId = entity->GetID();
            state->Base     = xlineEntity->GetXLine();

            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* xlineEntity = static_cast<XLineEntity*>(entity);
            auto* state       = static_cast<XLineDragState*>(dragState);

            // 从快照出发,避免误差累积
            XLine xl = state->Base;

            switch (activeGrip.GripType)
            {
            case Grip::Type::Start:
                // 平移整条线:方向不变,基点移到光标
                xl.Origin = worldPos;
                break;

            case Grip::Type::End:
                // 改变方向:基点不变,方向 = 通过点 - 基点
                xl.Direction = worldPos - xl.Origin;
                break;

            default:
                break;
            }

            xlineEntity->SetXLine(xl);

            // 同步 m_grips 中属于该 Entity 的夹点坐标(预览跟手)
            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Start: grip.WorldPos = xl.Origin;                break;
                case Grip::Type::End:   grip.WorldPos = xl.Origin + xl.Direction; break;
                default:                break;
                }
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* xlineEntity = static_cast<XLineEntity*>(entity);
            auto* state       = static_cast<XLineDragState*>(dragState);

            if (!xlineEntity || !state)
                return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::XLine;
            outEntry.BeforeXLine = state->Base;
            outEntry.AfterXLine  = xlineEntity->GetXLine();

            return true;
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<XLineDragState*>(dragState);
            if (!state) return;

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };   // 灰,半透明

            // Ghost:原始无限直线(向两侧延伸)
            if (state->Base.IsValid())
            {
                const Math::Vec3   dir = state->Base.UnitDirection();
                const Math::Point3 a   = state->Base.Origin - dir * kPreviewHalfLength;
                const Math::Point3 b   = state->Base.Origin + dir * kPreviewHalfLength;
                overlay.AddLine(a, b, kGhost);
            }
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* xlineEntity = static_cast<XLineEntity*>(entity);
            auto* state       = static_cast<XLineDragState*>(dragState);

            xlineEntity->SetXLine(state->Base);
        }

    private:
        static constexpr double kPreviewHalfLength = 1.0e6;
    };
}
