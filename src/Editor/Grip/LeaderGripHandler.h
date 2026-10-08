#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/LeaderEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <vector>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // LeaderDragState
    // ─────────────────────────────────────────────
    struct LeaderDragState : public IGripDragState
    {
        Object::ObjectID          EntityId = Object::InvalidID;
        std::vector<Math::Point3> Base;     // 拖拽开始时的引线顶点快照
    };

    // ─────────────────────────────────────────────
    // LeaderGripHandler
    //
    // 像 AutoCAD 一样夹点编辑引线路径(LeaderEntity)：
    //
    // 夹点布局：
    //   Start × n   — 每个路径顶点一个夹点(SubIndex = 顶点索引)
    //                  直接移动该顶点(箭头端 / 折点 / 文字挂接端都随之移动)
    //   Mid   × n-1 — 每段中点一个夹点(SubIndex = 段索引)
    //                  整段平移(两端顶点同步偏移)
    // ─────────────────────────────────────────────
    class LeaderGripHandler : public IEntityGripHandler
    {
    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* leader = static_cast<LeaderEntity*>(entity);
            const auto& v = leader->GetVertices();
            const auto  id = entity->GetID();

            // 每个顶点一个 Start 夹点
            for (int i = 0; i < static_cast<int>(v.size()); ++i)
                outGrips.push_back({ id, Grip::Type::Start, v[i], i });

            // 每段中点一个 Mid 夹点
            for (int i = 0; i + 1 < static_cast<int>(v.size()); ++i)
                outGrips.push_back({ id, Grip::Type::Mid, SegMid(v[i], v[i + 1]), i });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* leader = static_cast<LeaderEntity*>(entity);

            auto state      = std::make_unique<LeaderDragState>();
            state->EntityId = entity->GetID();
            state->Base     = leader->GetVertices();

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Start(顶点)：直接替换对应顶点坐标
        // Mid  (段中)：delta = worldPos - 快照段中点，两端顶点同步平移
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* leader = static_cast<LeaderEntity*>(entity);
            auto* state  = static_cast<LeaderDragState*>(dragState);

            // 从快照出发，避免误差累积
            std::vector<Math::Point3> out = state->Base;

            switch (activeGrip.GripType)
            {
            // ── 顶点拖动：直接替换 ───────────────────────────────────
            case Grip::Type::Start:
            {
                int idx = activeGrip.SubIndex;
                if (idx >= 0 && idx < static_cast<int>(out.size()))
                    out[idx] = worldPos;
                break;
            }

            // ── 段中点拖动：整段平移 ─────────────────────────────────
            case Grip::Type::Mid:
            {
                int seg = activeGrip.SubIndex;
                if (seg < 0 || seg + 1 >= static_cast<int>(out.size())) break;

                Math::Point3 oldMid = SegMid(state->Base[seg], state->Base[seg + 1]);
                double dx = worldPos.x - oldMid.x;
                double dy = worldPos.y - oldMid.y;

                out[seg].x     += dx;  out[seg].y     += dy;
                out[seg + 1].x += dx;  out[seg + 1].y += dy;
                break;
            }

            default:
                break;
            }

            leader->SetVertices(out);
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<LeaderDragState*>(dragState);

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };  // 灰，半透明

            // Ghost：原始引线路径
            for (size_t i = 1; i < state->Base.size(); ++i)
                overlay.AddLine(state->Base[i - 1], state->Base[i], kGhost);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* leader = static_cast<LeaderEntity*>(entity);
            auto* state  = static_cast<LeaderDragState*>(dragState);

            if (!leader || !state) return false;

            outEntry.Id           = entity->GetID();
            outEntry.Kind         = DragEntityEntry::Kind::Leader;
            outEntry.BeforeLeader = state->Base;
            outEntry.AfterLeader  = leader->GetVertices();

            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* leader = static_cast<LeaderEntity*>(entity);
            auto* state  = static_cast<LeaderDragState*>(dragState);
            leader->SetVertices(state->Base);
        }

    private:

        static Math::Point3 SegMid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
        }

        // 同步 grips 中属于 ownerId 的所有夹点坐标
        static void SyncGrips(Object::ObjectID ownerId, const std::vector<Math::Point3>& v, std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                int i = grip.SubIndex;
                if (grip.GripType == Grip::Type::Start)
                {
                    if (i >= 0 && i < static_cast<int>(v.size()))
                        grip.WorldPos = v[i];
                }
                else if (grip.GripType == Grip::Type::Mid)
                {
                    if (i >= 0 && i + 1 < static_cast<int>(v.size()))
                        grip.WorldPos = SegMid(v[i], v[i + 1]);
                }
            }
        }
    };
}
