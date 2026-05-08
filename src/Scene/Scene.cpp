#include "Scene/Scene.h"  
#include <algorithm>
#include <memory>
#include <utility>
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
}
