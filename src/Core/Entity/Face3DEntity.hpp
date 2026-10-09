#pragma once
#include "RectangleEntity.hpp"
#include <array>
#include <cstdint>

namespace MiniCAD
{
    // WCS corners in perimeter order. The current view draws wireframe edges;
    // keep the surface and hidden-edge data for lossless DWG/DXF exchange.
    class Face3DEntity : public RectangleEntity
    {
    public:
        Face3DEntity(ObjectID id, const Math::Point3& a, const Math::Point3& b,
                     const Math::Point3& c, const Math::Point3& d, std::uint32_t invisibleEdges = 0)
            : RectangleEntity(id, a, b, c, d), m_invisibleEdges(invisibleEdges & 15) {}

        std::uint32_t GetInvisibleEdges() const { return m_invisibleEdges; }
        void SetInvisibleEdges(std::uint32_t flags) { m_invisibleEdges = flags & 15; }

        std::unique_ptr<Entity> Clone(ObjectID id) const override
        {
            const auto& r = GetRectangle();
            auto result = std::make_unique<Face3DEntity>(id, r.P1, r.P2, r.P3, r.P4, m_invisibleEdges);
            result->SetAttr(GetAttr());
            return result;
        }

        void Draw(IDrawSink& sink, bool selected, bool hovered) const override
        {
            const auto color = selected ? IDrawSink::kSelectionColor : hovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
            const auto& r = GetRectangle();
            const std::array points{r.P1, r.P2, r.P3, r.P4};
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto& a = points[i]; const auto& b = points[(i + 1) % points.size()];
                if ((m_invisibleEdges & (1u << i)) == 0 && (a.x != b.x || a.y != b.y || a.z != b.z))
                    sink.DrawLine(a, b, color, false);
            }
        }
        DECLARE_RUNTIME_TYPE(Face3DEntity, RectangleEntity)
    private:
        std::uint32_t m_invisibleEdges;
    };
}
