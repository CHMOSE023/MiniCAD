#pragma once   
#include "../GeomKernel/Line.hpp" 
#include "../Math/Point3.hpp" 
#include "Entity.hpp"
using namespace MiniCAD::Math;

namespace MiniCAD
{
	class LineEntity : public Entity
	{
	public:
		LineEntity(ObjectID id, const Point3& start, const Point3& end) : Entity(id), m_line(start, end){};

		void         SetLine(const Line& line) { m_line = line; }
		const Line&  GetLine() const           { return m_line; };

		virtual	AABB GetBoundingBox()  const override { return m_line.GetBounds(); };

		DECLARE_RUNTIME_TYPE(LineEntity, Object)

	private:
		Line       m_line; 
	};
}