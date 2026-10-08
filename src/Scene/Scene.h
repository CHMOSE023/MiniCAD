#pragma once
#include "Core/Object/Object.hpp" 
#include "Scene/LayerManager.h"
#include "Scene/LineTypeTable.h"
#include "Scene/TextStyleTable.h"
#include "Scene/MLineStyleTable.h"
#include "BlockTable.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <atomic>
namespace MiniCAD
{
	class ISerializer;
	class InsertEntity;
	class Scene 
	{
	public:

		using ObjectID = Object::ObjectID;
		using DirtyCallback = std::function<void()>;

		Scene() = default;

		void AddEntity(std::unique_ptr<Object> entity);       // 添加实体 	
		std::unique_ptr<Object> RemoveEntity(ObjectID id); 	  // 移除并返回所有权（供 Undo 使用）
		  
		Object*       GetEntity(ObjectID id);                 // 通过ID查询
		const Object* GetEntity(ObjectID id) const ;

		bool Has(ObjectID id) const ;

		// 对象是否在锁定的图层上（锁定语义同 AutoCAD：可见、可选中、可捕捉，但不能修改 / 删除）。
		// 所有会修改或删除已有对象的操作都应跳过锁定对象；复制 / 偏移这类只读取原对象的操作不受限制
		bool IsEntityLocked(const Object& obj) const;

		std::vector<ObjectID> GetAllIDs() const;             // 返回所有实体 ID

		LayerManager&       GetLayerManager() { return m_layerManager; }
		const LayerManager& GetLayerManager() const  { return m_layerManager; }

		// ── 块表（BLOCK_RECORD）─────────────────────────────────
		BlockTable&       GetBlockTable()       { return m_blockTable; }
		const BlockTable& GetBlockTable() const { return m_blockTable; }

		// ── 线型表（LTYPE）──────────────────────────────────────
		LineTypeTable&       GetLineTypeTable()       { return m_lineTypeTable; }
		const LineTypeTable& GetLineTypeTable() const { return m_lineTypeTable; }

		// ── 文字样式表（STYLE）与当前文字样式（TEXTSTYLE）───────
		TextStyleTable&       GetTextStyleTable()       { return m_textStyleTable; }
		const TextStyleTable& GetTextStyleTable() const { return m_textStyleTable; }
		// 场景实体与块定义中用到的文字样式 ID（按绘制时输出的文字统计，覆盖所有带文字的实体）
		std::vector<TextStyleID> CollectUsedTextStyles() const;
		TextStyleID GetCurrentTextStyle() const          { return m_currentTextStyle; }
		void        SetCurrentTextStyle(TextStyleID id)  { m_currentTextStyle = id; }

		// ── 多线样式表（MLSTYLE）与当前多线样式 ─────────────────
		MLineStyleTable&       GetMLineStyleTable()       { return m_mlineStyleTable; }
		const MLineStyleTable& GetMLineStyleTable() const { return m_mlineStyleTable; }
		MLineStyleID GetCurrentMLineStyle() const         { return m_currentMLineStyle; }
		void         SetCurrentMLineStyle(MLineStyleID id){ m_currentMLineStyle = id; }

		// ── 当前实体属性（对应 AutoCAD CELTYPE / CELWEIGHT）──────
		// 新建实体时由工具读取写入 EntityAttr,默认 ByLayer。
		LineTypeID GetCurrentLineType() const            { return m_currentLineType; }
		void       SetCurrentLineType(LineTypeID id)     { m_currentLineType = id; }
		Lineweight GetCurrentLineweight() const          { return m_currentLineweight; }
		void       SetCurrentLineweight(Lineweight lw)   { m_currentLineweight = lw; }

		// 把单个 INSERT 的非拥有块指针解析到块表中的块定义（绘制前必需）。
		void ResolveInsert(InsertEntity& ins);
		// 批量重解析所有 INSERT（反序列化 / 加载完成后调用）。
		void ResolveAllInserts();

		void  ForEachObject(std::function<void(const Object&)> fn) const;  // 遍历所有实体（核心接口，供 ISceneReader 实现）  
		void  ForEachPreview(std::function<void(const Object&)> fn) const; // 遍历所有预览对象（核心接口，供 ISceneReader 实现）

