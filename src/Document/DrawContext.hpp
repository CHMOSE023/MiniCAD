#pragma once
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Editor/Overlay/Overlay.h"
#include <vector>
#include <unordered_set>

namespace MiniCAD
{
    class DrawContext : public IDrawSink
    {
    public:
        using ObjectID = uint64_t; 

        DrawContext(std::vector<Vertex_P3_C4>& sceneVertices, Overlay& overlay)
            : m_verts(sceneVertices)
            , m_overlay(overlay) 
        {
        }
          
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) override
        {
            if (isOverlay)
            {
                m_overlay.AddLine(a, b, color);
            }
            else
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
            }
        } 

        const Math::Color4& HighlightColor(bool isSelected) const
        {
            return isSelected ? IDrawSink::kSelectionColor : IDrawSink::kHoverColor;  // 来自 IDrawSink
        }

    private:
        Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& color) const
        {
            return
            {
                {
                    static_cast<float>(pt.x),
                    static_cast<float>(pt.y),
                    static_cast<float>(pt.z)
                },
                {
                    static_cast<float>(color.r),
                    static_cast<float>(color.g),
                    static_cast<float>(color.b),
                    static_cast<float>(color.a)
                }
            };
        }

        Overlay& m_overlay; 
        std::vector<Vertex_P3_C4>& m_verts; 
    };
}