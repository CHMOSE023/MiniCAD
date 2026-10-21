#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    struct InsertDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Math::Point3     Base;      // 插入点快照

        // 裁剪边界快照（夹点编辑裁剪线时用；撤销 / 取消靠它还原）
        std::vector<Math::Point3> BaseClip;
        bool                      ClipEdited = false;   // 本次拖的是裁剪夹点
    };

    // ─────────────────────────────────────────────
    // InsertGripHandler
    //
    // 块引用(InsertEntity)的夹点，按 Grip::Type 分三类：
    //
    //   Start , SubIndex = -1  : 插入点 —— 拖动整体移动块
    //   Corner, SubIndex = i   : 裁剪边界**顶点** i —— 拖动一个角
    //   Mid   , SubIndex = i   : 裁剪边界**边** i 的中点 —— 整条边平移
    //
    // 每条边上有 3 个夹点（两个端点 + 一个中点），与 RectangleGripHandler 的布局一致。
    // 边 i 定义为顶点 i → 顶点 (i+1)%n。
    // ─────────────────────────────────────────────
    class InsertGripHandler : public IEntityGripHandler
    {
    public:
        static constexpr int kInsertGripIndex = -1;   // 插入点夹点的 SubIndex

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* e = static_cast<InsertEntity*>(entity);
            const auto id = entity->GetID();

            outGrips.push_back({ id, Grip::Type::Start, e->GetPosition(), kInsertGripIndex });

            if (!e->HasClipBoundary())
                return;

            // ⚠ 裁剪边界存的是**块坐标**，而夹点的 WorldPos 要**世界坐标**
            //   （GripEditor 的命中测试拿它跟鼠标的世界点比）。不换算的话，
            //   块一旦有平移 / 旋转 / 缩放，夹点就会画在错误的位置、也拖不动。
            const auto& poly = e->GetClipBoundary();
            const int n = static_cast<int>(poly.size());
            const Math::Mat4 m = e->CellMatrix(0, 0);
            auto world = [&](const Math::Point3& p) { return m * p; };

            // 顶点夹点
            for (int i = 0; i < n; ++i)
                outGrips.push_back({ id, Grip::Type::Corner, world(poly[i]), i });

            // 边中点夹点（每条边一个）
            for (int i = 0; i < n; ++i)
                outGrips.push_back({ id, Grip::Type::Mid,
                                     EdgeMid(world(poly[i]), world(poly[(i + 1) % n])), i });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& activeGrip) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto  state = std::make_unique<InsertDragState>();
            state->EntityId   = entity->GetID();
            state->Base       = e->GetPosition();
            state->BaseClip   = e->GetClipBoundary();
            state->ClipEdited = (activeGrip.GripType == Grip::Type::Corner ||
                                 activeGrip.GripType == Grip::Type::Mid);
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* baseState, const Grip& activeGrip,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto* state = static_cast<InsertDragState*>(baseState);
            const Object::ObjectID ownerId = entity->GetID();

            // ── 裁剪边界：顶点 或 边中点 ──────────────────────────
            //
            // 鼠标给的是**世界坐标**，而边界存在**块坐标**里（见 BuildGrips 的说明），
            // 所以要先经插入变换的逆变换换算回块坐标，再写回边界。
            if (activeGrip.GripType == Grip::Type::Corner ||
                activeGrip.GripType == Grip::Type::Mid)
            {
                const Math::Point3 local = WorldToBlock(*e, worldPos);
                if (activeGrip.GripType == Grip::Type::Corner)
                    e->MoveClipBoundaryPoint(activeGrip.SubIndex, local);
                else
                    e->MoveClipBoundaryEdge(activeGrip.SubIndex, local);
                SyncClipGripsToWorld(grips, *e);
                return;
            }

            // ── 插入点（基点）：移动块 ─────────────────────────────────────
            //
            // ⚠ 坐标系：本项目的裁剪边界 `m_clip` 存在**块坐标系**里
            //   （与块内实体同系，导入时 SPATIAL_FILTER 的两个矩阵已把点算到该系，
            //     绘制时 ClipDrawSink 位于 TransformDrawSink 之**内**，见 InsertEntity::Draw）。
            //   所以边界**天然跟着块走**，这里什么都不用做 —— 若再去平移边界，
            //   就相当于平移了两次（边界会以两倍速度偏移）。
            //
            //   V0.1.0 的边界存在**世界坐标**里，那边才需要"拖动时固定边界"的特判；
            //   那条语义不可直接搬过来。
            e->SetPosition(worldPos);
            for (auto& grip : grips)
                if (grip.OwnerID == ownerId &&
                    (grip.GripType == Grip::Type::Start || grip.SubIndex == kInsertGripIndex))
                    grip.WorldPos = worldPos;

            // 边界夹点是**块坐标**，块动了之后它们在屏幕上也要跟着动，
            // 否则夹点会停在旧位置、与画出来的裁剪框错位。
            SyncClipGripsToWorld(grips, *e);
        }

        bool EndDrag(Entity* entity, IGripDragState* baseState, DragEntityEntry& outEntry) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto* state = static_cast<InsertDragState*>(baseState);
            if (!e || !state) return false;

            // 裁剪边界被编辑：只记边界快照，**不填** BeforePoint / AfterPoint ——
            // 块本身没动，填了会让消费端把块移到 (0,0,0)。
            if (state->ClipEdited)
            {
                const auto& after = e->GetClipBoundary();
                if (after == state->BaseClip)
                    return false;
                outEntry.Id         = entity->GetID();
                outEntry.Kind       = DragEntityEntry::Kind::Insert;
                outEntry.BeforeClip = state->BaseClip;
                outEntry.AfterClip  = after;
                outEntry.EditedClip = true;
                outEntry.ClipOnly   = true;      // 消费端据此跳过 SetPosition
                return true;
            }

            const Math::Point3 after = e->GetPosition();
            if (std::abs(after.x - state->Base.x) < Math::LengthEPS &&
                std::abs(after.y - state->Base.y) < Math::LengthEPS &&
                std::abs(after.z - state->Base.z) < Math::LengthEPS)
                return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Insert;
            outEntry.BeforePoint = state->Base;
            outEntry.AfterPoint  = after;
            // 裁剪边界不随基点移动，所以这里不记边界快照、也不置 EditedClip。
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* baseState) override
        {
            auto* e     = static_cast<InsertEntity*>(entity);
            auto* state = static_cast<InsertDragState*>(baseState);
            if (!e || !state) return;
            e->SetPosition(state->Base);
            e->SetClipBoundaryRaw(state->BaseClip);
        }

        // 拖拽预览：把裁剪边界描出来，让用户看清正在调整的范围
        void DrawPreview(Entity* entity, IGripDragState*, const Grip& activeGrip, Overlay& overlay) override
        {
            auto* e = static_cast<InsertEntity*>(entity);
            if (!e || !e->HasClipBoundary()) return;

            const auto& poly = e->GetClipBoundary();
            if (poly.size() < 2) return;

            // 边界是**块坐标**，Overlay 要**世界坐标**，中间必须经插入变换
            const Math::Mat4 m = e->CellMatrix(0, 0);

            // 正在调边界时用活动色（橙），否则用常规色
            const bool editing = (activeGrip.GripType == Grip::Type::Corner ||
                                  activeGrip.GripType == Grip::Type::Mid);
            const Math::Color4 color = editing ? GripColors{}.Active : GripColors{}.Normal;
            for (size_t i = 0; i < poly.size(); ++i)
                overlay.AddLine(m * poly[i], m * poly[(i + 1) % poly.size()], color);
        }

    private:
        static Math::Point3 EdgeMid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
        }

        // 世界坐标 → 块坐标。裁剪边界存在块坐标系里，鼠标给的是世界坐标。
        static Math::Point3 WorldToBlock(const InsertEntity& e, const Math::Point3& w)
        {
            const Math::Mat4 m = e.CellMatrix(0, 0);
            return Math::Mat4::Inverse(m) * w;
        }

        // 同步裁剪边界夹点到**世界坐标**（边界本身是块坐标，要经插入变换）
        static void SyncClipGripsToWorld(std::vector<Grip>& grips, const InsertEntity& e)
        {
            const auto& poly = e.GetClipBoundary();
            const int n = static_cast<int>(poly.size());
            const Math::Mat4 m = e.CellMatrix(0, 0);
            const Object::ObjectID ownerId = e.GetID();
            auto world = [&](const Math::Point3& p) { return m * p; };

            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;
                if (grip.GripType == Grip::Type::Corner)
                {
                    if (grip.SubIndex >= 0 && grip.SubIndex < n)
                        grip.WorldPos = world(poly[grip.SubIndex]);
                }
                else if (grip.GripType == Grip::Type::Mid)
                {
                    if (grip.SubIndex >= 0 && grip.SubIndex < n)
                        grip.WorldPos = EdgeMid(world(poly[grip.SubIndex]),
                                               world(poly[(grip.SubIndex + 1) % n]));
                }
            }
        }
    };
}
