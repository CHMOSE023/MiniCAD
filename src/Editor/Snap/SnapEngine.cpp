#include "SnapResult.h"
#include "SnapEngine.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
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
    // ─── 主入口，返回第一个有效结果─────────────────────────────────────────────────────────────── 
    SnapResult SnapEngine::Query(const Math::Point2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        if (EnableEndpoint) { auto r = TryEndpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableMidpoint) { auto r = TryMidpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
		if (EnableQuadrant) { auto r = TryQuadrant(sp, scene, cam, exclude); if (r.IsValid()) return r; } // 象限点捕捉放在最近点前面，优先捕捉圆的特征点
        if (EnableNearest)  { auto r = TryNearest (sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableGrid)     return TryGrid(sp, cam);
        return {};
    }

    // ─── Endpoint ─────────────────────────────────────────────────────────────
    SnapResult SnapEngine::TryEndpoint(const Math::Point2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) 
                    return;
                  
                if (obj.IsKindOf<PointEntity>())
                {
                    auto* point = static_cast<const PointEntity*>(&obj);
                    if (!point) return;

                    auto&  p = point->GetPoint();
                    double d = Math::Distance(sp, cam.WorldToScreen(p.Position));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Endpoint, p.Position, obj.GetID() }; 
                    }
                }

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    for (const Math::Point3& wp : { line->GetLine().Start, line->GetLine().End })
                    {
                        double d = Math::Distance(sp, cam.WorldToScreen(wp));
                        if (d < SnapRadiusPx && d < bestDist)
                        {
                            bestDist = d;
                            best = { SnapResult::Type::Endpoint, wp, obj.GetID() }; 
                        }
                    }
                }

                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto* rectangleEntity = static_cast<const RectangleEntity*>(&obj);
                    if (!rectangleEntity) return;

                    const auto& rect = rectangleEntity->GetRectangle();

                    for (const auto& p : { rect.P1, rect.P2, rect.P3, rect.P4 })
                    {
                        double d = Math::Distance(sp, cam.WorldToScreen(p));
                        if (d < SnapRadiusPx && d < bestDist)
                        {
                            bestDist = d;
                            best = { SnapResult::Type::Endpoint, p, obj.GetID() };
                        }
                    } 
                }

                // ── 圆心捕捉 ──────────────────────────────────────────
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto* circle = static_cast<const CircleEntity*>(&obj);
                    if (!circle) return;

                    const Math::Point3& center = circle->GetCircle().Center;
                    double d = Math::Distance(sp, cam.WorldToScreen(center));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Endpoint, center, obj.GetID() }; 
                    }
                }
            });

        return best;
    }

    // ─── Midpoint ─────────────────────────────────────────────────────────────
    SnapResult SnapEngine::TryMidpoint(const Math::Point2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    auto& L = line->GetLine();
                    Math::Point3 mid = Math::Midpoint(L.Start, L.End);

                    double d = Math::Distance(sp, cam.WorldToScreen(mid));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Midpoint, mid, obj.GetID() };
                    }
                }


                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto* rectangleEntity = static_cast<const RectangleEntity*>(&obj);
                    if (!rectangleEntity) return;

                    const auto& rect = rectangleEntity->GetRectangle();


                    

                    for (const auto& p : { rect.P1, rect.P2, rect.P3, rect.P4 })
                    {
                        double d = Math::Distance(sp, cam.WorldToScreen(p));
                        if (d < SnapRadiusPx && d < bestDist)
                        {
                            bestDist = d;
                            best = { SnapResult::Type::Midpoint, p, obj.GetID() };
                        }
                    }
                }
            });

        return best;
    }

    // ─── Nearest ──────────────────────────────────────────────────────────────
    SnapResult SnapEngine::TryNearest(const Math::Point2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        Math::Point3 worldMouse = cam.ScreenToWorld(sp.x, sp.y);

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    auto& L = line->GetLine();
                    Math::Point3 closest = Math::ClosestPointOnSegment(worldMouse, L.Start, L.End);

                    double d = Math::Distance(sp, cam.WorldToScreen(closest));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Nearest, closest, obj.GetID() };
                    }
                }

				// 圆上最近点：鼠标世界坐标 → 圆心方向单位向量 → 投影到圆上
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto* circle = static_cast<const CircleEntity*>(&obj);
                    if (!circle) return;

                    const Math::Point3& c = circle->GetCircle().Center;
                    const double        r = circle->GetCircle().Radius;

                    // 鼠标世界坐标 → 圆心方向单位向量 → 投影到圆上
                    double dx = worldMouse.x - c.x;
                    double dy = worldMouse.y - c.y;
                    double len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-10) return;   // 鼠标恰好在圆心，跳过

                    Math::Point3 onCircle =
                    {
                        c.x + r * (dx / len),
                        c.y + r * (dy / len),
                        c.z
                    };

                    double d = Math::Distance(sp, cam.WorldToScreen(onCircle));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Nearest, onCircle, obj.GetID() };
                    }
                }
            });

        return best;
    }
    SnapResult SnapEngine::TryQuadrant(const Math::Point2& sp, const Scene& scene, const Camera& cam, const std::unordered_set<Object::ObjectID>& exclude) const

    {
        SnapResult best;
        double bestDist = std::numeric_limits<double>::max();

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;
                if (!obj.IsKindOf<CircleEntity>()) return;

                auto* circle = static_cast<const CircleEntity*>(&obj);
                if (!circle) return;

                const Math::Point3& c = circle->GetCircle().Center;
                const double        r = circle->GetCircle().Radius;

                // 四个象限点：0° 90° 180° 270°
                const Math::Point3 quadrants[4] =
                {
                    { c.x + r, c.y,     c.z },   // 右
                    { c.x,     c.y + r, c.z },   // 上
                    { c.x - r, c.y,     c.z },   // 左
                    { c.x,     c.y - r, c.z }    // 下
                };

                for (const Math::Point3& qp : quadrants)
                {
                    double d = Math::Distance(sp, cam.WorldToScreen(qp));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Quadrant, qp, obj.GetID() };
                    }
                }
            });

        if (best.IsValid())
        {

            printf(" SnapResult::Type::Quadrant\n");
        }


        return best;
    }

    // ─── Grid ─────────────────────────────────────────────────────────────────
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

