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
	// ── 图纸布局 ──────────────────────────────────────────────────────────
	// 模型空间之外的「页」用纸张范围显示；每张布局对应一个图纸空间 BLOCK_RECORD。
	//
	// OwnerKey 是 DXF LAYOUT 的组码 330（owner，指向该布局的图纸空间 BLOCK_RECORD）。
	// 与 EntityAttr::LayoutKey 配对：图纸空间实体在导入时记录自己的组号 330，
	// 渲染 / 拾取只显示 `LayoutKey == 当前布局.OwnerKey` 的实体 ——
	// 没有这一项，切换布局只能改相机、改不了画什么（每张布局都显示同一批视口内容）。
	// 0 表示未提供（模型布局 / 老数据）→ 不做归属过滤（退化为全部显示）。
	struct DrawingLayout
	{
		std::string Name  = "模型";
		bool        Model = true;
		int         Order = 0;
		double      MinX = 0.0;
		double      MinY = 0.0;
		double      MaxX = 420.0;      // A3 横向，与 AutoCAD 默认图纸相近
		double      MaxY = 297.0;
		uint64_t    OwnerKey = 0;      // LAYOUT 组码 330
	};

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

		// ── 图纸布局（LAYOUT）────────────────────────────────────
		// 至少一项（模型页）。SetLayouts 总会保证第一项是模型页。
		const std::vector<DrawingLayout>& Layouts() const { return m_layouts; }
		// 同时保证每张图纸布局的 OwnerKey 非 0 且互不相同（缺的补一个新键，见 NewLayoutKey）：
		// 布局的增删、撤销、切换都按这个键认布局，导出时也按它区分各张布局的块记录
		void SetLayouts(std::vector<DrawingLayout> layouts);
		// 返回模型页的索引（通常为 0）
		int  ModelLayoutIndex() const;
		// 按 OwnerKey 找图纸布局，没有返回 -1
		int  FindLayout(uint64_t ownerKey) const;
		// 新建布局用的归属键：取在不会与 DWG 句柄冲突的高位区间，导出后读回会换成真正的块记录句柄
		uint64_t NewLayoutKey() const;

		// 当前空间：新建的实体落在哪里（EditorContext::ApplyCurrentAttr）。由 Document::SetActiveLayout 维护
		void     SetCurrentSpace(bool paper, uint64_t layoutKey) { m_currentPaper = paper; m_currentLayoutKey = paper ? layoutKey : 0; }
		bool     CurrentSpaceIsPaper() const { return m_currentPaper; }
		uint64_t CurrentLayoutKey() const    { return m_currentLayoutKey; }
		// 一张布局的读写（随 .mcad / JSON 保存，DWG / DXF 经 CadExchange 的 LAYOUT）
		static void SerializeLayout(ISerializer& s, DrawingLayout& l);

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

		// ── 全局线型比例（DXF HEADER 的 $LTSCALE）─────────────────────────────
		// 虚线铺设的实际周期 = LTYPE图案周期 × 实体线型比例(组码48) × 本值。
		//
		// 为什么必须带着它：大尺寸图纸（坐标在百万量级）为了让虚线在图上物理长度合理，
		// 会把 $LTSCALE 设成几百上千（实测 test0.dwg = 1000），而 LTYPE 表里的
		// 图案周期是小数值（DASH = 0.35）。少了这一乘，周期会小 1000 倍 ——
		// 再叠加渲染层「一个周期不足 4 像素就退化为实线」的护栏，虚线会整条消失。
		double GetLineTypeScale() const      { return m_lineTypeScale; }
		// <= 0 视为无效，按 1 处理（AutoCAD 也不接受非正值）
		void   SetLineTypeScale(double s)    { m_lineTypeScale = (s > 0.0 ? s : 1.0); }

		// ── 点样式（DXF HEADER 的 $PDMODE / $PDSIZE）──────────────────
		//
		// POINT 实体**自身不带显示信息**：DXF/DWG 里只有坐标（10/20/30）、
		// 可选的挤出方向（210）和角度（50）。画成什么样完全由图纸级 $PDMODE 决定。
		//
		// $PDMODE 是位组合（AutoCAD 的 PDMODE 定义）：
		//     低 4 位选"基本形"：0=实心小点 1=什么都不画 2=十字 3=叉 4=竖线
		//     bit5 (32) = 外加一个外接圆
		//     bit6 (64) = 外加一个外接方
		//     96 = 32|64 = 圆和方都加
		// 例：34 = 32|2 = 圆 + 十字 = ⊕。
		// 低 4 位取 0~4 之外时按"十字"处理。
		//
		// $PDSIZE 是图形尺寸：**>0 = 世界单位**；=0 = 视口高度的 5%；
		// <0 = 视口高度的 |值|%。后两种是**屏幕相对**的，换算还需要
		// worldPerPixel 与视口像素高度（由绘制时经 IDrawSink 提供）。
		//
		// 默认 0/0 = AutoCAD 默认"实心小点"。
		int    GetPointDisplayMode() const   { return m_pointDisplayMode; }
		void   SetPointDisplayMode(int m)    { m_pointDisplayMode = m; }
		double GetPointDisplaySize() const   { return m_pointDisplaySize; }
		void   SetPointDisplaySize(double s) { m_pointDisplaySize = s; }

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
		void MarkDirty()      { m_allDirty = true; m_geomAllDirty = true; m_dirty = true; ++m_geomVersion; ++m_displayVersion; if (m_onDirty) m_onDirty(); }
		void MarkDisplayDirty() { m_allDirty = true; m_dirty = true; ++m_displayVersion; if (m_onDirty) m_onDirty(); }
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
		// 显示版本号：全局显示变化（MarkDisplayDirty / MarkDirty）时自增，如图层开关改变块内子实体是否参与拾取
		uint64_t DisplayVersion() const { return m_displayVersion; }

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
		std::vector<DrawingLayout> m_layouts{ DrawingLayout{} };  // 图纸布局，第 0 项为模型页
		bool          m_currentPaper     = false;   // 当前空间是否为图纸空间
		uint64_t      m_currentLayoutKey = 0;       // 当前布局的 OwnerKey（模型空间为 0）
		LineTypeID    m_currentLineType   = LineTypeTable::ByLayerID;  // 当前线型(CELTYPE)
		Lineweight    m_currentLineweight = Lineweight::ByLayer;       // 当前线宽(CELWEIGHT)
		int           m_pointDisplayMode   = 0;      // $PDMODE：0 = 一个像素小点（AutoCAD 默认）
		double        m_lineTypeScale      = 1.0;    // $LTSCALE：全局线型比例
		double        m_pointDisplaySize   = 0.0;    // $PDSIZE：0 = 按视口高度百分比
		bool          m_dirty    = false;
		bool          m_allDirty = false;                  // 全局脏：所有实体顶点缓存失效
		bool          m_geomAllDirty = false;              // 全局几何脏：拾取空间索引需整表重建
		std::unordered_set<ObjectID> m_dirtyEntities;      // 实体级脏：仅这些实体需重新细分
		uint64_t      m_geomVersion = 0;   // 几何版本号（见 GeometryVersion）
		uint64_t      m_displayVersion = 0;    // 显示版本号（见 DisplayVersion）
		DirtyCallback m_onDirty;
	};
}
