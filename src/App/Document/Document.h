#pragma once
#include "App/Scene/Scene.h"
#include "App/Scene/LayerManager.h"
#include "App/Editor/Editor.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Input/IEventHandler.h"
#include "Core/Object/Object.hpp"
#include <unordered_set>
#include <string>
#include <memory>

namespace MiniCAD
{
	class Document : public IEventHandler
	{
    public:
        Document();

        // ── 初始化 ────────────────────────────────────────────
        void New();

        // ── IEventHandler ─────────────────────────────────────
        void OnInput(const InputEvent& e) override;

        // ── 数据访问 ──────────────────────────────────────────
        Scene*        GetScene() { return m_scene.get(); }
        Editor*       GetEditor() { return m_editor.get(); }
        LayerManager* GetLayerManager() { return &m_scene->GetLayerManager(); }

        const std::unordered_set<Object::ObjectID>& GetSelection() const
        { 
            return m_editor->GetSelection(); 
        }

        // ── Undo / Redo ───────────────────────────────────────
        void Undo() { m_cmdStack->Undo(*m_scene); }
        void Redo() { m_cmdStack->Redo(*m_scene); }
        bool CanUndo() const { return m_cmdStack->CanUndo(); }
        bool CanRedo() const { return m_cmdStack->CanRedo(); }

        // ── 文件操作 ──────────────────────────────────────────
        void Save(const std::string& path);
        void Load(const std::string& path);

    private:
        std::unique_ptr<Scene>        m_scene;
        std::unique_ptr<CommandStack> m_cmdStack;
        std::unique_ptr<Editor>       m_editor;
	};
}
 