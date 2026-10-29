#pragma once
#include "../GeomKernel/Circle.hpp"
#include "../GeomKernel/Curves.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"
#include "../Draw/CurveSampling.hpp"

namespace MiniCAD
{
    class CircleEntity : public Entity, public ICurveEntity
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

        // ── ICurveEntity ──────────────────────────────────────────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<CircleCurve>(m_circle); }
        ICurveEntity*       AsCurveEntity()       override { return this; }
        const ICurveEntity* AsCurveEntity() const override { return this; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<CircleEntity>(newId, m_circle.Center, m_circle.Radius);
            e->SetAttr(GetAttr());
            return e;
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();

            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);

            // 默认 64 段；深度放大时 sink 要求在视图附近加密（见 SampleCurve）
            constexpr int kSegments = 64;

            const auto& center = m_circle.Center;
            const double r    = m_circle.Radius;

            SampleCurve(sink, 0.0, Math::TwoPI, kSegments, r,
                        [&](double a) { return Math::Point3{ center.x + r * std::cos(a), center.y + r * std::sin(a), center.z }; },
                        [&](const Math::Point3& p0, const Math::Point3& p1) { sink.DrawLine(p0, p1, color, false); });
        }

        DECLARE_RUNTIME_TYPE(CircleEntity, Entity)

    private:
        Circle m_circle;
    };
}
