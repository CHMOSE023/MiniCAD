#include "LayerManager.h"
#include "Serialization/ISerializer.h"

namespace MiniCAD
{
    LayerManager::LayerManager()
    {
        m_layers[Layer::DefaultLayerID] = std::make_unique<Layer>(Layer::DefaultLayerID, "Default");
        WireLayerCallbacks(*m_layers[Layer::DefaultLayerID]);
    }

    LayerID LayerManager::AddLayer(const std::string& name)
    {
        LayerID id = m_nextID.fetch_add(1);
        m_layers[id] = std::make_unique<Layer>(id, name);
        WireLayerCallbacks(*m_layers[id]);
        NotifyChanged();
        return id;
    }

    LayerID LayerManager::AddLayer(std::unique_ptr<Layer> layer)
    {
        if (!layer)
            return Layer::DefaultLayerID;

        LayerID id = layer->GetID();
        if (id == Layer::DefaultLayerID || m_layers.count(id))
            return Layer::DefaultLayerID;

        WireLayerCallbacks(*layer);
        m_layers[id] = std::move(layer);
        if (id >= m_nextID)
            m_nextID = id + 1;

        NotifyChanged();
        return id;
    }

    bool LayerManager::RemoveLayer(LayerID id)
    {
        if (id == Layer::DefaultLayerID)
            return false;

        if (m_activeLayerID == id)
            m_activeLayerID = Layer::DefaultLayerID;

        const bool removed = m_layers.erase(id) > 0;
        if (removed)
            NotifyChanged();

        return removed;
    }

    bool LayerManager::HasLayer(LayerID id) const
    {
        return m_layers.count(id) > 0;
    }

    Layer* LayerManager::GetLayer(LayerID id)
    {
        auto it = m_layers.find(id);
        return it != m_layers.end() ? it->second.get() : nullptr;
    }

    const Layer* LayerManager::GetLayer(LayerID id) const
    {
        auto it = m_layers.find(id);
        return it != m_layers.end() ? it->second.get() : nullptr;
    }

    std::vector<LayerID> LayerManager::GetAllLayerIDs() const
    {
        std::vector<LayerID> ids;
        ids.reserve(m_layers.size());
        for (const auto& kv : m_layers)
            ids.push_back(kv.first);
        return ids;
    }

    void LayerManager::SetActiveLayerID(LayerID id)
    {
        if (HasLayer(id) && m_activeLayerID != id)
        {
            m_activeLayerID = id;
            NotifyChanged();
        }
    }

    void LayerManager::SetChangeCallback(ChangeCallback cb)
    {
        m_onChange = std::move(cb);
        for (auto& [id, layer] : m_layers)
            WireLayerCallbacks(*layer);
    }

    void LayerManager::Serialize(ISerializer& s) const
    {
        s.WriteUInt64(m_layers.size());
        for (const auto& [id, layer] : m_layers)
            layer->Serialize(s);

        s.WriteUInt64(m_activeLayerID);
        s.WriteUInt64(m_nextID.load());
    }

    void LayerManager::Deserialize(ISerializer& s)
    {
        m_layers.clear();

        uint64_t count = s.ReadUInt64();
        for (uint64_t i = 0; i < count; ++i)
        {
            auto layer = std::make_unique<Layer>(0, "");
            layer->Deserialize(s);
            WireLayerCallbacks(*layer);
            m_layers[layer->GetID()] = std::move(layer);
        }

        if (!HasLayer(Layer::DefaultLayerID))
        {
            m_layers[Layer::DefaultLayerID] = std::make_unique<Layer>(Layer::DefaultLayerID, "Default");
            WireLayerCallbacks(*m_layers[Layer::DefaultLayerID]);
        }

        m_activeLayerID = static_cast<LayerID>(s.ReadUInt64());
        if (!HasLayer(m_activeLayerID))
            m_activeLayerID = Layer::DefaultLayerID;

        m_nextID = static_cast<LayerID>(s.ReadUInt64());
        if (m_nextID <= Layer::DefaultLayerID)
            m_nextID = 1;
    }

    void LayerManager::NotifyChanged()
    {
        if (m_onChange)
            m_onChange();
    }

    void LayerManager::WireLayerCallbacks(Layer& layer)
    {
        layer.SetChangeCallback([this]()
            {
                NotifyChanged();
            });
    }
}
