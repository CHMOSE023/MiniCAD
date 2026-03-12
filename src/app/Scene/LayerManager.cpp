// ============================================================
// MiniCAD — app/Scene/LayerManager.cpp
// ============================================================
#include "app/Scene/LayerManager.h"
#include "app/Scene/Layer.hpp"
#include <vector>

namespace MiniCAD {

LayerManager::LayerManager() {
    // 默认图层
    m_layers[DEFAULT_LAYER_ID] = std::make_unique<Layer>(DEFAULT_LAYER_ID, "Default");
}

Layer::LayerID LayerManager::AddLayer(const std::string& name) {
    Layer::LayerID id = m_nextID.fetch_add(1);
    m_layers[id] = std::make_unique<Layer>(id, name);
    return id;
}

bool LayerManager::RemoveLayer(Layer::LayerID id) {
    if (id == DEFAULT_LAYER_ID) return false;
    return m_layers.erase(id) > 0;
}

Layer* LayerManager::GetLayer(Layer::LayerID id) {
    auto it = m_layers.find(id);
    return it != m_layers.end() ? it->second.get() : nullptr;
}

const Layer* LayerManager::GetLayer(Layer::LayerID id) const {
    auto it = m_layers.find(id);
    return it != m_layers.end() ? it->second.get() : nullptr;
}

std::vector<Layer::LayerID> LayerManager::GetAllLayerIDs() const {
    std::vector<Layer::LayerID> ids;
    ids.reserve(m_layers.size());
    for (auto& kv : m_layers) ids.push_back(kv.first);
    return ids;
}

void LayerManager::SetActiveLayerID(Layer::LayerID id) {
    if (m_layers.count(id)) m_activeLayerID = id;
}

} // namespace MiniCAD
