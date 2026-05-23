#pragma once
#include <vector>
#include "Render/VertexTypes.hpp"
#include "Camera.h"   
#include "Core/Math/PackedTypes.hpp"

namespace MiniCAD
{
    class Axis
    {
    public:
        std::vector<Vertex_P3_C4> BuildAxis(const Camera& camera, bool showAxis,float screenWidth, float screenHeight)
        {
            std::vector<Vertex_P3_C4>   verts = {};

            Math::Float4 gridColor = { 0.15f, 0.20f, 0.25f, 1.0f };
            Math::Float4 gridColor5 = { 0.20f, 0.30f, 0.40f, 1.0f };

			auto s2w = camera.ScreenToWorld(0, 0);  

            Math::Float3 worldTL =
            {
                static_cast<float>(s2w.x),
                static_cast<float>(s2w.y), 
                static_cast<float>(s2w.z)
            };

            s2w = camera.ScreenToWorld(screenWidth, screenHeight);

            Math::Float3 worldBR =
            {
                static_cast<float>(s2w.x),
                static_cast<float>(s2w.y),
                static_cast<float>(s2w.z)
            };

            float worldLeft = worldTL.x;
            float worldRight = worldBR.x;
            float worldTop = worldTL.y;
            float worldBottom = worldBR.y;

            float pixelsPerUnit = screenWidth / (worldRight - worldLeft);
            float rawStep = 60.0f / pixelsPerUnit;
            float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
            float normalized = rawStep / magnitude;

            float step;
            if (normalized < 2.0f) step = magnitude;
            else if (normalized < 5.0f) step = magnitude * 2.0f;
            else                        step = magnitude * 5.0f;

            if (showAxis)
            {
                // 原点 X 轴正半轴（红）
				auto w2s = camera.WorldToScreen({ 0, 0, 0 });
                Math::Float2 origin = { static_cast<float>(w2s.x), static_cast<float>(w2s.y) };
                if (origin.x < screenWidth && origin.y >= 0 && origin.y <= screenHeight)
                {
                    verts.push_back({ {origin.x,    origin.y, 0}, {0.8f, 0.2f, 0.2f, 1} });
                    verts.push_back({ {screenWidth, origin.y, 0}, {0.8f, 0.2f, 0.2f, 1} });
                }

                // 原点 Y 轴正半轴（绿）
                if (origin.y > 0 && origin.x >= 0 && origin.x <= screenWidth)
                {
                    verts.push_back({ {origin.x, origin.y, 0}, {0.2f, 0.8f, 0.2f, 1} });
                    verts.push_back({ {origin.x, 0,        0}, {0.2f, 0.8f, 0.2f, 1} });
                }  

                
            }
            return verts;
        }

    };
}
