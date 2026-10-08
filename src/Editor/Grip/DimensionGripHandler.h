#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/DimensionEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "Editor/Overlay/Overlay.h"

#include "GripType.h"
#include <memory>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // DimGripRole — 夹点语义角色（存入 Grip::SubIndex）
    //   各标注子类型按需取用，便于拖拽 / 同步时统一寻址。
    // ─────────────────────────────────────────────
    enum DimGripRole : int
    {
        DimRole_P1     = 0,   // 第一定义点 / 半径直径端点 / 坐标特征点
        DimRole_P2     = 1,   // 第二定义点
        DimRole_DimPt  = 2,   // 尺寸线经过点 / 标注弧点 / 坐标引线端（Angular/Ordinate 用）
        DimRole_Center = 3,   // 角度顶点 / 半径直径圆心
        DimRole_D1     = 4,   // 尺寸线端点1（尺寸界线1与尺寸线交点）
        DimRole_D2     = 5,   // 尺寸线端点2（尺寸界线2与尺寸线交点）
        DimRole_Text   = 6,   // 文字中点
    };

    // ─────────────────────────────────────────────
    // DimensionDragState — 拖拽开始时的几何快照
    // ─────────────────────────────────────────────
    struct DimensionDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        DimSnapshot      Base;
    };

    // ─────────────────────────────────────────────
    // DimensionGripHandler
    //
    // 仿 AutoCAD 标注夹点编辑。各子类型夹点：
    //   Aligned / Linear : P1 + P2 + D1 + D2 + Text（5 夹点）
    //     P1/P2 拖动定义点，尺寸线随动保持偏移；D1/D2/Text 拖动改变尺寸线偏移距离
    //   Angular          : 顶点 + 两边点 + 弧端点×2 + 标注弧点 + Text（与线性同构,
    //                      弧端/弧上点拖动改标注弧半径与侧,Text 自由放置文字）
    //   ArcLength        : 与 Angular 相同（圆心 + 弧两端 + 标注弧端点×2 + 标注弧点 + Text）
    //   JoggedRadius     : 真实圆心 + 弧上箭头点 + 替代圆心 + 折弯位置 + Text
    //   Radius           : 圆心 + 圆周点
    //   Diameter         : 直径两端点（圆心随中点）
    //   Ordinate         : 特征点 + 引线端点
    // ─────────────────────────────────────────────
    class DimensionGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* dim = static_cast<DimensionEntity*>(entity);
            const Object::ObjectID id = entity->GetID();

            auto push = [&](Grip::Type t, int role)
            {
                outGrips.push_back({ id, t, RolePos(dim, role), role });
            };

            switch (dim->GetType())
            {
            case DimType::JoggedRadius:
                push(Grip::Type::Center, DimRole_Center);
                push(Grip::Type::End,    DimRole_P1);     // 弧上箭头点
                push(Grip::Type::Start,  DimRole_P2);     // 替代圆心
                push(Grip::Type::Mid,    DimRole_D1);     // 折弯位置
                push(Grip::Type::Mid,    DimRole_Text);
                break;

            case DimType::Angular:
            case DimType::ArcLength:
                // 与线性同构:P1/P2 调整两边定义点(尺寸界线长度),
                // D1/D2(弧端)与 DimPt(弧上点)拖动调整标注弧半径/侧,
                // Text 拖动自由放置文字。
                push(Grip::Type::Center, DimRole_Center);
                push(Grip::Type::Start,  DimRole_P1);
                push(Grip::Type::End,    DimRole_P2);
                push(Grip::Type::Mid,    DimRole_D1);     // 标注弧端点1
                push(Grip::Type::Mid,    DimRole_D2);     // 标注弧端点2
                push(Grip::Type::Mid,    DimRole_DimPt);
                push(Grip::Type::Mid,    DimRole_Text);
                break;

            case DimType::Radius:
                push(Grip::Type::Center, DimRole_Center);
                push(Grip::Type::End,    DimRole_P1);
                break;

            case DimType::Diameter:
                push(Grip::Type::Start, DimRole_P1);
                push(Grip::Type::End,   DimRole_P2);
                break;

            case DimType::Ordinate:
                push(Grip::Type::Start, DimRole_P1);
                push(Grip::Type::End,   DimRole_DimPt);
                break;

            case DimType::Aligned:
            case DimType::Linear:
            default:
                push(Grip::Type::Start, DimRole_P1);
                push(Grip::Type::End,   DimRole_P2);
                push(Grip::Type::Mid,   DimRole_D1);     // 尺寸线端点1（界线1交点）
                push(Grip::Type::Mid,   DimRole_D2);     // 尺寸线端点2（界线2交点）
                push(Grip::Type::Mid,   DimRole_Text);   // 文字中点
                break;
            }
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* dim = static_cast<DimensionEntity*>(entity);

            auto state      = std::make_unique<DimensionDragState>();
            state->EntityId = entity->GetID();
            state->Base     = Capture(dim);

            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* dim   = static_cast<DimensionEntity*>(entity);
            auto* state = static_cast<DimensionDragState*>(dragState);

            // 从快照出发，避免误差累积
            DimSnapshot s     = state->Base;
            const int   role  = activeGrip.SubIndex;
            const Math::Point3 wp = worldPos;

            switch (s.Type)
            {
            case DimType::JoggedRadius:
                if      (role == DimRole_Center) s.CenterPoint  = wp;
                else if (role == DimRole_P1)     s.P1           = wp;
                else if (role == DimRole_P2)     s.P2           = wp;
                else if (role == DimRole_D1 || role == DimRole_DimPt) s.DimLinePoint = wp;
                else if (role == DimRole_Text)
                {
                    s.TextPos           = wp;
                    s.UseDefaultTextPos = false;
                }
                break;

            case DimType::Angular:
            case DimType::ArcLength:
                if      (role == DimRole_Center) s.CenterPoint  = wp;
                else if (role == DimRole_P1)     s.P1           = wp;
                else if (role == DimRole_P2)     s.P2           = wp;
                else if (role == DimRole_DimPt ||
                         role == DimRole_D1    ||
                         role == DimRole_D2)     s.DimLinePoint = wp;   // 弧端/弧上点 → 调整弧半径与标注侧
                else if (role == DimRole_Text)
                {
                    s.TextPos           = wp;
                    s.UseDefaultTextPos = false;
                }
                break;

            case DimType::Radius:
                if      (role == DimRole_Center) s.CenterPoint = wp;
                else if (role == DimRole_P1)     s.P1          = wp;
                break;

            case DimType::Diameter:
                if      (role == DimRole_P1) s.P1 = wp;
                else if (role == DimRole_P2) s.P2 = wp;
                s.CenterPoint = Mid(s.P1, s.P2);   // 圆心始终取两端中点
                break;

            case DimType::Ordinate:
                if      (role == DimRole_P1)    { s.P1 = wp; s.P2 = wp; }   // 特征点
                else if (role == DimRole_DimPt) s.DimLinePoint = wp;        // 引线端
                break;

            case DimType::Aligned:
            case DimType::Linear:
            default:
                if (role == DimRole_P1)
                {
                    Math::Vec3 delta = wp - s.P1;
                    s.P1           = wp;
                    s.DimLinePoint = s.DimLinePoint + delta;
                }
                else if (role == DimRole_P2)
                {
                    Math::Vec3 delta = wp - s.P2;
                    s.P2           = wp;
                    s.DimLinePoint = s.DimLinePoint + delta;
                }
                else if (role == DimRole_D1 || role == DimRole_D2 || role == DimRole_DimPt)
                {
                    s.DimLinePoint = wp;
                }
                else if (role == DimRole_Text)
                {
                    s.TextPos           = wp;
                    s.UseDefaultTextPos = false;
                }
                break;
            }

            Restore(dim, s);

            // 同步 m_grips 中属于该 Entity 的夹点坐标（实时跟手）
            const Object::ObjectID ownerId = entity->GetID();
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;
                grip.WorldPos = RolePos(dim, grip.SubIndex);
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* dim   = static_cast<DimensionEntity*>(entity);
            auto* state = static_cast<DimensionDragState*>(dragState);
            if (!dim || !state)
                return false;

            outEntry.Id        = entity->GetID();
            outEntry.Kind      = DragEntityEntry::Kind::Dimension;
            outEntry.BeforeDim = state->Base;
            outEntry.AfterDim  = Capture(dim);

            return true;
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<DimensionDragState*>(dragState);
            if (!state) return;

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };   // 灰，半透明
            const DimSnapshot& s = state->Base;

            switch (s.Type)
            {
            case DimType::Aligned:
            case DimType::Linear:
            {
                // Ghost 尺寸线 + 两条尺寸界线（用临时实体复算投影）
                DimensionEntity ghost(entity->GetID(), s.P1, s.P2, s.DimLinePoint);
                ghost.SetType(s.Type);
                ghost.SetLinearAngle(s.LinearAngle);
                const Math::Point3 d1 = ghost.DimLineP1();
                const Math::Point3 d2 = ghost.DimLineP2();
                overlay.AddLine(d1, d2, kGhost);
                overlay.AddLine(s.P1, d1, kGhost);
                overlay.AddLine(s.P2, d2, kGhost);
                break;
            }
            case DimType::Angular:
            case DimType::ArcLength:
            case DimType::Diameter:
                overlay.AddLine(s.CenterPoint, s.P1, kGhost);
                overlay.AddLine(s.CenterPoint, s.P2, kGhost);
                break;
            case DimType::JoggedRadius:
                overlay.AddLine(s.P2, s.DimLinePoint, kGhost);
                overlay.AddLine(s.DimLinePoint, s.P1, kGhost);
                break;
            case DimType::Radius:
                overlay.AddLine(s.CenterPoint, s.P1, kGhost);
                break;
            case DimType::Ordinate:
            default:
                overlay.AddLine(s.P1, s.DimLinePoint, kGhost);
                break;
            }
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* dim   = static_cast<DimensionEntity*>(entity);
            auto* state = static_cast<DimensionDragState*>(dragState);
            Restore(dim, state->Base);
        }

    private:
        static Math::Point3 Mid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
        }

        // 角色 → 当前世界坐标
        static Math::Point3 RolePos(const DimensionEntity* d, int role)
        {
            switch (role)
            {
            case DimRole_P1:     return d->GetP1();
            case DimRole_P2:     return d->GetP2();
            case DimRole_DimPt:  return d->GetDimLinePoint();
            case DimRole_Center: return d->GetCenterPoint();
            case DimRole_D1:     return d->DimLineP1();
            case DimRole_D2:     return d->DimLineP2();
            case DimRole_Text:   return d->TextPosition();
            default:             return {};
            }
        }

        static DimSnapshot Capture(const DimensionEntity* d)
        {
            DimSnapshot s;
            s.Type              = d->GetType();
            s.P1                = d->GetP1();
            s.P2                = d->GetP2();
            s.DimLinePoint      = d->GetDimLinePoint();
            s.CenterPoint       = d->GetCenterPoint();
            s.LinearAngle       = d->GetLinearAngle();
            s.Axis              = d->GetOrdinateAxis();
            s.TextPos           = d->TextPosition();
            s.UseDefaultTextPos = d->IsUsingDefaultTextPosition();
            return s;
        }

        static void Restore(DimensionEntity* d, const DimSnapshot& s)
        {
            d->SetType(s.Type);
            d->SetP1(s.P1);
            d->SetP2(s.P2);
            d->SetDimLinePoint(s.DimLinePoint);
            d->SetCenterPoint(s.CenterPoint);
            d->SetLinearAngle(s.LinearAngle);
            d->SetOrdinateAxis(s.Axis);
            if (s.UseDefaultTextPos)
                d->ResetTextPosition();
            else
                d->SetTextPosition(s.TextPos);
        }
    };
}
