#include "Editor/Overlay/EntityOverlay.h"
#include "Editor/Overlay/Overlay.h"
#include "Core/Entity/Entity.hpp"

#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"

namespace MiniCAD
{
    void DrawEntityToOverlay(Overlay& overlay, const Entity& entity, const Math::Color4& color)
    {
        if (entity.IsKindOf<PointEntity>())
        {
            overlay.AddPoint(static_cast<const PointEntity&>(entity).GetPoint().Position, color);
            return;
        }
        if (entity.IsKindOf<LineEntity>())
        {
            const auto& l = static_cast<const LineEntity&>(entity).GetLine();
            overlay.AddLine(l.Start, l.End, color);
            return;
        }
        if (entity.IsKindOf<CircleEntity>())
        {
            const auto& c = static_cast<const CircleEntity&>(entity).GetCircle();
            overlay.AddCircle(c.Center, c.Radius, color);
            return;
        }
        if (entity.IsKindOf<RectangleEntity>())
        {
            const auto& r = static_cast<const RectangleEntity&>(entity).GetRectangle();
            overlay.AddRect(r.P1, r.P2, r.P3, r.P4, color);
            return;
        }
        if (entity.IsKindOf<ArcEntity>())
        {
            const auto& a = static_cast<const ArcEntity&>(entity).GetArc();
            overlay.AddArc(a.Center, a.Radius, a.StartAngle, a.EndAngle, color);
            return;
        }
        if (entity.IsKindOf<EllipseEntity>())
        {
            const auto& el = static_cast<const EllipseEntity&>(entity).GetEllipse();
            overlay.AddEllipse(el, color);
            return;
        }
        if (entity.IsKindOf<PolylineEntity>())
        {
            overlay.AddPolyline(static_cast<const PolylineEntity&>(entity).GetPolyline(), color);
            return;
        }
        if (entity.IsKindOf<SplineEntity>())
        {
            const auto& sp = static_cast<const SplineEntity&>(entity).GetSpline();
            if (!sp.IsValid()) return;
            auto pts = sp.Tessellate(32);
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                overlay.AddLine(pts[i], pts[i + 1], color);
            return;
        }

        // 文字 / 其它类型:回退为包围盒矩形
        auto box = entity.GetBoundingBox();
        overlay.AddRect(box.Min, box.Max, color);
    }
}