		// ── DirtyFlag ──
		// 三种标脏：
		//   MarkEntityDirty(id) —— 单实体几何/属性变化（增删改），渲染端只需
		//                          重新细分该实体，其余实体可复用缓存顶点。
		//   MarkDirty()         —— 全局几何变化（批量加载、撤销/重做等），
		//                          所有实体缓存失效，拾取空间索引整表重建。
		//   MarkDisplayDirty()  —— 只有显示属性变化（图层颜色/可见性/线型/线宽等），
		//                          所有实体顶点缓存失效，但几何不变：不改几何版本号，
		//                          拾取空间索引保持不动（几十万实体时整表重建要上百毫秒）。
		bool IsDirty() const  { return m_dirty; }
		void MarkDirty()      { m_allDirty = true; m_geomAllDirty = true; m_dirty = true; ++m_geomVersion; if (m_onDirty) m_onDirty(); }
		void MarkDisplayDirty() { m_allDirty = true; m_dirty = true; if (m_onDirty) m_onDirty(); }
		void MarkEntityDirty(ObjectID id)
		{
			m_dirtyEntities.insert(id);
			m_dirty = true; ++m_geomVersion; if (m_onDirty) m_onDirty();
		}
		void ClearDirty()     { m_dirty = false; m_allDirty = false; m_geomAllDirty = false; m_dirtyEntities.clear(); }

		bool IsAllDirty() const         { return m_allDirty; }        // 所有实体的显示缓存失效
		bool IsGeometryAllDirty() const { return m_geomAllDirty; }    // 全局几何变化（MarkDirty）
		const std::unordered_set<ObjectID>& GetDirtyEntities() const { return m_dirtyEntities; }

		// ── 几何版本号 ──────────────────────────────────────────
		// 每次几何/拓扑变化（增删实体、编辑、平移/缩放标脏）自增。
		// 供拾取的空间索引判断是否需要重建（与相机无关，仅几何变更才需重建）。
		uint64_t GeometryVersion() const { return m_geomVersion; }

		void SetDirtyCallback(DirtyCallback cb) { m_onDirty = std::move(cb); }

		int EntityCount() const  { return (int)m_entities.size(); }

		// 所有可见的有界实体的包围盒（缩放到全图用）；没有可算的实体时返回 false
		bool GetExtents(AABB& out) const;
		  
		// ── 序列化 ───────────────────────────────────────────
		void Serialize(ISerializer& s) const;
		void Deserialize(ISerializer& s);

		// ── ID 分配 ────────────────────────────────────────── 
		ObjectID NextObjectID() { return m_nextObjectID.fetch_add(1, std::memory_order_relaxed); }

	private:
		void SyncNextObjectID();

	private:
		std::unordered_map<ObjectID, std::unique_ptr<Object>> m_entities;

		std::vector<std::unique_ptr<Object>> m_previews;          // 预览对象 ID 列表（保持顺序） 
		std::atomic<ObjectID>                m_nextObjectID{ 1 }; // 0 保留为 InvalidID

		LayerManager  m_layerManager;
		BlockTable    m_blockTable;        // 块定义表，拥有全部 BlockEntity
		LineTypeTable m_lineTypeTable;     // 线型定义表，拥有全部 LineTypeEntity
		TextStyleTable m_textStyleTable;   // 文字样式表
		TextStyleID    m_currentTextStyle = TextStyleTable::StandardID;   // 当前文字样式(TEXTSTYLE)
		MLineStyleTable m_mlineStyleTable; // 多线样式表
		MLineStyleID    m_currentMLineStyle = MLineStyleTable::StandardID;   // 当前多线样式(CMLSTYLE)
		LineTypeID    m_currentLineType   = LineTypeTable::ByLayerID;  // 当前线型(CELTYPE)
		Lineweight    m_currentLineweight = Lineweight::ByLayer;       // 当前线宽(CELWEIGHT)
		bool          m_dirty    = false;
		bool          m_allDirty = false;                  // 全局脏：所有实体顶点缓存失效
		bool          m_geomAllDirty = false;              // 全局几何脏：拾取空间索引需整表重建
		std::unordered_set<ObjectID> m_dirtyEntities;      // 实体级脏：仅这些实体需重新细分
		uint64_t      m_geomVersion = 0;   // 几何版本号（见 GeometryVersion）
		DirtyCallback m_onDirty;
	};
}
