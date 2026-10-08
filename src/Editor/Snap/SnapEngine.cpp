#include "SnapResult.h"
#include "SnapEngine.h"
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
#include "Core/Entity/ICurveEntity.hpp"
#include "Core/GeomKernel/CurveIntersect.hpp"
#include "Document/FeaturePoints.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include "Scene/Scene.h"
#include "Viewport/Camera.h"
#include "Core/Math/MathUtils.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include <unordered_set>

namespace MiniCAD
{
    // =========================================================================
    // 内部工具：屏幕距离比较辅助
    // =========================================================================
    namespace
    {
        // 隐藏图层(或自身不可见)的实体不参与捕捉;锁定图层仍可捕捉(同 AutoCAD)。
        inline bool IsSnapVisible(const Scene& scene, const Object& obj)
        {
            if (!obj.IsKindOf<Entity>()) return true;

            const auto& attr = static_cast<const Entity&>(obj).GetAttr();
            if (!attr.Visible) return false;

            if (const Layer* layer = scene.GetLayerManager().GetLayer(attr.LayerId))
                if (!layer->IsVisible())
                    return false;

            return true;
        }

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
        const std::unordered_set<Object::ObjectID>& exclude,
        const Math::Point3* fromPoint,
        const std::vector<Object::ObjectID>* candidates) const
    {
        // 候选集仅在本次 Query 执行期间生效（各 Try* 经 ForEachSnapObject 消费）
        m_queryCandidates = candidates;

        // ── 点类捕捉（端点/中点/交点/象限/垂足）：同一孔径内按「捕捉点到
        //    光标的屏幕距离」取最近者，距离相同时按上述顺序优先 ──
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        auto consider = [&](SnapResult r)
        {
            if (!r.IsValid()) return;
            double d = Math::Distance(sp, cam.WorldToScreen(r.WorldPos));
            if (d < bestDist) { bestDist = d; best = r; }
        };

        if (IsModeEnabled(SnapMode::Endpoint))     consider(TryEndpoint(sp, scene, cam, exclude));
        if (IsModeEnabled(SnapMode::Midpoint))     consider(TryMidpoint(sp, scene, cam, exclude));
        if (IsModeEnabled(SnapMode::Intersection)) consider(TryIntersection(sp, scene, cam, exclude));
        if (IsModeEnabled(SnapMode::Quadrant))     consider(TryQuadrant(sp, scene, cam, exclude));
        if (IsModeEnabled(SnapMode::Perpendicular) && fromPoint)
            consider(TryPerpendicular(sp, scene, cam, exclude, *fromPoint));

        // ── 最近点：仅在点类捕捉均未命中时兜底(光标贴近曲线即生效) ──
        if (!best.IsValid() && IsModeEnabled(SnapMode::Nearest))
            best = TryNearest(sp, scene, cam, exclude);

        m_queryCandidates = nullptr;   // 候选集指针仅本次查询有效

        if (best.IsValid()) return best;
        if (IsModeEnabled(SnapMode::Grid)) return TryGrid(sp, cam);
        return {};
    }

    // =========================================================================
    // TryEndpoint / TryMidpoint / TryQuadrant
    //
    // 各实体的端点 / 中点 / 象限点由 CollectFeaturePoints 枚举（Document/FeaturePoints），
    // 关联标注按同一套枚举的序号记录关联点。
    // =========================================================================
    SnapResult SnapEngine::TryFeature(const Math::Point2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude, int kind, SnapResult::Type type) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();
        std::vector<Math::Point3> pts;

        ForEachSnapObject(scene, [&](const Object& obj)
            {
                if (exclude.contains(obj.GetID()) || !IsSnapVisible(scene, obj)) return;
                if (!obj.IsKindOf<Entity>()) return;

                pts.clear();
                CollectFeaturePoints(static_cast<const Entity&>(obj), static_cast<FeatureKind>(kind), pts);
                for (const auto& p : pts)
                    TryUpdateBest(p, sp, cam, m_snapRadiusPx, bestDist, best, type, obj.GetID());
            });

