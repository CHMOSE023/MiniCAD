#include "RotateMoveCommand.h"
#include "Scene/Scene.h"
#include "Document/Command/EntityRotate.h"

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
    namespace
    {
        auto Norm = [](double x) {
            x = std::fmod(x, Math::TwoPI);
            if (x < 0.0) x += Math::TwoPI;
            return x;
        };

        Math::Vec3 RotVec2D(const Math::Vec3& v, double a) {
            double c = std::cos(a), s = std::sin(a);
            return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
        }

        Point Rotate(const Point& src, const Math::Point3& piv, double a) {
            Point o = src; o.Position = RotatePoint(src.Position, piv, a); return o;
        }
        Line Rotate(const Line& src, const Math::Point3& piv, double a) {
            Line o = src;
            o.Start = RotatePoint(src.Start, piv, a);
            o.End   = RotatePoint(src.End,   piv, a);
            return o;
        }
        Circle Rotate(const Circle& src, const Math::Point3& piv, double a) {
            Circle o = src; o.Center = RotatePoint(src.Center, piv, a); return o;
        }
        Rectangle Rotate(const Rectangle& src, const Math::Point3& piv, double a) {
            Rectangle o = src;
            o.P1 = RotatePoint(src.P1, piv, a);
            o.P2 = RotatePoint(src.P2, piv, a);
            o.P3 = RotatePoint(src.P3, piv, a);
            o.P4 = RotatePoint(src.P4, piv, a);
            return o;
        }
        Arc Rotate(const Arc& src, const Math::Point3& piv, double a) {
            Arc o = src;
            o.Center     = RotatePoint(src.Center, piv, a);
            o.StartAngle = Norm(src.StartAngle + a);
            o.EndAngle   = Norm(src.EndAngle   + a);
            return o;
        }
        Ellipse Rotate(const Ellipse& src, const Math::Point3& piv, double a) {
            Ellipse o = src;
            o.Center   = RotatePoint(src.Center, piv, a);
            o.Rotation = src.Rotation + a;
            return o;
        }
        Polyline Rotate(const Polyline& src, const Math::Point3& piv, double a) {
            Polyline o = src;
            for (auto& p : o.Points) p = RotatePoint(p, piv, a);
            return o;
        }
        Spline Rotate(const Spline& src, const Math::Point3& piv, double a) {
            Spline o = src;
            for (auto& p : o.FitPoints) p = RotatePoint(p, piv, a);
            o.Build();
            return o;
        }

        HatchEdge RotateEdge(const HatchEdge& src, const Math::Point3& piv, double a) {
            HatchEdge o = src;
            switch (src.Type) {
            case HatchEdge::Kind::Poly:
                for (auto& p : o.Poly.Points) p = RotatePoint(p, piv, a);
                break;
            case HatchEdge::Kind::EllipseArc:
                o.Ell.Center   = RotatePoint(o.Ell.Center, piv, a);
                o.Ell.Rotation = o.Ell.Rotation + a;
                break;
            case HatchEdge::Kind::Spline:
                for (auto& p : o.Spl.FitPoints) p = RotatePoint(p, piv, a);
                o.Spl.Build();
                break;
            }
            return o;
        }
        std::vector<HatchLoop> RotateLoops(const std::vector<HatchLoop>& src, const Math::Point3& piv, double a) {
            std::vector<HatchLoop> out = src;
            for (auto& loop : out)
                for (auto& edge : loop.Edges)
                    edge = RotateEdge(edge, piv, a);
            return out;
        }
    }

    RotateMoveCommand::RotateMoveCommand(const std::vector<Object::ObjectID>& ids,
                                         const Math::Point3& pivot,
                                         double angle,
                                         Scene& scene)
        : m_pivot(pivot), m_angle(angle)
    {
        m_entries.reserve(ids.size());

        for (auto id : ids)
        {
            auto* obj = scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>()) continue;
            auto* entity = static_cast<Entity*>(obj);

            MoveEntityEntry e;
            e.Id = id;

            if (entity->IsKindOf<PointEntity>())
            {
                e.Kind        = MoveEntityEntry::Kind::Point;
                e.BeforePoint = static_cast<PointEntity*>(entity)->GetPoint();
                e.AfterPoint  = Rotate(e.BeforePoint, pivot, angle);
            }
            else if (entity->IsKindOf<LineEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Line;
                e.BeforeLine = static_cast<LineEntity*>(entity)->GetLine();
                e.AfterLine  = Rotate(e.BeforeLine, pivot, angle);
            }
            else if (entity->IsKindOf<CircleEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Circle;
                e.BeforeCircle = static_cast<CircleEntity*>(entity)->GetCircle();
                e.AfterCircle  = Rotate(e.BeforeCircle, pivot, angle);
            }
            else if (entity->IsKindOf<RectangleEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Rectangle;
                e.BeforeRect = static_cast<RectangleEntity*>(entity)->GetRectangle();
                e.AfterRect  = Rotate(e.BeforeRect, pivot, angle);
            }
            else if (entity->IsKindOf<ArcEntity>())
            {
                e.Kind      = MoveEntityEntry::Kind::Arc;
                e.BeforeArc = static_cast<ArcEntity*>(entity)->GetArc();
                e.AfterArc  = Rotate(e.BeforeArc, pivot, angle);
            }
            else if (entity->IsKindOf<EllipseEntity>())
            {
                e.Kind          = MoveEntityEntry::Kind::Ellipse;
                e.BeforeEllipse = static_cast<EllipseEntity*>(entity)->GetEllipse();
                e.AfterEllipse  = Rotate(e.BeforeEllipse, pivot, angle);
            }
            else if (entity->IsKindOf<PolylineEntity>())
            {
                e.Kind           = MoveEntityEntry::Kind::Polyline;
                e.BeforePolyline = static_cast<PolylineEntity*>(entity)->GetPolyline();
                e.AfterPolyline  = Rotate(e.BeforePolyline, pivot, angle);
            }
            else if (entity->IsKindOf<SplineEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Spline;
                e.BeforeSpline = static_cast<SplineEntity*>(entity)->GetSpline();
                e.AfterSpline  = Rotate(e.BeforeSpline, pivot, angle);
            }
            else if (entity->IsKindOf<TextEntity>())
            {
                auto* te = static_cast<TextEntity*>(entity);
                e.Kind       = MoveEntityEntry::Kind::Text;
                e.BeforeText = { te->GetPosition(), te->GetRotation() };
                e.AfterText  = { RotatePoint(te->GetPosition(), pivot, angle),
                                 te->GetRotation() + angle };
            }
            else if (entity->IsKindOf<MTextEntity>())
            {
                auto* me = static_cast<MTextEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::MText;
                e.BeforeMText = { me->GetPosition(), me->GetRotation() };
                e.AfterMText  = { RotatePoint(me->GetPosition(), pivot, angle),
                                  me->GetRotation() + angle };
            }
            else if (entity->IsKindOf<InsertEntity>())
            {
                auto* ie = static_cast<InsertEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Insert;
                e.BeforeInsert = { ie->GetPosition(), ie->GetRotation() };
                e.AfterInsert  = { RotatePoint(ie->GetPosition(), pivot, angle),
                                   ie->GetRotation() + angle };
            }
            else if (entity->IsKindOf<DimensionEntity>())
            {
                auto* de = static_cast<DimensionEntity*>(entity);
                e.Kind      = MoveEntityEntry::Kind::Dimension;
                e.BeforeDim = { de->GetP1(), de->GetP2(), de->GetDimLinePoint(),
                                de->GetCenterPoint(), de->GetLinearAngle() };
                e.AfterDim  = { RotatePoint(de->GetP1(),          pivot, angle),
                                RotatePoint(de->GetP2(),          pivot, angle),
                                RotatePoint(de->GetDimLinePoint(), pivot, angle),
                                RotatePoint(de->GetCenterPoint(),  pivot, angle),
                                de->GetLinearAngle() + angle };
            }
            else if (entity->IsKindOf<HatchEntity>())
            {
                auto* he = static_cast<HatchEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::Hatch;
                e.BeforeHatch = he->GetLoops();
                e.AfterHatch  = RotateLoops(he->GetLoops(), pivot, angle);
            }
            else if (entity->IsKindOf<LeaderEntity>())
            {
                auto* le = static_cast<LeaderEntity*>(entity);
                e.Kind              = MoveEntityEntry::Kind::Leader;
                e.BeforeLeaderVerts = le->GetVertices();
                e.AfterLeaderVerts.clear();
                for (const auto& v : le->GetVertices())
                    e.AfterLeaderVerts.push_back(RotatePoint(v, pivot, angle));
            }
            else if (entity->IsKindOf<MLeaderEntity>())
            {
                auto* ml = static_cast<MLeaderEntity*>(entity);
                e.Kind = MoveEntityEntry::Kind::MLeader;
                e.BeforeMLeader.landing = ml->GetLanding();
                for (const auto& ln : ml->GetLeaderLines())
                    e.BeforeMLeader.linePoints.push_back(ln.points);
                e.AfterMLeader.landing = RotatePoint(ml->GetLanding(), pivot, angle);
                for (const auto& pts : e.BeforeMLeader.linePoints)
                {
                    std::vector<Math::Point3> t;
                    t.reserve(pts.size());
                    for (const auto& p : pts) t.push_back(RotatePoint(p, pivot, angle));
                    e.AfterMLeader.linePoints.push_back(std::move(t));
                }
            }
            else if (entity->IsKindOf<RayEntity>())
            {
                auto* re = static_cast<RayEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Ray;
                e.BeforePosVec = { re->GetOrigin(), re->GetDirection() };
                e.AfterPosVec  = { RotatePoint(re->GetOrigin(), pivot, angle),
                                   RotVec2D(re->GetDirection(), angle) };
            }
            else if (entity->IsKindOf<XLineEntity>())
            {
                auto* xl = static_cast<XLineEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::XLine;
                e.BeforePosVec = { xl->GetOrigin(), xl->GetDirection() };
                e.AfterPosVec  = { RotatePoint(xl->GetOrigin(), pivot, angle),
                                   RotVec2D(xl->GetDirection(), angle) };
            }
            else if (entity->IsKindOf<ToleranceEntity>())
            {
                auto* te = static_cast<ToleranceEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Tolerance;
                e.BeforePosVec = { te->GetInsertion(), te->GetDirection() };
                e.AfterPosVec  = { RotatePoint(te->GetInsertion(), pivot, angle),
                                   RotVec2D(te->GetDirection(), angle) };
            }
            else
            {
                continue;
            }

            m_entries.push_back(std::move(e));
        }
    }

    static void ApplyEntry(Scene& scene, const MoveEntityEntry& e, bool useAfter)
    {
        auto* obj = scene.GetEntity(e.Id);
        if (!obj) return;
        auto* entity = static_cast<Entity*>(obj);

        switch (e.Kind)
        {
        case MoveEntityEntry::Kind::Point:
            static_cast<PointEntity*>(entity)->SetPoint(useAfter ? e.AfterPoint : e.BeforePoint);
            break;
        case MoveEntityEntry::Kind::Line:
            static_cast<LineEntity*>(entity)->SetLine(useAfter ? e.AfterLine : e.BeforeLine);
            break;
        case MoveEntityEntry::Kind::Circle:
            static_cast<CircleEntity*>(entity)->SetCircle(useAfter ? e.AfterCircle : e.BeforeCircle);
            break;
        case MoveEntityEntry::Kind::Rectangle:
            static_cast<RectangleEntity*>(entity)->SetRectangle(useAfter ? e.AfterRect : e.BeforeRect);
            break;
        case MoveEntityEntry::Kind::Arc:
            static_cast<ArcEntity*>(entity)->SetArc(useAfter ? e.AfterArc : e.BeforeArc);
            break;
        case MoveEntityEntry::Kind::Ellipse:
            static_cast<EllipseEntity*>(entity)->SetEllipse(useAfter ? e.AfterEllipse : e.BeforeEllipse);
            break;
        case MoveEntityEntry::Kind::Polyline:
            static_cast<PolylineEntity*>(entity)->SetPolyline(useAfter ? e.AfterPolyline : e.BeforePolyline);
            break;
        case MoveEntityEntry::Kind::Spline:
            static_cast<SplineEntity*>(entity)->GetSpline() = useAfter ? e.AfterSpline : e.BeforeSpline;
            break;
        case MoveEntityEntry::Kind::Text:
        {
            const auto& snap = useAfter ? e.AfterText : e.BeforeText;
            auto* te = static_cast<TextEntity*>(entity);
            te->SetPosition(snap.pos);
            te->SetRotation(static_cast<float>(snap.rotation));
            break;
        }
        case MoveEntityEntry::Kind::MText:
        {
            const auto& snap = useAfter ? e.AfterMText : e.BeforeMText;
            auto* me = static_cast<MTextEntity*>(entity);
            me->SetPosition(snap.pos);
            me->SetRotation(snap.rotation);
            break;
        }
        case MoveEntityEntry::Kind::Insert:
        {
            const auto& snap = useAfter ? e.AfterInsert : e.BeforeInsert;
            auto* ie = static_cast<InsertEntity*>(entity);
            ie->SetPosition(snap.pos);
            ie->SetRotation(snap.rotation);
            break;
        }
        case MoveEntityEntry::Kind::Dimension:
        {
            const auto& snap = useAfter ? e.AfterDim : e.BeforeDim;
            auto* de = static_cast<DimensionEntity*>(entity);
            de->SetP1(snap.p1);
            de->SetP2(snap.p2);
            de->SetDimLinePoint(snap.dimPt);
            de->SetCenterPoint(snap.center);
            de->SetLinearAngle(snap.linearAngle);
            break;
        }
        case MoveEntityEntry::Kind::Hatch:
            static_cast<HatchEntity*>(entity)->GetLoops() = useAfter ? e.AfterHatch : e.BeforeHatch;
            break;
        case MoveEntityEntry::Kind::Leader:
            static_cast<LeaderEntity*>(entity)->SetVertices(useAfter ? e.AfterLeaderVerts : e.BeforeLeaderVerts);
            break;
        case MoveEntityEntry::Kind::MLeader:
        {
            const auto& snap = useAfter ? e.AfterMLeader : e.BeforeMLeader;
            auto* ml = static_cast<MLeaderEntity*>(entity);
            ml->SetLanding(snap.landing);
            auto& lines = ml->GetLeaderLines();
            for (size_t i = 0; i < snap.linePoints.size() && i < lines.size(); ++i)
                lines[i].points = snap.linePoints[i];
            break;
        }
        case MoveEntityEntry::Kind::Ray:
        {
            const auto& snap = useAfter ? e.AfterPosVec : e.BeforePosVec;
            auto* re = static_cast<RayEntity*>(entity);
            re->SetOrigin(snap.pos);
            re->SetDirection(snap.dir);
            break;
        }
        case MoveEntityEntry::Kind::XLine:
        {
            const auto& snap = useAfter ? e.AfterPosVec : e.BeforePosVec;
            auto* xl = static_cast<XLineEntity*>(entity);
            xl->SetOrigin(snap.pos);
            xl->SetDirection(snap.dir);
            break;
        }
        case MoveEntityEntry::Kind::Tolerance:
        {
            const auto& snap = useAfter ? e.AfterPosVec : e.BeforePosVec;
            auto* te = static_cast<ToleranceEntity*>(entity);
            te->SetInsertion(snap.pos);
            te->SetDirection(snap.dir);
            break;
        }
        }
    }

    bool RotateMoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, true);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);   // 仅受影响实体重新细分
        return !m_entries.empty();
    }

    void RotateMoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, false);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);
    }
}
