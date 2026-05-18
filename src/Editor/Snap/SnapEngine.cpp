#include "SnapResult.h"
#include "SnapEngine.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include "Scene/Scene.h"
#include "Editor/Viewport/Camera.h"
#include "Core/Math/MathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace MiniCAD
{
    // =========================================================================
    // 内部工具：屏幕距离比较辅助
    // =========================================================================
    namespace
    {
        // 候选点与光标的屏幕距离，满足阈值则更新 best
        inline void TryUpdateBest(const Math::Point3& worldPt,
            const Math::Point2& sp,
            const Camera& cam,
            double snapRadiusPx,
            double& bestDist,
            SnapResult& best,
            SnapResult::Type type,
            Object::ObjectID id)
        {
            double d = Math::Distance(sp, cam.WorldToScreen(worldPt));
            if (d < snapRadiusPx && d < bestDist)
            {
                bestDist = d;
                best = { type, worldPt, id };
            }
        }
    }

    // =========================================================================
    // 主入口
    // =========================================================================
    SnapResult SnapEngine::Query(const Math::Point2& sp, const Scene& scene,
        const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        if (EnableEndpoint) { auto r = TryEndpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableMidpoint) { auto r = TryMidpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableQuadrant) { auto r = TryQuadrant(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableNearest)  { auto r = TryNearest(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableGrid)     return TryGrid(sp, cam);
        return {};
    }

    // =========================================================================
    // TryEndpoint
    //
    // 各实体端点规则：
    //   Line         → Start / End
    //   Rectangle    → P1 / P2 / P3 / P4
    //   Point        → Position
    //   Circle       → Center（AutoCAD 中圆心属于 Endpoint 捕捉）
    //   Arc          → StartPoint / EndPoint / Center
    //   Ellipse      → Center
    //   Polyline     → 所有顶点（Points）
    //   Spline       → 首尾拟合点
    // =========================================================================
    SnapResult SnapEngine::TryEndpoint(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                const auto id = obj.GetID();
                const auto T = SnapResult::Type::Endpoint;

                // ── Point ──────────────────────────────────────────────────────
                if (obj.IsKindOf<PointEntity>())
                {
                    auto* e = static_cast<const PointEntity*>(&obj);
                    TryUpdateBest(e->GetPoint().Position, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Line ───────────────────────────────────────────────────────
                if (obj.IsKindOf<LineEntity>())
                {
                    auto* e = static_cast<const LineEntity*>(&obj);
                    TryUpdateBest(e->GetLine().Start, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(e->GetLine().End, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Rectangle ──────────────────────────────────────────────────
                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto* e = static_cast<const RectangleEntity*>(&obj);
                    const auto& r = e->GetRectangle();
                    for (const auto& p : { r.P1, r.P2, r.P3, r.P4 })
                        TryUpdateBest(p, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Circle：圆心 ───────────────────────────────────────────────
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto* e = static_cast<const CircleEntity*>(&obj);
                    TryUpdateBest(e->GetCircle().Center, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Arc：StartPoint / EndPoint / Center ────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto* e = static_cast<const ArcEntity*>(&obj);
                    const auto& arc = e->GetArc();
                    TryUpdateBest(arc.StartPoint(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(arc.EndPoint(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(arc.Center, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Ellipse：Center ────────────────────────────────────────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto* e = static_cast<const EllipseEntity*>(&obj);
                    TryUpdateBest(e->GetEllipse().Center, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Polyline：所有顶点 ─────────────────────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto* e = static_cast<const PolylineEntity*>(&obj);
                    for (const auto& pt : e->GetPolyline().Points)
                        TryUpdateBest(pt, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Spline：所有拟合点 ────────────────────────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto* e = static_cast<const SplineEntity*>(&obj);
                    const auto& spline = e->GetSpline();

                    for (const auto& fp : spline.FitPoints)
                        TryUpdateBest(fp, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

            });

        return best;
    }

    // =========================================================================
    // TryMidpoint
    //
    //   Line         → 线段中点
    //   Rectangle    → 四条边的中点
    //   Arc          → 弧中点（MidPoint，参数 t=0.5 处）
    //   Ellipse      → 四个轴端点之间的弧中点（即 t=π/4, 3π/4, 5π/4, 7π/4 处）
    //   Polyline     → 每段的中点（直线段取几何中点，弧段取弧上中点）
    //   Spline       → 相邻拟合点的弦中点（轻量近似，不重建曲线）
    // =========================================================================
    SnapResult SnapEngine::TryMidpoint(const Math::Point2& sp, const Scene& scene,
        const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                const auto id = obj.GetID();
                const auto T = SnapResult::Type::Midpoint;

                // ── Line ───────────────────────────────────────────────────────
                if (obj.IsKindOf<LineEntity>())
                {
                    auto* e = static_cast<const LineEntity*>(&obj);
                    TryUpdateBest(Math::Midpoint(e->GetLine().Start, e->GetLine().End),
                        sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Rectangle：四条边中点 ───────────────────────────────────────
                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto* e = static_cast<const RectangleEntity*>(&obj);
                    const auto& r = e->GetRectangle();
                    TryUpdateBest(Math::Midpoint(r.P1, r.P2), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(Math::Midpoint(r.P2, r.P3), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(Math::Midpoint(r.P3, r.P4), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(Math::Midpoint(r.P4, r.P1), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Arc：弧段中点 ──────────────────────────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto* e = static_cast<const ArcEntity*>(&obj);
                    TryUpdateBest(e->GetArc().MidPoint(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Ellipse：四段弧的中点（t = π/4 * (2k+1)，k=0..3）─────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto* e = static_cast<const EllipseEntity*>(&obj);
                    const auto& el = e->GetEllipse();

                    // 每个象限弧的参数中点：t = 45° / 135° / 225° / 315°
                    constexpr double kMidAngles[4] =
                    {
                        Math::PI * 0.25,
                        Math::PI * 0.75,
                        Math::PI * 1.25,
                        Math::PI * 1.75
                    };
                    for (double t : kMidAngles)
                        TryUpdateBest(el.PointAt(t), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Polyline：每段中点 ────────────────────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto* e = static_cast<const PolylineEntity*>(&obj);
                    const auto& pl = e->GetPolyline();

                    for (int i = 0; i < pl.SegCount(); ++i)
                    {
                        Math::Point3 mid;
                        if (pl.SegIsArc(i))
                        {
                            // 弧段：取弧上参数中点
                            ArcGeom arc = Polyline::ComputeArc(pl.SegStart(i), pl.SegEnd(i), pl.SegBulge(i));
                            double  midAngle = arc.StartAngle + arc.SweepAngle * 0.5;
                            mid =
                            {
                                arc.Center.x + arc.Radius * std::cos(midAngle),
                                arc.Center.y + arc.Radius * std::sin(midAngle),
                                pl.SegStart(i).z
                            };
                        }
                        else
                        {
                            // 直线段：几何中点
                            mid = Math::Midpoint(pl.SegStart(i), pl.SegEnd(i));
                        }
                        TryUpdateBest(mid, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    }
                }

                // ── Spline：相邻拟合点的弦中点 ───────────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto* e = static_cast<const SplineEntity*>(&obj);
                    const auto& spline = e->GetSpline();   //  改名 spline，不再遮蔽参数 sp
                    if (!spline.IsValid()) return;

                    for (const auto& seg : spline.Segments)
                        TryUpdateBest(seg.Evaluate(0.5), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }
            });

        return best;
    }

    // =========================================================================
    // TryQuadrant
    //
    //   Circle  → 0° / 90° / 180° / 270° 四个象限点
    //   Arc     → 只取弧段角度范围内的象限点（与 AutoCAD 一致）
    //   Ellipse → VertexE / VertexN / VertexW / VertexS（轴端点）
    // =========================================================================
    SnapResult SnapEngine::TryQuadrant(const Math::Point2& sp, const Scene& scene,
        const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                const auto id = obj.GetID();
                const auto T = SnapResult::Type::Quadrant;

                // ── Circle：固定四个象限点 ────────────────────────────────────
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto* e = static_cast<const CircleEntity*>(&obj);
                    const auto& c = e->GetCircle();

                    const Math::Point3 qpts[4] =
                    {
                        { c.Center.x + c.Radius, c.Center.y,            c.Center.z },
                        { c.Center.x,            c.Center.y + c.Radius, c.Center.z },
                        { c.Center.x - c.Radius, c.Center.y,            c.Center.z },
                        { c.Center.x,            c.Center.y - c.Radius, c.Center.z }
                    };
                    for (const auto& qp : qpts)
                        TryUpdateBest(qp, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Arc：只捕捉弧角度范围内的象限点 ──────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto* e = static_cast<const ArcEntity*>(&obj);
                    const auto& arc = e->GetArc();

                    constexpr double kQuadAngles[4] =
                    {
                        0.0,
                        Math::PI * 0.5,
                        Math::PI,
                        Math::PI * 1.5
                    };

                    for (double qa : kQuadAngles)
                    {
                        if (!arc.ContainsAngle(qa)) continue;   // 不在弧范围内则跳过
                        TryUpdateBest(arc.PointAt(qa), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    }
                }

                // ── Ellipse：四个轴端点（AutoCAD 的 Quadrant 对椭圆也有效）────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto* e = static_cast<const EllipseEntity*>(&obj);
                    const auto& el = e->GetEllipse();

                    TryUpdateBest(el.VertexE(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(el.VertexN(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(el.VertexW(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    TryUpdateBest(el.VertexS(), sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }
            });

        return best;
    }

    // =========================================================================
    // TryNearest
    //
    //   Line         → 线段上最近点
    //   Rectangle    → 四条边上最近点
    //   Circle       → 圆周上最近点（投影到圆心方向）
    //   Arc          → 弧上最近点（Arc::ClosestPoint）
    //   Ellipse      → 椭圆周最近点（Ellipse::ClosestPoint，牛顿迭代）
    //   Polyline     → Tessellate 后逐段最近点
    //   Spline       → Tessellate 后逐段最近点
    // =========================================================================
    SnapResult SnapEngine::TryNearest(const Math::Point2& sp, const Scene& scene,
        const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        Math::Point3 worldMouse = cam.ScreenToWorld(sp.x, sp.y);

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                const auto id = obj.GetID();
                const auto T = SnapResult::Type::Nearest;

                // ── Line ───────────────────────────────────────────────────────
                if (obj.IsKindOf<LineEntity>())
                {
                    auto* e = static_cast<const LineEntity*>(&obj);
                    auto& L = e->GetLine();
                    Math::Point3 closest = Math::ClosestPointOnSegment(worldMouse, L.Start, L.End);
                    TryUpdateBest(closest, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Rectangle ──────────────────────────────────────────────────
                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto* e = static_cast<const RectangleEntity*>(&obj);
                    const auto& r = e->GetRectangle();

                    for (auto [a, b] : {
                        std::pair{r.P1, r.P2}, {r.P2, r.P3}, {r.P3, r.P4}, {r.P4, r.P1} })
                    {
                        TryUpdateBest(Math::ClosestPointOnSegment(worldMouse, a, b),
                            sp, cam, SnapRadiusPx, bestDist, best, T, id);
                    }
                }

                // ── Circle：投影到圆周 ─────────────────────────────────────────
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto* e = static_cast<const CircleEntity*>(&obj);
                    const auto& c = e->GetCircle();

                    double dx = worldMouse.x - c.Center.x;
                    double dy = worldMouse.y - c.Center.y;
                    double len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-10) return;

                    Math::Point3 onCircle =
                    {
                        c.Center.x + c.Radius * (dx / len),
                        c.Center.y + c.Radius * (dy / len),
                        c.Center.z
                    };
                    TryUpdateBest(onCircle, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Arc：弧上最近点（考虑角度范围）────────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto* e = static_cast<const ArcEntity*>(&obj);
                    Math::Point3 closest = e->GetArc().ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Ellipse：椭圆周最近点（牛顿迭代）─────────────────────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto* e = static_cast<const EllipseEntity*>(&obj);
                    Math::Point3 closest = e->GetEllipse().ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Polyline：Tessellate 后逐段最近点 ────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto* e = static_cast<const PolylineEntity*>(&obj);
                    const auto& pl = e->GetPolyline();

                    // 直接用 Polyline 的几何最近点查询（内部处理弧段）
                    Math::Point3 closest = pl.ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }

                // ── Spline：Tessellate 后逐段最近点 ──────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto* e = static_cast<const SplineEntity*>(&obj);
                    const auto& sp_ = e->GetSpline();
                    if (!sp_.IsValid()) return;

                    // 用 Spline::ClosestPoint（内部逐段采样）
                    Math::Point3 closest = sp_.ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, SnapRadiusPx, bestDist, best, T, id);
                }
            });

        return best;
    }

    // =========================================================================
    // TryGrid
    // =========================================================================
    SnapResult SnapEngine::TryGrid(const Math::Point2& sp, const Camera& cam) const
    {
        Math::Point3 w = cam.ScreenToWorld(sp.x, sp.y);
        return
        {
            SnapResult::Type::Grid,
            {
                std::round(w.x / GridSize) * GridSize,
                std::round(w.y / GridSize) * GridSize,
                0.0
            },
            Object::InvalidID
        };
    }

}
