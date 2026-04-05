#include "App/Scene/Scene.h"
#include "Serialization/ISerializer.h"
#include "Core/Entity/ObjectFactory.hpp"
#include <algorithm>

namespace MiniCAD
{
    Scene::Scene()
    {
        m_layerManager.SetChangeCallback([this]()
            {
                MarkDirty();
            });
    }

    void Scene::AddEntity(std::unique_ptr<Object> entity)
    {
        if (!entity)
            return;

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

    bool Scene::IsEntityVisible(ObjectID id) const
    {
        const auto* obj = GetEntity(id);
        if (!obj)
            return false;

        if (obj->IsKindOf<LineEntity>())
        {
            const auto* line = static_cast<const LineEntity*>(obj);
            if (!line->GetAttr().Visible)
                return false;

            const auto* layer = m_layerManager.GetLayer(line->GetLayerID());
            return layer ? layer->IsVisible() : true;
        }

        return true;
    }

    bool Scene::IsEntitySelectable(ObjectID id) const
    {
        const auto* obj = GetEntity(id);
        if (!obj || !IsEntityVisible(id))
            return false;

        if (obj->IsKindOf<LineEntity>())
        {
            const auto* line = static_cast<const LineEntity*>(obj);
            const auto* layer = m_layerManager.GetLayer(line->GetLayerID());
            return layer ? !layer->IsLocked() : true;
        }

        return true;
    }

    bool Scene::IsEntityEditable(ObjectID id) const
    {
        return IsEntitySelectable(id);
    }

    bool Scene::ReassignEntitiesToLayer(LayerID fromLayer, LayerID toLayer)
    {
        if (fromLayer == toLayer)
            return false;

        if (!m_layerManager.HasLayer(toLayer))
            return false;

        bool changed = false;
        for (auto& [id, obj] : m_entities)
        {
            if (obj->IsKindOf<LineEntity>())
            {
                auto* line = static_cast<LineEntity*>(obj.get());
                if (line->GetLayerID() == fromLayer)
                {
                    line->SetLayerId(toLayer);
                    changed = true;
                }
            }
        }

        if (changed)
            MarkDirty();

        return changed;
    }

    bool Scene::RemoveLayer(LayerID id, LayerID fallbackLayer)
    {
        if (id == Layer::DefaultLayerID)
            return false;

        if (!m_layerManager.HasLayer(id) || !m_layerManager.HasLayer(fallbackLayer))
            return false;

        ReassignEntitiesToLayer(id, fallbackLayer);
        return m_layerManager.RemoveLayer(id);
    }

    std::vector<Scene::ObjectID> Scene::GetAllIDs() const
    {
        std::vector<ObjectID> ids;
        ids.reserve(m_entities.size());
        for (const auto& kv : m_entities)
            ids.push_back(kv.first);

        return ids;
    }

    void Scene::Serialize(ISerializer& s) const
    {
        m_layerManager.Serialize(s);

        s.WriteUInt64(m_entities.size());
        for (const auto& pair : m_entities)
        {
            Object::ObjectID id = pair.first;
            Object* obj = pair.second.get();

            s.WriteUInt64(id);
            obj->Serialize(s);
        }
    }

    void Scene::Deserialize(ISerializer& s)
    {
        m_layerManager.Deserialize(s);
        m_entities.clear();

        uint64_t objCount = s.ReadUInt64();
        for (uint64_t i = 0; i < objCount; ++i)
        {
            Object::ObjectID id = s.ReadUInt64();
            std::unique_ptr<Object> obj = ObjectFactory::Get().CreateFromSerializer(s);
            if (!obj)
                continue;

            obj->SetID(id);
            m_entities[id] = std::move(obj);
        }

        SyncNextObjectID();
    }

    void Scene::SyncNextObjectID()
    {
        ObjectID maxID = 0;
        for (const auto& [id, _] : m_entities)
            maxID = std::max(maxID, id);

        m_nextObjectID.store(maxID + 1, std::memory_order_relaxed);
    }
}
