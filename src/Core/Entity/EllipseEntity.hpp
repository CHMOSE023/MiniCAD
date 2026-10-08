#pragma once
#include "../GeomKernel/Ellipse.hpp"
#include "../GeomKernel/Curves.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"
#include <cmath>

namespace MiniCAD
{
    // 椭圆 / 椭圆弧（同 DXF ELLIPSE：参数区间 [StartParam, EndParam] 不是整周即为椭圆弧）
    class EllipseEntity : public Entity, public ICurveEntity
    {
    public:
        EllipseEntity(ObjectID id, const Math::Point3& center, double rx, double ry, double rotation = 0.0)
            : Entity(id)
            , m_ellipse(center, rx, ry, rotation)
        {
        }
        EllipseEntity(ObjectID id, const Ellipse& e)
            : Entity(id)
            , m_ellipse(e)
        {
        }

        void            SetEllipse(const Ellipse& e) { m_ellipse = e; }
        const Ellipse&  GetEllipse() const           { return m_ellipse; }

        // 便捷修改器
        void SetCenter  (const Math::Point3& c)  { m_ellipse.Center   = c;  }
        void SetRadiusX (double rx)              { m_ellipse.RadiusX  = rx; }
        void SetRadiusY (double ry)              { m_ellipse.RadiusY  = ry; }
        void SetRotation(double rot)             { m_ellipse.Rotation = rot;}
        void SetParams  (double s, double e)     { m_ellipse.SetParams(s, e); }

        bool IsArc() const { return !m_ellipse.IsFull(); }

        // ── ICurveEntity ──────────────────────────────────────────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<EllipseCurve>(m_ellipse); }
        ICurveEntity*       AsCurveEntity()       override { return this; }
        const ICurveEntity* AsCurveEntity() const override { return this; }

        // ── Entity 接口 ───────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            return m_ellipse.GetBounds();
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<EllipseEntity>(newId, m_ellipse);
            e->SetAttr(GetAttr());
            return e;
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
            // 分段数按跨度比例取，椭圆弧与整椭圆的弦密度一致
            const double t0       = m_ellipse.IsFull() ? 0.0 : m_ellipse.StartParam;
            const double sweep    = m_ellipse.SweepParam();
            const int    segments = std::max(4, static_cast<int>(std::ceil(64.0 * sweep / Math::TwoPI)));

            for (int i = 0; i < segments; ++i)
            {
                double a = t0 + sweep *  i      / segments;
                double b = t0 + sweep * (i + 1) / segments;
                sink.DrawLine(m_ellipse.PointAt(a), m_ellipse.PointAt(b),
                              color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(EllipseEntity, Entity)

    private:
        Ellipse m_ellipse;
    };
}
