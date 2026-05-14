#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/GeomKernel/Rectangle.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "GripType.h"
#include <vector>
#include <memory>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // RectangleDragState
    //
    // 快照：拖拽开始时四个顶点的位置
    // ─────────────────────────────────────────────
    struct RectangleDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Rectangle        Base;   // 拖拽开始时的四顶点快照
    };

    // ─────────────────────────────────────────────
    // RectangleGripHandler
    //
    // 夹点布局（8 个）：
    //   Corner × 4  —— P1 / P2 / P3 / P4，SubIndex 0-3
    //                   自由拖拽单个角点
    //
    //   Mid    × 4  —— Edge0(P1-P2) / Edge1(P2-P3) /
    //                   Edge2(P3-P4) / Edge3(P4-P1) 的中点，SubIndex 0-3
    //                   整条边平移（两端点同步偏移）
    // ─────────────────────────────────────────────
    class RectangleGripHandler : public IEntityGripHandler
    {
    public:

        // ─────────────────────────────────────────
        // BuildGrips
        // ─────────────────────────────────────────
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* rectEntity = static_cast<RectangleEntity*>(entity);
            const Rectangle& R = rectEntity->GetRectangle();
            const auto id = entity->GetID();

            // 四个角点 Corner，SubIndex 对应 P1=0 / P2=1 / P3=2 / P4=3
            outGrips.push_back({ id, Grip::Type::Corner, R.P1, 0 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P2, 1 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P3, 2 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P4, 3 });

            // 四条边的中点 Mid，SubIndex 对应边编号 0-3
            outGrips.push_back({ id, Grip::Type::Mid, EdgeMid(R.P1, R.P2), 0 }); // 底边 P1-P2
            outGrips.push_back({ id, Grip::Type::Mid, EdgeMid(R.P2, R.P3), 1 }); // 右边 P2-P3
            outGrips.push_back({ id, Grip::Type::Mid, EdgeMid(R.P3, R.P4), 2 }); // 顶边 P3-P4
            outGrips.push_back({ id, Grip::Type::Mid, EdgeMid(R.P4, R.P1), 3 }); // 左边 P4-P1
        }

        // ─────────────────────────────────────────
        // BeginDrag — 冻结四顶点快照
        // ─────────────────────────────────────────
        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* rectEntity = static_cast<RectangleEntity*>(entity);

            auto state      = std::make_unique<RectangleDragState>();
            state->EntityId = entity->GetID();
            state->Base     = rectEntity->GetRectangle();

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Corner：自由移动单个角点，其余三个不变
        // Mid   ：整条边平移，以快照中点为基准计算 delta，
        //         同时更新对应边的两个角点
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* rectEntity = static_cast<RectangleEntity*>(entity);
            auto* state      = static_cast<RectangleDragState*>(dragState);

            // 始终从快照出发，避免误差累积
            Rectangle rect = state->Base;

            if (activeGrip.GripType == Grip::Type::Corner)
            {
                ApplyCorner(rect, activeGrip.SubIndex, worldPos);
            }
            else if (activeGrip.GripType == Grip::Type::Mid)
            {
                ApplyEdgeMid(rect, state->Base, activeGrip.SubIndex, worldPos);
            }

            // 更新 Entity 几何
            rectEntity->SetRectangle(rect);

            // 同步 grips 中属于该 Entity 的所有夹点坐标
            SyncGrips(grips, entity->GetID(), rect);
        }

        // ─────────────────────────────────────────
        // DrawPreview — Ghost 显示原始矩形轮廓
        // ─────────────────────────────────────────
        void DrawPreview(Entity* entity, IGripDragState* dragState,
                         const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<RectangleDragState*>(dragState);

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 }; // 灰，半透明

            const Rectangle& B = state->Base;
            overlay.AddLine(B.P1, B.P2, kGhost);
            overlay.AddLine(B.P2, B.P3, kGhost);
            overlay.AddLine(B.P3, B.P4, kGhost);
            overlay.AddLine(B.P4, B.P1, kGhost);
        }

        // ─────────────────────────────────────────
        // EndDrag — 推入 CommandStack
        // ─────────────────────────────────────────
        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* rectEntity = static_cast<RectangleEntity*>(entity);
            auto* state      = static_cast<RectangleDragState*>(dragState);

            if (!rectEntity || !state)
                return false;

            const Rectangle& after = rectEntity->GetRectangle();

            // 位置未变则不产生命令，避免污染撤销栈
            if (RectEqual(after, state->Base))
                return false;
             
            outEntry.Id             = entity->GetID();
            outEntry.Kind           = DragEntityEntry::Kind::Rectangle;
            outEntry.BeforeRect     = state->Base;
            outEntry.AfterRect      = after;

			return true;
        }

        // ─────────────────────────────────────────
        // CancelDrag — 还原到 Base 快照
        // ─────────────────────────────────────────
        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* rectEntity = static_cast<RectangleEntity*>(entity);
            auto* state      = static_cast<RectangleDragState*>(dragState);

            rectEntity->SetRectangle(state->Base);
        }

    private:

        // ─── 工具函数 ────────────────────────────

        static Math::Point3 EdgeMid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5,
                     (a.y + b.y) * 0.5,
                     (a.z + b.z) * 0.5 };
        }

        // Corner 拖拽：直接替换对应顶点
        static void ApplyCorner(Rectangle& rect, int subIndex, const Math::Point3& worldPos)
        {
            switch (subIndex)
            {
            case 0: rect.P1 = worldPos; break;
            case 1: rect.P2 = worldPos; break;
            case 2: rect.P3 = worldPos; break;
            case 3: rect.P4 = worldPos; break;
            default: break;
            }
        }

        // Mid 拖拽：整条边平移
        //   delta = worldPos - 快照中点
        //   边的两个角点各自 += delta
        static void ApplyEdgeMid(Rectangle& rect, const Rectangle& base,
                                 int edgeIndex, const Math::Point3& worldPos)
        {
            // 按 edgeIndex 找到快照中点，求 delta
            Math::Point3 oldMid;
            switch (edgeIndex)
            {
            case 0: oldMid = EdgeMid(base.P1, base.P2); break; // 底边
            case 1: oldMid = EdgeMid(base.P2, base.P3); break; // 右边
            case 2: oldMid = EdgeMid(base.P3, base.P4); break; // 顶边
            case 3: oldMid = EdgeMid(base.P4, base.P1); break; // 左边
            default: return;
            }

            const double dx = worldPos.x - oldMid.x;
            const double dy = worldPos.y - oldMid.y;
            const double dz = worldPos.z - oldMid.z;

            auto translate = [&](Math::Point3& snap, Math::Point3& out) {
                out = { snap.x + dx, snap.y + dy, snap.z + dz };
            };

            switch (edgeIndex)
            {
            case 0: translate(const_cast<Math::Point3&>(base.P1), rect.P1);
                    translate(const_cast<Math::Point3&>(base.P2), rect.P2); break;
            case 1: translate(const_cast<Math::Point3&>(base.P2), rect.P2);
                    translate(const_cast<Math::Point3&>(base.P3), rect.P3); break;
            case 2: translate(const_cast<Math::Point3&>(base.P3), rect.P3);
                    translate(const_cast<Math::Point3&>(base.P4), rect.P4); break;
            case 3: translate(const_cast<Math::Point3&>(base.P4), rect.P4);
                    translate(const_cast<Math::Point3&>(base.P1), rect.P1); break;
            default: break;
            }
        }

        // 同步 grips 数组中属于 ownerId 的所有夹点坐标
        static void SyncGrips(std::vector<Grip>& grips,
                               Object::ObjectID ownerId,
                               const Rectangle& rect)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId)
                    continue;

                if (grip.GripType == Grip::Type::Corner)
                {
                    switch (grip.SubIndex)
                    {
                    case 0: grip.WorldPos = rect.P1; break;
                    case 1: grip.WorldPos = rect.P2; break;
                    case 2: grip.WorldPos = rect.P3; break;
                    case 3: grip.WorldPos = rect.P4; break;
                    default: break;
                    }
                }
                else if (grip.GripType == Grip::Type::Mid)
                {
                    switch (grip.SubIndex)
                    {
                    case 0: grip.WorldPos = EdgeMid(rect.P1, rect.P2); break;
                    case 1: grip.WorldPos = EdgeMid(rect.P2, rect.P3); break;
                    case 2: grip.WorldPos = EdgeMid(rect.P3, rect.P4); break;
                    case 3: grip.WorldPos = EdgeMid(rect.P4, rect.P1); break;
                    default: break;
                    }
                }
            }
        }

        // 四顶点全等判定
        static bool RectEqual(const Rectangle& a, const Rectangle& b)
        {
            auto pt_eq = [](const Math::Point3& p, const Math::Point3& q) {
                return p.x == q.x && p.y == q.y && p.z == q.z;
            };
            return pt_eq(a.P1, b.P1) && pt_eq(a.P2, b.P2)
                && pt_eq(a.P3, b.P3) && pt_eq(a.P4, b.P4);
        }
    };
}
