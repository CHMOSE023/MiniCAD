#include "App/Scene/Scene.h" 
#include "Serialization/ISerializer.h"  
#include "Core/Entity/ObjectFactory.hpp"  
namespace MiniCAD
{ 

	void Scene::AddEntity(std::unique_ptr<Object> entity)
	{
		if (!entity) return;
		ObjectID id = entity->GetID();  
		entity->SetLayerID(m_layerManager.GetActiveLayerID()); // 如果 ID 已存在，则替换原有实体
		m_entities[id] = std::move(entity);
		MarkDirty();
	}

	std::unique_ptr<Object> Scene::RemoveEntity(ObjectID id)
	{
		auto it = m_entities.find(id);
		if (it == m_entities.end()) 
			return nullptr;

		std::unique_ptr<Object> ret = std::move(it->second);
		m_entities.erase(it);
		MarkDirty();
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

	bool Scene::Has(ObjectID id) const {
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

	void Scene::Serialize(ISerializer& s) const
	{ 
		// 保存图层
		auto  layerIds = m_layerManager.GetAllLayerIDs();
		s.WriteUInt64(layerIds.size());
		for (auto& layerId : layerIds)
		{
			auto layer = m_layerManager.GetLayer(layerId);
			if (layer == nullptr)
			{
				continue;
			}
			s.WriteUInt64(layer->GetID());
			s.WriteString(layer->GetName());
			auto& c = layer->GetColor();
			s.WriteFloat(c.x);
			s.WriteFloat(c.y);
			s.WriteFloat(c.z);
			s.WriteFloat(c.w);
			s.WriteBool(layer->IsVisible());
			s.WriteBool(layer->IsLocked());
		}

		// 保存对象
		s.WriteUInt64(m_entities.size());

		for (auto& pair : m_entities)
		{
			Object::ObjectID id = pair.first;
			Object* obj = pair.second.get();

			s.WriteUInt64(id);                // 保存对象ID
			s.WriteUInt64(obj->GetLayerID()); // 保存所属图层ID
			obj->Serialize(s);                // 调用对象自己的序列化
		}
	}

	void Scene::Deserialize(ISerializer& s)
	{
		// 1️ 先清空原数据
		m_entities.clear();

		// 2️ 加载图层
		uint64_t layerCount = s.ReadUInt64();
		for (uint64_t i = 0; i < layerCount; ++i)
		{
			auto id = s.ReadUInt64();
			std::string name = s.ReadString();
			DirectX::XMFLOAT4 color{
				s.ReadFloat(),
				s.ReadFloat(),
				s.ReadFloat(),
				s.ReadFloat()
			};
			bool visible = s.ReadBool();
			bool locked = s.ReadBool();

			auto layer = std::make_unique<Layer>(id, name);
			layer->SetColor(color);
			layer->SetVisible(visible);
			layer->SetLocked(locked);

			m_layerManager.AddLayer(std::move(layer));
		}

		// 3️ 加载对象
		uint64_t objCount = s.ReadUInt64();
		for (uint64_t i = 0; i < objCount; ++i)
		{ 
			Object::ObjectID id = s.ReadUInt64();
			Object::LayerID layerID = s.ReadUInt64();

			// 4️ 使用 ObjectFactory 创建具体对象
			std::unique_ptr<Object> obj = ObjectFactory::CreateFromSerializer(s);

			// 设置 LayerID
			obj->SetLayerID(layerID);

			// 保存到 m_entities map
			m_entities[id] = std::move(obj);
			 
		}
		 
	}
}