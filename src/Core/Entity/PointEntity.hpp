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
		DECLARE_RUNTIME_TYPE(PointEntity, Object)

	private:
		Point       m_point;
	};
}
