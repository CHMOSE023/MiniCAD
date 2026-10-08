#pragma once
#include "../GeomKernel/XLine.hpp"
#include "../GeomKernel/Curves.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"

namespace MiniCAD
{
    // 射线 / 半无限直线（对应 DXF RAY）。
    //   组码 10 = 起点(Origin)，组码 11 = 单位方向向量(Direction)。
    // 从起点沿方向单侧无限延伸;几何数据复用 XLine(起点 + 方向)。
    // 与 XLINE 一致,包围盒退化为起点一点,不计入「缩放到范围」。
    class RayEntity : public Entity, public ICurveEntity
    {
    public:
        RayEntity(ObjectID id, const Math::Point3& origin, const Math::Vec3& direction)
            : Entity(id)
            , m_ray(origin, direction)
        {
        }

        // ── 访问 / 修改 ────────────────────────────────────────────────────
        void          SetRay(const XLine& r)             { m_ray = r; }
        const XLine&  GetRay() const                     { return m_ray; }

        const Math::Point3& GetOrigin() const            { return m_ray.Origin; }     // 组码 10
        void                SetOrigin(const Math::Point3& p) { m_ray.Origin = p; }

        const Math::Vec3&   GetDirection() const         { return m_ray.Direction; }  // 组码 11
        void                SetDirection(const Math::Vec3& d) { m_ray.Direction = d; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            return AABB(m_ray.Origin, m_ray.Origin);
        }

        // 射线:包围盒退化为起点,须始终参与拾取候选(见 SpatialIndex overflow)。
        bool IsBoundless() const override { return true; }

        // ── ICurveEntity ──────────────────────────────────────────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<RayCurve>(m_ray); }
        ICurveEntity*       AsCurveEntity()       override { return this; }
        const ICurveEntity* AsCurveEntity() const override { return this; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<RayEntity>(newId, m_ray.Origin, m_ray.Direction);
            e->SetAttr(GetAttr());
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_ray.IsValid()) return;

            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                      : ResolveDrawColor(sink);

            // 单侧延伸:仅沿 +Direction 方向以足够大半长近似「无限」。
            const Math::Vec3   dir = m_ray.UnitDirection();
            const Math::Point3 end = m_ray.Origin + dir * kDrawHalfLength;
            sink.DrawLine(m_ray.Origin, end, color, false);
        }

        DECLARE_RUNTIME_TYPE(RayEntity, Entity)

    private:
        static constexpr double kDrawHalfLength = 1.0e6;   // 近似无限延伸的长度

        XLine m_ray;   // 复用无限直线几何承载 起点 + 方向
    };
}
