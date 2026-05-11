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
		 
		virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered)  const override
		{
			 const auto& attr = GetAttr();
			 
			 const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;

			 sink.DrawLine(m_line.Start, m_line.End, color, isSelected || isHovered);

		}

		DECLARE_RUNTIME_TYPE(LineEntity, Entity)

	private:
		Line       m_line; 
	};
}