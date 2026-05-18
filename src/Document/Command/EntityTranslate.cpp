#include "EntityTranslate.h"

#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"

namespace MiniCAD
{
    void TranslateEntityInPlace(Entity& entity, const Math::Vec3& d)
    {
        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            p.Position += d;
            pe.SetPoint(p);
            return;
        }
        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            l.Start += d;  l.End += d;
            le.SetLine(l);
            return;
        }
        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            c.Center += d;
            ce.SetCircle(c);
            return;
        }
        if (entity.IsKindOf<RectangleEntity>())
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            r.P1 += d;  r.P2 += d;  r.P3 += d;  r.P4 += d;
            re.SetRectangle(r);
            return;
        }
        if (entity.IsKindOf<ArcEntity>())
        {
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            a.Center += d;
            ae.SetArc(a);
            return;
        }
        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            el.Center += d;
            ee.SetEllipse(el);
            return;
        }
        if (entity.IsKindOf<PolylineEntity>())
        {
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) p += d;
            pe.SetPolyline(std::move(pl));
            return;
        }
        if (entity.IsKindOf<SplineEntity>())
        {
            auto& se = static_cast<SplineEntity&>(entity);
            auto& sp = se.GetSpline();
            for (auto& p : sp.FitPoints) p += d;
            sp.Build();
            return;
        }
    }
}
