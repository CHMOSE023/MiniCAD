#pragma once
#include <vector>
#include "Camera.h"   
#include "Render/VertexTypes.hpp"
#include "Core/Math/PackedTypes.hpp"
namespace MiniCAD
{
    class Gizmo
    {
    public:
        std::vector<Vertex_P3_C4> BuildGizmo(const Camera& camera, bool showGizmo, float screenWidth, float screenHeight)
        {
            std::vector<Vertex_P3_C4>   verts = {};

            Math::Float4 gridColor  = { 0.15f, 0.20f, 0.25f, 1.0f };
            Math::Float4 gridColor5 = { 0.20f, 0.30f, 0.40f, 1.0f };

            auto s2w = camera.ScreenToWorld(0, 0);
            Math::Float3 worldTL = { static_cast<float>(s2w.x), static_cast<float>(s2w.y), static_cast<float>(s2w.z) };
            s2w = camera.ScreenToWorld(screenWidth, screenHeight);
            Math::Float3 worldBR = { static_cast<float>(s2w.x), static_cast<float>(s2w.y), static_cast<float>(s2w.z) };

            float worldLeft   = worldTL.x;
            float worldRight  = worldBR.x;
            float worldTop    = worldTL.y;
            float worldBottom = worldBR.y;

            float pixelsPerUnit = screenWidth / (worldRight - worldLeft);
            float rawStep       = 60.0f / pixelsPerUnit;
            float magnitude     = std::pow(10.0f, std::floor(std::log10(rawStep)));
            float normalized    = rawStep / magnitude;

            float step;
            if (normalized < 2.0f) step = magnitude;
            else if (normalized < 5.0f) step = magnitude * 2.0f;
            else                        step = magnitude * 5.0f;

            if (showGizmo && !camera.IsPlanView())
            {
                BuildTripod(camera, screenWidth, screenHeight, verts);
                return verts;
            }

            if (showGizmo)
            {

				auto w2s = camera.WorldToScreen({ 0, 0, 0 });
                // 坐标轴图标
                Math::Float2 origin   = { static_cast<float>(w2s.x), static_cast<float>(w2s.y) };
                Math::Float4 color    = { 1.0f, 1.0f, 1.0f, 1.0f };
                float len       = 50.0f;
                float boxSize   = 5.0f;
                float s         = 15.0f;

                bool originVisible = origin.x >= 0 && origin.x <= screenWidth
                    && origin.y >= 0 && origin.y <= screenHeight;
                float ax = originVisible ? origin.x : 30.0f;
                float ay = originVisible ? origin.y : screenHeight - 30.0f;

                // 原点小方框
                verts.push_back({ {ax - boxSize, ay - boxSize, 0}, color });
                verts.push_back({ {ax + boxSize, ay - boxSize, 0}, color });
                verts.push_back({ {ax + boxSize, ay - boxSize, 0}, color });
                verts.push_back({ {ax + boxSize, ay + boxSize, 0}, color });
                verts.push_back({ {ax + boxSize, ay + boxSize, 0}, color });
                verts.push_back({ {ax - boxSize, ay + boxSize, 0}, color });
                verts.push_back({ {ax - boxSize, ay + boxSize, 0}, color });
                verts.push_back({ {ax - boxSize, ay - boxSize, 0}, color });

                // X 轴
                verts.push_back({ {ax,       ay, 0}, color });
                verts.push_back({ {ax + len, ay, 0}, color });
                // 手绘 X
                float xx = ax + len + 4.0f, xy = ay - s;
                verts.push_back({ {xx,     xy,     0}, color });
                verts.push_back({ {xx + s, xy + s, 0}, color });
                verts.push_back({ {xx + s, xy,     0}, color });
                verts.push_back({ {xx,     xy + s, 0}, color });

                // Y 轴
                verts.push_back({ {ax, ay,       0}, color });
                verts.push_back({ {ax, ay - len, 0}, color });
                // 手绘 Y
                float yx = ax + 4.0f, yy = ay - len - s - 4.0f;
                verts.push_back({ {yx,           yy,           0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s,       yy,           0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s / 2,   yy + s,       0}, color });

                
            }
            return verts;
        }

    private:
        // 三维视图（M6）：原点在屏幕内时画在原点，否则画在左下角；X / Y / Z 三根轴按视图方向投影，端部标字母
        static void BuildTripod(const Camera& camera, float screenWidth, float screenHeight, std::vector<Vertex_P3_C4>& verts)
        {
            const auto o = camera.WorldToScreen({ 0, 0, 0 });
            const bool originVisible = o.x >= 0 && o.x <= screenWidth && o.y >= 0 && o.y <= screenHeight;
            const float ax = originVisible ? static_cast<float>(o.x) : 50.0f;
            const float ay = originVisible ? static_cast<float>(o.y) : screenHeight - 50.0f;
            const Math::Float4 colors[3] = { { 1.0f, 0.35f, 0.35f, 1.0f }, { 0.35f, 1.0f, 0.35f, 1.0f }, { 0.45f, 0.6f, 1.0f, 1.0f } };
            const Math::Vec3 r = camera.GetRight(), u = camera.GetUp();
            const float len = 45.0f, s = 6.0f;
            auto line = [&](float x0, float y0, float x1, float y1, const Math::Float4& c) {
                verts.push_back({ { x0, y0, 0 }, c });
                verts.push_back({ { x1, y1, 0 }, c });
            };
            for (int i = 0; i < 3; ++i)
            {
                // 世界轴方向在屏幕上的投影（屏幕 y 向下）
                const double wx = i == 0 ? 1 : 0, wy = i == 1 ? 1 : 0, wz = i == 2 ? 1 : 0;
                const float dx = static_cast<float>(wx * r.x + wy * r.y + wz * r.z);
                const float dy = static_cast<float>(-(wx * u.x + wy * u.y + wz * u.z));
                const float ex = ax + dx * len, ey = ay + dy * len;
                line(ax, ay, ex, ey, colors[i]);
                if (dx * dx + dy * dy < 0.01f)
                    continue;   // 指向屏幕里外：不标字母
                const float lx = ax + dx * (len + 12.0f), ly = ay + dy * (len + 12.0f);
                if (i == 0)
                {
                    line(lx - s, ly - s, lx + s, ly + s, colors[i]);
                    line(lx + s, ly - s, lx - s, ly + s, colors[i]);
                }
                else if (i == 1)
                {
                    line(lx - s, ly - s, lx, ly, colors[i]);
                    line(lx + s, ly - s, lx, ly, colors[i]);
                    line(lx, ly, lx, ly + s, colors[i]);
                }
                else
                {
                    line(lx - s, ly - s, lx + s, ly - s, colors[i]);
                    line(lx + s, ly - s, lx - s, ly + s, colors[i]);
                    line(lx - s, ly + s, lx + s, ly + s, colors[i]);
                }
            }
        }
    };
}
          
