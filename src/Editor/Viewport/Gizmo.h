#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Camera.h"   
#include "Render/D3D11/Shader.h"  

namespace MiniCAD
{
    class Gizmo
    {
    public:
        std::vector<Vertex_P3_C4> BuildGizmo(const Camera& camera, bool showGizmo, float screenWidth, float screenHeight)
        {
            std::vector<Vertex_P3_C4>   verts = {};

            XMFLOAT4 gridColor = { 0.15f, 0.20f, 0.25f, 1.0f };
            XMFLOAT4 gridColor5 = { 0.20f, 0.30f, 0.40f, 1.0f };

            XMFLOAT3 worldTL = camera.ScreenToWorld(0, 0);
            XMFLOAT3 worldBR = camera.ScreenToWorld(screenWidth, screenHeight);

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
               
                // 坐标轴图标
                XMFLOAT2 origin = camera.WorldToScreen({ 0, 0, 0 });
                XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
                float len = 50.0f;
                float boxSize = 5.0f;
                float s = 15.0f;

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
                verts.push_back({ {yx,         yy,         0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s,     yy,         0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s / 2,   yy + s / 2,   0}, color });
                verts.push_back({ {yx + s / 2,   yy + s,     0}, color });

                
            }
            return verts;
        }

    };
}
          