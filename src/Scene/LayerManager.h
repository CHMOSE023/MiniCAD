#pragma once 
#include "Scene/Layer.h"
#include  "Core/Entity/EntityAttr.hpp"
#include <atomic> 
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>   
namespace MiniCAD
{
	class LayerManager
	{
	public:
		LayerManager();
		LayerID AddLayer(const std::string& name);      // 添加图层，返回新图层 ID
		LayerID AddLayer(std::unique_ptr<Layer> layer);

		bool RemoveLayer(LayerID id);                   // 移除图层（不能移除默认图层 0）

		Layer* GetLayer(LayerID id);
		const Layer* GetLayer(LayerID id) const;

		std::vector<LayerID> GetAllLayerIDs() const;    // 所有图层 ID 列表

		const Layer&  GetActiveLayer()   const { return *m_layers.at(m_activeLayerID); };
		const LayerID GetActiveLayerID() const { return m_activeLayerID; }
		void          SetActiveLayerID(LayerID id);

	private:
		std::unordered_map< LayerID, std::unique_ptr<Layer>> m_layers;

		LayerID                 m_activeLayerID = Layer::DefaultLayerID;
		std::atomic< LayerID>   m_nextID{ 1 };
	};
}
