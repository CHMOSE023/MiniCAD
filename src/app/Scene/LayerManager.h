// ============================================================
// MiniCAD — app/Scene/LayerManager.h
// 职责：图层管理：增删改查 / 当前激活图层
// 依赖：app/Scene/Layer.h
// 约束：不依赖 render/ ui/
// ============================================================
#pragma once

#include "app/Scene/Layer.hpp"
#include <unordered_map>
#include <memory>
#include <vector>
#include <atomic> 

namespace MiniCAD {

    class LayerManager {
    public:
        LayerManager();

        // 添加图层，返回新图层 ID
        Layer::LayerID AddLayer(const std::string& name);

        // 移除图层（不能移除默认图层 0）
        bool RemoveLayer(Layer::LayerID id);

        Layer* GetLayer(Layer::LayerID id);
        const Layer* GetLayer(Layer::LayerID id) const;

        // 所有图层 ID 列表
        std::vector<Layer::LayerID> GetAllLayerIDs() const;

        Layer::LayerID GetActiveLayerID() const { return m_activeLayerID; }
        void           SetActiveLayerID(Layer::LayerID id);

        // 默认图层 ID
        static constexpr Layer::LayerID DEFAULT_LAYER_ID = 0;

    private:
        std::unordered_map<Layer::LayerID, std::unique_ptr<Layer>> m_layers;
        Layer::LayerID                m_activeLayerID = DEFAULT_LAYER_ID;
        std::atomic<Layer::LayerID>   m_nextID{ 1 };
    };

} // namespace MiniCAD
