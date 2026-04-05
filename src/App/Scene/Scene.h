#pragma once
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "App/Scene/LayerManager.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>

namespace MiniCAD
{
    class ISerializer;

    class Scene
    {
    public:
        using ObjectID = Object::ObjectID;
        using DirtyCallback = std::function<void()>;

        Scene();

        void AddEntity(std::unique_ptr<Object> entity);
        std::unique_ptr<Object> RemoveEntity(ObjectID id);

        Object* GetEntity(ObjectID id);
        const Object* GetEntity(ObjectID id) const;

        bool Has(ObjectID id) const;
        bool IsEntityVisible(ObjectID id) const;
        bool IsEntitySelectable(ObjectID id) const;
        bool IsEntityEditable(ObjectID id) const;
        bool ReassignEntitiesToLayer(LayerID fromLayer, LayerID toLayer);
        bool RemoveLayer(LayerID id, LayerID fallbackLayer = Layer::DefaultLayerID);

        std::vector<ObjectID> GetAllIDs() const;

        LayerManager& GetLayerManager() { return m_layerManager; }
        const LayerManager& GetLayerManager() const { return m_layerManager; }

        bool IsDirty() const { return m_dirty; }
        void MarkDirty() { m_dirty = true; if (m_onDirty) m_onDirty(); }
        void ClearDirty() { m_dirty = false; }

        void SetDirtyCallback(DirtyCallback cb) { m_onDirty = std::move(cb); }

        int EntityCount() const { return (int)m_entities.size(); }
        const std::unordered_map<ObjectID, std::unique_ptr<Object>>& GetEntities() const { return m_entities; }

        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

        ObjectID NextObjectID() { return m_nextObjectID.fetch_add(1, std::memory_order_relaxed); }

    private:
        void SyncNextObjectID();

        std::unordered_map<ObjectID, std::unique_ptr<Object>> m_entities;
        std::atomic<ObjectID> m_nextObjectID{ 1 };
        LayerManager m_layerManager;
        bool m_dirty = false;
        DirtyCallback m_onDirty;
    };
}
