#pragma once

#include "IEntityGripHandler.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include <memory>
#include <unordered_map>

#include "GripType.h"

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // Circle Drag State
    // ─────────────────────────────────────────────
    struct CircleDragState : public IGripDragState
    {
        CircleSnapshot Base;
        CircleSnapshot Current;

        Grip::Type ActiveType = Grip::Type::Center;
        int SubIndex = -1;

        // quadrant 初始角度（扩展用，目前用于稳定语义）
        std::unordered_map<int, double> QuadrantAngles;
    };

    // ─────────────────────────────────────────────
    // Circle Grip Handler
    // ─────────────────────────────────────────────
    class CircleGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────────
        // BuildGrips（纯显示层）
        // ─────────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            const auto& C = circle->GetCircle();

            const double r = C.Radius;

            // center grip
            outGrips.push_back({
                entity->GetID(),
                Grip::Type::Center,
                C.Center,
                0
            });

            // quadrant grips（固定 4 点）
            outGrips.push_back({
                entity->GetID(),
                Grip::Type::Quadrant,
                { C.Center.x + r, C.Center.y, C.Center.z },
                0
            });

            outGrips.push_back({
                entity->GetID(),
                Grip::Type::Quadrant,
                { C.Center.x, C.Center.y + r, C.Center.z },
                1
            });

            outGrips.push_back({
                entity->GetID(),
                Grip::Type::Quadrant,
                { C.Center.x - r, C.Center.y, C.Center.z },
                2
            });

            outGrips.push_back({
                entity->GetID(),
                Grip::Type::Quadrant,
                { C.Center.x, C.Center.y - r, C.Center.z },
                3
            });
        }

        // ─────────────────────────────────────────────
        // BeginDrag（冻结初始状态）
        // ─────────────────────────────────────────────
        std::unique_ptr<IGripDragState>
        BeginDrag(Entity* entity, const Grip& activeGrip) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            const auto& C = circle->GetCircle();

            auto state = std::make_unique<CircleDragState>();

            state->Base = { C.Center, C.Radius };
            state->Current = state->Base;

            state->ActiveType = activeGrip.GripType;
            state->SubIndex = activeGrip.SubIndex;

            // quadrant 语义预留（可扩展约束系统）
            state->QuadrantAngles[0] = 0.0;
            state->QuadrantAngles[1] = Math::TwoPI;
            state->QuadrantAngles[2] = Math::PI;
            state->QuadrantAngles[3] = 3.0 * Math::TwoPI;

            return state;
        }

        // ─────────────────────────────────────────────
        // UpdateDrag（核心逻辑）
        // ─────────────────────────────────────────────
        void UpdateDrag( Entity* entity,     IGripDragState* baseState,   const Grip& activeGrip,  const Math::Point3& worldPos,   std::vector<Grip>& /*grips*/)  override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            auto* state  = static_cast<CircleDragState*>(baseState);

            CircleSnapshot out = state->Base;

            switch (activeGrip.GripType)
            {
                // ─────────────────────────────
                // 圆心拖动（平移）
                // ─────────────────────────────
                case Grip::Type::Center:
                {
                    out.Center = worldPos;
                    break;
                }

                // ─────────────────────────────
                // 半径拖动（任意象限点）
                // ─────────────────────────────
                case Grip::Type::Quadrant:
                {
                    double dx = worldPos.x - state->Base.Center.x;
                    double dy = worldPos.y - state->Base.Center.y;

                    double r = std::sqrt(dx * dx + dy * dy);

                    // 防止退化
                    out.Radius = (r > 1e-6) ? r : 1e-6;

                    break;
                }

                default:
                    break;
            }

            state->Current = out;

            circle->SetCircle({
                out.Center,
                out.Radius
            });
        }

        // ─────────────────────────────────────────────
        // EndDrag（提交点）
        // ─────────────────────────────────────────────
        void EndDrag(Entity*, IGripDragState*) override
        {
            // 交给 GripEditor CommandStack 处理
        }

        // ─────────────────────────────────────────────
        // CancelDrag（回滚）
        // ─────────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* circle = static_cast<CircleEntity*>(entity);
            auto* state  = static_cast<CircleDragState*>(baseState);

            circle->SetCircle({
                state->Base.Center,
                state->Base.Radius
            });
        }
    };
}
