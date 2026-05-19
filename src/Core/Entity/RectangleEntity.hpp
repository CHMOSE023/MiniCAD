#pragma once
#include "Entity.hpp"
#include "../GeomKernel/Rectangle.hpp"
#include "../Math/Point3.hpp"

namespace MiniCAD
{  
    class RectangleEntity : public Entity
    {
    public:   
        RectangleEntity(ObjectID id, const Math::Point3& a, const Math::Point3& b)
            : Entity(id), m_rect(a, b) 
        {
        }

        RectangleEntity(ObjectID id, const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Point3& d)
            : Entity(id), m_rect(a, b, c, d)
        {
        }
         
		void             SetRectangle(const Rectangle& rect) { m_rect = rect; } 
        const Rectangle& GetRectangle() const { return m_rect; }

        virtual AABB GetBoundingBox() const override { return m_rect.GetBounds(); }
         
        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<RectangleEntity>(newId, m_rect.P1, m_rect.P2, m_rect.P3, m_rect.P4);
            e->SetAttr(GetAttr());
            return e;
        }


        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;

			// 绘制矩形边框（4 条线段）
            sink.DrawLine(m_rect.P1, m_rect.P2, color, false);
            sink.DrawLine(m_rect.P2, m_rect.P3, color, false);
            sink.DrawLine(m_rect.P3, m_rect.P4, color, false);
			sink.DrawLine(m_rect.P4, m_rect.P1, color, false);
        };

        DECLARE_RUNTIME_TYPE(RectangleEntity, Entity)
         
    private:
        Rectangle  m_rect;
    };

} 
