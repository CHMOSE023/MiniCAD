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
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    // ── 小工具:几何整体平移 ────────────────────────────────────
    static Point     Translate(const Point& p,     const Math::Vec3& d) { return Point    { p.Position + d }; }
    static Line      Translate(const Line& l,      const Math::Vec3& d) { return Line     { l.Start + d, l.End + d }; }
    static Circle    Translate(const Circle& c,    const Math::Vec3& d) { return Circle   { c.Center + d, c.Radius }; }
    static Rectangle Translate(const Rectangle& r, const Math::Vec3& d) { return Rectangle{ r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d }; }
    static Arc       Translate(const Arc& a,       const Math::Vec3& d) { return Arc      { a.Center + d, a.Radius, a.StartAngle, a.EndAngle }; }
    static Ellipse   Translate(const Ellipse& e,   const Math::Vec3& d) { return Ellipse  { e.Center + d, e.RadiusX, e.RadiusY, e.Rotation }; }

    static Polyline Translate(const Polyline& src, const Math::Vec3& d)
    {
        Polyline out = src;                       // 拷贝 Points + Bulges
        for (auto& p : out.Points) p += d;        // 只平移顶点;bulge 是相对量,不变
        return out;
    }

    static Spline Translate(const Spline& src, const Math::Vec3& d)
    {
        Spline out = src;
        for (auto& p : out.FitPoints) p += d;
        out.Build();                              // 拟合点变了,重建内部数据
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
            else
            {
                continue;   // 不识别的 Entity 类型,跳过
            }

            m_entries.push_back(std::move(e));
        }
    }

    // ────────────────────────────────────────────────────────────
    //  应用快照
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
        }
    }

    bool MoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, /*useAfter=*/true);
        scene.MarkDirty();
        return !m_entries.empty();
    }

    void MoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, /*useAfter=*/false);
        scene.MarkDirty();
    }
}
