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

namespace MiniCAD
{
    // 思路:复用 MoveEntityEntry 的 Before/After 字段,
    // After = 把 Before 镜像后的结果.写入和读出与 MoveCommand 一致.

    namespace {
        // 工具:对一份几何做镜像后返回新值
        // (这里直接克隆 + MirrorEntityInPlace 再取出会更通用,
        //  但 Entry 直接存几何 struct,我们就在 struct 层面手写.)
        Point  Mirror(const Point& src, const MirrorAxis& axis) 
        { 
            Point  o = src;  o.Position = ReflectPoint(src.Position, axis);  return o;
        }
        Line   Mirror(const Line& src, const MirrorAxis& axis)
        {
            Line   o = src;
            o.Start = ReflectPoint(src.Start, axis);
            o.End = ReflectPoint(src.End, axis);
            return o;
        }
        Circle Mirror(const Circle& src, const MirrorAxis& axis) {
            Circle o = src;  o.Center = ReflectPoint(src.Center, axis);     return o;
        }
        Rectangle Mirror(const Rectangle& src, const MirrorAxis& axis) {
            Rectangle o = src;
            o.P1 = ReflectPoint(src.P1, axis);
            o.P2 = ReflectPoint(src.P2, axis);
            o.P3 = ReflectPoint(src.P3, axis);
            o.P4 = ReflectPoint(src.P4, axis);
            return o;
        }
        Arc    Mirror(const Arc& src, const MirrorAxis& axis) {
            Arc o = src;
            double theta = std::atan2(axis.P1.y - axis.P0.y, axis.P1.x - axis.P0.x);
            o.Center = ReflectPoint(src.Center, axis);
            double newEnd = 2.0 * theta - src.StartAngle;
            double newStart = 2.0 * theta - src.EndAngle;
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
                };
            o.StartAngle = Norm(newStart);
            o.EndAngle = Norm(newEnd);
            return o;
        }
        Ellipse Mirror(const Ellipse& src, const MirrorAxis& axis) {
            Ellipse o = src;
            double theta = std::atan2(axis.P1.y - axis.P0.y, axis.P1.x - axis.P0.x);
            o.Center = ReflectPoint(src.Center, axis);
            o.Rotation = 2.0 * theta - src.Rotation;
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
    }

    MirrorMoveCommand::MirrorMoveCommand(const std::vector<Object::ObjectID>& ids,
        const MirrorAxis& axis,
        Scene& scene)
        : m_axis(axis)
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
                e.AfterPoint = Mirror(e.BeforePoint, axis);
            }
            else if (entity->IsKindOf<LineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Line;
                e.BeforeLine = static_cast<LineEntity*>(entity)->GetLine();
                e.AfterLine = Mirror(e.BeforeLine, axis);
            }
            else if (entity->IsKindOf<CircleEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Circle;
                e.BeforeCircle = static_cast<CircleEntity*>(entity)->GetCircle();
                e.AfterCircle = Mirror(e.BeforeCircle, axis);
            }
            else if (entity->IsKindOf<RectangleEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Rectangle;
                e.BeforeRect = static_cast<RectangleEntity*>(entity)->GetRectangle();
                e.AfterRect = Mirror(e.BeforeRect, axis);
            }
            else if (entity->IsKindOf<ArcEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Arc;
                e.BeforeArc = static_cast<ArcEntity*>(entity)->GetArc();
                e.AfterArc = Mirror(e.BeforeArc, axis);
            }
            else if (entity->IsKindOf<EllipseEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Ellipse;
                e.BeforeEllipse = static_cast<EllipseEntity*>(entity)->GetEllipse();
                e.AfterEllipse = Mirror(e.BeforeEllipse, axis);
            }
            else if (entity->IsKindOf<PolylineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Polyline;
                e.BeforePolyline = static_cast<PolylineEntity*>(entity)->GetPolyline();
                e.AfterPolyline = Mirror(e.BeforePolyline, axis);
            }
            else if (entity->IsKindOf<SplineEntity>())
            {
                e.Kind = MoveEntityEntry::Kind::Spline;
                e.BeforeSpline = static_cast<SplineEntity*>(entity)->GetSpline();
                e.AfterSpline = Mirror(e.BeforeSpline, axis);
            }
            else
            {
                continue;
            }

            m_entries.push_back(std::move(e));
        }
    }

    // Apply 的实现和 MoveCommand 完全一样,可以抽到 MoveEntityEntry.cpp 共用一份.
    // 这里为独立性给一份本地版本:
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

    bool MirrorMoveCommand::Execute(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, true);
        scene.MarkDirty();
        return !m_entries.empty();
    }

    void MirrorMoveCommand::Undo(Scene& scene)
    {
        for (const auto& e : m_entries) ApplyEntry(scene, e, false);
        scene.MarkDirty();
    }
}
