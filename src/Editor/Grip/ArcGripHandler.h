#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/GeomKernel/Arc.hpp"
#include "Core/Math/Constants.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // ArcDragState
    // ─────────────────────────────────────────────
    struct ArcDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Arc              Base;      // 拖拽开始时的弧快照
    };

    // ─────────────────────────────────────────────
    // ArcGripHandler
    //
    // 夹点布局（5 个）：
    //   Center   × 1  — 圆心，拖动整体平移
    //   Start    × 1  — 弧起点，拖动修改 StartAngle
    //   End      × 1  — 弧终点，拖动修改 EndAngle
    //   Mid      × 1  — 弧中点，拖动修改半径（圆心不变）
    //   Quadrant × 1  — 弧上距离光标最近的象限点（仅辅助捕捉）
    // ─────────────────────────────────────────────
    class ArcGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────
        // BuildGrips
        // ─────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);
            const Arc& arc = arcEnt->GetArc();
            const auto id  = entity->GetID();

            // 圆心
            outGrips.push_back({ id, Grip::Type::Center,   arc.Center,     0 });
            // 起点
            outGrips.push_back({ id, Grip::Type::Start,    arc.StartPoint(), 1 });
            // 终点
            outGrips.push_back({ id, Grip::Type::End,      arc.EndPoint(),   2 });
            // 弧中点（用于拖动半径）
            outGrips.push_back({ id, Grip::Type::Mid,      arc.MidPoint(),   3 });
        }

        // ─────────────────────────────────────────
        // BeginDrag
        // ─────────────────────────────────────────
        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);

            auto state      = std::make_unique<ArcDragState>();
            state->EntityId = entity->GetID();
            state->Base     = arcEnt->GetArc();

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Center  → 整体平移，半径/角度不变
        // Start   → 修改 StartAngle（光标角度对应弧上角）
        // End     → 修改 EndAngle
        // Mid     → 修改 Radius（圆心不动，光标到圆心的距离）
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState,
                        const Grip& activeGrip, const Math::Point3& worldPos,
                        std::vector<Grip>& grips) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);
            auto* state  = static_cast<ArcDragState*>(dragState);

            Arc out = state->Base;  // 始终从快照出发

            switch (activeGrip.GripType)
            {
            // ── 圆心拖动：整体平移 ────────────────────────────────────
            case Grip::Type::Center:
            {
                double dx = worldPos.x - state->Base.Center.x;
                double dy = worldPos.y - state->Base.Center.y;
                out.Center.x += dx;
                out.Center.y += dy;
                break;
            }

            // ── 起点拖动：修改 StartAngle ─────────────────────────────
            case Grip::Type::Start:
            {
                double angle = std::atan2(worldPos.y - state->Base.Center.y,
                                          worldPos.x - state->Base.Center.x);
                out.StartAngle = angle;
                break;
            }

            // ── 终点拖动：修改 EndAngle ───────────────────────────────
            case Grip::Type::End:
            {
                double angle = std::atan2(worldPos.y - state->Base.Center.y,
                                          worldPos.x - state->Base.Center.x);
                out.EndAngle = angle;
                break;
            }

            // ── 弧中点拖动：修改 Radius ───────────────────────────────
            case Grip::Type::Mid:
            {
                double dx = worldPos.x - state->Base.Center.x;
                double dy = worldPos.y - state->Base.Center.y;
                double r  = std::sqrt(dx * dx + dy * dy);
                out.Radius = (r > 1e-6) ? r : 1e-6;
                break;
            }

            default:
                break;
            }

            arcEnt->SetArc(out);
            SyncGrips(entity->GetID(), out, grips);
        }

        // ─────────────────────────────────────────
        // DrawPreview
        // ─────────────────────────────────────────
        void DrawPreview(Entity* entity, IGripDragState* dragState,
                         const Grip& activeGrip, Overlay& overlay) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);
            auto* state  = static_cast<ArcDragState*>(dragState);

            const Math::Color4 kGhost  = { 0.55, 0.55, 0.55, 0.45 };  // 灰，半透明
            const Math::Color4 kHelper = { 1.0,  0.85, 0.0,  0.60 };  // 金黄辅助线

            // Ghost：原始弧
            overlay.AddArc(state->Base.Center, state->Base.Radius,
                           state->Base.StartAngle, state->Base.EndAngle, kGhost);

            // 辅助线：圆心 → 起点 / 圆心 → 终点
            overlay.AddLine(state->Base.Center, state->Base.StartPoint(), kHelper);
            overlay.AddLine(state->Base.Center, state->Base.EndPoint(),   kHelper);
        }

        // ─────────────────────────────────────────
        // EndDrag
        // ─────────────────────────────────────────
        bool EndDrag(Entity* entity, IGripDragState* dragState,
                     DragEntityEntry& outEntry) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);
            auto* state  = static_cast<ArcDragState*>(dragState);

            if (!arcEnt || !state) return false;

            const Arc& after = arcEnt->GetArc();
            const Arc& b     = state->Base;

            // 未变化则不产生命令
            if (std::abs(after.Center.x   - b.Center.x)   < Math::LengthEPS &&
                std::abs(after.Center.y   - b.Center.y)   < Math::LengthEPS &&
                std::abs(after.Radius     - b.Radius)     < Math::LengthEPS &&
                std::abs(after.StartAngle - b.StartAngle) < Math::AngleEPS  &&
                std::abs(after.EndAngle   - b.EndAngle)   < Math::AngleEPS)
                return false;

            outEntry.Id        = entity->GetID();
            outEntry.Kind      = DragEntityEntry::Kind::Arc;
            outEntry.BeforeArc = state->Base;
            outEntry.AfterArc  = after;

            return true;
        }

        // ─────────────────────────────────────────
        // CancelDrag
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* arcEnt = static_cast<ArcEntity*>(entity);
            auto* state  = static_cast<ArcDragState*>(dragState);
            arcEnt->SetArc(state->Base);
        }

    private:

        static void SyncGrips(Object::ObjectID ownerId, const Arc& arc,
                               std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Center: grip.WorldPos = arc.Center;       break;
                case Grip::Type::Start:  grip.WorldPos = arc.StartPoint(); break;
                case Grip::Type::End:    grip.WorldPos = arc.EndPoint();   break;
                case Grip::Type::Mid:    grip.WorldPos = arc.MidPoint();   break;
                default: break;
                }
            }
        }
    };
}