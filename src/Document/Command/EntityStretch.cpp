#include "EntityStretch.h"

#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
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
#include <vector>

namespace MiniCAD
{
    namespace
    {
        // 窗口内则位移，返回是否移动了
        struct Mover
        {
            const StretchWindow& w;
            double dx, dy;
            bool   moved = false;

            bool operator()(Math::Point3& p)
            {
                if (!w.Contains(p)) return false;
                p.x += dx; p.y += dy;
                moved = true;
                return true;
            }
        };

        // 在窗口内的一个点：原位置
        void Translate(Math::Point3& p, double dx, double dy) { p.x += dx; p.y += dy; }

        // 圆弧：保持包角不变地移动一个端点。chord = 起点→终点，弧凸向弦的右侧（逆时针弧），
        // 弦中点到弧中点的距离 = (弦长 / 2) · tan(包角 / 4)，所以包角相同则「弧的饱满程度」相同
        bool StretchArcEnd(Arc& arc, bool moveStart, double dx, double dy)
        {
            const double sweep = arc.SweepAngle();
            Math::Point3 s = arc.StartPoint(), e = arc.EndPoint();
            if (moveStart) Translate(s, dx, dy); else Translate(e, dx, dy);

            const double cx = e.x - s.x, cy = e.y - s.y;
            const double len = std::hypot(cx, cy);
            if (len < Math::LengthEPS)
                return false;

            const double h = len * 0.5 * std::tan(sweep * 0.25);
            const Math::Point3 mid{ (s.x + e.x) * 0.5 + cy / len * h,        // 右法线 = (cy, -cx) / len
                                    (s.y + e.y) * 0.5 - cx / len * h,
                                    s.z };
            auto result = Arc::FromThreePoints(s, mid, e);
            if (!result)
                return false;
            arc = *result;
            return true;
        }
    }

