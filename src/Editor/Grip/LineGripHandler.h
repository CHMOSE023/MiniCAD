#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Document/Command/DragEntitiesCommand.h"

#include "GripType.h"
#include <vector>
#include <memory>

namespace MiniCAD
{ 
    struct LineDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        LineSegment      Base;      // 拖拽开始时的快照，用于：
                                    //   UpdateDrag  — 基于快照增量计算，避免误差累积
                                    //   CancelDrag  — 还原到此状态
                                    //   EndDrag     — before 数据写入命令
    };

    // ─────────────────────────────────────────────
    // LineGripHandler
    // ─────────────────────────────────────────────
    class LineGripHandler : public IEntityGripHandler
    {
    public:

       
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            const Line& L    = lineEntity->GetLine(); 

            outGrips.push_back({ entity->GetID(), Grip::Type::Start, L.Start,      0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Mid,   L.Midpoint(), 1 });
            outGrips.push_back({ entity->GetID(), Grip::Type::End,   L.End,        2 });
        }
         

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            const Line& L = lineEntity->GetLine();

            auto state      = std::make_unique<LineDragState>();
            state->EntityId = entity->GetID();
            state->Base     = { L.Start, L.End };

            return state;
        }
         
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            auto* state      = static_cast<LineDragState*>(dragState);

            // 从快照出发，避免误差累积
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
                // 平移整条线：以快照中点为基准计算 delta
                double midX = (state->Base.Start.x + state->Base.End.x) * 0.5;
                double midY = (state->Base.Start.y + state->Base.End.y) * 0.5;

                double dx = worldPos.x - midX;
                double dy = worldPos.y - midY;

                seg.Start.x += dx;  seg.Start.y += dy;
                seg.End.x   += dx;  seg.End.y   += dy;
                break;
            }

            default:
                break;
            }

            // 更新 Entity 几何
            lineEntity->SetLine(Line(seg.Start, seg.End));

            // 同步 m_grips 中属于该 Entity 的夹点坐标
            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Start:
                    grip.WorldPos = seg.Start;
                    break;
                case Grip::Type::End:
                    grip.WorldPos = seg.End;
                    break;
                case Grip::Type::Mid:   // 修复：z 轴也取平均 
                    grip.WorldPos = { (seg.Start.x + seg.End.x) * 0.5,   (seg.Start.y + seg.End.y) * 0.5,   (seg.Start.z + seg.End.z) * 0.5 };
                    break;
                default:
                    break;
                }
            }
        }

        // ─────────────────────────────────────────
        // EndDrag — 推入 CommandStack（修复核心）
        // ─────────────────────────────────────────
        void EndDrag(Entity* entity, IGripDragState* dragState, CommandStack& cmdStack) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            auto* state      = static_cast<LineDragState*>(dragState);

            const Line& L = lineEntity->GetLine();

            LineSegment after = { L.Start, L.End };

            // 位置未变则不产生命令（避免空操作污染撤销栈）
            if (after.Start == state->Base.Start && after.End == state->Base.End)
                return;

            DragEntityEntry entry;
            entry.Id         = entity->GetID();
            entry.Kind       = DragEntityEntry::Kind::Line;
            entry.BeforeLine = state->Base;
            entry.AfterLine  = after;

            std::vector<DragEntityEntry> entries;
            entries.push_back(std::move(entry)); 
            cmdStack.Push(std::make_unique<DragEntitiesCommand>(std::move(entries)));
        }
       
        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, Overlay& overlay) override
        { 
            auto* state = static_cast<LineDragState*>(dragState);
               
            const Math::Color4 kGhost  = { 0.55, 0.55, 0.55, 0.45 };   // 灰，半透明
            const Math::Color4 kHelper = { 1.0,  0.85, 0.0,  0.70 };   // 金黄
            const Math::Color4 kFixed  = { 1.0,  0.85, 0.0,  0.90 };   // 金黄（固定端点）

            // ── 1. Ghost：原始线段位置 ────────────────────────────────
            overlay.AddLine(state->Base.Start, state->Base.End, kGhost);
         }

        // ─────────────────────────────────────────
        // CancelDrag — 还原到 Base 快照
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* lineEntity = static_cast<LineEntity*>(entity);
            auto* state      = static_cast<LineDragState*>(dragState);

            lineEntity->SetLine(Line(state->Base.Start, state->Base.End));
        }
    };
}

 