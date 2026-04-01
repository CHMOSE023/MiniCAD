#pragma once 
#include "App/Scene/Layer.h"
#include <atomic> 
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>  
#include "Core/Object/Object.hpp"
namespace MiniCAD
{
	class LayerManager
	{
	public:
		LayerManager();
		Layer::LayerID AddLayer(const std::string& name);      // 添加图层，返回新图层 ID
		Layer::LayerID AddLayer(std::unique_ptr<Layer> layer);

		bool RemoveLayer(Layer::LayerID id);                   // 移除图层（不能移除默认图层 0）

		Layer*       GetLayer(Layer::LayerID id);
		const Layer* GetLayer(Layer::LayerID id) const;

		std::vector<Layer::LayerID> GetAllLayerIDs() const;    // 所有图层 ID 列表

		Layer::LayerID GetActiveLayerID() const { return m_activeLayerID; }
		void           SetActiveLayerID(Layer::LayerID id); 

	private:
		std::unordered_map<Layer::LayerID, std::unique_ptr<Layer>> m_layers;

		Layer::LayerID                m_activeLayerID = Object::DefaultLayerID;
		std::atomic<Layer::LayerID>   m_nextID{ 1 };
	};
}