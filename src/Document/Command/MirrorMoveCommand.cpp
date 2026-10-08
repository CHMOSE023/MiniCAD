#include "MirrorMoveCommand.h"
#include "Scene/Scene.h"

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
    namespace {
        static double AxisTheta(const MirrorAxis& axis) {
            return std::atan2(axis.P1.y - axis.P0.y, axis.P1.x - axis.P0.x);
        }

        Point Mirror(const Point& src, const MirrorAxis& axis) {
            Point o = src; o.Position = ReflectPoint(src.Position, axis); return o;
        }
        Line Mirror(const Line& src, const MirrorAxis& axis) {
            Line o = src;
            o.Start = ReflectPoint(src.Start, axis);
            o.End   = ReflectPoint(src.End,   axis);
            return o;
        }
        Circle Mirror(const Circle& src, const MirrorAxis& axis) {
            Circle o = src; o.Center = ReflectPoint(src.Center, axis); return o;
        }
        Rectangle Mirror(const Rectangle& src, const MirrorAxis& axis) {
            Rectangle o = src;
            o.P1 = ReflectPoint(src.P1, axis);
            o.P2 = ReflectPoint(src.P2, axis);
            o.P3 = ReflectPoint(src.P3, axis);
            o.P4 = ReflectPoint(src.P4, axis);
            return o;
        }
        Arc Mirror(const Arc& src, const MirrorAxis& axis) {
            Arc o = src;
            double theta = AxisTheta(axis);
            o.Center = ReflectPoint(src.Center, axis);
            double newEnd   = 2.0 * theta - src.StartAngle;
            double newStart = 2.0 * theta - src.EndAngle;
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
            };
            o.StartAngle = Norm(newStart);
            o.EndAngle   = Norm(newEnd);
            return o;
        }
        Ellipse Mirror(const Ellipse& src, const MirrorAxis& axis) {
            Ellipse o = src;
            double theta = AxisTheta(axis);
            o.Center   = ReflectPoint(src.Center, axis);
            o.Rotation = 2.0 * theta - src.Rotation;
            // 反射后参数方向反转：P(t) 的像为新椭圆的 P'(-t)，弧 [s, e] → [-e, -s]
            if (!o.IsFull())
                o.SetParams(-src.EndParam, -src.StartParam);
            return o;
        }
        Polyline Mirror(const Polyline& src, const MirrorAxis& axis) {
            Polyline o = src;
            for (auto& p : o.Points) p = ReflectPoint(p, axis);
            for (auto& b : o.Bulges) b = -b;
            return o;
        }
        Spline Mirror(const Spline& src, const MirrorAxis& axis) {
            Spline o = src;
            for (auto& p : o.FitPoints) p = ReflectPoint(p, axis);
            o.Build();
            return o;
        }

        HatchEdge MirrorEdge(const HatchEdge& src, const MirrorAxis& axis) {
            HatchEdge o = src;
            double theta = AxisTheta(axis);
            switch (src.Type) {
            case HatchEdge::Kind::Poly:
                for (auto& p : o.Poly.Points) p = ReflectPoint(p, axis);
                for (auto& b : o.Poly.Bulges) b = -b;
                break;
            case HatchEdge::Kind::EllipseArc:
                o.Ell.Center   = ReflectPoint(o.Ell.Center, axis);
                o.Ell.Rotation = 2.0 * theta - o.Ell.Rotation;
                break;
            case HatchEdge::Kind::Spline:
                for (auto& p : o.Spl.FitPoints) p = ReflectPoint(p, axis);
                o.Spl.Build();
                break;
            }
            return o;
        }
        std::vector<HatchLoop> MirrorLoops(const std::vector<HatchLoop>& src, const MirrorAxis& axis) {
            std::vector<HatchLoop> out = src;
            for (auto& loop : out)
                for (auto& edge : loop.Edges)
                    edge = MirrorEdge(edge, axis);
            return out;
        }
    }

    MirrorMoveCommand::MirrorMoveCommand(const std::vector<Object::ObjectID>& ids,
                                         const MirrorAxis& axis,
                                         Scene& scene)
        : m_axis(axis)
    {
        m_entries.reserve(ids.size());
        double theta = AxisTheta(axis);

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
                e.AfterPoint  = Mirror(e.BeforePoint, axis);
            }
            else if (entity->IsKindOf<LineEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Line;
                e.BeforeLine = static_cast<LineEntity*>(entity)->GetLine();
                e.AfterLine  = Mirror(e.BeforeLine, axis);
            }
            else if (entity->IsKindOf<CircleEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Circle;
                e.BeforeCircle = static_cast<CircleEntity*>(entity)->GetCircle();
                e.AfterCircle  = Mirror(e.BeforeCircle, axis);
            }
            else if (entity->IsKindOf<RectangleEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Rectangle;
                e.BeforeRect = static_cast<RectangleEntity*>(entity)->GetRectangle();
                e.AfterRect  = Mirror(e.BeforeRect, axis);
            }
            else if (entity->IsKindOf<ArcEntity>())
            {
                e.Kind      = MoveEntityEntry::Kind::Arc;
                e.BeforeArc = static_cast<ArcEntity*>(entity)->GetArc();
                e.AfterArc  = Mirror(e.BeforeArc, axis);
            }
            else if (entity->IsKindOf<EllipseEntity>())
            {
                e.Kind          = MoveEntityEntry::Kind::Ellipse;
                e.BeforeEllipse = static_cast<EllipseEntity*>(entity)->GetEllipse();
                e.AfterEllipse  = Mirror(e.BeforeEllipse, axis);
            }
            else if (entity->IsKindOf<PolylineEntity>())
            {
                e.Kind           = MoveEntityEntry::Kind::Polyline;
                e.BeforePolyline = static_cast<PolylineEntity*>(entity)->GetPolyline();
                e.AfterPolyline  = Mirror(e.BeforePolyline, axis);
            }
            else if (entity->IsKindOf<SplineEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Spline;
                e.BeforeSpline = static_cast<SplineEntity*>(entity)->GetSpline();
                e.AfterSpline  = Mirror(e.BeforeSpline, axis);
            }
            else if (entity->IsKindOf<TextEntity>())
            {
                auto* te = static_cast<TextEntity*>(entity);
                e.Kind       = MoveEntityEntry::Kind::Text;
                e.BeforeText = { te->GetPosition(), te->GetRotation() };
                e.AfterText  = { ReflectPoint(te->GetPosition(), axis),
                                 2.0 * theta - te->GetRotation() };
            }
            else if (entity->IsKindOf<MTextEntity>())
            {
                auto* me = static_cast<MTextEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::MText;
                e.BeforeMText = { me->GetPosition(), me->GetRotation() };
                e.AfterMText  = { ReflectPoint(me->GetPosition(), axis),
                                  2.0 * theta - me->GetRotation() };
            }
            else if (entity->IsKindOf<InsertEntity>())
            {
                // 镜像=反射(行列式为负),pos+rot 无法表达,须翻转一个缩放轴。
                // 分解:Rz(2θ-rot)·Scale(sx,-sy,sz),与 MirrorEntityInPlace 一致。
                auto* ie = static_cast<InsertEntity*>(entity);
                Math::Vec3 sc = ie->GetScale();
                Math::Vec3 scMirrored = sc; scMirrored.y = -scMirrored.y;
                e.Kind         = MoveEntityEntry::Kind::Insert;
                e.BeforeInsert = { ie->GetPosition(), ie->GetRotation(), sc };
                e.AfterInsert  = { ReflectPoint(ie->GetPosition(), axis),
                                   2.0 * theta - ie->GetRotation(),
                                   scMirrored };
            }
            else if (entity->IsKindOf<DimensionEntity>())
            {
                auto* de = static_cast<DimensionEntity*>(entity);
                e.Kind      = MoveEntityEntry::Kind::Dimension;
                e.BeforeDim = { de->GetP1(), de->GetP2(), de->GetDimLinePoint(),
                                de->GetCenterPoint(), de->GetLinearAngle() };
                e.AfterDim  = { ReflectPoint(de->GetP1(),          axis),
                                ReflectPoint(de->GetP2(),          axis),
                                ReflectPoint(de->GetDimLinePoint(), axis),
                                ReflectPoint(de->GetCenterPoint(),  axis),
                                2.0 * theta - de->GetLinearAngle() };
            }
            else if (entity->IsKindOf<HatchEntity>())
            {
                auto* he = static_cast<HatchEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::Hatch;
                e.BeforeHatch = he->GetLoops();
                e.AfterHatch  = MirrorLoops(he->GetLoops(), axis);
            }
            else if (entity->IsKindOf<LeaderEntity>())
            {
                auto* le = static_cast<LeaderEntity*>(entity);
                e.Kind              = MoveEntityEntry::Kind::Leader;
                e.BeforeLeaderVerts = le->GetVertices();
                e.AfterLeaderVerts.clear();
                for (const auto& v : le->GetVertices())
                    e.AfterLeaderVerts.push_back(ReflectPoint(v, axis));
            }
            else if (entity->IsKindOf<MLeaderEntity>())
            {
                auto* ml = static_cast<MLeaderEntity*>(entity);
                e.Kind = MoveEntityEntry::Kind::MLeader;
                e.BeforeMLeader.landing = ml->GetLanding();
                for (const auto& ln : ml->GetLeaderLines())
                    e.BeforeMLeader.linePoints.push_back(ln.points);
                e.AfterMLeader.landing = ReflectPoint(ml->GetLanding(), axis);
                for (const auto& pts : e.BeforeMLeader.linePoints)
                {
                    std::vector<Math::Point3> t;
                    t.reserve(pts.size());
                    for (const auto& p : pts) t.push_back(ReflectPoint(p, axis));
                    e.AfterMLeader.linePoints.push_back(std::move(t));
                }
            }
            else if (entity->IsKindOf<RayEntity>())
            {
                auto* re = static_cast<RayEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Ray;
                e.BeforePosVec = { re->GetOrigin(), re->GetDirection() };
                e.AfterPosVec  = { ReflectPoint (re->GetOrigin(),    axis),
                                   ReflectVector(re->GetDirection(), axis) };
            }
            else if (entity->IsKindOf<XLineEntity>())
            {
                auto* xl = static_cast<XLineEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::XLine;
                e.BeforePosVec = { xl->GetOrigin(), xl->GetDirection() };
                e.AfterPosVec  = { ReflectPoint (xl->GetOrigin(),    axis),
                                   ReflectVector(xl->GetDirection(), axis) };
            }
            else if (entity->IsKindOf<ToleranceEntity>())
            {
                auto* te = static_cast<ToleranceEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Tolerance;
                e.BeforePosVec = { te->GetInsertion(), te->GetDirection() };
                e.AfterPosVec  = { ReflectPoint (te->GetInsertion(), axis),
                                   ReflectVector(te->GetDirection(), axis) };
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
            ie->SetScale(snap.scale);
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

    bool MirrorMoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, true);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);   // 仅受影响实体重新细分
        return !m_entries.empty();
    }

    void MirrorMoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, false);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);
    }
}
