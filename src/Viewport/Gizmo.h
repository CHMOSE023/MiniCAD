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

    };
}
          
