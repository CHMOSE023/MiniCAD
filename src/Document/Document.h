#pragma once
#include "Import/CadExchange.h"

namespace MiniCAD { class FontSystem; }

#include "Scene/Scene.h"
#include "Scene/LayerManager.h"
#include "Document/CommandStack/CommandStack.h"
#include "Viewport/Camera.h"
#include <string>

namespace MiniCAD
{
	class ISerializer;
	class Document
	{
	public:
		Document();

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
		bool LoadFromFile(const std::string& path, std::string* error = nullptr);

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

		// ── 序列化 ───────────────────────────────────────────
		void Serialize(ISerializer& s) const;
		void Deserialize(ISerializer& s);

	private:
		CadSaveVersion m_cadVersion = CadSaveVersion::R2018;
		std::shared_ptr<const CadSource> m_cadSource;   // 打开的 DWG / DXF 原图：另存为同格式同版本时据此原样保留未修改的对象
		Scene          m_scene;
		CommandStack   m_cmdStack;

		std::string    m_name = "Untitled";
		std::string    m_path;
		bool           m_dirty = false;

		FontSystem*    m_fontSystem = nullptr;
		CameraState    m_cameraState;
	};
}
