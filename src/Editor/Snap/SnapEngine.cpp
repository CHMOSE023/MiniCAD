#include "SnapResult.h"
#include "SnapEngine.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Object/Object.hpp"
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
        if (EnableNearest)  { auto r = TryNearest(sp, scene, cam, exclude); if (r.IsValid()) return r; }
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
                if (exclude.contains(obj.GetID())) return;

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

                if (obj.IsKindOf<PointEntity>())
                {
                    auto* point = static_cast<const PointEntity*>(&obj);
                    if (!point) return;

                    auto& p = point->GetPoint();
                    double d = Math::Distance(sp, cam.WorldToScreen(p.Position));
                    if (d < SnapRadiusPx && d < bestDist)
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Endpoint, p.Position, obj.GetID() };
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
            });

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

