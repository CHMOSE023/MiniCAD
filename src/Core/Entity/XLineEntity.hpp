#pragma once
#include "../GeomKernel/XLine.hpp"
#include "../GeomKernel/Curves.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"

namespace MiniCAD
{
    // 构造线 / 无限直线（对应 DXF XLINE）。
    //   组码 10 = 基点(Origin)，组码 11 = 单位方向向量(Direction)。
    // 向两侧无限延伸;参与拾取但不参与「缩放到范围」(包围盒退化为基点)。
    class XLineEntity : public Entity, public ICurveEntity
    {
    public:
        XLineEntity(ObjectID id, const Math::Point3& origin, const Math::Vec3& direction)
            : Entity(id)
            , m_xline(origin, direction)
        {
        }

        // ── 访问 / 修改 ────────────────────────────────────────────────────
        void          SetXLine(const XLine& xl)          { m_xline = xl; }
        const XLine&  GetXLine() const                   { return m_xline; }

        const Math::Point3& GetOrigin() const            { return m_xline.Origin; }     // 组码 10
        void                SetOrigin(const Math::Point3& p) { m_xline.Origin = p; }

        const Math::Vec3&   GetDirection() const         { return m_xline.Direction; }  // 组码 11
        void                SetDirection(const Math::Vec3& d) { m_xline.Direction = d; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        // 无限直线:AutoCAD 中不计入图形范围,故包围盒退化为基点一点。
        AABB GetBoundingBox() const override
        {
            return AABB(m_xline.Origin, m_xline.Origin);
        }

        // 无限直线:包围盒退化为基点,须始终参与拾取候选(见 SpatialIndex overflow)。
        bool IsBoundless() const override { return true; }

        // ── ICurveEntity ──────────────────────────────────────────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<XLineCurve>(m_xline); }
        ICurveEntity*       AsCurveEntity()       override { return this; }
        const ICurveEntity* AsCurveEntity() const override { return this; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<XLineEntity>(newId, m_xline.Origin, m_xline.Direction);
            e->SetAttr(GetAttr());
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_xline.IsValid()) return;

            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                      : ResolveDrawColor(sink);

            // 无视口信息,以一个足够大的半长向两侧延伸近似「无限」。
            const Math::Vec3   dir = m_xline.UnitDirection();
            const Math::Point3 a   = m_xline.Origin - dir * kDrawHalfLength;
            const Math::Point3 b   = m_xline.Origin + dir * kDrawHalfLength;
            sink.DrawLine(a, b, color, false);
        }

        DECLARE_RUNTIME_TYPE(XLineEntity, Entity)

    private:
        static constexpr double kDrawHalfLength = 1.0e6;   // 近似无限延伸的半长

        XLine m_xline;
    };
}
