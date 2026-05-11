#pragma once
#include "../GeomKernel/Circle.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"

namespace MiniCAD
{
    class CircleEntity : public Entity
    {
    public:
        CircleEntity(ObjectID id, const Math::Point3& center, double radius)
            : Entity(id)
            , m_circle(center, radius)
        {
        }

        void          SetCircle(const Circle& circle) { m_circle = circle; }
        const Circle& GetCircle() const { return m_circle; }

        // 便捷修改器
        void SetCenter(const Math::Point3& center) { m_circle.Center = center; }
        void SetRadius(double radius) { m_circle.Radius = radius; }

        virtual AABB GetBoundingBox() const override { return m_circle.GetBounds(); }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();

            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;

            constexpr int kSegments = 64;

            const auto& center = m_circle.Center;
            const double r    = m_circle.Radius;

            for (int i = 0; i < kSegments; ++i)
            {
                double a0 = Math::TwoPI * i / kSegments;
                double a1 = Math::TwoPI * (i + 1) / kSegments;

                Math::Point3 p0 =
                {
                    center.x + r * std::cos(a0),
                    center.y + r * std::sin(a0),
                    center.z
                };

                Math::Point3 p1 =
                {
                    center.x + r * std::cos(a1),
                    center.y + r * std::sin(a1),
                    center.z
                }; 

                sink.DrawLine(p0, p1, color, isSelected || isHovered);
            }

        }

        DECLARE_RUNTIME_TYPE(CircleEntity, Entity)

    private:
        Circle m_circle;
    };
}
