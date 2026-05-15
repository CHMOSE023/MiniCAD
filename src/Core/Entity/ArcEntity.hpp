#pragma once
#include "../GeomKernel/Arc.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"
#include <stdexcept>

namespace MiniCAD
{
    class ArcEntity : public Entity
    {
    public:
        // ── 方式 1：圆心 + 半径 + 起止角（弧度）────────────────────────
        ArcEntity(ObjectID id, const Math::Point3& center, double radius, double startAngle, double endAngle)
            : Entity(id)
            , m_arc(center, radius, startAngle, endAngle)
        {
        }

        // ── 方式 2：三点法（起点 / 弧上一点 / 终点）─────────────────────
        //   三点共线时抛出 std::invalid_argument
        static ArcEntity FromThreePoints(ObjectID id, const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3)
        {
            auto arc = Arc::FromThreePoints(p1, p2, p3);
            if (!arc)
                throw std::invalid_argument("ArcEntity::FromThreePoints: points are collinear");
            return ArcEntity(id, *arc);
        }

        // ── 访问 / 修改 ───────────────────────────────────────────────
        void       SetArc(const Arc& arc) { m_arc = arc; }
        const Arc& GetArc() const         { return m_arc; }

        void SetCenter(const Math::Point3& c)  { m_arc.Center     = c; }
        void SetRadius(double r)               { m_arc.Radius     = r; }
        void SetStartAngle(double a)           { m_arc.StartAngle = a; }
        void SetEndAngle(double a)             { m_arc.EndAngle   = a; }

        // 用新的三点重新定义弧（失败时保持原弧不变，返回 false）
        bool RedefineFromThreePoints(const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3)
        {
            auto arc = Arc::FromThreePoints(p1, p2, p3);
            if (!arc) return false;
            m_arc = *arc;
            return true;
        }

        // ── Entity 接口 ───────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override { return m_arc.GetBounds(); }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr  = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;
            constexpr int kSegments = 64;
            const double  sweep     = m_arc.SweepAngle();

            for (int i = 0; i < kSegments; ++i)
            {
                double a0 = m_arc.StartAngle + sweep *  i      / kSegments;
                double a1 = m_arc.StartAngle + sweep * (i + 1) / kSegments;
                sink.DrawLine(m_arc.PointAt(a0), m_arc.PointAt(a1), color, isSelected || isHovered);
            }
        }

        DECLARE_RUNTIME_TYPE(ArcEntity, Entity)

    private:
        // 供 FromThreePoints 工厂使用的私有构造
        ArcEntity(ObjectID id, const Arc& arc) : Entity(id), m_arc(arc)
        {
        }

        Arc m_arc;
    };
}