#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Serialization/ISerializer.h"
#include "Serialization/EntityIO.h"
#include <algorithm>
#include <memory>
#include <utility>
namespace MiniCAD
{
	void Scene::AddEntity(std::unique_ptr<Object> entity)
	{
		if (!entity) return;
		ObjectID id = entity->GetID();
		if (entity->IsKindOf<InsertEntity>())
			ResolveInsert(static_cast<InsertEntity&>(*entity));
		m_entities[id] = std::move(entity);
		MarkEntityDirty(id);   // 单实体新增：其余实体顶点缓存保持有效
	}

	void Scene::ResolveInsert(InsertEntity& ins)
	{
		ins.SetBlock(m_blockTable.Find(ins.GetBlockId()));
	}

	bool Scene::IsEntityLocked(const Object& obj) const
	{
		if (!obj.IsKindOf<Entity>())
			return false;
		const Layer* layer = m_layerManager.GetLayer(static_cast<const Entity&>(obj).GetAttr().LayerId);
		return layer && layer->IsLocked();
	}

	void Scene::ResolveAllInserts()
	{
		for (auto& [id, obj] : m_entities)
		{
			if (obj && obj->IsKindOf<InsertEntity>())
				ResolveInsert(static_cast<InsertEntity&>(*obj));
		}
	}

	std::unique_ptr<Object> Scene::RemoveEntity(ObjectID id)
	{
		auto it = m_entities.find(id);
		if (it == m_entities.end())
			return nullptr;

		std::unique_ptr<Object> ret = std::move(it->second);
		m_entities.erase(it);
		MarkEntityDirty(id);   // 单实体移除：其余实体顶点缓存保持有效
		return ret;
	}

