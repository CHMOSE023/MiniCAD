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

            // 三维视图（M6）：栅格画在 z = 0 平面上（世界坐标，随视图透视缩短），间距按目标点附近的像素大小取（约 60 像素一格，每 5 格加深）。
        // 范围为屏幕四角投到平面上的区域，限制在目标点周围 4 倍视图高度以内（接近水平的视线投得很远）；
        // 视线与 XY 平面接近平行时平面看成一条线，不画
        static std::vector<Vertex_P3_C4> BuildGrid3D(const Camera& camera, float screenWidth, float screenHeight)
        {
            std::vector<Vertex_P3_C4> verts;
            if (std::abs(camera.GetViewDir().z) < 0.05)
                return verts;
            const Math::Float4 gridColor  = { 0.15f, 0.20f, 0.25f, 1.0f };
            const Math::Float4 gridColor5 = { 0.20f, 0.30f, 0.40f, 1.0f };

            const double wpp   = camera.GetWorldPerPixel();
            const double limit = 4.0 * wpp * screenHeight;
            const Math::Point3 t = camera.GetTarget();
            double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
            for (int i = 0; i < 4; ++i)
            {
                const Math::Point3 p = camera.ScreenToWorld((i & 1) ? double(screenWidth) : 0.0, (i & 2) ? double(screenHeight) : 0.0);
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
            x0 = std::max(x0, t.x - limit); x1 = std::min(x1, t.x + limit);
            y0 = std::max(y0, t.y - limit); y1 = std::min(y1, t.y + limit);
            if (!(x0 < x1 && y0 < y1))
                return verts;

            const double rawStep    = 60.0 * wpp;
            const double magnitude  = std::pow(10.0, std::floor(std::log10(rawStep)));
            const double normalized = rawStep / magnitude;
            const double step = normalized < 2.0 ? magnitude : (normalized < 5.0 ? magnitude * 2.0 : magnitude * 5.0);
            auto add = [&](double ax, double ay, double bx, double by, const Math::Float4& c) {
                verts.push_back({ { static_cast<float>(ax), static_cast<float>(ay), 0.0f }, c });
                verts.push_back({ { static_cast<float>(bx), static_cast<float>(by), 0.0f }, c });
            };
            const long long i0 = static_cast<long long>(std::floor(x0 / step)), i1 = static_cast<long long>(std::ceil(x1 / step));
            const long long j0 = static_cast<long long>(std::floor(y0 / step)), j1 = static_cast<long long>(std::ceil(y1 / step));
            if (i1 - i0 > 2000 || j1 - j0 > 2000)
                return verts;
            for (long long i = i0; i <= i1; ++i)
                add(i * step, j0 * step, i * step, j1 * step, i % 5 == 0 ? gridColor5 : gridColor);
            for (long long j = j0; j <= j1; ++j)
                add(i0 * step, j * step, i1 * step, j * step, j % 5 == 0 ? gridColor5 : gridColor);
            return verts;
        }
    };
}
