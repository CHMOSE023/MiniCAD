#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/GeomKernel/Ellipse.hpp"
#include "Core/Math/Constants.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // EllipseDragState
    // ─────────────────────────────────────────────
    struct EllipseDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Ellipse          Base;      // 拖拽开始时的椭圆快照
    };

    // ─────────────────────────────────────────────
    // EllipseGripHandler
    //
    // 夹点布局（整椭圆 5 个）：
    //   Center   × 1 — 椭圆中心，整体平移
    //   Quadrant × 4 — 四个轴端点（沿长短轴方向），SubIndex 0-3
    //                    0 = +X 轴端（E），修改 RadiusX
    //                    1 = +Y 轴端（N），修改 RadiusY
    //                    2 = -X 轴端（W），修改 RadiusX（镜像）
    //                    3 = -Y 轴端（S），修改 RadiusY（镜像）
    // 椭圆弧：轴端点只保留落在弧上的；另加
    //   Start / End  — 弧的两端，沿椭圆改起止参数（其余几何不变）
    // ─────────────────────────────────────────────
    class EllipseGripHandler : public IEntityGripHandler
    {
    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* ellEnt = static_cast<EllipseEntity*>(entity);
            const Ellipse& el = ellEnt->GetEllipse();
            const auto id     = entity->GetID();

            outGrips.push_back({ id, Grip::Type::Center,   el.Center,     0 });
            for (int k = 0; k < 4; ++k)     // +X / +Y / -X / -Y
            {
                const double t = Math::PI * 0.5 * k;
                if (el.ContainsParam(t))
                    outGrips.push_back({ id, Grip::Type::Quadrant, el.PointAt(t), k });
            }
            if (!el.IsFull())
            {
                outGrips.push_back({ id, Grip::Type::Start, el.StartPoint(), 0 });
                outGrips.push_back({ id, Grip::Type::End,   el.EndPoint(),   0 });
            }
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* ellEnt = static_cast<EllipseEntity*>(entity);

            auto state      = std::make_unique<EllipseDragState>();
            state->EntityId = entity->GetID();
            state->Base     = ellEnt->GetEllipse();

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Center    → 整体平移，轴长/旋转不变
        // Quadrant  → 修改对应半轴长度
        //               SubIndex 0/2（E/W）→ RadiusX = 光标到中心的沿长轴投影距离
        //               SubIndex 1/3（N/S）→ RadiusY = 光标到中心的沿短轴投影距离
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState,
                        const Grip& activeGrip, const Math::Point3& worldPos,
                        std::vector<Grip>& grips) override
        {
            auto* ellEnt = static_cast<EllipseEntity*>(entity);
            auto* state  = static_cast<EllipseDragState*>(dragState);

            Ellipse out = state->Base;

            switch (activeGrip.GripType)
            {
            // ── 中心拖动：整体平移 ────────────────────────────────────
            case Grip::Type::Center:
                out.Center = worldPos;
                break;

            // ── 轴端点拖动：修改半轴长 ───────────────────────────────
            case Grip::Type::Quadrant:
            {
                double dx     = worldPos.x - state->Base.Center.x;
                double dy     = worldPos.y - state->Base.Center.y;
                double cosR   = std::cos(state->Base.Rotation);
                double sinR   = std::sin(state->Base.Rotation);

                // 投影到局部坐标轴
                double localX = dx * cosR + dy * sinR;  // 沿长轴分量
                double localY = -dx * sinR + dy * cosR; // 沿短轴分量

                switch (activeGrip.SubIndex)
                {
                case 0: // E → 修改 RadiusX
                case 2: // W → 修改 RadiusX（负方向，取绝对值）
                    out.RadiusX = std::max(1e-6, std::abs(localX));
                    break;
                case 1: // N → 修改 RadiusY
                case 3: // S → 修改 RadiusY（负方向，取绝对值）
                    out.RadiusY = std::max(1e-6, std::abs(localY));
                    break;
                default:
                    break;
                }
                break;
            }

            // ── 弧端点拖动：沿椭圆改起止参数 ─────────────────────────
            case Grip::Type::Start:
                out.SetParams(out.ParamOfPoint(worldPos), out.EndParam);
                break;
            case Grip::Type::End:
                out.SetParams(out.StartParam, out.ParamOfPoint(worldPos));
                break;

            default:
                break;
            }

            ellEnt->SetEllipse(out);
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState,
                         const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<EllipseDragState*>(dragState);

            const Math::Color4 kGhost  = { 0.55, 0.55, 0.55, 0.45 };
            const Math::Color4 kHelper = { 1.0,  0.85, 0.0,  0.55 };

            // Ghost 原始椭圆
            overlay.AddEllipse(state->Base, kGhost);

            // 辅助线：长轴 / 短轴
            overlay.AddLine(state->Base.VertexW(), state->Base.VertexE(), kHelper);
            overlay.AddLine(state->Base.VertexS(), state->Base.VertexN(), kHelper);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState,
                     DragEntityEntry& outEntry) override
        {
            auto* ellEnt = static_cast<EllipseEntity*>(entity);
            auto* state  = static_cast<EllipseDragState*>(dragState);

            if (!ellEnt || !state) return false;

            const Ellipse& after = ellEnt->GetEllipse();
            const Ellipse& b     = state->Base;

            if (std::abs(after.Center.x  - b.Center.x)  < Math::LengthEPS &&
                std::abs(after.Center.y  - b.Center.y)  < Math::LengthEPS &&
                std::abs(after.RadiusX   - b.RadiusX)   < Math::LengthEPS &&
                std::abs(after.RadiusY   - b.RadiusY)   < Math::LengthEPS &&
                std::abs(after.Rotation  - b.Rotation)  < Math::AngleEPS &&
                std::abs(after.StartParam - b.StartParam) < Math::AngleEPS &&
                std::abs(after.EndParam   - b.EndParam)   < Math::AngleEPS)
                return false;

            outEntry.Id            = entity->GetID();
            outEntry.Kind          = DragEntityEntry::Kind::Ellipse;
            outEntry.BeforeEllipse = state->Base;
            outEntry.AfterEllipse  = after;

            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* ellEnt = static_cast<EllipseEntity*>(entity);
            auto* state  = static_cast<EllipseDragState*>(dragState);

            ellEnt->SetEllipse(state->Base);
        }

    private:

        static void SyncGrips(Object::ObjectID ownerId, const Ellipse& el,
                               std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                switch (grip.GripType)
                {
                case Grip::Type::Center:
                    grip.WorldPos = el.Center;
                    break;
                case Grip::Type::Quadrant:
                    grip.WorldPos = el.PointAt(Math::PI * 0.5 * grip.SubIndex);
                    break;
                case Grip::Type::Start:
                    grip.WorldPos = el.StartPoint();
                    break;
                case Grip::Type::End:
                    grip.WorldPos = el.EndPoint();
                    break;
                default:
                    break;
                }
            }
        }
    };
}