	Object* Scene::GetEntity(ObjectID id)
	{
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	const Object* Scene::GetEntity(ObjectID id) const
	{
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	bool Scene::Has(ObjectID id) const
	{
		return m_entities.count(id) > 0;
	}

	std::vector<Scene::ObjectID> Scene::GetAllIDs() const
	{
		std::vector<ObjectID> ids;
		ids.reserve(m_entities.size());
		for (auto& kv : m_entities)
			ids.push_back(kv.first);

		return ids;
	}

	bool Scene::GetExtents(AABB& out) const
	{
		AABB box = AABB::Empty();
		bool any = false;
		for (const auto& [id, obj] : m_entities)
		{
			if (!obj || !obj->IsKindOf<Entity>())
				continue;
			const Entity& e = static_cast<const Entity&>(*obj);
			if (e.IsBoundless() || !e.GetAttr().Visible)
				continue;
			const Layer* layer = m_layerManager.GetLayer(e.GetAttr().LayerId);
			if (layer && !layer->IsVisible())
				continue;
			const AABB b = e.GetBoundingBox();
			if (!(b.Min.x <= b.Max.x) || !std::isfinite(b.Min.x) || !std::isfinite(b.Max.x)
				|| !std::isfinite(b.Min.y) || !std::isfinite(b.Max.y))
				continue;   // 空的或非法的包围盒
			box.Expand(b);
			any = true;
		}
		if (any)
			out = box;
		return any;
	}

	void Scene::ForEachObject(std::function<void(const Object&)> fn) const
	{
		for (const auto& [id, obj] : m_entities)
		{
			fn(*obj);
		}
	}

	void Scene::ForEachPreview(std::function<void(const Object&)> fn) const
	{
		for (const auto& obj : m_previews)
		{
			fn(*obj);
		}
	}

	void Scene::SyncNextObjectID()
	{
		ObjectID maxID = 0;

		for (auto& [id, _] : m_entities)
		{
			maxID = std::max(maxID, id);
		}

		m_nextObjectID.store(maxID + 1, std::memory_order_relaxed);
	}

	namespace
	{
		// 只记录文字输出所用的样式 ID
		class TextStyleUsageSink : public IDrawSink
		{
		public:
			std::vector<TextStyleID> ids;
			void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
			void EmitMText(const Math::Point3&, const std::string&, uint32_t styleId, double, double, double, const Math::Color4&) override
			{
				if (std::find(ids.begin(), ids.end(), styleId) == ids.end())
					ids.push_back(styleId);
			}
		};
	}

	std::vector<TextStyleID> Scene::CollectUsedTextStyles() const
	{
		TextStyleUsageSink sink;
		ForEachObject([&](const Object& o)
		{
			if (o.IsKindOf<Entity>())
				static_cast<const Entity&>(o).Draw(sink, false, false);
		});
		m_blockTable.ForEach([&](BlockID, const BlockEntity& blk)     // 未被插入的块定义也算
		{
			for (const auto& e : blk.GetEntities())
				if (e) e->Draw(sink, false, false);
		});
		return sink.ids;
	}

	void Scene::Serialize(ISerializer& s) const
	{
		uint64_t nextId = m_nextObjectID.load(std::memory_order_relaxed);
		s.Value("nextObjectId", nextId);
		uint32_t clt = m_currentLineType;
		s.Value("currentLineType", clt);
		int32_t clw = static_cast<int32_t>(LineweightToRaw(m_currentLineweight));
		s.Value("currentLineweight", clw);

		if (s.BeginObject("layers"))
		{
			m_layerManager.Serialize(s);
			s.EndObject();
		}
		if (s.BeginObject("lineTypes"))
		{
			m_lineTypeTable.Serialize(s);
			s.EndObject();
		}
		if (s.BeginObject("blocks"))
		{
			m_blockTable.Serialize(s);
			s.EndObject();
		}
		if (s.BeginObject("textStyles"))
		{
			m_textStyleTable.Serialize(s);
			s.EndObject();
		}
		uint32_t cts = m_currentTextStyle;
		s.Value("currentTextStyle", cts);
		if (s.BeginObject("mlineStyles"))
		{
			m_mlineStyleTable.Serialize(s);
			s.EndObject();
		}
		uint32_t cms = m_currentMLineStyle;
		s.Value("currentMLineStyle", cms);

		// 实体按 ID 升序写出,保证文件内容确定(unordered_map 无序)。
		std::vector<ObjectID> ids = GetAllIDs();
		std::sort(ids.begin(), ids.end());

		size_t n = ids.size();
		if (s.BeginArray("entities", n))
		{
			for (ObjectID id : ids)
			{
				const Object* obj = m_entities.at(id).get();
				if (!obj || !obj->IsKindOf<Entity>())
					continue;
				if (s.BeginElement(0))
				{
					EntityIO::Write(s, static_cast<const Entity&>(*obj));
					s.EndElement();
				}
			}
			s.EndArray();
		}
	}

	void Scene::Deserialize(ISerializer& s)
	{
		m_entities.clear();
		m_previews.clear();

		if (s.BeginObject("layers"))
		{
			m_layerManager.Deserialize(s);
			s.EndObject();
		}
		if (s.BeginObject("lineTypes"))
		{
			m_lineTypeTable.Deserialize(s);
			s.EndObject();
		}
		if (s.BeginObject("blocks"))
		{
			m_blockTable.Deserialize(s);
			s.EndObject();
		}
		m_textStyleTable = TextStyleTable();       // 旧文件无文字样式表:用预置样式
		if (s.BeginObject("textStyles"))
		{
			m_textStyleTable.Deserialize(s);
			s.EndObject();
		}
		uint32_t cts = TextStyleTable::StandardID;
		s.Value("currentTextStyle", cts);
		m_currentTextStyle = m_textStyleTable.Find(cts) ? cts : TextStyleTable::StandardID;

		m_mlineStyleTable = MLineStyleTable();     // 旧文件无多线样式表:用预置样式
		if (s.BeginObject("mlineStyles"))
		{
			m_mlineStyleTable.Deserialize(s);
			s.EndObject();
		}
		uint32_t cms = MLineStyleTable::StandardID;
		s.Value("currentMLineStyle", cms);
		m_currentMLineStyle = m_mlineStyleTable.Find(cms) ? cms : MLineStyleTable::StandardID;

		size_t n = 0;
		if (s.BeginArray("entities", n))
		{
			for (size_t i = 0; i < n; ++i)
			{
				if (!s.BeginElement(i)) continue;
				if (auto e = EntityIO::Read(s))
				{
					if (e->GetID() != Object::InvalidID && !m_entities.count(e->GetID()))
						AddEntity(std::move(e));   // AddEntity 内部解析 INSERT 块指针
				}
				s.EndElement();
			}
			s.EndArray();
		}

		uint32_t clt = LineTypeTable::ByLayerID;
		s.Value("currentLineType", clt);
		m_currentLineType = clt;
		int32_t clw = static_cast<int32_t>(LineweightToRaw(Lineweight::ByLayer));
		s.Value("currentLineweight", clw);
		m_currentLineweight = LineweightFromRaw(static_cast<int16_t>(clw));

		// nextObjectId 取档案值与现有最大实体 ID+1 的较大者。
		SyncNextObjectID();
		uint64_t nextId = m_nextObjectID.load(std::memory_order_relaxed);
		uint64_t fileNext = nextId;
		s.Value("nextObjectId", fileNext);
		m_nextObjectID.store(std::max(nextId, fileNext), std::memory_order_relaxed);

		MarkDirty();
	}
}
