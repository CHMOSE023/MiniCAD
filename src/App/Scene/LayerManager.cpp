#include "LayerManager.h"  
#include "Serialization/ISerializer.h"
namespace MiniCAD
{
    LayerManager::LayerManager()
    {
        m_layers[Layer::DefaultLayerID] = std::make_unique<Layer>(Layer::DefaultLayerID, "Default");  // 默认图层
    }

    LayerID LayerManager::AddLayer(const std::string& name)
    {
        LayerID id = m_nextID.fetch_add(1);
        m_layers[id] = std::make_unique<Layer>(id, name);
        return id;
    }
    LayerID LayerManager::AddLayer(std::unique_ptr<Layer> layer)
    {
        LayerID id = layer->GetID();
        if (id == Layer::DefaultLayerID || m_layers.count(id))

            return Layer::DefaultLayerID; // ID 已存在，拒绝添加

        m_layers[id] = std::move(layer);

        // 保持 m_nextID 大于现有所有 ID
        if (id >= m_nextID)
            m_nextID = id + 1;

        return id;
    }

    bool LayerManager::RemoveLayer(LayerID id)
    {
        if (id == Layer::DefaultLayerID)
            return false;

        return m_layers.erase(id) > 0;
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
        for (auto& kv : m_layers) ids.push_back(kv.first);
        return ids;
    }

    void LayerManager::SetActiveLayerID(LayerID id)
    {
        if (m_layers.count(id)) m_activeLayerID = id;
    }
    void LayerManager::Serialize(ISerializer& s) const
    {        
        s.WriteUInt64(m_layers.size());           // 图层数量
        for (const auto& [id, layer] : m_layers)  // 每个 Layer
        {
            layer->Serialize(s);   
        }         
        s.WriteUInt64(m_activeLayerID);           // 当前激活图层
        s.WriteUInt64(m_nextID.load());           // ID生成器状态
    }
    void LayerManager::Deserialize(ISerializer & s)
    {     
        m_layers.clear();                                // 清空现有数据            
        uint64_t count = s.ReadUInt64();                 // 读图层数量        
        for (uint64_t i = 0; i < count; ++i)             // 读每个 Layer
        {
            auto layer = std::make_unique<Layer>(0, ""); // 临时占位

            layer->Deserialize(s);                       // 让 Layer 自己恢复数据
            LayerID id = layer->GetID();
            m_layers[id] = std::move(layer);
        }  
        m_activeLayerID = s.ReadUInt64();                 //  读当前激活图层       
        m_nextID = s.ReadUInt64();                        // 恢复 ID 生成器
    }
}
