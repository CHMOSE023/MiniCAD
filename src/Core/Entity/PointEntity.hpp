#pragma once  
#include <DirectXMath.h> 
#include "Core/GeomKernel/Point.hpp" 
#include "Entity.hpp"
using namespace DirectX;

namespace MiniCAD
{
	class PointEntity : public Entity
	{
	public:
		PointEntity(ObjectID id, const XMFLOAT3& position) : Entity(id), m_point(position) {};

		void         SetPoint(const Point& point) { m_point = point; }
		const Point& GetPoint() const             { return m_point; };

		virtual	AABB GetBoundingBox()  const override { return m_point.GetBounds(); };
		DECLARE_RUNTIME_TYPE(PointEntity, Object)

	private:
		Point       m_point;
	};
}