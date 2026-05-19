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
#include "Core/Math/Constants.hpp"

#include <cmath>

namespace MiniCAD
{
    // 在 struct 层面"旋转一份几何"返回新值,用于 After 快照
    namespace 
    {
        auto Norm = [](double x) {
            x = std::fmod(x, Math::TwoPI);
            if (x < 0.0) x += Math::TwoPI;
            return x;
        };

        Point Rotate(const Point& src, const Math::Point3& piv, double a) {
            Point o = src;  o.Position = RotatePoint(src.Position, piv, a);  return o;
        }
        Line Rotate(const Line& src, const Math::Point3& piv, double a) 
        {
            Line o = src;
            o.Start = RotatePoint(src.Start, piv, a);
            o.End = RotatePoint(src.End, piv, a);
            return o;
        }
        Circle Rotate(const Circle& src, const Math::Point3& piv, double a) {
            Circle o = src;  o.Center = RotatePoint(src.Center, piv, a);  return o;
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
            o.Center = RotatePoint(src.Center, piv, a);
            o.StartAngle = Norm(src.StartAngle + a);
            o.EndAngle = Norm(src.EndAngle + a);
            return o;
        }
        Ellipse Rotate(const Ellipse& src, const Math::Point3& piv, double a) {
            Ellipse o = src;
            o.Center = RotatePoint(src.Center, piv, a);
            o.Rotation = src.Rotation + a;
            return o;
        }
        Polyline Rotate(const Polyline& src, const Math::Point3& piv, double a) {
            Polyline o = src;
            for (auto& p : o.Points) p = RotatePoint(p, piv, a);
            // bulges 不变
            return o;
        }
        Spline Rotate(const Spline& src, const Math::Point3& piv, double a) {
            Spline o = src;
            for (auto& p : o.FitPoints) p = RotatePoint(p, piv, a);
            o.Build();
            return o;
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
                e.Kind = MoveEntityEntry::Kind::Point;
                e.BeforePoint = static_cast<PointEntity*>(entity)->GetPoint();
                e.AfterPoint = Rotate(e.BeforePoint, pivot, angle);
            }
            else if (entity->IsKindOf<LineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Line;
                e.BeforeLine = static_cast<LineEntity*>(entity)->GetLine();
                e.AfterLine = Rotate(e.BeforeLine, pivot, angle);
            }
            else if (entity->IsKindOf<CircleEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Circle;
                e.BeforeCircle = static_cast<CircleEntity*>(entity)->GetCircle();
                e.AfterCircle = Rotate(e.BeforeCircle, pivot, angle);
            }
            else if (entity->IsKindOf<RectangleEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Rectangle;
                e.BeforeRect = static_cast<RectangleEntity*>(entity)->GetRectangle();
                e.AfterRect = Rotate(e.BeforeRect, pivot, angle);
            }
            else if (entity->IsKindOf<ArcEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Arc;
                e.BeforeArc = static_cast<ArcEntity*>(entity)->GetArc();
                e.AfterArc = Rotate(e.BeforeArc, pivot, angle);
            }
            else if (entity->IsKindOf<EllipseEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Ellipse;
                e.BeforeEllipse = static_cast<EllipseEntity*>(entity)->GetEllipse();
                e.AfterEllipse = Rotate(e.BeforeEllipse, pivot, angle);
            }
            else if (entity->IsKindOf<PolylineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Polyline;
                e.BeforePolyline = static_cast<PolylineEntity*>(entity)->GetPolyline();
                e.AfterPolyline = Rotate(e.BeforePolyline, pivot, angle);
            }
            else if (entity->IsKindOf<SplineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Spline;
                e.BeforeSpline = static_cast<SplineEntity*>(entity)->GetSpline();
                e.AfterSpline = Rotate(e.BeforeSpline, pivot, angle);
            }
            else
            {
                continue;
            }

            m_entries.push_back(std::move(e));
        }
    }

    // 和 MoveCommand/MirrorMoveCommand 的 ApplyEntry 完全一致 ——
    // 三份重复了!下面留个 TODO,等下一个工具再统一抽到 MoveEntityEntry.cpp
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

    bool RotateMoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, true);
        scene.MarkDirty();
        return !m_entries.empty();
    }

    void RotateMoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, false);
        scene.MarkDirty();
    }
}
