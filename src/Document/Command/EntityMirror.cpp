#include "EntityMirror.h"

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
    Math::Point3 ReflectPoint(const Math::Point3& p, const MirrorAxis& axis)
    {
        Math::Vec3 dir{ axis.P1.x - axis.P0.x, axis.P1.y - axis.P0.y, 0.0 };
        double lenSq = dir.x * dir.x + dir.y * dir.y;
        if (lenSq < Math::LengthEPS * Math::LengthEPS) return p;

        double dx = p.x - axis.P0.x;
        double dy = p.y - axis.P0.y;
        double t  = (dx * dir.x + dy * dir.y) / lenSq;
        double px = axis.P0.x + t * dir.x;
        double py = axis.P0.y + t * dir.y;
        return { 2.0 * px - p.x, 2.0 * py - p.y, p.z };
    }

    Math::Vec3 ReflectVector(const Math::Vec3& v, const MirrorAxis& axis)
    {
        Math::Vec3 dir{ axis.P1.x - axis.P0.x, axis.P1.y - axis.P0.y, 0.0 };
        double lenSq = dir.x * dir.x + dir.y * dir.y;
        if (lenSq < Math::LengthEPS * Math::LengthEPS) return v;
        double dot = (v.x * dir.x + v.y * dir.y) / lenSq;
        return { 2.0 * dot * dir.x - v.x, 2.0 * dot * dir.y - v.y, v.z };
    }

    static double AxisAngle(const MirrorAxis& axis)
    {
        return std::atan2(axis.P1.y - axis.P0.y, axis.P1.x - axis.P0.x);
    }

    static double ReflectAngle(double a, double theta) { return 2.0 * theta - a; }

    void MirrorEntityInPlace(Entity& entity, const MirrorAxis& axis)
    {
        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            p.Position = ReflectPoint(p.Position, axis);
            pe.SetPoint(p);
            return;
        }

        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            l.Start = ReflectPoint(l.Start, axis);
            l.End   = ReflectPoint(l.End,   axis);
            le.SetLine(l);
            return;
        }

        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            c.Center = ReflectPoint(c.Center, axis);
            ce.SetCircle(c);
            return;
        }

        if (entity.IsKindOf<RectangleEntity>())
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            r.P1 = ReflectPoint(r.P1, axis);
            r.P2 = ReflectPoint(r.P2, axis);
            r.P3 = ReflectPoint(r.P3, axis);
            r.P4 = ReflectPoint(r.P4, axis);
            re.SetRectangle(r);
            return;
        }

        if (entity.IsKindOf<ArcEntity>())
        {
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            double theta = AxisAngle(axis);
            a.Center = ReflectPoint(a.Center, axis);
            double newEnd   = ReflectAngle(a.StartAngle, theta);
            double newStart = ReflectAngle(a.EndAngle,   theta);
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
            };
            a.StartAngle = Norm(newStart);
            a.EndAngle   = Norm(newEnd);
            ae.SetArc(a);
            return;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            double theta = AxisAngle(axis);
            el.Center   = ReflectPoint(el.Center, axis);
            el.Rotation = ReflectAngle(el.Rotation, theta);
            // 反射后参数方向反转：P(t) 的像为新椭圆的 P'(-t)，弧 [s, e] → [-e, -s]
            if (!el.IsFull())
                el.SetParams(-el.EndParam, -el.StartParam);
            ee.SetEllipse(el);
            return;
        }

        if (entity.IsKindOf<PolylineEntity>())
        {
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) p = ReflectPoint(p, axis);
            for (auto& b : pl.Bulges) b = -b;
            pe.SetPolyline(std::move(pl));
            return;
        }

        if (entity.IsKindOf<SplineEntity>())
        {
            auto& se = static_cast<SplineEntity&>(entity);
            auto& sp = se.GetSpline();
            for (auto& p : sp.FitPoints) p = ReflectPoint(p, axis);
            sp.Build();
            return;
        }

        if (entity.IsKindOf<TextEntity>())
        {
            auto& te = static_cast<TextEntity&>(entity);
            double theta = AxisAngle(axis);
            te.SetPosition(ReflectPoint(te.GetPosition(), axis));
            te.SetRotation(static_cast<float>(ReflectAngle(te.GetRotation(), theta)));
            return;
        }

        if (entity.IsKindOf<MTextEntity>())
        {
            auto& me = static_cast<MTextEntity&>(entity);
            double theta = AxisAngle(axis);
            me.SetPosition(ReflectPoint(me.GetPosition(), axis));
            me.SetRotation(ReflectAngle(me.GetRotation(), theta));
            return;
        }

        if (entity.IsKindOf<InsertEntity>())
        {
            // 块引用的渲染变换为 T(pos)·Rz(rot)·Scale(s)·T(-base),线性部分行列式为正,
            // 无法只靠 pos+rot 表示反射(行列式为负)。绕角 θ 的镜像分解为
            // Rz(2θ-rot)·Scale(sx,-sy,sz),故除反射插入点、反射角度外,还须翻转一个缩放轴。
            auto& ie = static_cast<InsertEntity&>(entity);
            double theta = AxisAngle(axis);
            ie.SetPosition(ReflectPoint(ie.GetPosition(), axis));
            ie.SetRotation(ReflectAngle(ie.GetRotation(), theta));
            Math::Vec3 sc = ie.GetScale();
            sc.y = -sc.y;
            ie.SetScale(sc);
            return;
        }

        if (entity.IsKindOf<DimensionEntity>())
        {
            auto& de = static_cast<DimensionEntity&>(entity);
            double theta = AxisAngle(axis);
            de.SetP1         (ReflectPoint(de.GetP1(),          axis));
            de.SetP2         (ReflectPoint(de.GetP2(),          axis));
            de.SetDimLinePoint(ReflectPoint(de.GetDimLinePoint(), axis));
            de.SetCenterPoint (ReflectPoint(de.GetCenterPoint(),  axis));
            de.SetLinearAngle (ReflectAngle(de.GetLinearAngle(),  theta));
            return;
        }

        if (entity.IsKindOf<HatchEntity>())
        {
            auto& he = static_cast<HatchEntity&>(entity);
            double theta = AxisAngle(axis);
            for (auto& loop : he.GetLoops())
                for (auto& edge : loop.Edges)
                {
                    switch (edge.Type)
                    {
                    case HatchEdge::Kind::Poly:
                        for (auto& p : edge.Poly.Points) p = ReflectPoint(p, axis);
                        for (auto& b : edge.Poly.Bulges) b = -b;
                        break;
                    case HatchEdge::Kind::EllipseArc:
                        edge.Ell.Center   = ReflectPoint(edge.Ell.Center, axis);
                        edge.Ell.Rotation = ReflectAngle(edge.Ell.Rotation, theta);
                        break;
                    case HatchEdge::Kind::Spline:
                        for (auto& p : edge.Spl.FitPoints) p = ReflectPoint(p, axis);
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
            for (auto& v : verts) v = ReflectPoint(v, axis);
            le.SetVertices(std::move(verts));
            return;
        }

        if (entity.IsKindOf<MLeaderEntity>())
        {
            auto& ml = static_cast<MLeaderEntity&>(entity);
            ml.SetLanding   (ReflectPoint (ml.GetLanding(),    axis));
            ml.SetDoglegDir (ReflectVector(ml.GetDoglegDir(),  axis));
            for (auto& ln : ml.GetLeaderLines())
                for (auto& p : ln.points) p = ReflectPoint(p, axis);
            return;
        }

        if (entity.IsKindOf<RayEntity>())
        {
            auto& re = static_cast<RayEntity&>(entity);
            re.SetOrigin   (ReflectPoint (re.GetOrigin(),    axis));
            re.SetDirection(ReflectVector(re.GetDirection(), axis));
            return;
        }

        if (entity.IsKindOf<XLineEntity>())
        {
            auto& xl = static_cast<XLineEntity&>(entity);
            xl.SetOrigin   (ReflectPoint (xl.GetOrigin(),    axis));
            xl.SetDirection(ReflectVector(xl.GetDirection(), axis));
            return;
        }

        if (entity.IsKindOf<ToleranceEntity>())
        {
            auto& te = static_cast<ToleranceEntity&>(entity);
            double theta = AxisAngle(axis);
            te.SetInsertion(ReflectPoint (te.GetInsertion(), axis));
            te.SetDirection(ReflectVector(te.GetDirection(), axis));
            (void)theta;
            return;
        }
    }
}
