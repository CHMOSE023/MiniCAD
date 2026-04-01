#include "LayerManager.h"  
namespace MiniCAD
{
    LayerManager::LayerManager() 
    { 
        m_layers[Object::DefaultLayerID] = std::make_unique<Layer>(Object::DefaultLayerID, "Default");  // 默认图层
    }

    Layer::LayerID LayerManager::AddLayer(const std::string& name) 
    {
        Layer::LayerID id = m_nextID.fetch_add(1);
        m_layers[id] = std::make_unique<Layer>(id, name);
        return id;
    }
    Layer::LayerID LayerManager::AddLayer(std::unique_ptr<Layer> layer)
    {
        Layer::LayerID id = layer->GetID();
        if (id == Object::DefaultLayerID || m_layers.count(id)) 

			return Object::DefaultLayerID; // ID 已存在，拒绝添加

        m_layers[id] = std::move(layer);

        // 保持 m_nextID 大于现有所有 ID
        if (id >= m_nextID)
            m_nextID = id + 1;

        return id;
    }

    bool LayerManager::RemoveLayer(Layer::LayerID id) 
    {
        if (id == Object::DefaultLayerID)
            return false;

        return m_layers.erase(id) > 0;
    }

    Layer* LayerManager::GetLayer(Layer::LayerID id)
    {
        auto it = m_layers.find(id);
        return it != m_layers.end() ? it->second.get() : nullptr;
    }

    const Layer* LayerManager::GetLayer(Layer::LayerID id) const 
    {
        auto it = m_layers.find(id);
        return it != m_layers.end() ? it->second.get() : nullptr;
    } 

    std::vector<Layer::LayerID> LayerManager::GetAllLayerIDs() const
    {
        std::vector<Layer::LayerID> ids;
        ids.reserve(m_layers.size());
        for (auto& kv : m_layers) ids.push_back(kv.first);
        return ids;
    }

    void LayerManager::SetActiveLayerID(Layer::LayerID id)
    {
        if (m_layers.count(id)) m_activeLayerID = id;
    } 
}
