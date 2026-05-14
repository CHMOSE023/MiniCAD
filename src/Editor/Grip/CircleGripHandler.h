#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // CircleDragState
    //
    // 修复：
    //   - 移除 Current（无人读取）
    //   - 移除 ActiveType / SubIndex（UpdateDrag 已通过 activeGrip 传入，冗余）
    //   - 移除 QuadrantAngles（象限夹点固定为 0°/90°/180°/270°，
    //     用 SubIndex 即可定位，无需存储角度）
    // ─────────────────────────────────────────────
    struct CircleDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Circle           Base;      // 拖拽开始时的快照，用于：
                                    //   UpdateDrag  — 基于快照增量计算
                                    //   CancelDrag  — 还原到此状态
                                    //   EndDrag     — before 数据写入命令
    };

    // ─────────────────────────────────────────────
    // CircleGripHandler
    // ─────────────────────────────────────────────
    class CircleGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────
        // BuildGrips
        // ─────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            const auto& C = circle->GetCircle();
            const double r = C.Radius;

            // 圆心夹点
            outGrips.push_back({ entity->GetID(), Grip::Type::Center,   C.Center, 0 });

            // 四个象限夹点，SubIndex 对应固定角度：
            //   0 →   0° → (+r,  0)
            //   1 →  90° → ( 0, +r)
            //   2 → 180° → (-r,  0)
            //   3 → 270° → ( 0, -r)
            outGrips.push_back({ entity->GetID(), Grip::Type::Quadrant, { C.Center.x + r, C.Center.y,     C.Center.z }, 0 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Quadrant, { C.Center.x,     C.Center.y + r, C.Center.z }, 1 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Quadrant, { C.Center.x - r, C.Center.y,     C.Center.z }, 2 });
            outGrips.push_back({ entity->GetID(), Grip::Type::Quadrant, { C.Center.x,     C.Center.y - r, C.Center.z }, 3 });
        }

        // ─────────────────────────────────────────
        // BeginDrag — 冻结初始状态
        // ─────────────────────────────────────────
        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            const auto& C = circle->GetCircle();

            auto state      = std::make_unique<CircleDragState>();
            state->EntityId = entity->GetID();
            state->Base     = { C.Center, C.Radius };

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // 修复：
        //   1. 基于 Base 快照计算，避免误差累积
        //   2. 同步 grips 中属于该 Entity 的夹点坐标
        //
        // Center 拖动：圆心跟随光标，半径不变，象限夹点整体平移
        // Quadrant 拖动：半径 = 光标到基准圆心的距离，圆心不动，四个象限夹点重新布置
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* baseState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            auto* state  = static_cast<CircleDragState*>(baseState);

            // 从快照出发，避免误差累积
            Circle   out = state->Base;

            switch (activeGrip.GripType)
            {
            // ─────────────────────────────────────
            // 圆心拖动：平移，半径不变
            // ─────────────────────────────────────
            case Grip::Type::Center:
                out.Center = worldPos;
                break;

            // ─────────────────────────────────────
            // 象限点拖动：调整半径，圆心不动
            // ─────────────────────────────────────
            case Grip::Type::Quadrant:
            {
                double dx = worldPos.x - state->Base.Center.x;
                double dy = worldPos.y - state->Base.Center.y;
                double r  = std::sqrt(dx * dx + dy * dy);

                out.Radius = (r > 1e-6) ? r : 1e-6;   // 防止半径退化为 0
                break;
            }

            default:
                break;
            }

            // 更新 Entity 几何
            circle->SetCircle({ out.Center, out.Radius });

            // 同步 m_grips 中属于该 Entity 的夹点坐标
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, Overlay& overlay) override
        { 
			auto* circle = static_cast<CircleEntity*>(entity);

            auto* state = static_cast<CircleDragState*>(dragState);
            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };   // 灰，半透明
            const Math::Color4 kHelper = { 1.0,  0.85, 0.0,  0.70 };  // 金黄
            const Math::Color4 kFixed = { 1.0,  0.85, 0.0,  0.90 };   // 金黄（固定端点）
             
            // ── 1. Ghost：原始线段位置 ────────────────────────────────
            overlay.AddCircle(state->Base.Center, state->Base.Radius, kGhost);
            overlay.AddLine  (state->Base.Center, circle->GetCircle().Center, kFixed);  // 从圆心到光标的辅助线，显示拖动方向 
        }


        // ─────────────────────────────────────────
        // EndDrag — 推入 CommandStack
        // ─────────────────────────────────────────
        bool EndDrag(Entity* entity, IGripDragState* baseState,   DragEntityEntry& outEntry) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            auto* state  = static_cast<CircleDragState*>(baseState);

            if (!circle || !state)
                return false;
            
            const auto& C = circle->GetCircle();
            Circle after  = { C.Center, C.Radius };

            // 位置和半径都未变，不产生命令 
            auto& b = state->Base;
            if (std::abs(after.Center.x - b.Center.x) < Math::LengthEPS &&
                std::abs(after.Center.y - b.Center.y) < Math::LengthEPS &&
                std::abs(after.Center.z - b.Center.z) < Math::LengthEPS &&
                std::abs(after.Radius   - b.Radius)   < Math::LengthEPS)
                return false;

            outEntry.Id           = entity->GetID();
            outEntry.Kind         = DragEntityEntry::Kind::Circle;
            outEntry.BeforeCircle = state->Base;
            outEntry.AfterCircle  = after;

			return true;
        }

        // ─────────────────────────────────────────
        // CancelDrag — 还原到 Base 快照
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            auto* state  = static_cast<CircleDragState*>(baseState);

            circle->SetCircle({ state->Base.Center, state->Base.Radius });
        }

    private:

        // ─────────────────────────────────────────
        // SyncGrips — 将圆的夹点坐标同步到 m_grips
        //
        // 象限夹点位置由 SubIndex 决定，与角度存储无关：
        //   0 →   0° → (+r,  0)
        //   1 →  90° → ( 0, +r)
        //   2 → 180° → (-r,  0)
        //   3 → 270° → ( 0, -r)
        // ─────────────────────────────────────────
        static void SyncGrips(Object::ObjectID ownerId,    const Circle& snap,  std::vector<Grip>& grips)
        {
            const double r  = snap.Radius;
            const auto&  c  = snap.Center;

            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Center:
                    grip.WorldPos = c;
                    break;

                case Grip::Type::Quadrant:
                    switch (grip.SubIndex)
                    {
                    case 0: grip.WorldPos = { c.x + r, c.y,     c.z }; break;
                    case 1: grip.WorldPos = { c.x,     c.y + r, c.z }; break;
                    case 2: grip.WorldPos = { c.x - r, c.y,     c.z }; break;
                    case 3: grip.WorldPos = { c.x,     c.y - r, c.z }; break;
                    default: break;
                    }
                    break;

                default:
                    break;
                }
            }
        }
    };
}
