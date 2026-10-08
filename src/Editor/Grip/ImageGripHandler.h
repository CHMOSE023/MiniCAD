#pragma once
#include "IEntityGripHandler.h"
#include "Core/Entity/ImageEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include "GripType.h"
#include <cmath>
#include <memory>
#include <vector>

namespace MiniCAD
{
    // 图像夹点：四个角点，拖动时以对角点为锚点等比缩放（不允许把图像拉歪）。
    // 整体移动走 Move / 拖动实体，这里不提供边中点夹点。
    class ImageGripHandler : public IEntityGripHandler
    {
    public:
        struct DragState : public IGripDragState
        {
            Rectangle Base;
        };

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            const Rectangle& R = static_cast<ImageEntity*>(entity)->GetRectangle();
            const auto id = entity->GetID();
            outGrips.push_back({ id, Grip::Type::Corner, R.P1, 0 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P2, 1 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P3, 2 });
            outGrips.push_back({ id, Grip::Type::Corner, R.P4, 3 });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override
        {
            auto st  = std::make_unique<DragState>();
            st->Base = static_cast<ImageEntity*>(entity)->GetRectangle();
            return st;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* st = static_cast<DragState*>(dragState);
            const Rectangle& B = st->Base;
            const Math::Point3 pts[4] = { B.P1, B.P2, B.P3, B.P4 };

            const int i = activeGrip.SubIndex;
            if (i < 0 || i > 3) return;
            const Math::Point3& anchor = pts[(i + 2) % 4];
            const Math::Point3& moved  = pts[i];

            // 比例 = 光标在「锚点 → 原角点」方向上的投影长度 / 原对角线长度
            const double dx = moved.x - anchor.x, dy = moved.y - anchor.y;
            const double d2 = dx * dx + dy * dy;
            if (d2 < 1e-18) return;
            double k = ((worldPos.x - anchor.x) * dx + (worldPos.y - anchor.y) * dy) / d2;
            if (k < 1e-4) k = 1e-4;

            auto scaled = [&](const Math::Point3& p) -> Math::Point3
            {
                return { anchor.x + (p.x - anchor.x) * k, anchor.y + (p.y - anchor.y) * k, p.z };
            };
            Rectangle r{ scaled(B.P1), scaled(B.P2), scaled(B.P3), scaled(B.P4) };
            static_cast<ImageEntity*>(entity)->SetRectangle(r);

            const Math::Point3 np[4] = { r.P1, r.P2, r.P3, r.P4 };
            for (auto& g : grips)
                if (g.OwnerID == entity->GetID() && g.SubIndex >= 0 && g.SubIndex < 4)
                    g.WorldPos = np[g.SubIndex];
        }

        void DrawPreview(Entity*, IGripDragState* dragState, const Grip&, Overlay& overlay) override
        {
            const Rectangle& B = static_cast<DragState*>(dragState)->Base;
            const Math::Color4 ghost = { 0.55, 0.55, 0.55, 0.45 };
            overlay.AddLine(B.P1, B.P2, ghost);
            overlay.AddLine(B.P2, B.P3, ghost);
            overlay.AddLine(B.P3, B.P4, ghost);
            overlay.AddLine(B.P4, B.P1, ghost);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& out) override
        {
            auto* e  = static_cast<ImageEntity*>(entity);
            auto* st = static_cast<DragState*>(dragState);
            const Rectangle& a = e->GetRectangle();
            const Rectangle& b = st->Base;
            auto same = [](const Math::Point3& p, const Math::Point3& q)
            { return std::abs(p.x - q.x) < 1e-9 && std::abs(p.y - q.y) < 1e-9; };
            if (same(a.P1, b.P1) && same(a.P2, b.P2) && same(a.P3, b.P3) && same(a.P4, b.P4))
                return false;

            out.Id         = entity->GetID();
            out.Kind       = DragEntityEntry::Kind::Rectangle;
            out.BeforeRect = b;
            out.AfterRect  = a;
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            static_cast<ImageEntity*>(entity)->SetRectangle(static_cast<DragState*>(dragState)->Base);
        }
    };
}
