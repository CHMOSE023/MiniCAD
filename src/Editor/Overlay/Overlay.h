#pragma once
#include <vector>
#include <memory>
#include <functional>
#include "Core/Object/Object.hpp"
#include "Render/D3D11/Shader.h" 
#include "Editor/Viewport/Viewport.h" 
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
namespace MiniCAD
{
    /// <summary>
    /// “非持久、仅用于显示”的临时几何数据
    /// </summary>
    class Overlay
    {
    public:
        Overlay(Viewport& viewport) :m_viewport(viewport) {};

        // 清空
        void Clear()
        {
            m_lines.clear();
            m_points.clear();
        } 

        // ===== Line =====
        void AddLine(const Math::Point3& aa, const Math::Point3& bb, const Math::Color4& mcolor)
        {
            Float3 a =
            {
                static_cast<float>(aa.x),
                static_cast<float>(aa.y),
                static_cast<float>(aa.z),
            };

            Float3 b =
            {
                static_cast<float>(bb.x),
                static_cast<float>(bb.y),
                static_cast<float>(bb.z),
            };

            Float4 color =
            {
                static_cast<float>(mcolor.r),
                static_cast<float>(mcolor.g),
                static_cast<float>(mcolor.b),
                static_cast<float>(mcolor.a),
            };

            m_lines.push_back({ a, b, color });
        }

        // ===== Point =====
        void AddPoint(const  Math::Point3& p, const  Math::Color4& color)
        {
            Float3 fp =
            {
                static_cast<float>(p.x),
                static_cast<float>(p.y),
                static_cast<float>(p.z),
			};

            Float4 fcolor =
            {
                static_cast<float>(color.r),
                static_cast<float>(color.g),
                static_cast<float>(color.b),
                static_cast<float>(color.a),
			};

            m_points.push_back({ fp, fcolor });
        }

        // ===== Export to GPU vertices =====
        void ToVertices(std::vector<Vertex_P3_C4>& out) const
        {
            out.reserve(out.size() + m_lines.size() * 2 + m_points.size());
            double PI    = 3.14159265358979323846;
            double TwoPI = PI * 2.0;
            // Lines -> 2 vertices
            for (const auto& l : m_lines)
            {
                out.push_back({ {l.a.x, l.a.y, l.a.z}, {l.color.x, l.color.y, l.color.z, l.color.w} });
                out.push_back({ {l.b.x, l.b.y, l.b.z}, {l.color.x, l.color.y, l.color.z, l.color.w} });
            }

            // Points 
            for (const auto& pt : m_points)
            {
                // 生成圆
                const float radius   = 6.0f;
                const int   segments = 24;

                auto& camera = m_viewport.GetCamera();

                auto point = camera.WorldToScreen({ pt.p.x, pt.p.y, pt.p.z });

                for (int i = 0; i < segments; ++i)
                {
                    double a0 = (i / (float)segments)       * TwoPI;
                    double a1 = ((i + 1) / (float)segments) * TwoPI;
                      
					auto worldP0 = camera.ScreenToWorld(point.x + cosf(a0) * radius, point.y + sinf(a0) * radius);
					auto worldP1 = camera.ScreenToWorld(point.x + cosf(a1) * radius, point.y + sinf(a1) * radius);

                    out.push_back({ 
                        {
                           static_cast<float>  (worldP0.x),
                           static_cast<float>  (worldP0.y),
                           static_cast<float>  (worldP0.z)
                        },
                        {
                            static_cast<float>  (pt.color.x),
                            static_cast<float>  (pt.color.y), 
                            static_cast<float>  (pt.color.z),
                            static_cast<float>  (pt.color.w)
                        } 
                    });

                    out.push_back({
                        {
                           static_cast<float>  (worldP1.x),
                           static_cast<float>  (worldP1.y),
                           static_cast<float>  (worldP1.z)
                        },
                        {
                            static_cast<float>  (pt.color.x),
                            static_cast<float>  (pt.color.y),
                            static_cast<float>  (pt.color.z),
                            static_cast<float>  (pt.color.w)
                        }
                    }); 

                }
            }
        }

        bool Empty() const
        {
            return m_lines.empty() && m_points.empty();
        }
          
    private:
        struct Float3
        {
            float x;
            float y;
            float z;
        };

        struct Float4
        {
            float x;
            float y;
            float z;
            float w;
        };

        struct Line
        {
            Float3 a;
            Float3 b;
            Float4 color;
        }; 

        struct Point
        {
            Float3 p;
            Float4 color;
        };

    private:
        std::vector<Line>  m_lines;
        std::vector<Point> m_points;

    private:

        Viewport& m_viewport;
    }; 
}