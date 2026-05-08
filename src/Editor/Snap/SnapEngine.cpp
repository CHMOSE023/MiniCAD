#include "SnapResult.h"
#include "SnapEngine.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Scene/Scene.h"
#include "Editor/Viewport/Camera.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>

using namespace DirectX;

namespace MiniCAD
{
    // ─── 内部工具 ─────────────────────────────────────────────────────────────
    static float Dist2D(const XMFLOAT2& a, const XMFLOAT2& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    static XMFLOAT3 ClosestPointOnSegment(const XMFLOAT3& p, const XMFLOAT3& a, const XMFLOAT3& b)
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float lenSq = dx * dx + dy * dy;

        if (lenSq < 1e-8f) return a;

        float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq, 0.f, 1.f);
        return { a.x + t * dx, a.y + t * dy, 0.f };
    }

    // ─── 主入口 ───────────────────────────────────────────────────────────────
    SnapResult SnapEngine::Query(const XMFLOAT2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        if (EnableEndpoint) { auto r = TryEndpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableMidpoint) { auto r = TryMidpoint(sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableNearest)  { auto r = TryNearest (sp, scene, cam, exclude); if (r.IsValid()) return r; }
        if (EnableGrid)     return TryGrid(sp, cam);
        return {};
    }

    // ─── Endpoint ─────────────────────────────────────────────────────────────
    SnapResult SnapEngine::TryEndpoint(const XMFLOAT2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        float bestDist = FLT_MAX;

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;  //  跳过选中对象

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    for (const XMFLOAT3& wp : { line->GetLine().Start, line->GetLine().End })
                    {
                        float d = Dist2D(sp, cam.WorldToScreen(wp));
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

                    float d = Dist2D(sp, cam.WorldToScreen(p.Position));  // 计算距离
                    if (d < SnapRadiusPx && d < bestDist)                 // 双重判断
                    {
                        bestDist = d;
                        best = { SnapResult::Type::Endpoint, p.Position, obj.GetID() };
                    }  
                } 
            });

        return best;
    }

    // ─── Midpoint ─────────────────────────────────────────────────────────────
    SnapResult SnapEngine::TryMidpoint(const XMFLOAT2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        float bestDist = FLT_MAX;

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;  // 跳过选中对象

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    auto& L = line->GetLine();
                    XMFLOAT3 mid = { (L.Start.x + L.End.x) * 0.5f, (L.Start.y + L.End.y) * 0.5f, 0.f };

                    float d = Dist2D(sp, cam.WorldToScreen(mid));
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
    SnapResult SnapEngine::TryNearest(const XMFLOAT2& sp, const Scene& scene, const Camera& cam,
        const std::unordered_set<Object::ObjectID>& exclude) const
    {
        SnapResult best;
        float bestDist = FLT_MAX;

        XMFLOAT3 worldMouse = cam.ScreenToWorld(sp.x, sp.y);

        scene.ForEachObject([&](const Object& obj)
            {
                if (exclude.contains(obj.GetID())) return;  // 跳过选中对象

                if (obj.IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(&obj);
                    if (!line) return;

                    auto& L = line->GetLine();
                    XMFLOAT3 closest = ClosestPointOnSegment(worldMouse, L.Start, L.End);

                    float d = Dist2D(sp, cam.WorldToScreen(closest));
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
    SnapResult SnapEngine::TryGrid(const XMFLOAT2& sp, const Camera& cam) const
    {
        XMFLOAT3 w = cam.ScreenToWorld(sp.x, sp.y);
        return
        {
            SnapResult::Type::Grid,
            {
                std::round(w.x / GridSize) * GridSize,
                std::round(w.y / GridSize) * GridSize,
                0.f
            },
            Object::InvalidID
        };
    }

}  
