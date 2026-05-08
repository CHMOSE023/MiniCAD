#pragma once  
#include <DirectXMath.h> 
#include "Core/GeomKernel/Line.hpp" 
#include "Entity.hpp"
using namespace DirectX;

namespace MiniCAD
{
	class LineEntity : public Entity
	{
	public:
		LineEntity(ObjectID id, const XMFLOAT3& start, const XMFLOAT3& end) : Entity(id), m_line(start, end){};

		void        SetLine(const Line& line) { m_line = line; }
		const Line& GetLine() const { return m_line; };

		virtual	AABB GetBoundingBox()  const override { return m_line.GetBounds(); };
		DECLARE_RUNTIME_TYPE(LineEntity, Object)

	private:
		Line       m_line; 
	};
}