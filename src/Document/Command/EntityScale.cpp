#include "EntityScale.h"

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
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/ToleranceEntity.hpp"

#include <cmath>

namespace MiniCAD
{
    Math::Point3 ScalePoint(const Math::Point3& p, const Math::Point3& base, double k)
    {
        return { base.x + (p.x - base.x) * k,
                 base.y + (p.y - base.y) * k,
                 base.z + (p.z - base.z) * k };
    }

    bool ScaleEntityInPlace(Entity& entity, const Math::Point3& base, double k)
    {
        if (!(k > 0.0) || !std::isfinite(k))
            return false;

        auto sp = [&](const Math::Point3& p) { return ScalePoint(p, base, k); };

        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            p.Position = sp(p.Position);
            pe.SetPoint(p);
            return true;
        }

        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            l.Start = sp(l.Start);
            l.End   = sp(l.End);
            le.SetLine(l);
            return true;
        }

        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            c.Center = sp(c.Center);
            c.Radius *= k;
            ce.SetCircle(c);
            return true;
        }

        if (entity.IsKindOf<ArcEntity>())
        {
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();
            a.Center = sp(a.Center);
            a.Radius *= k;
            ae.SetArc(a);
            return true;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();
            el.Center = sp(el.Center);
            el.RadiusX *= k;
            el.RadiusY *= k;
            ee.SetEllipse(el);
            return true;
        }

        if (entity.IsKindOf<RectangleEntity>())      // 含实心填充、图像（同为四个角点）
        {
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            r.P1 = sp(r.P1); r.P2 = sp(r.P2); r.P3 = sp(r.P3); r.P4 = sp(r.P4);
            re.SetRectangle(r);
            return true;
        }

        if (entity.IsKindOf<PolylineEntity>())       // 含擦除；bulge 与尺寸无关，等比缩放不变
        {
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) p = sp(p);
            pe.SetPolyline(std::move(pl));
            pe.SetWidth(pe.GetWidth() * k);
            return true;
        }

        if (entity.IsKindOf<SplineEntity>())
        {
            auto& sl = static_cast<SplineEntity&>(entity).GetSpline();
            for (auto& p : sl.FitPoints)     p = sp(p);
            for (auto& p : sl.ControlPoints) p = sp(p);
            sl.Build();
            return true;
        }

        if (entity.IsKindOf<TextEntity>())
        {
            auto& te = static_cast<TextEntity&>(entity);
            te.SetPosition(sp(te.GetPosition()));
            te.SetHeight(static_cast<float>(te.GetHeight() * k));
            return true;
        }

        if (entity.IsKindOf<TableEntity>())          // 表格是多行文字的子类，要先于多行文字判断
        {
            auto& tb = static_cast<TableEntity&>(entity);
            tb.SetPosition(sp(tb.GetPosition()));
            tb.SetHeight(tb.GetHeight() * k);
            std::vector<double> cols = tb.ColWidths(), rows = tb.RowHeights();
            for (double& w : cols) w *= k;
            for (double& h : rows) h *= k;
            tb.SetColWidths(std::move(cols));
            tb.SetRowHeights(std::move(rows));
            tb.SetMargin(tb.Margin() * k);
            return true;
        }

        if (entity.IsKindOf<MTextEntity>())
        {
            auto& me = static_cast<MTextEntity&>(entity);
            me.SetPosition(sp(me.GetPosition()));
            me.SetHeight(me.GetHeight() * k);
            me.SetBoxWidth(me.GetBoxWidth() * k);
            me.SetDefinedHeight(me.GetDefinedHeight() * k);
            if (me.GetColumnType() != MTextColumnType::None)
            {
                std::vector<double> heights = me.GetColumnHeights();
                for (double& h : heights) h *= k;
                me.SetColumns(me.GetColumnType(), me.GetColumnCount(), me.GetColumnWidth() * k, me.GetColumnGutter() * k);
                me.SetColumnHeights(std::move(heights));
            }
            return true;
        }

        if (entity.IsKindOf<InsertEntity>())
        {
            auto& ie = static_cast<InsertEntity&>(entity);
            ie.SetPosition(sp(ie.GetPosition()));
            ie.SetScale(ie.GetScale() * k);
            ie.SetArray(ie.GetColumnCount(), ie.GetRowCount(), ie.GetColumnSpacing() * k, ie.GetRowSpacing() * k);
            return true;
        }

        if (entity.IsKindOf<DimensionEntity>())
        {
            auto& de = static_cast<DimensionEntity&>(entity);
            de.SetP1(sp(de.GetP1()));
            de.SetP2(sp(de.GetP2()));
            de.SetDimLinePoint(sp(de.GetDimLinePoint()));
            de.SetCenterPoint(sp(de.GetCenterPoint()));
            if (!de.IsUsingDefaultTextPosition())
                de.SetTextPosition(sp(de.TextPosition()));
            return true;
        }

        if (entity.IsKindOf<HatchEntity>())          // 含面域
        {
            auto& he = static_cast<HatchEntity&>(entity);
            for (auto& loop : he.GetLoops())
                for (auto& edge : loop.Edges)
                {
                    switch (edge.Type)
                    {
                    case HatchEdge::Kind::Poly:
                        for (auto& p : edge.Poly.Points) p = sp(p);
                        break;
                    case HatchEdge::Kind::EllipseArc:
                        edge.Ell.Center = sp(edge.Ell.Center);
                        edge.Ell.RadiusX *= k;
                        edge.Ell.RadiusY *= k;
                        break;
                    case HatchEdge::Kind::Spline:
                        for (auto& p : edge.Spl.FitPoints)     p = sp(p);
                        for (auto& p : edge.Spl.ControlPoints) p = sp(p);
                        edge.Spl.Build();
                        break;
                    }
                }
            he.SetScale(he.GetScale() * k);          // 图案比例跟着变，缩放后图案疏密与图形保持一致
            return true;
        }

        if (entity.IsKindOf<MLineEntity>())          // 多线是引线的子类，要先于引线判断
        {
            auto& ml = static_cast<MLineEntity&>(entity);
            auto verts = ml.GetVertices();
            for (auto& v : verts) v = sp(v);
            ml.SetVertices(std::move(verts));
            ml.SetScale(ml.GetScale() * k);          // 多线比例决定线间距
            return true;
        }

        if (entity.IsKindOf<LeaderEntity>())
        {
            auto& le = static_cast<LeaderEntity&>(entity);
            auto verts = le.GetVertices();
            for (auto& v : verts) v = sp(v);
            le.SetVertices(std::move(verts));
            return true;
        }

        if (entity.IsKindOf<MLeaderEntity>())
        {
            auto& ml = static_cast<MLeaderEntity&>(entity);
            ml.SetLanding(sp(ml.GetLanding()));
            for (auto& ln : ml.GetLeaderLines())
                for (auto& p : ln.points) p = sp(p);
            return true;
        }

        if (entity.IsKindOf<RayEntity>())
        {
            auto& re = static_cast<RayEntity&>(entity);
            re.SetOrigin(sp(re.GetOrigin()));        // 方向不变
            return true;
        }

        if (entity.IsKindOf<XLineEntity>())
        {
            auto& xl = static_cast<XLineEntity&>(entity);
            xl.SetOrigin(sp(xl.GetOrigin()));
            return true;
        }

        if (entity.IsKindOf<ToleranceEntity>())
        {
            auto& te = static_cast<ToleranceEntity&>(entity);
            te.SetInsertion(sp(te.GetInsertion()));
            return true;
        }

        return false;
    }
}
