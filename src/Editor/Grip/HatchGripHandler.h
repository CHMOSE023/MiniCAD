#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/Math/Constants.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <vector>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // HatchDragState
    // ─────────────────────────────────────────────
    struct HatchDragState : public IGripDragState
    {
        Object::ObjectID       EntityId = Object::InvalidID;
        std::vector<HatchLoop> Base;     // 拖拽开始时的全部边界环快照
    };

    // ─────────────────────────────────────────────
    // HatchGripHandler
    //
    // 像 AutoCAD 一样夹点编辑图案填充(HatchEntity)的边界。边界按「类型化边界边」
    // 存储,夹点也按边类型给出,使填充编辑与原曲线一致：
    //
    //   · Poly 边(直线 + 圆弧) : 每顶点 Start 夹点 + 每段 Mid 夹点
    //                            (直线段整段平移；圆弧段三点重算 bulge)
    //   · EllipseArc 边        : Center(圆心平移) + 两端点(沿椭圆改参数) +
    //                            Quadrant×2(改长/短半轴)
    //   · Spline 边            : 每拟合点 Tangent 夹点(移动并重建样条)
    //
    // SubIndex 打包 = loop<<20 | edge<<10 | local。
    // ─────────────────────────────────────────────
    class HatchGripHandler : public IEntityGripHandler
    {
        static int  Pack(int loop, int edge, int local) { return (loop << 20) | (edge << 10) | local; }
        static void Unpack(int s, int& loop, int& edge, int& local)
        { loop = (s >> 20) & 1023; edge = (s >> 10) & 1023; local = s & 1023; }

    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* hatch = static_cast<HatchEntity*>(entity);
            const auto& loops = hatch->GetLoops();
            const auto  id    = entity->GetID();

            for (int li = 0; li < static_cast<int>(loops.size()); ++li)
            {
                const auto& edges = loops[li].Edges;
                for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei)
                {
                    const HatchEdge& e = edges[ei];
                    switch (e.Type)
                    {
                    case HatchEdge::Kind::Poly:
                    {
                        const Polyline& pl = e.Poly;
                        for (int i = 0; i < static_cast<int>(pl.Points.size()); ++i)
                            outGrips.push_back({ id, Grip::Type::Start, pl.Points[i], Pack(li, ei, i) });
                        for (int i = 0; i < pl.SegCount(); ++i)
                            outGrips.push_back({ id, Grip::Type::Mid, SegMid(pl, i), Pack(li, ei, i) });
                        break;
                    }
                    case HatchEdge::Kind::EllipseArc:
                        outGrips.push_back({ id, Grip::Type::Center,   e.Ell.Center,        Pack(li, ei, 0) });
                        outGrips.push_back({ id, Grip::Type::Start,    e.Ell.PointAt(e.A0), Pack(li, ei, 0) });
                        outGrips.push_back({ id, Grip::Type::End,      e.Ell.PointAt(e.A1), Pack(li, ei, 0) });
                        outGrips.push_back({ id, Grip::Type::Quadrant, e.Ell.VertexE(),     Pack(li, ei, 0) });
                        outGrips.push_back({ id, Grip::Type::Quadrant, e.Ell.VertexN(),     Pack(li, ei, 1) });
                        break;
                    case HatchEdge::Kind::Spline:
                        for (int i = 0; i < static_cast<int>(e.Spl.FitPoints.size()); ++i)
                            outGrips.push_back({ id, Grip::Type::Tangent, e.Spl.FitPoints[i], Pack(li, ei, i) });
                        break;
                    }
                }
            }
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* hatch = static_cast<HatchEntity*>(entity);
            auto state      = std::make_unique<HatchDragState>();
            state->EntityId = entity->GetID();
            state->Base     = hatch->GetLoops();
            return state;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* hatch = static_cast<HatchEntity*>(entity);
            auto* state = static_cast<HatchDragState*>(dragState);

            std::vector<HatchLoop> out = state->Base;   // 从快照出发

            int li, ei, local;
            Unpack(activeGrip.SubIndex, li, ei, local);
            if (li < 0 || li >= static_cast<int>(out.size())) return;
            auto& edges = out[li].Edges;
            if (ei < 0 || ei >= static_cast<int>(edges.size())) return;
            HatchEdge&       e    = edges[ei];
            const HatchEdge& base = state->Base[li].Edges[ei];

            switch (e.Type)
            {
            case HatchEdge::Kind::Poly:
            {
                Polyline&       pl = e.Poly;
                const Polyline& bp = base.Poly;
                if (activeGrip.GripType == Grip::Type::Start)
                {
                    if (local >= 0 && local < static_cast<int>(pl.Points.size()))
                        pl.Points[local] = worldPos;
                }
                else if (activeGrip.GripType == Grip::Type::Mid)
                {
                    int seg = local;
                    if (seg >= 0 && seg < pl.SegCount())
                    {
                        if (bp.SegIsArc(seg))
                        {
                            double nb = ComputeBulgeFromThreePoints(pl.Points[seg], worldPos, pl.Points[seg + 1]);
                            if (std::abs(nb) > 1e-12) pl.Bulges[seg] = nb;
                        }
                        else
                        {
                            Math::Point3 oldMid = SegLineMid(bp.SegStart(seg), bp.SegEnd(seg));
                            double dx = worldPos.x - oldMid.x, dy = worldPos.y - oldMid.y;
                            pl.Points[seg].x     += dx;  pl.Points[seg].y     += dy;
                            pl.Points[seg + 1].x += dx;  pl.Points[seg + 1].y += dy;
                        }
                    }
                }
                break;
            }
            case HatchEdge::Kind::EllipseArc:
            {
                switch (activeGrip.GripType)
                {
                case Grip::Type::Center:
                    e.Ell.Center = worldPos;
                    break;
                case Grip::Type::Start:
                    e.A0 = EllipseParam(e.Ell, worldPos);
                    break;
                case Grip::Type::End:
                    e.A1 = EllipseParam(e.Ell, worldPos);
                    break;
                case Grip::Type::Quadrant:
                {
                    // 把光标投影到长/短轴方向,取距离作半轴。
                    double cR = std::cos(e.Ell.Rotation), sR = std::sin(e.Ell.Rotation);
                    double dx = worldPos.x - e.Ell.Center.x, dy = worldPos.y - e.Ell.Center.y;
                    if (local == 0) e.Ell.RadiusX = std::max(1e-6, std::abs(dx * cR + dy * sR));
                    else            e.Ell.RadiusY = std::max(1e-6, std::abs(-dx * sR + dy * cR));
                    break;
                }
                default: break;
                }
                break;
            }
            case HatchEdge::Kind::Spline:
            {
                if (local >= 0 && local < static_cast<int>(e.Spl.FitPoints.size()))
                {
                    e.Spl.FitPoints[local] = worldPos;
                    e.Spl.Build();
                }
                break;
            }
            }

            hatch->GetLoops() = out;
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<HatchDragState*>(dragState);
            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };
            for (const auto& loop : state->Base)
            {
                std::vector<Math::Point3> pts = loop.Tessellate();
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    overlay.AddLine(pts[i], pts[i + 1], kGhost);
                if (pts.size() >= 2) overlay.AddLine(pts.back(), pts.front(), kGhost);
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* hatch = static_cast<HatchEntity*>(entity);
            auto* state = static_cast<HatchDragState*>(dragState);
            if (!hatch || !state) return false;

            outEntry.Id          = entity->GetID();
            outEntry.Kind        = DragEntityEntry::Kind::Hatch;
            outEntry.BeforeHatch = state->Base;
            outEntry.AfterHatch  = hatch->GetLoops();
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* hatch = static_cast<HatchEntity*>(entity);
            auto* state = static_cast<HatchDragState*>(dragState);
            hatch->GetLoops() = state->Base;
        }

    private:

        // ── 夹点坐标同步 ───────────────────────────────────────────────────
        static void SyncGrips(Object::ObjectID ownerId, const std::vector<HatchLoop>& loops, std::vector<Grip>& grips)
        {
            for (auto& g : grips)
            {
                if (g.OwnerID != ownerId) continue;
                int li, ei, local; Unpack(g.SubIndex, li, ei, local);
                if (li < 0 || li >= static_cast<int>(loops.size())) continue;
                const auto& edges = loops[li].Edges;
                if (ei < 0 || ei >= static_cast<int>(edges.size())) continue;
                const HatchEdge& e = edges[ei];

                switch (e.Type)
                {
                case HatchEdge::Kind::Poly:
                    if (g.GripType == Grip::Type::Start)
                    {
                        if (local >= 0 && local < static_cast<int>(e.Poly.Points.size()))
                            g.WorldPos = e.Poly.Points[local];
                    }
                    else if (g.GripType == Grip::Type::Mid)
                    {
                        if (local >= 0 && local < e.Poly.SegCount())
                            g.WorldPos = SegMid(e.Poly, local);
                    }
                    break;
                case HatchEdge::Kind::EllipseArc:
                    switch (g.GripType)
                    {
                    case Grip::Type::Center:   g.WorldPos = e.Ell.Center;        break;
                    case Grip::Type::Start:    g.WorldPos = e.Ell.PointAt(e.A0); break;
                    case Grip::Type::End:      g.WorldPos = e.Ell.PointAt(e.A1); break;
                    case Grip::Type::Quadrant: g.WorldPos = (local == 0) ? e.Ell.VertexE() : e.Ell.VertexN(); break;
                    default: break;
                    }
                    break;
                case HatchEdge::Kind::Spline:
                    if (g.GripType == Grip::Type::Tangent &&
                        local >= 0 && local < static_cast<int>(e.Spl.FitPoints.size()))
                        g.WorldPos = e.Spl.FitPoints[local];
                    break;
                }
            }
        }

        // ── 椭圆:世界点 → 参数 t ──────────────────────────────────────────
        static double EllipseParam(const Ellipse& el, const Math::Point3& p)
        {
            double cR = std::cos(el.Rotation), sR = std::sin(el.Rotation);
            double dx = p.x - el.Center.x, dy = p.y - el.Center.y;
            double lx =  dx * cR + dy * sR;
            double ly = -dx * sR + dy * cR;
            double t = std::atan2(ly / (el.RadiusY + Math::LengthEPS), lx / (el.RadiusX + Math::LengthEPS));
            if (t < 0.0) t += Math::TwoPI;
            return t;
        }

        // ── 多段线段中点(直线几何中点 / 弧中点)──────────────────────────
        static Math::Point3 SegMid(const Polyline& pl, int i)
        {
            if (pl.SegIsArc(i)) return ArcGeomMid(pl, i);
            return SegLineMid(pl.SegStart(i), pl.SegEnd(i));
        }
        static Math::Point3 SegLineMid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
        }
        static Math::Point3 ArcGeomMid(const Polyline& pl, int i)
        {
            ArcGeom arc = Polyline::ComputeArc(pl.SegStart(i), pl.SegEnd(i), pl.SegBulge(i));
            double midAngle = arc.StartAngle + arc.SweepAngle * 0.5;
            return { arc.Center.x + arc.Radius * std::cos(midAngle),
                     arc.Center.y + arc.Radius * std::sin(midAngle),
                     pl.SegStart(i).z };
        }

        // 三点求 bulge(A=弧起点,M=弧上经过点,B=弧终点)。
        static double ComputeBulgeFromThreePoints(const Math::Point3& A, const Math::Point3& M, const Math::Point3& B)
        {
            double ax = A.x, ay = A.y, mx = M.x, my = M.y, bx = B.x, by = B.y;
            double D = 2.0 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
            if (std::abs(D) < 1e-10) return 0.0;
            double a2 = ax * ax + ay * ay, m2 = mx * mx + my * my, b2 = bx * bx + by * by;
            double cx = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / D;
            double cy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / D;
            double sa = std::atan2(ay - cy, ax - cx);
            double ea = std::atan2(by - cy, bx - cx);
            double cross = (mx - ax) * (by - ay) - (my - ay) * (bx - ax);
            double sweep;
            if (cross >= 0.0) { sweep = ea - sa; if (sweep <= 0.0) sweep += Math::TwoPI; return -std::tan(sweep * 0.25); }
            else              { sweep = sa - ea; if (sweep <= 0.0) sweep += Math::TwoPI; return  std::tan(sweep * 0.25); }
        }
    };
}
