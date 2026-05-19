#pragma once  
#include "../Math/Point3.hpp"
#include "Core/GeomKernel/Point.hpp" 
#include "Entity.hpp" 

namespace MiniCAD
{
	class PointEntity : public Entity
	{
	public:
		PointEntity(ObjectID id, const  Math::Point3& position) : Entity(id), m_point(position) {};

		void         SetPoint(const Point& point) { m_point = point; }
		const Point& GetPoint() const             { return m_point; };

		virtual	AABB GetBoundingBox()  const override { return m_point.GetBounds(); };

		std::unique_ptr<Entity> Clone(ObjectID newId) const override
		{
			auto e = std::make_unique<PointEntity>(newId, m_point.Position);
			e->SetAttr(GetAttr());
			return e;
		} 

		virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered)   const override
		{
			const auto& attr = GetAttr();
			const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color; 

			// 绘制为十字
			const float s = 0.2f;
			auto        p = m_point.Position;
			Math::Point3 p1 = { p.x - s ,p.y,p.z };
			Math::Point3 p2 = { p.x + s ,p.y,p.z };
			Math::Point3 p3 = { p.x ,p.y - s,p.z };
			Math::Point3 p4 = { p.x ,p.y + s,p.z };

			sink.DrawLine(p1, p2, color, false);
			sink.DrawLine(p3, p4, color, false); 

		}

		DECLARE_RUNTIME_TYPE(PointEntity, Entity)

	private:
		Point       m_point;
	};
}
