#include "MoveCommand.h"
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
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    // ── 几何整体平移 ──────────────────────────────────────────────
    static Point     Translate(const Point& p,     const Math::Vec3& d) { return Point    { p.Position + d }; }
    static Line      Translate(const Line& l,      const Math::Vec3& d) { return Line     { l.Start + d, l.End + d }; }
    static Circle    Translate(const Circle& c,    const Math::Vec3& d) { return Circle   { c.Center + d, c.Radius }; }
    static Rectangle Translate(const Rectangle& r, const Math::Vec3& d) { return Rectangle{ r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d }; }
    static Arc       Translate(const Arc& a,       const Math::Vec3& d) { return Arc      { a.Center + d, a.Radius, a.StartAngle, a.EndAngle }; }
    static Ellipse   Translate(const Ellipse& e,   const Math::Vec3& d) { Ellipse o = e; o.Center = e.Center + d; return o; }

    static Polyline Translate(const Polyline& src, const Math::Vec3& d)
    {
        Polyline out = src;
        for (auto& p : out.Points) p += d;
        return out;
    }

    static Spline Translate(const Spline& src, const Math::Vec3& d)
    {
        Spline out = src;
        for (auto& p : out.FitPoints) p += d;
        out.Build();
        return out;
    }

    static HatchEdge TranslateHatchEdge(const HatchEdge& src, const Math::Vec3& d)
    {
        HatchEdge out = src;
        switch (src.Type)
        {
        case HatchEdge::Kind::Poly:
            for (auto& p : out.Poly.Points) p += d;
            break;
        case HatchEdge::Kind::EllipseArc:
            out.Ell.Center = out.Ell.Center + d;
            break;
        case HatchEdge::Kind::Spline:
            for (auto& p : out.Spl.FitPoints) p += d;
            out.Spl.Build();
            break;
        }
        return out;
    }

    static std::vector<HatchLoop> TranslateHatchLoops(const std::vector<HatchLoop>& src, const Math::Vec3& d)
    {
        std::vector<HatchLoop> out = src;
        for (auto& loop : out)
            for (auto& edge : loop.Edges)
                edge = TranslateHatchEdge(edge, d);
        return out;
    }

    // ────────────────────────────────────────────────────────────
    //  构造:对每个目标拍 Before 快照,算出 After 快照
    // ────────────────────────────────────────────────────────────
    MoveCommand::MoveCommand(const std::vector<Object::ObjectID>& ids,
                             const Math::Vec3& delta,
                             Scene& scene)
        : m_delta(delta)
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
                e.AfterPoint  = Translate(e.BeforePoint, delta);
            }
            else if (entity->IsKindOf<LineEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Line;
                e.BeforeLine = static_cast<LineEntity*>(entity)->GetLine();
                e.AfterLine  = Translate(e.BeforeLine, delta);
            }
            else if (entity->IsKindOf<CircleEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Circle;
                e.BeforeCircle = static_cast<CircleEntity*>(entity)->GetCircle();
                e.AfterCircle  = Translate(e.BeforeCircle, delta);
            }
            else if (entity->IsKindOf<RectangleEntity>())
            {
                e.Kind       = MoveEntityEntry::Kind::Rectangle;
                e.BeforeRect = static_cast<RectangleEntity*>(entity)->GetRectangle();
                e.AfterRect  = Translate(e.BeforeRect, delta);
            }
            else if (entity->IsKindOf<ArcEntity>())
            {
                e.Kind      = MoveEntityEntry::Kind::Arc;
                e.BeforeArc = static_cast<ArcEntity*>(entity)->GetArc();
                e.AfterArc  = Translate(e.BeforeArc, delta);
            }
            else if (entity->IsKindOf<EllipseEntity>())
            {
                e.Kind          = MoveEntityEntry::Kind::Ellipse;
                e.BeforeEllipse = static_cast<EllipseEntity*>(entity)->GetEllipse();
                e.AfterEllipse  = Translate(e.BeforeEllipse, delta);
            }
            else if (entity->IsKindOf<PolylineEntity>())
            {
                e.Kind           = MoveEntityEntry::Kind::Polyline;
                e.BeforePolyline = static_cast<PolylineEntity*>(entity)->GetPolyline();
                e.AfterPolyline  = Translate(e.BeforePolyline, delta);
            }
            else if (entity->IsKindOf<SplineEntity>())
            {
                e.Kind         = MoveEntityEntry::Kind::Spline;
                e.BeforeSpline = static_cast<SplineEntity*>(entity)->GetSpline();
                e.AfterSpline  = Translate(e.BeforeSpline, delta);
            }
            else if (entity->IsKindOf<TextEntity>())
            {
                auto* te = static_cast<TextEntity*>(entity);
                e.Kind       = MoveEntityEntry::Kind::Text;
                e.BeforeText = { te->GetPosition(), te->GetRotation() };
                e.AfterText  = { te->GetPosition() + delta, te->GetRotation() };
            }
            else if (entity->IsKindOf<MTextEntity>())
            {
                auto* me = static_cast<MTextEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::MText;
                e.BeforeMText = { me->GetPosition(), me->GetRotation() };
                e.AfterMText  = { me->GetPosition() + delta, me->GetRotation() };
            }
            else if (entity->IsKindOf<InsertEntity>())
            {
                auto* ie = static_cast<InsertEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Insert;
                e.BeforeInsert = { ie->GetPosition(), ie->GetRotation() };
                e.AfterInsert  = { ie->GetPosition() + delta, ie->GetRotation() };
            }
            else if (entity->IsKindOf<DimensionEntity>())
            {
                auto* de = static_cast<DimensionEntity*>(entity);
                e.Kind      = MoveEntityEntry::Kind::Dimension;
                e.BeforeDim = { de->GetP1(), de->GetP2(), de->GetDimLinePoint(), de->GetCenterPoint(), de->GetLinearAngle() };
                e.AfterDim  = { de->GetP1() + delta, de->GetP2() + delta,
                                de->GetDimLinePoint() + delta, de->GetCenterPoint() + delta,
                                de->GetLinearAngle() };
            }
            else if (entity->IsKindOf<HatchEntity>())
            {
                auto* he = static_cast<HatchEntity*>(entity);
                e.Kind        = MoveEntityEntry::Kind::Hatch;
                e.BeforeHatch = he->GetLoops();
                e.AfterHatch  = TranslateHatchLoops(he->GetLoops(), delta);
            }
            else if (entity->IsKindOf<LeaderEntity>())
            {
                auto* le = static_cast<LeaderEntity*>(entity);
                e.Kind              = MoveEntityEntry::Kind::Leader;
                e.BeforeLeaderVerts = le->GetVertices();
                e.AfterLeaderVerts.clear();
                for (const auto& v : le->GetVertices())
                    e.AfterLeaderVerts.push_back(v + delta);
            }
            else if (entity->IsKindOf<MLeaderEntity>())
            {
                auto* ml = static_cast<MLeaderEntity*>(entity);
                e.Kind = MoveEntityEntry::Kind::MLeader;
                e.BeforeMLeader.landing = ml->GetLanding();
                for (const auto& ln : ml->GetLeaderLines())
                    e.BeforeMLeader.linePoints.push_back(ln.points);
                e.AfterMLeader.landing = ml->GetLanding() + delta;
                for (const auto& pts : e.BeforeMLeader.linePoints)
                {
                    std::vector<Math::Point3> t;
                    t.reserve(pts.size());
                    for (const auto& p : pts) t.push_back(p + delta);
                    e.AfterMLeader.linePoints.push_back(std::move(t));
                }
            }
            else if (entity->IsKindOf<RayEntity>())
            {
                auto* re = static_cast<RayEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Ray;
                e.BeforePosVec = { re->GetOrigin(), re->GetDirection() };
                e.AfterPosVec  = { re->GetOrigin() + delta, re->GetDirection() };
            }
            else if (entity->IsKindOf<XLineEntity>())
            {
                auto* xl = static_cast<XLineEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::XLine;
                e.BeforePosVec = { xl->GetOrigin(), xl->GetDirection() };
                e.AfterPosVec  = { xl->GetOrigin() + delta, xl->GetDirection() };
            }
            else if (entity->IsKindOf<ToleranceEntity>())
            {
                auto* te = static_cast<ToleranceEntity*>(entity);
                e.Kind         = MoveEntityEntry::Kind::Tolerance;
                e.BeforePosVec = { te->GetInsertion(), te->GetDirection() };
                e.AfterPosVec  = { te->GetInsertion() + delta, te->GetDirection() };
            }
            else
            {
                continue;   // 不识别的 Entity 类型,跳过
            }

            m_entries.push_back(std::move(e));
        }
    }

    // ────────────────────────────────────────────────────────────
    //  应用快照（Execute/Undo 共用）
    // ────────────────────────────────────────────────────────────
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

    bool MoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, /*useAfter=*/true);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);   // 仅受影响实体重新细分
        return !m_entries.empty();
    }

    void MoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, /*useAfter=*/false);
        for (const auto& e : m_entries) scene.MarkEntityDirty(e.Id);
    }
}
