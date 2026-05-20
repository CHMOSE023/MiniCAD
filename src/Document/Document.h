#pragma once

// FontSystem 只在 Document.cpp 中需要完整定义，这里前向声明即可
namespace MiniCAD { class FontSystem; }

#include "Scene/Scene.h"
#include "Scene/LayerManager.h"
#include "Editor/Context/EditorContext.h"
#include "Document/CommandStack/CommandStack.h" 
#include "Editor/Input/IInputHandler.h" 
#include "Editor/Input/InputEvent.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"  
#include "Editor/Context/ViewState.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Snap/SnapResult.h"
#include "Render/IRenderer.h"
#include <vector>
#include <string>

namespace MiniCAD
{
	class Document : public IInputHandler
	{
	public:
		Document(IRenderer& render, float width = 600, float height = 400);

		// ── IEventHandler ─────────────────────────────────────
		bool OnInput(const InputEvent& e) override;
		 
		// ── 视图与呈现 ──────────────────────────────────────────
		void Resize(float width, float height);
		void Render();

		EditorContext&         GetEditor()          { return m_editor; }
        const  EditorContext&  GetEditor() const    { return m_editor; }

	    // ── 数据访问 ──────────────────────────────────────────
        Scene&              GetScene()              { return m_scene; }
        const Scene&        GetScene()  const       { return m_scene; } 

        Viewport&           GetViewport()           { return m_viewport; }
        const  Viewport&    GetViewport() const     { return m_viewport; }

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

		ViewState BuildViewState(); // 构建渲染状态

		// 由 UIManager 在每帧渲染前设置 ImGui 字体图集 SRV
		void SetFontTexture(void* srv) { m_fontTexture = srv; }

		// 由 DocumentManager 在创建文档后注入，用于矢量文字渲染
		void SetFontSystem(FontSystem* fs) { m_fontSystem = fs; }

	private:
		void UpdateSceneVerties(); // 更新屏幕渲染数据

	private:
		EditorContext  m_editor;
		Scene          m_scene;
		CommandStack   m_cmdStack;
		Viewport       m_viewport;
		Overlay        m_overlay;
		Picking        m_picking;
		SnapEngine     m_snap;
		SnapResult     m_currentSnap;

		// 鼠标位置
		int            m_mouseX = 0;
		int            m_mouseY = 0;
		std::string    m_name = "Untitled";
		std::string    m_path;
		bool           m_dirty = false;

		std::vector<Vertex_P3_C4>    m_sceneVertices;   // 场景线段（含矢量文字笔划）
		std::vector<Vertex_P3_C4_UV> m_textVertices;    // 纹理文字四边形（ImGui / Web 路径）
		std::vector<Vertex_P3_C4>    m_overlayVertices; // 预览数据
		std::vector<Vertex_P3_C4>    m_gripVertices;    // 夹点数据
		void*                        m_fontTexture = nullptr;
		FontSystem*                  m_fontSystem  = nullptr; // 非拥有，由 DocumentManager 注入
	};
}
