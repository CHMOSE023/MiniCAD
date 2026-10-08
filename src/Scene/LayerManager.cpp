#include "LayerManager.h"
#include "Layer.h"
#include "Serialization/ISerializer.h"
#include <algorithm>
#include <utility>
#include <memory>
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
        {
            return Layer::DefaultLayerID; // ID 已存在，拒绝添加
        }

        m_layers[id] = std::move(layer);


        if (id >= m_nextID)               // 保持 m_nextID 大于现有所有 ID
        {
            m_nextID = id + 1;
        }

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
        uint32_t active = m_activeLayerID;
        s.Value("active", active);
        uint32_t next = m_nextID.load();
        s.Value("nextId", next);

        // 按 ID 升序写出,保证文件内容确定。
        std::vector<LayerID> ids;
        ids.reserve(m_layers.size());
        for (auto& kv : m_layers) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());

        size_t n = ids.size();
        if (s.BeginArray("items", n))
        {
            for (LayerID id : ids)
            {
                if (s.BeginElement(0))
                {
                    m_layers.at(id)->Serialize(s);
                    s.EndElement();
                }
            }
            s.EndArray();
        }
    }

    void LayerManager::Deserialize(ISerializer& s)
    {
        m_layers.clear();

        size_t n = 0;
        if (s.BeginArray("items", n))
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (!s.BeginElement(i)) continue;
                auto layer = std::make_unique<Layer>();
                layer->Deserialize(s);
                m_layers[layer->GetID()] = std::move(layer);
                s.EndElement();
            }
            s.EndArray();
        }

        // 防御:文件缺默认图层时补回(实体的 LayerId 0 必须可解析)。
        if (!m_layers.count(Layer::DefaultLayerID))
            m_layers[Layer::DefaultLayerID] = std::make_unique<Layer>(Layer::DefaultLayerID, "Default");

        uint32_t active = Layer::DefaultLayerID;
        s.Value("active", active);
        m_activeLayerID = m_layers.count(active) ? active : Layer::DefaultLayerID;

        uint32_t next = 1;
        s.Value("nextId", next);
        for (auto& kv : m_layers)
            next = std::max(next, kv.first + 1);
        m_nextID.store(next);
    }
}
