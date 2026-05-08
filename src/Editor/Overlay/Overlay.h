#pragma once
#include <vector>
#include <memory>
#include <functional>
#include "Core/Object/Object.hpp"
#include "Render/D3D11/Shader.h" 
#include "Editor/Viewport/Viewport.h" 

namespace MiniCAD
{
    /// <summary>
    /// “非持久、仅用于显示”的临时几何数据
    /// </summary>
    class Overlay
    {
    public:
        Overlay(Viewport& viewport) 
            :m_viewport(viewport)
        {
        };

        // 清空
        void Clear()
        {
            m_lines.clear();
            m_points.clear();
        } 

        // ===== Line =====
        void AddLine(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT4& color)
        {
            m_lines.push_back({ a, b, color });
        }

        // ===== Point =====
        void AddPoint(const XMFLOAT3& p, const XMFLOAT4& color)
        {
            m_points.push_back({ p, color });
        }

        // ===== Export to GPU vertices =====
        void ToVertices(std::vector<Vertex_P3_C4>& out) const
        {
            out.reserve(out.size() + m_lines.size() * 2 + m_points.size());

            // Lines -> 2 vertices
            for (const auto& l : m_lines)
            {
                out.push_back({ l.a, l.color });
                out.push_back({ l.b, l.color });
            }

            // Points 
            for (const auto& pt : m_points)
            {
                // 生成圆
                const float radius   = 6.0f;
                const int   segments = 24;

                auto camera = m_viewport.GetCamera();

                auto point = camera.WorldToScreen(pt.p);

                for (int i = 0; i < segments; ++i)
                {
                    float a0 = (i / (float)segments) * DirectX::XM_2PI;
                    float a1 = ((i + 1) / (float)segments) * DirectX::XM_2PI;

                    XMFLOAT2 p0 = {
                        point.x + cosf(a0) * radius,
                        point.y + sinf(a0) * radius, 
                    };

                    XMFLOAT2 p1 = {
                         point.x + cosf(a1) * radius,
                         point.y + sinf(a1) * radius, 
                    }; 

                    out.push_back({ camera.ScreenToWorld(p0.x, p0.y) , pt.color });
                    out.push_back({ camera.ScreenToWorld(p1.x, p1.y), pt.color });
                }
            }
        }

        bool Empty() const
        {
            return m_lines.empty() && m_points.empty();
        }
         

    private:
        struct Line
        {
            XMFLOAT3 a;
            XMFLOAT3 b;
            XMFLOAT4 color;
        };

        struct Point
        {
            XMFLOAT3 p;
            XMFLOAT4 color;
        };

    private:
        std::vector<Line>  m_lines;
        std::vector<Point> m_points;

    private:

        Viewport& m_viewport;
    }; 
}