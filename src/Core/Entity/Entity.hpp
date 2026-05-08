#pragma once  
#include "Core/Object/Object.hpp"  
#include "EntityAttr.hpp"
#include "Core/GeomKernel/AABB.hpp"
namespace MiniCAD
{
	class Entity : public Object
	{
	public: 
		EntityAttr& GetAttr() { return m_attr; }
		const EntityAttr& GetAttr() const { return m_attr; }

		void              SetAttr(const EntityAttr& a) { m_attr = a; }

		LayerID           GetLayerID() const { return m_attr.LayerId; }
		void              SetLayerId(LayerID id) { m_attr.LayerId = id; }
		virtual AABB      GetBoundingBox() const = 0;

		DECLARE_RUNTIME_TYPE(Entity, Object)

	protected:
		explicit Entity(ObjectID id) : Object(id) {}
	private:
		EntityAttr m_attr;
	};
}
