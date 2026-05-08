#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Camera.h"   
#include "Render/D3D11/Shader.h"  

namespace MiniCAD
{
    class Axis
    {
    public:
        std::vector<Vertex_P3_C4> BuildAxis(const Camera& camera, bool showAxis,float screenWidth, float screenHeight)
        {
            std::vector<Vertex_P3_C4>   verts = {};

            XMFLOAT4 gridColor = { 0.15f, 0.20f, 0.25f, 1.0f };
            XMFLOAT4 gridColor5 = { 0.20f, 0.30f, 0.40f, 1.0f };

            XMFLOAT3 worldTL = camera.ScreenToWorld(0, 0);
            XMFLOAT3 worldBR = camera.ScreenToWorld(screenWidth, screenHeight);

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
                XMFLOAT2 origin = camera.WorldToScreen({ 0, 0, 0 });
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