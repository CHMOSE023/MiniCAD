#pragma once
#include "Render/Viewport/Grid.h"
#include "Render/D3D11/Renderer.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Object/Object.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "App/Scene/LayerManager.h"
#include <atomic>
namespace MiniCAD
{ 
	class Scene
	{ 
	public:
		using ObjectID = Object::ObjectID;
		using DirtyCallback = std::function<void()>;
		 
		Scene() = default;   

		void AddEntity(std::unique_ptr<Object> entity);     // 添加实体 	
		std::unique_ptr<Object> RemoveEntity(ObjectID id); 	// 移除并返回所有权（供 Undo 使用）
		 
		Object* GetEntity(ObjectID id);                     // 通过ID查询
		const Object* GetEntity(ObjectID id) const;

		bool Has(ObjectID id) const;

		std::vector<ObjectID> GetAllIDs() const;            // 返回所有实体 ID

		LayerManager& GetLayerManager() { return m_layerManager; }
		const LayerManager& GetLayerManager() const { return m_layerManager; }

		// ── DirtyFlag ── 
		bool IsDirty() const { return m_dirty; }
		void MarkDirty()     { m_dirty = true; if (m_onDirty) m_onDirty(); }
		void ClearDirty()    { m_dirty = false; }

		void SetDirtyCallback(DirtyCallback cb) { m_onDirty = std::move(cb); }

		int EntityCount() const { return (int)m_entities.size(); }

		const std::unordered_map<ObjectID, std::unique_ptr<Object>>& GetEntities() const { return m_entities; }

		// ── 序列化 ───────────────────────────────────────────
		void Serialize(ISerializer& s) const;
		void Deserialize(ISerializer& s);

		// ── ID 分配 ──────────────────────────────────────────
		// 创建新实体时调用，保证 ID 全局唯一且不与已加载数据冲突
		ObjectID NextObjectID() { return m_nextObjectID.fetch_add(1, std::memory_order_relaxed); }
	private:     
		void SyncNextObjectID();

		std::unordered_map<ObjectID, std::unique_ptr<Object>> m_entities;

		std::atomic<ObjectID>    m_nextObjectID{ 1 };   // 0 保留为 InvalidID

		LayerManager  m_layerManager;
		bool          m_dirty = false;
		DirtyCallback m_onDirty;
	};
}