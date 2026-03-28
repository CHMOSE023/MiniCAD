#pragma once  
#include <DirectXMath.h>
#include "Core/Object/Object.hpp"
#include "Core/Object/RuntimeType.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "EntityAttr.hpp"
using namespace DirectX;

namespace MiniCAD
{
	class LineEntity: public Object
	{ 
	public: 
		LineEntity(ObjectID id, const XMFLOAT3& start, const XMFLOAT3& end) : Object(id), m_line(start, end), m_attr{} {};

		const Line& GetLine() const { return m_line; }

		void SetLine(const Line& line) { m_line = line; }

		EntityAttr& GetAttr() { return m_attr; } 
		void SetAttr(const EntityAttr& a) { m_attr = a; } 

		AABB GetBoundingBox() const { return m_line.GetBounds(); };

		void Serialize(ISerializer& s) const override {};
		void Deserialize(ISerializer& s) override {};

		DECLARE_RUNTIME_TYPE(LineEntity, Object)
	private:
		Line       m_line; 
		EntityAttr m_attr;
	};  


}