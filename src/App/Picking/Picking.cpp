#include "Picking.h"
#include "Render/Viewport/Camera.h"
#include "App/Scene/Scene.h"

using namespace DirectX;

namespace MiniCAD
{
    // 点选：返回离相机最近的对象ID
    Object::ObjectID Picking::PickPoint(const Scene& scene, const IViewContext& view, float mouseX, float mouseY) const
    {
        // 屏幕坐标 → 世界坐标
        XMFLOAT3 worldPos = view.ScreenToWorld(mouseX, mouseY);
        XMVECTOR mouse = XMVectorSet(worldPos.x, worldPos.y, 0.f, 0.f);


        // 拾取容忍阈值（世界空间单位）
        // 需要根据缩放换算：屏幕 5 像素对应多少世界单位
        constexpr float kPixelTolerance = 5.f;
        XMFLOAT3 p0 = view.ScreenToWorld(mouseX, mouseY);
        XMFLOAT3 p1 = view.ScreenToWorld(mouseX + kPixelTolerance, mouseY);
        float tolerance = fabsf(p1.x - p0.x); // 5 像素对应的世界单位

        Object::ObjectID bestID = Object::InvalidID;
        float            bestDist = FLT_MAX;

        for (auto id : scene.GetAllIDs())
        {
            if (!scene.IsEntitySelectable(id))
                continue;

            const auto* obj = scene.GetEntity(id);
            if (!obj) continue;

            if (obj->IsKindOf<LineEntity>())
            {
                const auto* line = static_cast<const LineEntity*>(obj);
                const auto& geo = line->GetLine();

                float dist = PointToSegmentDistance(
                    mouse,
                    XMVectorSet(geo.Start.x, geo.Start.y, 0.f, 0.f),
                    XMVectorSet(geo.End.x, geo.End.y, 0.f, 0.f));

                if (dist < tolerance && dist < bestDist)
                {
                    bestDist = dist;
                    bestID = id;
                }
            }
        }

        return bestID;
    }

    // 点到线段的最短距离
    float Picking::PointToSegmentDistance(XMVECTOR p, XMVECTOR a, XMVECTOR b) const
    {
        XMVECTOR ab = XMVectorSubtract(b, a);
        XMVECTOR ap = XMVectorSubtract(p, a);

        float lenSq = XMVectorGetX(XMVector2Dot(ab, ab));
        if (lenSq < 1e-10f)
        {
            // 线段退化为点
            return XMVectorGetX(XMVector2Length(ap));
        }

        // t = dot(ap, ab) / dot(ab, ab)，clamp 到 [0,1]
        float t = XMVectorGetX(XMVector2Dot(ap, ab)) / lenSq;
        t = std::clamp(t, 0.f, 1.f);

        // 最近点
        XMVECTOR closest = XMVectorAdd(a, XMVectorScale(ab, t));
        XMVECTOR diff = XMVectorSubtract(p, closest);

        return XMVectorGetX(XMVector2Length(diff));
    }

    // ── Cohen-Sutherland 区域码 ────────────────────────────────────
    // 判断线段是否与轴对齐矩形相交（用于交叉选）
    static bool SegmentIntersectsRect(
        float x0, float y0, float x1, float y1,   // 线段端点
        float rxMin, float ryMin, float rxMax, float ryMax)
    {
        // 区域码位掩码
        constexpr int LEFT = 1, RIGHT = 2, BOTTOM = 4, TOP = 8;

        auto code = [&](float x, float y) {
            int c = 0;
            if (x < rxMin) c |= LEFT;
            else if (x > rxMax) c |= RIGHT;
            if (y < ryMin) c |= BOTTOM;
            else if (y > ryMax) c |= TOP;
            return c;
            };

        int c0 = code(x0, y0);
        int c1 = code(x1, y1);

        while (true)
        {
            if (!(c0 | c1))   return true;   // 完全在矩形内
            if (c0 & c1)      return false;  // 完全在同侧外部

            // 选取在外部的端点，沿矩形边裁剪
            int c = c0 ? c0 : c1;
            float x, y;

            if (c & TOP)
            {
                x = x0 + (x1 - x0) * (ryMax - y0) / (y1 - y0);
                y = ryMax;
            }
            else if (c & BOTTOM)
            {
                x = x0 + (x1 - x0) * (ryMin - y0) / (y1 - y0);
                y = ryMin;
            }
            else if (c & RIGHT)
            {
                y = y0 + (y1 - y0) * (rxMax - x0) / (x1 - x0);
                x = rxMax;
            }
            else // LEFT
            {
                y = y0 + (y1 - y0) * (rxMin - x0) / (x1 - x0);
                x = rxMin;
            }

            if (c == c0) { x0 = x; y0 = y; c0 = code(x0, y0); }
            else { x1 = x; y1 = y; c1 = code(x1, y1); }
        }
    }

    // ── PickRect：双模式框选 ─────────────────────────────────────────
    // worldA = 按下时世界坐标，worldB = 松开时世界坐标
    //   worldB 在 worldA 右侧（worldB.x > worldA.x）→ 窗口选：两端点都在矩形内
    //   worldB 在 worldA 左侧（worldB.x < worldA.x）→ 交叉选：线段与矩形相交即可
    std::vector<Object::ObjectID> Picking::PickRect(
        const Scene& scene,
        const XMFLOAT2& worldA,
        const XMFLOAT2& worldB) const
    {
        // 判断框选方向
        const bool windowMode = worldB.x >= worldA.x;  // 从左到右 = 窗口选

        float rxMin = std::min(worldA.x, worldB.x);
        float rxMax = std::max(worldA.x, worldB.x);
        float ryMin = std::min(worldA.y, worldB.y);
        float ryMax = std::max(worldA.y, worldB.y);

        auto inRect = [&](const XMFLOAT3& p) {
            return p.x >= rxMin && p.x <= rxMax
                && p.y >= ryMin && p.y <= ryMax;
            };

        std::vector<ObjectID> result;

        for (auto id : scene.GetAllIDs())
        {
            if (!scene.IsEntitySelectable(id))
                continue;

            const auto* obj = scene.GetEntity(id);
            if (!obj) continue;

            if (obj->IsKindOf<LineEntity>())
            {
                const auto* line = static_cast<const LineEntity*>(obj);
                const auto& geo = line->GetLine();

                bool hit = false;
                if (windowMode)
                {
                    // 窗口选：两端点都必须在矩形内
                    hit = inRect(geo.Start) && inRect(geo.End);
                }
                else
                {
                    // 交叉选：线段与矩形任意相交即可
                    hit = SegmentIntersectsRect(
                        geo.Start.x, geo.Start.y,
                        geo.End.x, geo.End.y,
                        rxMin, ryMin, rxMax, ryMax);
                }

                if (hit)
                    result.push_back(id);
            }
        }

        return result;
    }
}
