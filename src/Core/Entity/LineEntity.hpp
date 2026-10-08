#pragma once
#include "../GeomKernel/Line.hpp"
#include "../GeomKernel/Curves.hpp"
#include "../Math/Point3.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"
using namespace MiniCAD::Math;

namespace MiniCAD
{
	class LineEntity : public Entity, public ICurveEntity
	{
	public:
		LineEntity(ObjectID id, const Point3& start, const Point3& end) : Entity(id), m_line(start, end){};

		void         SetLine(const Line& line) { m_line = line; }
		const Line&  GetLine() const           { return m_line; };

		virtual	AABB GetBoundingBox()  const override { return m_line.GetBounds(); };

		// ── ICurveEntity ──────────────────────────────────────────────
		std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<LineCurve>(m_line); }
		ICurveEntity*           AsCurveEntity()       override { return this; }
		const ICurveEntity*     AsCurveEntity() const override { return this; }

		std::unique_ptr<Entity> Clone(ObjectID newId) const override
		{
			auto e = std::make_unique<LineEntity>(newId, m_line.Start, m_line.End);
			e->SetAttr(GetAttr());
			return e;
		}


		virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered)  const override
		{
			 const auto& attr = GetAttr();

			 const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);

			 sink.DrawLine(m_line.Start, m_line.End, color, false);
		}

		DECLARE_RUNTIME_TYPE(LineEntity, Entity)

	private:
		Line       m_line; 
	};
}