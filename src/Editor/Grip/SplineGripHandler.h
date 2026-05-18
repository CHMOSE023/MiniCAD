#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/Math/Constants.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // SplineDragState
    // ─────────────────────────────────────────────
    struct SplineDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Spline           Base;      // 拖拽开始时的样条快照（含 FitPoints + Boundary）
    };

    // ─────────────────────────────────────────────
    // SplineGripHandler
    //
    // 夹点布局：
    //   Start × n — 每个拟合点一个夹点（SubIndex = 拟合点索引）
    //               移动拟合点后立即重建样条（Build）
    //
    // 与 AutoCAD 行为对应：
    //   拖动拟合点（Fit Point）→ 曲线实时重拟合
    // ─────────────────────────────────────────────
    class SplineGripHandler : public IEntityGripHandler
    {
    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* spEnt = static_cast<SplineEntity*>(entity);
            const Spline& sp = spEnt->GetSpline();
            const auto id    = entity->GetID();

            // 每个拟合点一个 Start 夹点
            for (int i = 0; i < static_cast<int>(sp.FitPoints.size()); ++i)
                outGrips.push_back({ id, Grip::Type::Start, sp.FitPoints[i], i });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* spEnt = static_cast<SplineEntity*>(entity);

            auto state      = std::make_unique<SplineDragState>();
            state->EntityId = entity->GetID();
            state->Base     = spEnt->GetSpline();   // 深拷贝（含 FitPoints + Segments）

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // 移动拟合点 SubIndex，立即重建样条曲线。
        // 重建开销与拟合点数量成正比（O(n) Thomas 追赶法），
        // 通常 < 1ms，满足实时预览需求。
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState,
                        const Grip& activeGrip, const Math::Point3& worldPos,
                        std::vector<Grip>& grips) override
        {
            auto* spEnt = static_cast<SplineEntity*>(entity);
            auto* state  = static_cast<SplineDragState*>(dragState);

            if (activeGrip.GripType != Grip::Type::Start) return;

            int idx = activeGrip.SubIndex;
            if (idx < 0 || idx >= static_cast<int>(spEnt->GetSpline().FitPoints.size()))
                return;

            // 直接修改拟合点并重建
            spEnt->SetFitPoint(idx, worldPos);   // 内部调用 Build()

            // 同步 grips 中该拟合点的夹点坐标
            SyncGrips(entity->GetID(), spEnt->GetSpline(), grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState,
                         const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<SplineDragState*>(dragState);

            const Math::Color4 kGhost    = { 0.55, 0.55, 0.55, 0.45 };  // 灰，半透明
            const Math::Color4 kCtrlLine = { 0.4,  0.6,  0.8,  0.25 };  // 蓝色辅助多边形

            // Ghost：原始样条曲线
            if (state->Base.IsValid())
            {
                auto pts = state->Base.Tessellate(24);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    overlay.AddLine(pts[i], pts[i + 1], kGhost);
            }

            // 辅助线：原始拟合点控制多边形
            const auto& fps = state->Base.FitPoints;
            for (size_t i = 0; i + 1 < fps.size(); ++i)
                overlay.AddLine(fps[i], fps[i + 1], kCtrlLine);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState,
                     DragEntityEntry& outEntry) override
        {
            auto* spEnt = static_cast<SplineEntity*>(entity);
            auto* state  = static_cast<SplineDragState*>(dragState);

            if (!spEnt || !state) return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Spline;
            outEntry.BeforeSpline = state->Base;
            outEntry.AfterSpline  = spEnt->GetSpline();

            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* spEnt = static_cast<SplineEntity*>(entity);
            auto* state  = static_cast<SplineDragState*>(dragState);

            // 还原所有拟合点并重建
            spEnt->GetSpline() = state->Base;
            spEnt->GetSpline().Build();
        }

    private:

        static void SyncGrips(Object::ObjectID ownerId, const Spline& sp,
                               std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;
                if (grip.GripType != Grip::Type::Start) continue;

                int idx = grip.SubIndex;
                if (idx >= 0 && idx < static_cast<int>(sp.FitPoints.size()))
                    grip.WorldPos = sp.FitPoints[idx];
            }
        }
    };
}