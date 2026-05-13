#pragma once 
#include "IEntityGripHandler.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "GripType.h"
#include <vector>
#include <memory>
#include "GripType.h"

namespace MiniCAD
{

    // ─────────────────────────────────────────────
    // Line Drag State
    // ─────────────────────────────────────────────
    struct LineDragState : public IGripDragState
    {
        LineSegment Base;   // 初始线段快照 

        LineSegment Current;
    };

    // ─────────────────────────────────────────────
    // Line Grip Handler
    // ─────────────────────────────────────────────
    class LineGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────
        // BuildGrips
        // ─────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            printf("LineGripHandler->BuildGrips\n");
            auto* lineEntity = static_cast<LineEntity*>(entity);
            const Line& L = lineEntity->GetLine();

            outGrips.push_back({ entity->GetID(), Grip::Type::Start, L.Start, 0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Mid, L.Midpoint(), 1 });
            outGrips.push_back({ entity->GetID(), Grip::Type::End, L.End, 2 });
        }

        // ─────────────────────────────────────────
        // BeginDrag
        // ─────────────────────────────────────────
        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& activeGrip) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            const Line& L = lineEntity->GetLine();

            auto state = std::make_unique<LineDragState>();

            state->Base = { L.Start, L.End };
            state->Current = state->Base;

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& /*grips*/) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            auto* state      = static_cast<LineDragState*>(dragState);

            LineSegment seg = state->Base;

            switch (activeGrip.GripType)
            {
            case Grip::Type::Start:
                seg.Start = worldPos;
                break;

            case Grip::Type::End:
                seg.End = worldPos;
                break;

            case Grip::Type::Mid:
            {
                // 平移整条线
                auto mid = seg.Start;
                mid.x = (seg.Start.x + seg.End.x) * 0.5;
                mid.y = (seg.Start.y + seg.End.y) * 0.5;

                double dx = worldPos.x - mid.x;
                double dy = worldPos.y - mid.y;

                seg.Start.x += dx;
                seg.Start.y += dy;
                seg.End.x += dx;
                seg.End.y += dy;
                break;
            }

            default:
                break;
            }

            state->Current = seg;

            lineEntity->SetLine(Line(seg.Start, seg.End));
        }

        // ─────────────────────────────────────────
        // EndDrag
        // ─────────────────────────────────────────
        void EndDrag(   Entity* /*entity*/,   IGripDragState* /*dragState*/) override
        {
            // 这里一般不做逻辑（CommandStack 已记录）
        }

        // ─────────────────────────────────────────
        // CancelDrag
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            auto* state      = static_cast<LineDragState*>(dragState);

            lineEntity->SetLine(Line(state->Base.Start, state->Base.End));
        }
    };
}