        return best;
    }

    SnapResult SnapEngine::TryEndpoint(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        return TryFeature(sp, scene, cam, exclude, static_cast<int>(FeatureKind::Endpoint), SnapResult::Type::Endpoint);
    }

    SnapResult SnapEngine::TryMidpoint(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        return TryFeature(sp, scene, cam, exclude, static_cast<int>(FeatureKind::Midpoint), SnapResult::Type::Midpoint);
    }

    SnapResult SnapEngine::TryQuadrant(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        return TryFeature(sp, scene, cam, exclude, static_cast<int>(FeatureKind::Quadrant), SnapResult::Type::Quadrant);
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
    SnapResult SnapEngine::TryNearest(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        Math::Point3 worldMouse = cam.ScreenToWorld(sp.x, sp.y);

        ForEachSnapObject(scene, [&](const Object& obj)
            {
                if (exclude.contains(obj.GetID()) || !IsSnapVisible(scene, obj)) return;

                const auto id = obj.GetID();
                const auto T = SnapResult::Type::Nearest;

                // ── Line ───────────────────────────────────────────────────────
                if (obj.IsKindOf<LineEntity>())
                {
                    auto* e = static_cast<const LineEntity*>(&obj);
                    auto& L = e->GetLine();
                    Math::Point3 closest = Math::ClosestPointOnSegment(worldMouse, L.Start, L.End);
                    TryUpdateBest(closest, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
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
                            sp, cam, m_snapRadiusPx, bestDist, best, T, id);
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
                    TryUpdateBest(onCircle, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── Arc：弧上最近点（考虑角度范围）────────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto* e = static_cast<const ArcEntity*>(&obj);
                    Math::Point3 closest = e->GetArc().ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── Ellipse：椭圆周最近点（牛顿迭代）─────────────────────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto* e = static_cast<const EllipseEntity*>(&obj);
                    Math::Point3 closest = e->GetEllipse().ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── Polyline：Tessellate 后逐段最近点 ────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto* e = static_cast<const PolylineEntity*>(&obj);
                    const auto& pl = e->GetPolyline();

                    // 直接用 Polyline 的几何最近点查询（内部处理弧段）
                    Math::Point3 closest = pl.ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── Spline：Tessellate 后逐段最近点 ──────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto* e = static_cast<const SplineEntity*>(&obj);
                    const auto& sp_ = e->GetSpline();
                    if (!sp_.IsValid()) return;

                    // 用 Spline::ClosestPoint（内部逐段采样）
                    Math::Point3 closest = sp_.ClosestPoint(worldMouse);
                    TryUpdateBest(closest, sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── XLine：无限直线上最近点（参数不设限）──────────────────────
                if (obj.IsKindOf<XLineEntity>())
                {
                    auto* e = static_cast<const XLineEntity*>(&obj);
                    const XLine& xl = e->GetXLine();
                    if (!xl.IsValid()) return;
                    TryUpdateBest(xl.ClosestPoint(worldMouse), sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }

                // ── Ray：半无限直线上最近点（参数 clamp 到 t ≥ 0）─────────────
                if (obj.IsKindOf<RayEntity>())
                {
                    auto* e = static_cast<const RayEntity*>(&obj);
                    const XLine& r = e->GetRay();
                    if (!r.IsValid()) return;
                    double t = std::max(0.0, r.ProjectParam(worldMouse));
                    TryUpdateBest(r.PointAt(t), sp, cam, m_snapRadiusPx, bestDist, best, T, id);
                }
            });

        return best;
    }

    // =========================================================================
    // TryIntersection
    //
    // 通过 ICurve 抽象统一处理所有曲线两两求交（Geom::IntersectCurves），不再按
    // 类型分派。先按「曲线到光标的屏幕距离 ≤ 捕捉半径」剔除候选 —— 交点若落在
    // 捕捉半径内，则两条母曲线都必经过光标附近，故该剔除不漏解又把两两规模压到极小。
    // =========================================================================
    SnapResult SnapEngine::TryIntersection(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const
    {
        const Math::Point3 worldMouse = cam.ScreenToWorld(sp.x, sp.y);

        struct Cand { Object::ObjectID id; std::unique_ptr<ICurve> curve; };
        std::vector<Cand> cands;

        ForEachSnapObject(scene, [&](const Object& obj)
            {
                if (exclude.contains(obj.GetID()) || !IsSnapVisible(scene, obj))   return;
                if (!obj.IsKindOf<Entity>())         return;

                const ICurveEntity* ce = static_cast<const Entity*>(&obj)->AsCurveEntity();
                if (!ce) return;

                auto curve = ce->MakeCurve();
                Math::Point3 onCurve = curve->ClosestPoint(worldMouse);
                if (Math::Distance(sp, cam.WorldToScreen(onCurve)) > m_snapRadiusPx) return;

                cands.push_back({ obj.GetID(), std::move(curve) });
            });

        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        for (size_t i = 0; i < cands.size(); ++i)
            for (size_t j = i + 1; j < cands.size(); ++j)
            {
                auto pts = Geom::IntersectCurves(*cands[i].curve, *cands[j].curve);
                for (const auto& p : pts)
                {
                    const double before = bestDist;
                    TryUpdateBest(p, sp, cam, m_snapRadiusPx, bestDist, best,
                                  SnapResult::Type::Intersection, cands[i].id);
                    if (bestDist < before) best.SourceID2 = cands[j].id;
                }
            }

        return best;
    }

    // =========================================================================
    // TryPerpendicular
    //
    // 自基点 fromPoint（工具锚点 / 活动夹点）向所指曲线作垂线，捕捉垂足。
    // 对直线 / 射线 / 构造线 / 圆 / 弧 / 椭圆，「基点到曲线的最近点」即法向垂足，
    // 故复用 ICurve::ClosestPoint(fromPoint)。
    //
    // 判定与其他点类捕捉一致：光标须贴近垂足点本身（孔径内）才命中——
    // 不做「指到曲线任意处即给出远处垂足」的延伸垂足，避免过度灵敏、
    // 也避免与最近点捕捉互相抢占。
    // =========================================================================
    SnapResult SnapEngine::TryPerpendicular(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude, const Math::Point3& fromPoint) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        ForEachSnapObject(scene, [&](const Object& obj)
            {
                if (exclude.contains(obj.GetID()) || !IsSnapVisible(scene, obj))   return;
                if (!obj.IsKindOf<Entity>())         return;

                const ICurveEntity* ce = static_cast<const Entity*>(&obj)->AsCurveEntity();
                if (!ce) return;

                Math::Point3 foot = ce->MakeCurve()->ClosestPoint(fromPoint);
                TryUpdateBest(foot, sp, cam, m_snapRadiusPx, bestDist, best,
                              SnapResult::Type::Perpendicular, obj.GetID());
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
                std::round(w.x / m_gridSize) * m_gridSize,
                std::round(w.y / m_gridSize) * m_gridSize,
                0.0
            },
            Object::InvalidID
        };
    }

}
