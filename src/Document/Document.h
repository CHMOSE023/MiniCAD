#pragma once

namespace MiniCAD { class FontSystem; }

#include "Scene/Scene.h"
#include "Scene/LayerManager.h"
#include "Document/CommandStack/CommandStack.h"
#include <string>

namespace MiniCAD
{
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

		// 保存 / 另存为
		bool Save();
		bool SaveAs(const std::string& path);
		bool SaveToFile(const std::string& path);

		// 由 DocumentManager 在创建文档后注入，用于矢量文字渲染
		void SetFontSystem(FontSystem* fs) { m_fontSystem = fs; }
		FontSystem* GetFontSystem() const  { return m_fontSystem; }

	private:
		Scene          m_scene;
		CommandStack   m_cmdStack;

		std::string    m_name = "Untitled";
		std::string    m_path;
		bool           m_dirty = false;

		FontSystem*    m_fontSystem = nullptr;
	};
}
