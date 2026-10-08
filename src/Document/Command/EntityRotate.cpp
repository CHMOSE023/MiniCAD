#include "EntityRotate.h"

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
#include "Core/Math/Constants.hpp"

#include <cmath>

namespace MiniCAD
{
    Math::Point3 RotatePoint(const Math::Point3& p, const Math::Point3& pivot, double angle)
    {
        double c = std::cos(angle);
        double s = std::sin(angle);
        double dx = p.x - pivot.x;
        double dy = p.y - pivot.y;
        return { pivot.x + dx * c - dy * s,
                 pivot.y + dx * s + dy * c,
                 p.z };
    }

    static Math::Vec3 RotateVec2D(const Math::Vec3& v, double angle)
    {
        double c = std::cos(angle), s = std::sin(angle);
        return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
    }

    void RotateEntityInPlace(Entity& entity, const Math::Point3& pivot, double angle)
    {
        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            p.Position = RotatePoint(p.Position, pivot, angle);
            pe.SetPoint(p);
            return;
        }

        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            l.Start = RotatePoint(l.Start, pivot, angle);
            l.End   = RotatePoint(l.End,   pivot, angle);
            le.SetLine(l);
            return;
        }

        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            c.Center = RotatePoint(c.Center, pivot, angle);
            ce.SetCircle(c);
            return;
        }

        if (entity.IsKindOf<RectangleEntity>())
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            r.P1 = RotatePoint(r.P1, pivot, angle);
            r.P2 = RotatePoint(r.P2, pivot, angle);
            r.P3 = RotatePoint(r.P3, pivot, angle);
            r.P4 = RotatePoint(r.P4, pivot, angle);
            re.SetRectangle(r);
            return;
        }

        if (entity.IsKindOf<ArcEntity>())
        {
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            a.Center     = RotatePoint(a.Center, pivot, angle);
            a.StartAngle = a.StartAngle + angle;
            a.EndAngle   = a.EndAngle   + angle;
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
            };
            a.StartAngle = Norm(a.StartAngle);
            a.EndAngle   = Norm(a.EndAngle);
            ae.SetArc(a);
            return;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            el.Center   = RotatePoint(el.Center, pivot, angle);
            el.Rotation = el.Rotation + angle;
            ee.SetEllipse(el);
            return;
        }

        if (entity.IsKindOf<PolylineEntity>())
        {
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) p = RotatePoint(p, pivot, angle);
            pe.SetPolyline(std::move(pl));
            return;
        }

        if (entity.IsKindOf<SplineEntity>())
        {
            auto& se = static_cast<SplineEntity&>(entity);
            auto& sp = se.GetSpline();
            for (auto& p : sp.FitPoints) p = RotatePoint(p, pivot, angle);
            sp.Build();
            return;
        }

        if (entity.IsKindOf<TextEntity>())
        {
            auto& te = static_cast<TextEntity&>(entity);
            te.SetPosition(RotatePoint(te.GetPosition(), pivot, angle));
            te.SetRotation(te.GetRotation() + static_cast<float>(angle));
            return;
        }

        if (entity.IsKindOf<MTextEntity>())
        {
            auto& me = static_cast<MTextEntity&>(entity);
            me.SetPosition(RotatePoint(me.GetPosition(), pivot, angle));
            me.SetRotation(me.GetRotation() + angle);
            return;
        }

        if (entity.IsKindOf<InsertEntity>())
        {
            auto& ie = static_cast<InsertEntity&>(entity);
            ie.SetPosition(RotatePoint(ie.GetPosition(), pivot, angle));
            ie.SetRotation(ie.GetRotation() + angle);
            return;
        }

        if (entity.IsKindOf<DimensionEntity>())
        {
            auto& de = static_cast<DimensionEntity&>(entity);
            de.SetP1         (RotatePoint(de.GetP1(),          pivot, angle));
            de.SetP2         (RotatePoint(de.GetP2(),          pivot, angle));
            de.SetDimLinePoint(RotatePoint(de.GetDimLinePoint(), pivot, angle));
            de.SetCenterPoint (RotatePoint(de.GetCenterPoint(),  pivot, angle));
            de.SetLinearAngle (de.GetLinearAngle() + angle);
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
                        for (auto& p : edge.Poly.Points) p = RotatePoint(p, pivot, angle);
                        break;
                    case HatchEdge::Kind::EllipseArc:
                        edge.Ell.Center   = RotatePoint(edge.Ell.Center, pivot, angle);
                        edge.Ell.Rotation = edge.Ell.Rotation + angle;
                        break;
                    case HatchEdge::Kind::Spline:
                        for (auto& p : edge.Spl.FitPoints) p = RotatePoint(p, pivot, angle);
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
            for (auto& v : verts) v = RotatePoint(v, pivot, angle);
            le.SetVertices(std::move(verts));
            return;
        }

        if (entity.IsKindOf<MLeaderEntity>())
        {
            auto& ml = static_cast<MLeaderEntity&>(entity);
            ml.SetLanding(RotatePoint(ml.GetLanding(), pivot, angle));
            ml.SetDoglegDir(RotateVec2D(ml.GetDoglegDir(), angle));
            for (auto& ln : ml.GetLeaderLines())
                for (auto& p : ln.points) p = RotatePoint(p, pivot, angle);
            return;
        }

        if (entity.IsKindOf<RayEntity>())
        {
            auto& re = static_cast<RayEntity&>(entity);
            re.SetOrigin   (RotatePoint(re.GetOrigin(), pivot, angle));
            re.SetDirection(RotateVec2D(re.GetDirection(), angle));
            return;
        }

        if (entity.IsKindOf<XLineEntity>())
        {
            auto& xl = static_cast<XLineEntity&>(entity);
            xl.SetOrigin   (RotatePoint(xl.GetOrigin(), pivot, angle));
            xl.SetDirection(RotateVec2D(xl.GetDirection(), angle));
            return;
        }

        if (entity.IsKindOf<ToleranceEntity>())
        {
            auto& te = static_cast<ToleranceEntity&>(entity);
            te.SetInsertion(RotatePoint(te.GetInsertion(), pivot, angle));
            te.SetDirection(RotateVec2D(te.GetDirection(), angle));
            return;
        }
    }
}
