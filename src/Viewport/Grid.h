#pragma once 
#include "Camera.h"   
#include "Render/VertexTypes.hpp"
#include "Core/Math/PackedTypes.hpp"
#include <vector>
#include <cmath> 
namespace MiniCAD
{
    class Grid
    {
    public:
        std::vector<Vertex_P3_C4> BuildGrid(const Camera& camera, bool showGrid,  float screenWidth, float screenHeight)
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

            if (showGrid)
            {
                // 垂直网格线
                float startX = std::floor(worldLeft / step) * step;
                float endX = std::ceil(worldRight / step) * step;
                for (float wx = startX; wx <= endX; wx += step)
                {
                    int  index = (int)std::round(wx / step);
                    auto color = (index % 5 == 0) ? gridColor5 : gridColor; 
                    auto top   = camera.WorldToScreen({ wx, worldTop, 0 }); 
                     
                    verts.push_back({ {static_cast<float>(top.x), 0,            0}, color });
                    verts.push_back({ {static_cast<float>(top.x), screenHeight, 0}, color });
                }

                // 水平网格线
                float startY = std::floor(worldBottom / step) * step;
                float endY   = std::ceil(worldTop / step) * step;
                for (float wy = startY; wy <= endY; wy += step)
                {
                    int  index = (int)std::round(wy / step);
                    auto color = (index % 5 == 0) ? gridColor5 : gridColor;

                    auto left  = camera.WorldToScreen({ worldLeft, wy, 0 });

                    verts.push_back({ {0,           static_cast<float>(left.y), 0}, color });
                    verts.push_back({ {screenWidth, static_cast<float>(left.y), 0}, color });
                }
                return verts;
            
            }

            return verts;

        }

    };
}
