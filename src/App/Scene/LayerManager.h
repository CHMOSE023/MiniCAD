#pragma once 
#include "App/Scene/Layer.h"
#include <atomic> 
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>  
#include "Core/Object/Object.hpp"
#include "Serialization/ISerializer.h"
namespace MiniCAD
{
	class ISerializer; 

	class LayerManager
	{
	public:
		LayerManager();
		LayerID AddLayer(const std::string& name);      // 添加图层，返回新图层 ID
		LayerID AddLayer(std::unique_ptr<Layer> layer);

		bool RemoveLayer(LayerID id);                   // 移除图层（不能移除默认图层 0）

		Layer*       GetLayer(LayerID id);
		const Layer* GetLayer(LayerID id) const;

		std::vector<LayerID> GetAllLayerIDs() const;    // 所有图层 ID 列表

		LayerID GetActiveLayerID() const { return m_activeLayerID; }
		void    SetActiveLayerID( LayerID id); 

		// --- Serialization 接口 ---
		// 将对象写入序列化器
		void Serialize(ISerializer& s) const;

		// 从序列化器读取对象状态
		void Deserialize(ISerializer& s) ;

	private:
		std::unordered_map< LayerID, std::unique_ptr<Layer>> m_layers;

		 LayerID                m_activeLayerID = Layer::DefaultLayerID;
		std::atomic< LayerID>   m_nextID{ 1 };
	};
}