    bool StretchEntityInPlace(Entity& entity, const StretchWindow& w, double dx, double dy)
    {
        if (std::abs(dx) < 1e-12 && std::abs(dy) < 1e-12)
            return false;

        Mover mv{ w, dx, dy };

        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            if (mv(p.Position)) pe.SetPoint(p);
            return mv.moved;
        }

        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            mv(l.Start);
            mv(l.End);
            if (mv.moved) le.SetLine(l);
            return mv.moved;
        }

        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            if (mv(c.Center)) ce.SetCircle(c);
            return mv.moved;
        }

        if (entity.IsKindOf<ArcEntity>())
        {
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            const bool startIn = w.Contains(a.StartPoint());
            const bool endIn   = w.Contains(a.EndPoint());
            if (w.Contains(a.Center) || (startIn && endIn))
            {
                Translate(a.Center, dx, dy);
                ae.SetArc(a);
                return true;
            }
            if (startIn != endIn && StretchArcEnd(a, startIn, dx, dy))
            {
                ae.SetArc(a);
                return true;
            }
            return false;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            if (mv(el.Center)) ee.SetEllipse(el);
            return mv.moved;
        }

        if (entity.IsKindOf<ImageEntity>())          // 图像是矩形的子类，要先于矩形判断：整个在窗口内才平移
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            if (!(w.Contains(r.P1) && w.Contains(r.P2) && w.Contains(r.P3) && w.Contains(r.P4)))
                return false;
            Translate(r.P1, dx, dy); Translate(r.P2, dx, dy); Translate(r.P3, dx, dy); Translate(r.P4, dx, dy);
            re.SetRectangle(r);
            return true;
        }

        if (entity.IsKindOf<RectangleEntity>())      // 含实心填充：窗口内的角点各自位移
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            mv(r.P1); mv(r.P2); mv(r.P3); mv(r.P4);
            if (mv.moved) re.SetRectangle(r);
            return mv.moved;
        }

        if (entity.IsKindOf<PolylineEntity>())       // 含擦除
        {
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) mv(p);
            if (mv.moved) pe.SetPolyline(std::move(pl));
            return mv.moved;
        }

        if (entity.IsKindOf<SplineEntity>())
        {
            auto& sl = static_cast<SplineEntity&>(entity).GetSpline();
            for (auto& p : sl.FitPoints)     mv(p);
            for (auto& p : sl.ControlPoints) mv(p);
            if (mv.moved) sl.Build();
            return mv.moved;
        }

        if (entity.IsKindOf<TextEntity>())
        {
            auto& te = static_cast<TextEntity&>(entity);
            auto p = te.GetPosition();
            if (mv(p)) te.SetPosition(p);
            return mv.moved;
        }

        // 表格是多行文字的子类，只移动插入点；两者在这里处理方式相同
        if (entity.IsKindOf<MTextEntity>())
        {
            auto& me = static_cast<MTextEntity&>(entity);
            auto p = me.GetPosition();
            if (mv(p)) me.SetPosition(p);
            return mv.moved;
        }

        if (entity.IsKindOf<InsertEntity>())
        {
            auto& ie = static_cast<InsertEntity&>(entity);
            auto p = ie.GetPosition();
            if (mv(p)) ie.SetPosition(p);
            return mv.moved;
        }

        if (entity.IsKindOf<DimensionEntity>())
        {
            auto& de = static_cast<DimensionEntity&>(entity);
            auto p1 = de.GetP1(), p2 = de.GetP2(), dl = de.GetDimLinePoint(), c = de.GetCenterPoint();
            const bool m1 = mv(p1), m2 = mv(p2), m3 = mv(dl), m4 = mv(c);
            if (!mv.moved) return false;
            if (m1) de.SetP1(p1);
            if (m2) de.SetP2(p2);
            if (m3) de.SetDimLinePoint(dl);
            if (m4) de.SetCenterPoint(c);
            if (!de.IsUsingDefaultTextPosition())
            {
                auto t = de.TextPosition();
                if (mv(t)) de.SetTextPosition(t);
            }
            return true;
        }

        if (entity.IsKindOf<HatchEntity>())          // 含面域：整个包围盒在窗口内才平移
        {
            const AABB box = entity.GetBoundingBox();
            if (!(w.Contains(box.Min) && w.Contains(box.Max)))
                return false;
            auto& he = static_cast<HatchEntity&>(entity);
            for (auto& loop : he.GetLoops())
                for (auto& edge : loop.Edges)
                {
                    switch (edge.Type)
                    {
                    case HatchEdge::Kind::Poly:
                        for (auto& p : edge.Poly.Points) Translate(p, dx, dy);
                        break;
                    case HatchEdge::Kind::EllipseArc:
                        Translate(edge.Ell.Center, dx, dy);
                        break;
                    case HatchEdge::Kind::Spline:
                        for (auto& p : edge.Spl.FitPoints)     Translate(p, dx, dy);
                        for (auto& p : edge.Spl.ControlPoints) Translate(p, dx, dy);
                        edge.Spl.Build();
                        break;
                    }
                }
            return true;
        }

        if (entity.IsKindOf<LeaderEntity>())         // 含多线
        {
            auto& le = static_cast<LeaderEntity&>(entity);
            auto verts = le.GetVertices();
            for (auto& v : verts) mv(v);
            if (mv.moved) le.SetVertices(std::move(verts));
            return mv.moved;
        }

        if (entity.IsKindOf<MLeaderEntity>())
        {
            auto& ml = static_cast<MLeaderEntity&>(entity);
            auto landing = ml.GetLanding();
            if (mv(landing)) ml.SetLanding(landing);
            for (auto& ln : ml.GetLeaderLines())
                for (auto& p : ln.points) mv(p);
            return mv.moved;
        }

        if (entity.IsKindOf<RayEntity>())
        {
            auto& re = static_cast<RayEntity&>(entity);
            auto o = re.GetOrigin();
            if (mv(o)) re.SetOrigin(o);
            return mv.moved;
        }

        if (entity.IsKindOf<XLineEntity>())
        {
            auto& xl = static_cast<XLineEntity&>(entity);
            auto o = xl.GetOrigin();
            if (mv(o)) xl.SetOrigin(o);
            return mv.moved;
        }

        if (entity.IsKindOf<ToleranceEntity>())
        {
            auto& te = static_cast<ToleranceEntity&>(entity);
            auto p = te.GetInsertion();
            if (mv(p)) te.SetInsertion(p);
            return mv.moved;
        }

        return false;
    }
}
