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

namespace MiniCAD
{ 
	class Scene
	{ 
	public:
		using ObjectID = Object::ObjectID;
		using DirtyCallback = std::function<void()>;


		Scene() = default;   // 显式要回默认构造, 生成默认构造函数 

		void AddEntity(std::unique_ptr<Object> entity);     // 添加实体 	
		std::unique_ptr<Object> RemoveEntity(ObjectID id); 	// 移除并返回所有权（供 Undo 使用）
		 
		Object* GetEntity(ObjectID id);                     // 通过ID查询
		const Object* GetEntity(ObjectID id) const;

		bool Has(ObjectID id) const;

		std::vector<ObjectID> GetAllIDs() const;            // 返回所有实体 ID
		 

		// ── DirtyFlag ── 
		bool IsDirty() const { return m_dirty; }
		void MarkDirty()     { m_dirty = true; if (m_onDirty) m_onDirty(); }
		void ClearDirty()    { m_dirty = false; }

		void SetDirtyCallback(DirtyCallback cb) { m_onDirty = std::move(cb); }

		int EntityCount() const { return (int)m_entities.size(); }
	private:     

		std::unordered_map<ObjectID, std::unique_ptr<Object>> m_entities;

		bool          m_dirty = false;
		DirtyCallback m_onDirty;
	};
}