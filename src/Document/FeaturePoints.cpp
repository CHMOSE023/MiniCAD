#include "FeaturePoints.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Constants.hpp"
#include <cmath>

namespace MiniCAD
{
    namespace
    {
        // ─────────────────────────────────────────────────────────────────────
        // 端点
        //   Point → 位置；Line → 起点 / 终点；Rectangle → 四个角点；
        //   Region → 多段线边界的顶点、椭圆弧边的两端；MLine → 各元素线的顶点；
        //   Circle → 圆心；Arc → 起点 / 终点 / 圆心；Ellipse → 圆心，椭圆弧另加两端；
        //   Polyline → 所有顶点；Spline → 所有拟合点；Ray / XLine → 基点
        // ─────────────────────────────────────────────────────────────────────
        void Endpoints(const Entity& obj, std::vector<Math::Point3>& out)
        {
            if (obj.IsKindOf<PointEntity>())
                out.push_back(static_cast<const PointEntity&>(obj).GetPoint().Position);

            if (obj.IsKindOf<LineEntity>())
            {
                const auto& l = static_cast<const LineEntity&>(obj).GetLine();
                out.push_back(l.Start);
                out.push_back(l.End);
            }

            if (obj.IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<const RectangleEntity&>(obj).GetRectangle();
                out.insert(out.end(), { r.P1, r.P2, r.P3, r.P4 });
            }

            if (obj.IsKindOf<RegionEntity>())
            {
                for (const auto& loop : static_cast<const RegionEntity&>(obj).GetLoops())
                    for (const auto& edge : loop.Edges)
                    {
                        if (edge.Type == HatchEdge::Kind::Poly)
                            out.insert(out.end(), edge.Poly.Points.begin(), edge.Poly.Points.end());
                        else if (edge.Type == HatchEdge::Kind::EllipseArc && !(edge.A1 - edge.A0 >= Math::TwoPI - 1e-9))
                        {
                            out.push_back(edge.StartPt());
                            out.push_back(edge.EndPt());
                        }
                    }
            }

            if (obj.IsKindOf<MLineEntity>())
            {
                for (const auto& el : static_cast<const MLineEntity&>(obj).ComputeGeometry().Elements)
                    out.insert(out.end(), el.begin(), el.end());
            }

            if (obj.IsKindOf<CircleEntity>())
                out.push_back(static_cast<const CircleEntity&>(obj).GetCircle().Center);

            if (obj.IsKindOf<ArcEntity>())
            {
                const auto& arc = static_cast<const ArcEntity&>(obj).GetArc();
                out.push_back(arc.StartPoint());
                out.push_back(arc.EndPoint());
                out.push_back(arc.Center);
            }

            if (obj.IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<const EllipseEntity&>(obj).GetEllipse();
                out.push_back(el.Center);
                if (!el.IsFull())
                {
                    out.push_back(el.StartPoint());
                    out.push_back(el.EndPoint());
                }
            }

            if (obj.IsKindOf<PolylineEntity>())
            {
                const auto& pts = static_cast<const PolylineEntity&>(obj).GetPolyline().Points;
                out.insert(out.end(), pts.begin(), pts.end());
            }

            if (obj.IsKindOf<SplineEntity>())
            {
                const auto& fps = static_cast<const SplineEntity&>(obj).GetSpline().FitPoints;
                out.insert(out.end(), fps.begin(), fps.end());
            }

            if (obj.IsKindOf<RayEntity>())
                out.push_back(static_cast<const RayEntity&>(obj).GetOrigin());

            if (obj.IsKindOf<XLineEntity>())
                out.push_back(static_cast<const XLineEntity&>(obj).GetOrigin());
        }

