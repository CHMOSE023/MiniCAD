#pragma once  
#include "Core/Object/Object.hpp"  
#include "EntityAttr.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/OCS.hpp"
#include <memory>
namespace MiniCAD
{
	class ICurveEntity;   // 参数曲线编辑混入接口（见 ICurveEntity.hpp）

	class Entity : public Object
	{
	public:
		EntityAttr&       GetAttr()                    { return m_attr; }
		const EntityAttr& GetAttr() const              { return m_attr; } 
		void              SetAttr(const EntityAttr& a) { m_attr = a; } 
		LayerID           GetLayerID() const           { return m_attr.LayerId; }
		void              SetLayerId(LayerID id)       { m_attr.LayerId = id; }

		// ── OCS / 挤出方向(DXF 210/220/230)────────────────────────────────
		const Math::Vec3& GetExtrusion() const         { return m_attr.Extrusion; }
		void              SetExtrusion(const Math::Vec3& n) { m_attr.Extrusion = n; }
		// 由挤出方向构造的物体坐标系,用于 OCS<->WCS 变换。
		Math::OCS         GetOCS() const               { return Math::BuildOCS(m_attr.Extrusion); }
		bool              HasDefaultExtrusion() const   { return Math::IsDefaultExtrusion(m_attr.Extrusion); }

		// ── 扩展数据透传(XDATA / 扩展字典 / reactors)───────────────────────
		ExtendedData&       GetXData()                 { return m_attr.XData; }
		const ExtendedData& GetXData() const           { return m_attr.XData; }

		// 解析绘制基色:Color 为 ByLayer 时取所在图层颜色,否则用已解析的 RGBA。
		// 各实体 Draw 中统一经此取色,保证“颜色随层”。
		Math::Color4 ResolveDrawColor(const IDrawSink& sink) const
		{
			return (m_attr.Color.Method == ColorMethod::ByLayer)
				? sink.GetLayerColor(m_attr.LayerId)
				: static_cast<Math::Color4>(m_attr.Color);
		}

		virtual AABB      GetBoundingBox() const= 0;
		virtual void      Draw(IDrawSink& sink, bool isSelected, bool isHovered) const = 0;

		// 无界实体(XLine / Ray):包围盒退化为基点一点,无法用世界 AABB 均匀网格
		// 定位,拾取时须始终作为候选(见 SpatialIndex 的 overflow)。默认有界。
		virtual bool      IsBoundless() const           { return false; }

		virtual std::unique_ptr<Entity> Clone(ObjectID newId) const = 0;

		// 若该实体可作为参数曲线编辑(Trim/Extend/Intersect/Snap)，返回其 ICurveEntity
		// 视图，否则返回 nullptr。曲线实体覆写为 { return this; }。免去 dynamic_cast。
		virtual ICurveEntity* AsCurveEntity()             { return nullptr; }
		virtual const ICurveEntity* AsCurveEntity() const { return nullptr; }

		DECLARE_RUNTIME_TYPE(Entity, Object)

	protected:
		explicit Entity(ObjectID id) : Object(id) {}
	private:
		EntityAttr  m_attr;
	};
}
