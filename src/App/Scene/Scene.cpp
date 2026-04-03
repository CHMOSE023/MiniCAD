#include "App/Scene/Scene.h" 
#include "Serialization/ISerializer.h"  
#include "Core/Entity/ObjectFactory.hpp"  
#include <algorithm>
namespace MiniCAD
{  
	void Scene::AddEntity(std::unique_ptr<Object> entity)
	{
		if (!entity) return;
		ObjectID id = entity->GetID();   
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

	void Scene::Serialize(ISerializer& s) const
	{ 
		// 保存图层
		m_layerManager.Serialize(s);
        
		// 保存对象
		s.WriteUInt64(m_entities.size());

		for (auto& pair : m_entities)
		{
			Object::ObjectID id = pair.first;

			Object* obj = pair.second.get();

			s.WriteUInt64(id);                // 保存对象ID 

			obj->Serialize(s);                // 调用对象自己的序列化
		}
	}

	void Scene::Deserialize(ISerializer& s)
	{
		// 1 加载图层
		m_layerManager.Deserialize(s);

		// 2 先清空原数据
		m_entities.clear();	

		// 3️ 加载对象
		uint64_t objCount = s.ReadUInt64();
		for (uint64_t i = 0; i < objCount; ++i)
		{ 
			Object::ObjectID id = s.ReadUInt64();  

			// 4️ 使用 ObjectFactory 创建具体对象
			std::unique_ptr<Object> obj = ObjectFactory::Get().CreateFromSerializer(s); 
			if (!obj) continue;   // 未知类型，跳过
			
			obj->SetID(id); // 恢复对象 ID（Deserialize 后对象的 ID 是 InvalidID）
			
			m_entities[id] = std::move(obj);
			 
		}

		SyncNextObjectID();
		 
	}
	void Scene::SyncNextObjectID()
	{
		ObjectID maxID = 0;
		for (auto& [id, _] : m_entities)
			maxID = std::max(maxID, id);

		m_nextObjectID.store(maxID + 1, std::memory_order_relaxed);
	}
}