        // ─────────────────────────────────────────────────────────────────────
        // 中点
        //   Line → 中点；Rectangle → 四条边的中点；Arc → 弧中点；
        //   Ellipse → 椭圆弧取参数中点，整椭圆取 45° / 135° / 225° / 315° 参数处；
        //   Polyline → 每段中点（弧段取弧上中点）；Spline → 每段参数中点
        // ─────────────────────────────────────────────────────────────────────
        void Midpoints(const Entity& obj, std::vector<Math::Point3>& out)
        {
            if (obj.IsKindOf<LineEntity>())
            {
                const auto& l = static_cast<const LineEntity&>(obj).GetLine();
                out.push_back(Math::Midpoint(l.Start, l.End));
            }

            if (obj.IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<const RectangleEntity&>(obj).GetRectangle();
                out.push_back(Math::Midpoint(r.P1, r.P2));
                out.push_back(Math::Midpoint(r.P2, r.P3));
                out.push_back(Math::Midpoint(r.P3, r.P4));
                out.push_back(Math::Midpoint(r.P4, r.P1));
            }

            if (obj.IsKindOf<ArcEntity>())
                out.push_back(static_cast<const ArcEntity&>(obj).GetArc().MidPoint());

            if (obj.IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<const EllipseEntity&>(obj).GetEllipse();
                if (!el.IsFull())
                    out.push_back(el.PointAt(el.MidParam()));
                else
                    for (double t : { Math::PI * 0.25, Math::PI * 0.75, Math::PI * 1.25, Math::PI * 1.75 })
                        out.push_back(el.PointAt(t));
            }

            if (obj.IsKindOf<PolylineEntity>())
            {
                const auto& pl = static_cast<const PolylineEntity&>(obj).GetPolyline();
                for (int i = 0; i < pl.SegCount(); ++i)
                {
                    if (pl.SegIsArc(i))
                    {
                        ArcGeom arc = Polyline::ComputeArc(pl.SegStart(i), pl.SegEnd(i), pl.SegBulge(i));
                        const double midAngle = arc.StartAngle + arc.SweepAngle * 0.5;
                        out.push_back({ arc.Center.x + arc.Radius * std::cos(midAngle),
                                        arc.Center.y + arc.Radius * std::sin(midAngle),
                                        pl.SegStart(i).z });
                    }
                    else
                        out.push_back(Math::Midpoint(pl.SegStart(i), pl.SegEnd(i)));
                }
            }

            if (obj.IsKindOf<SplineEntity>())
            {
                const auto& spline = static_cast<const SplineEntity&>(obj).GetSpline();
                if (spline.IsValid())
                    for (const auto& seg : spline.Segments)
                        out.push_back(seg.Evaluate(0.5));
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // 象限点
        //   Circle → 0° / 90° / 180° / 270°；Arc → 只取弧角度范围内的；
        //   Ellipse → 参数 0 / π/2 / π / 3π/2 处的轴端点（椭圆弧只取范围内的）
        // ─────────────────────────────────────────────────────────────────────
        void Quadrants(const Entity& obj, std::vector<Math::Point3>& out)
        {
            if (obj.IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<const CircleEntity&>(obj).GetCircle();
                out.push_back({ c.Center.x + c.Radius, c.Center.y,            c.Center.z });
                out.push_back({ c.Center.x,            c.Center.y + c.Radius, c.Center.z });
                out.push_back({ c.Center.x - c.Radius, c.Center.y,            c.Center.z });
                out.push_back({ c.Center.x,            c.Center.y - c.Radius, c.Center.z });
            }

            if (obj.IsKindOf<ArcEntity>())
            {
                const auto& arc = static_cast<const ArcEntity&>(obj).GetArc();
                for (double qa : { 0.0, Math::PI * 0.5, Math::PI, Math::PI * 1.5 })
                    if (arc.ContainsAngle(qa))
                        out.push_back(arc.PointAt(qa));
            }

            if (obj.IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<const EllipseEntity&>(obj).GetEllipse();
                for (int k = 0; k < 4; ++k)
                {
                    const double t = Math::PI * 0.5 * k;
                    if (el.ContainsParam(t))
                        out.push_back(el.PointAt(t));
                }
            }
        }

        void Centers(const Entity& obj, std::vector<Math::Point3>& out)
        {
            if (obj.IsKindOf<CircleEntity>())
                out.push_back(static_cast<const CircleEntity&>(obj).GetCircle().Center);
            else if (obj.IsKindOf<ArcEntity>())
                out.push_back(static_cast<const ArcEntity&>(obj).GetArc().Center);
            else if (obj.IsKindOf<EllipseEntity>())
                out.push_back(static_cast<const EllipseEntity&>(obj).GetEllipse().Center);
        }
    }

    void CollectFeaturePoints(const Entity& e, FeatureKind kind, std::vector<Math::Point3>& out)
    {
        switch (kind)
        {
        case FeatureKind::Endpoint: Endpoints(e, out); break;
        case FeatureKind::Midpoint: Midpoints(e, out); break;
        case FeatureKind::Quadrant: Quadrants(e, out); break;
        case FeatureKind::Center:   Centers(e, out);   break;
        }
    }
}
