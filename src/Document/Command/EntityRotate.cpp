#include "EntityRotate.h"

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
    Math::Point3 RotatePoint(const Math::Point3& p,  const Math::Point3& pivot,  double angle)
    {
        double c = std::cos(angle);
        double s = std::sin(angle);
        double dx = p.x - pivot.x;
        double dy = p.y - pivot.y;
        return {
            pivot.x + dx * c - dy * s,
            pivot.y + dx * s + dy * c,
            p.z
        };
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
            l.End = RotatePoint(l.End, pivot, angle);
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
            // Rectangle 是四个独立顶点,旋转后仍然是任意四边形 —— 你的 Rectangle
            // struct 不假定 AABB 对齐,直接旋转四个顶点即可
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
            // Arc 绕向不变,只需要给 StartAngle/EndAngle 各加 angle
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            a.Center = RotatePoint(a.Center, pivot, angle);
            a.StartAngle = a.StartAngle + angle;
            a.EndAngle = a.EndAngle + angle;

            // 规范化到 [0, 2π) 避免数值漂移
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
                };
            a.StartAngle = Norm(a.StartAngle);
            a.EndAngle = Norm(a.EndAngle);
            ae.SetArc(a);
            return;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            el.Center = RotatePoint(el.Center, pivot, angle);
            el.Rotation = el.Rotation + angle;
            ee.SetEllipse(el);
            return;
        }

        if (entity.IsKindOf<PolylineEntity>())
        {
            // Bulges 不变(旋转保绕向)
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
    }
}
