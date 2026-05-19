#pragma once
#include "../GeomKernel/Spline.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"

namespace MiniCAD
{
    class SplineEntity : public Entity
    {
    public:

        SplineEntity(ObjectID id, std::vector<Math::Point3> fitPoints, SplineBoundary boundary = SplineBoundary::Natural)
            : Entity(id)
            , m_spline(std::move(fitPoints), boundary)
        {
        }

        // 直接接受已构建的 Spline
        SplineEntity(ObjectID id, Spline spline)
            : Entity(id)
            , m_spline(std::move(spline))
        {
        }

        // ── 访问 / 修改 ────────────────────────────────────────────────────
        const Spline& GetSpline() const { return m_spline; }
        Spline& GetSpline() { return m_spline; }

        void SetBoundary(SplineBoundary b)
        {
            m_spline.Boundary = b;
            m_spline.Build();
        }

        // 追加拟合点并重建
        void AddFitPoint(const Math::Point3& pt)
        {
            m_spline.FitPoints.push_back(pt);
            m_spline.Build();
        }

        // 修改某个拟合点并重建
        void SetFitPoint(int i, const Math::Point3& pt)
        {
            m_spline.FitPoints[i] = pt;
            m_spline.Build();
        }

        int FitPointCount() const
        {
            return static_cast<int>(m_spline.FitPoints.size());
        }

        // ── Entity 接口 ────────────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            return m_spline.GetBounds();
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<SplineEntity>(newId, m_spline);
            e->SetAttr(GetAttr());
            return e;
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_spline.IsValid()) return;

            const auto& attr = GetAttr();
            const Math::Color4& curveColor = isSelected ? IDrawSink::kSelectionColor
                : isHovered ? IDrawSink::kHoverColor
                : attr.Color;
            constexpr int kSamplesPerSeg = 32;
            auto pts = m_spline.Tessellate(kSamplesPerSeg);

            for (size_t i = 0; i + 1 < pts.size(); ++i)
                sink.DrawLine(pts[i], pts[i + 1], curveColor, false);

            // 选中时显示控制点标记
            // if (isSelected)
            // {
            //     const Math::Color4 cpColor = { 0.4, 0.8, 1.0, 0.8 };
            //     for (const auto& fp : m_spline.FitPoints)
            //         sink.DrawPoint(fp, cpColor);
            // }
        }

        DECLARE_RUNTIME_TYPE(SplineEntity, Entity)

    private:
        Spline m_spline;
    };
}
