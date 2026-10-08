#include "EntityTranslate.h"

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
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/ToleranceEntity.hpp"

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
        if (entity.IsKindOf<TextEntity>())
        {
            auto& te = static_cast<TextEntity&>(entity);
            te.SetPosition(te.GetPosition() + d);
            return;
        }
        if (entity.IsKindOf<MTextEntity>())
        {
            auto& me = static_cast<MTextEntity&>(entity);
            me.SetPosition(me.GetPosition() + d);
            return;
        }
        if (entity.IsKindOf<InsertEntity>())
        {
            auto& ie = static_cast<InsertEntity&>(entity);
            ie.SetPosition(ie.GetPosition() + d);
            // rotation 不变
            return;
        }
        if (entity.IsKindOf<DimensionEntity>())
        {
            auto& de = static_cast<DimensionEntity&>(entity);
            de.SetP1         (de.GetP1()          + d);
            de.SetP2         (de.GetP2()          + d);
            de.SetDimLinePoint(de.GetDimLinePoint() + d);
            de.SetCenterPoint (de.GetCenterPoint()  + d);
            // linearAngle 不变
            return;
        }
        if (entity.IsKindOf<HatchEntity>())
        {
            auto& he = static_cast<HatchEntity&>(entity);
            for (auto& loop : he.GetLoops())
                for (auto& edge : loop.Edges)
                {
                    switch (edge.Type)
                    {
                    case HatchEdge::Kind::Poly:
                        for (auto& p : edge.Poly.Points) p += d;
                        break;
                    case HatchEdge::Kind::EllipseArc:
                        edge.Ell.Center = edge.Ell.Center + d;
                        break;
                    case HatchEdge::Kind::Spline:
                        for (auto& p : edge.Spl.FitPoints) p += d;
                        edge.Spl.Build();
                        break;
                    }
                }
            return;
        }
        if (entity.IsKindOf<LeaderEntity>())
        {
            auto& le = static_cast<LeaderEntity&>(entity);
            auto verts = le.GetVertices();
            for (auto& v : verts) v = v + d;
            le.SetVertices(std::move(verts));
            return;
        }
        if (entity.IsKindOf<MLeaderEntity>())
        {
            auto& ml = static_cast<MLeaderEntity&>(entity);
            ml.SetLanding(ml.GetLanding() + d);
            // doglegDir 不变（方向向量）
            for (auto& ln : ml.GetLeaderLines())
                for (auto& p : ln.points) p = p + d;
            return;
        }
        if (entity.IsKindOf<RayEntity>())
        {
            auto& re = static_cast<RayEntity&>(entity);
            re.SetOrigin(re.GetOrigin() + d);
            // direction 不变
            return;
        }
        if (entity.IsKindOf<XLineEntity>())
        {
            auto& xl = static_cast<XLineEntity&>(entity);
            xl.SetOrigin(xl.GetOrigin() + d);
            // direction 不变
            return;
        }
        if (entity.IsKindOf<ToleranceEntity>())
        {
            auto& te = static_cast<ToleranceEntity&>(entity);
            te.SetInsertion(te.GetInsertion() + d);
            // direction 不变
            return;
        }
    }
}
