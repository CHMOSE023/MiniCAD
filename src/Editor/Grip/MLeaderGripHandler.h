#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <vector>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // MLeaderDragState
    // ─────────────────────────────────────────────
    struct MLeaderDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        MLeaderSnapshot  Base;     // 拖拽开始时的多重引线快照
    };

    // ─────────────────────────────────────────────
    // MLeaderGripHandler
    //
    // 像 AutoCAD 一样夹点编辑多重引线(MLeaderEntity)：
    //
    // 夹点布局：
    //   Start  × ∑顶点 — 每条引线的每个顶点一个夹点
    //                    (箭头尖端 = 顶点 0；SubIndex 打包 = 引线号*Stride + 顶点号)
    //                    直接移动该顶点
    //   Center × 1     — 基线端点(landing)夹点
    //                    平移整个内容侧(基线/dogleg/文字随之移动，
    //                    各引线末点自动重连到新的 dogleg 起点)
    // ─────────────────────────────────────────────
    class MLeaderGripHandler : public IEntityGripHandler
    {
        // SubIndex 打包：line * kStride + vertex
        static constexpr int kStride = 1 << 16;
        static int  Pack(int line, int vert)              { return line * kStride + vert; }
        static void Unpack(int s, int& line, int& vert)   { line = s / kStride; vert = s % kStride; }

    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* ml = static_cast<MLeaderEntity*>(entity);
            const auto& lines = ml->GetLeaderLines();
            const auto  id    = entity->GetID();

            // 每条引线的每个顶点一个 Start 夹点
            for (int li = 0; li < static_cast<int>(lines.size()); ++li)
            {
                const auto& pts = lines[li].points;
                for (int vi = 0; vi < static_cast<int>(pts.size()); ++vi)
                    outGrips.push_back({ id, Grip::Type::Start, pts[vi], Pack(li, vi) });
            }

            // 基线端点(landing)夹点
            outGrips.push_back({ id, Grip::Type::Center, ml->GetLanding(), -1 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* ml = static_cast<MLeaderEntity*>(entity);

            auto state      = std::make_unique<MLeaderDragState>();
            state->EntityId = entity->GetID();
            state->Base     = Snapshot(ml);

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Start (顶点) ：直接替换对应引线的对应顶点
        // Center(基线) ：landing 移到光标，内容侧整体平移
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* ml    = static_cast<MLeaderEntity*>(entity);
            auto* state = static_cast<MLeaderDragState*>(dragState);

            // 从快照出发，避免误差累积
            MLeaderSnapshot out = state->Base;

            switch (activeGrip.GripType)
            {
            case Grip::Type::Start:
            {
                int li, vi;
                Unpack(activeGrip.SubIndex, li, vi);
                if (li >= 0 && li < static_cast<int>(out.Lines.size()))
                {
                    auto& pts = out.Lines[li].points;
                    if (vi >= 0 && vi < static_cast<int>(pts.size()))
                        pts[vi] = worldPos;
                }
                break;
            }

            case Grip::Type::Center:
                out.Landing = worldPos;
                break;

            default:
                break;
            }

            Restore(ml, out);
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<MLeaderDragState*>(dragState);

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };  // 灰，半透明

            // Ghost：原始各引线折线
            for (const auto& ln : state->Base.Lines)
                for (size_t i = 1; i < ln.points.size(); ++i)
                    overlay.AddLine(ln.points[i - 1], ln.points[i], kGhost);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* ml    = static_cast<MLeaderEntity*>(entity);
            auto* state = static_cast<MLeaderDragState*>(dragState);

            if (!ml || !state) return false;

            outEntry.Id            = entity->GetID();
            outEntry.Kind          = DragEntityEntry::Kind::MLeader;
            outEntry.BeforeMLeader = state->Base;
            outEntry.AfterMLeader  = Snapshot(ml);

            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* ml    = static_cast<MLeaderEntity*>(entity);
            auto* state = static_cast<MLeaderDragState*>(dragState);
            Restore(ml, state->Base);
        }

    private:

        static MLeaderSnapshot Snapshot(const MLeaderEntity* ml)
        {
            MLeaderSnapshot s;
            s.Lines     = ml->GetLeaderLines();
            s.Landing   = ml->GetLanding();
            s.DoglegDir = ml->GetDoglegDir();
            return s;
        }

        static void Restore(MLeaderEntity* ml, const MLeaderSnapshot& s)
        {
            ml->GetLeaderLines() = s.Lines;
            ml->SetLanding(s.Landing);
            ml->SetDoglegDir(s.DoglegDir);
        }

        // 同步 grips 中属于 ownerId 的所有夹点坐标
        static void SyncGrips(Object::ObjectID ownerId, const MLeaderSnapshot& s, std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                if (grip.GripType == Grip::Type::Start)
                {
                    int li, vi;
                    Unpack(grip.SubIndex, li, vi);
                    if (li >= 0 && li < static_cast<int>(s.Lines.size()))
                    {
                        const auto& pts = s.Lines[li].points;
                        if (vi >= 0 && vi < static_cast<int>(pts.size()))
                            grip.WorldPos = pts[vi];
                    }
                }
                else if (grip.GripType == Grip::Type::Center)
                {
                    grip.WorldPos = s.Landing;
                }
            }
        }
    };
}
