#pragma once
#include "Import/CadExchange.h"

namespace MiniCAD { class FontSystem; }
namespace MiniCAD::Nodes { class Graph; }

#include "Scene/Scene.h"
#include "Scene/LayerManager.h"
#include "Document/CommandStack/CommandStack.h"
#include "Viewport/Camera.h"
#include "Core/Entity/EntityAttr.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
	class ISerializer;
	class Document
	{
	public:
		Document();
		~Document();

	    // ── 数据访问 ──────────────────────────────────────────
        Scene&              GetScene()              { return m_scene; }
        const Scene&        GetScene()  const       { return m_scene; }

        CommandStack&       GetCommandStack()       { return m_cmdStack; }
        const CommandStack& GetCommandStack() const { return m_cmdStack; }

        LayerManager&       GetLayerManager()       { return m_scene.GetLayerManager(); }
        const LayerManager& GetLayerManager() const { return m_scene.GetLayerManager(); }

		// ── Undo / Redo ───────────────────────────────────────
		void Undo()          { m_cmdStack.Undo(m_scene); }
		void Redo()          { m_cmdStack.Redo(m_scene); }
		bool CanUndo() const { return m_cmdStack.CanUndo(); }
		bool CanRedo() const { return m_cmdStack.CanRedo(); }

		// ── 文档信息 ───────────────────────────────
		const std::string& GetName() const { return m_name; }
		const std::string& GetPath() const { return m_path; }

		void SetPath(const std::string& path);
		void SetName(const std::string& name);

		bool IsDirty() const { return m_dirty; }
		void MarkDirty()     { m_dirty = true; }
		void MarkSaved()     { m_dirty = false; }

		bool HasPath() const { return !m_path.empty(); }

		// 保存 / 另存为 / 打开(JSON 文档)
		bool Save();
		bool SaveAs(const std::string& path);
		bool SaveToFile(const std::string& path);

		// 保存为 DWG / DXF 时的版本：打开 DWG / DXF 时取自文件，另存为时由用户选择
		void           SetCadSaveVersion(CadSaveVersion v) { m_cadVersion = v; }
		CadSaveVersion GetCadSaveVersion() const          { return m_cadVersion; }
		// progress：DWG / DXF 的读取进度（0～1），在调用线程上定期调用；其他格式不报告
		bool LoadFromFile(const std::string& path, std::string* error = nullptr, const ImportProgress& progress = nullptr);

		// 字符串级存取(Web 端下载/上传等无文件系统场景)。
		// 加载按内容嗅探格式(MCAD 魔数 → 二进制,否则 JSON),不改变路径。
		std::string SaveToString(bool binary = false) const;
		bool        LoadFromString(const std::string& text);

		// 由 DocumentManager 在创建文档后注入，用于矢量文字渲染
		void SetFontSystem(FontSystem* fs) { m_fontSystem = fs; }
		FontSystem* GetFontSystem() const  { return m_fontSystem; }

		// ── 视口状态（切换文档时保存/恢复）──────────────────────
		const CameraState& GetCameraState() const    { return m_cameraState; }
		void SetCameraState(const CameraState& s)    { m_cameraState = s; }

		// ── 图纸布局（LAYOUT / 图纸空间）─────────────────────────
		// 当前活动布局的索引（0 = 模型页，Scene::SetLayouts 保证）
		int  ActiveLayout() const;
		// 当前布局是否为图纸空间（非模型页）
		bool ActiveLayoutIsPaper() const;
		// 当前布局的图纸空间归属键（DrawingLayout::OwnerKey）；模型页返回 0 = 不过滤
		uint64_t ActiveLayoutOwnerKey() const;
		// 切换布局：保存旧布局的视图、换新布局的视图、清选择集。
		// 相机状态由调用方使用（Document 不持有 Viewport），返回值是切换后应采用的相机状态
		bool SetActiveLayout(int index, CameraState& camera);
		// 当前布局已被删除（删除布局、撤销新建布局）时回到模型页，camera 换成模型页的视图；
		// 返回是否切换了。界面在每次命令后调用
		bool ValidateActiveLayout(CameraState& camera);
		// 实体是否应在**当前布局**下显示。
		// 模型页：显示模型空间实体；布局页：显示本布局的图纸空间实体。
		bool IsVisibleInActiveLayout(const EntityAttr& attr) const;

		// ── 节点图（参数化定义，docs/NodeGraph/PLAN.md）──────────────
		// 每个文档一张，随 .mcad / JSON 保存（DWG / DXF 不保存）。修改后由编辑器调用 MarkDirty
		Nodes::Graph&       GetNodeGraph();
		const Nodes::Graph* FindNodeGraph() const { return m_nodeGraph.get(); }   // 还没有创建时为空

		// ── 序列化 ───────────────────────────────────────────
		void Serialize(ISerializer& s) const;
		void Deserialize(ISerializer& s);

	private:
		CadSaveVersion m_cadVersion = kDefaultCadSaveVersion;
		std::shared_ptr<const CadSource> m_cadSource;   // 打开的 DWG / DXF 原图：另存为同格式同版本时据此原样保留未修改的对象
		Scene          m_scene;
		CommandStack   m_cmdStack;

		std::string    m_name = "Untitled";
		std::string    m_path;
		bool           m_dirty = false;

		FontSystem*    m_fontSystem = nullptr;
		CameraState    m_cameraState;
		std::unique_ptr<Nodes::Graph> m_nodeGraph;

		// ── 布局状态 ──────────────────────────────────────────
		uint64_t m_activeKey = 0;      // 当前布局的 OwnerKey（0 = 模型页）
		// 每张布局各自的视图，按 OwnerKey（模型页为 0）记（切换布局时保存 / 恢复，避免来回切换后视角乱跳）
		std::unordered_map<uint64_t, CameraState> m_layoutViews;
	};
}
