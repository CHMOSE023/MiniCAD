#pragma once
#include "../GeomKernel/Ellipse.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"
#include <cmath>

namespace MiniCAD
{
    class EllipseEntity : public Entity
    {
    public:
        EllipseEntity(ObjectID id, const Math::Point3& center, double rx, double ry, double rotation = 0.0)
            : Entity(id)
            , m_ellipse(center, rx, ry, rotation)
        {
        }

        void            SetEllipse(const Ellipse& e) { m_ellipse = e; }
        const Ellipse&  GetEllipse() const           { return m_ellipse; }

        // 便捷修改器
        void SetCenter  (const Math::Point3& c)  { m_ellipse.Center   = c;  }
        void SetRadiusX (double rx)              { m_ellipse.RadiusX  = rx; }
        void SetRadiusY (double ry)              { m_ellipse.RadiusY  = ry; }
        void SetRotation(double rot)             { m_ellipse.Rotation = rot;}

        // ── Entity 接口 ───────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            return m_ellipse.GetBounds();
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<EllipseEntity>(newId, m_ellipse.Center, m_ellipse.RadiusX, m_ellipse.RadiusY, m_ellipse.Rotation);
            e->SetAttr(GetAttr());
            return e;
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;
            constexpr int kSegments = 64;

            for (int i = 0; i < kSegments; ++i)
            {
                double t0 = Math::TwoPI *  i      / kSegments;
                double t1 = Math::TwoPI * (i + 1) / kSegments;
                sink.DrawLine(m_ellipse.PointAt(t0), m_ellipse.PointAt(t1),
                              color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(EllipseEntity, Entity)

    private:
        Ellipse m_ellipse;
    };
}
