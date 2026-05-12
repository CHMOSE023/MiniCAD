#pragma once
#include <vector>
#include <memory>
#include <cmath>
#include <functional>
#include "Core/Object/Object.hpp"
#include "Render/D3D11/Shader.h" 
#include "Editor/Viewport/Viewport.h" 
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Math/Constants.hpp"
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

        // ===== Rect =====
        void AddRect(const Math::Point3& a, const Math::Point3& b, const Math::Color4& mcolor)
        {
            // 规范化矩形：计算四个顶点
            Math::Point3 p1 = { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; // BottomLeft
            Math::Point3 p3 = { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; // TopRight
			Math::Point3 p2 = { p3.x, p1.y, p1.z }; // BottomRight
            Math::Point3 p4 = { p1.x, p3.y, p3.z }; // TopLeft
			AddRect(p1, p2, p3, p4, mcolor);
        }
        void AddRect(const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3, const Math::Point3& p4, const Math::Color4& mcolor)
        {
            
            AddLine(p1, p2, mcolor);
            AddLine(p2, p3, mcolor);
            AddLine(p3, p4, mcolor);
			AddLine(p4, p1, mcolor);
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

        // ===== Circle =====
        void AddCircle(const Math::Point3& center, double radius, const Math::Color4& mcolor, int segments = 64)
        {
            if (radius <= 0.0 || segments < 3)
                return;

            Float4 color =
            {
                static_cast<float>(mcolor.r),
                static_cast<float>(mcolor.g),
                static_cast<float>(mcolor.b),
                static_cast<float>(mcolor.a),
            };

            // 预分配：每段 1 条线 = 2 个顶点
            m_lines.reserve(m_lines.size() + segments);

            for (int i = 0; i < segments; ++i)
            {
                double a0 = (i / static_cast<double>(segments)) * Math::TwoPI;
                double a1 = ((i + 1) / static_cast<double>(segments)) * Math::TwoPI;

                Float3 p0 =
                {
                    static_cast<float>(center.x + std::cos(a0) * radius),
                    static_cast<float>(center.y + std::sin(a0) * radius),
                    static_cast<float>(center.z),
                };

                Float3 p1 =
                {
                    static_cast<float>(center.x + std::cos(a1) * radius),
                    static_cast<float>(center.y + std::sin(a1) * radius),
                    static_cast<float>(center.z),
                };

                m_lines.push_back({ p0, p1, color });
            }
        }
       
        // ===== Export to GPU vertices =====
        void ToVertices(std::vector<Vertex_P3_C4>& out) const
        {
            out.reserve(out.size() + m_lines.size() * 2 + m_points.size());
           
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
                    double a0 = (i / (float)segments)       * Math::TwoPI;
                    double a1 = ((i + 1) / (float)segments) * Math::TwoPI;
                      
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