#pragma once
#include "App/Scene/Scene.h"
#include "App/Scene/LayerManager.h"
#include "App/Editor/Editor.h"
#include "App/CommandStack/CommandStack.h" 
#include "Core/Object/Object.hpp"
#include <unordered_set>
#include <string>
#include <filesystem>
#include <memory>

namespace MiniCAD
{
    class Document : public IInputHandler
    {
    public:
        Document();

        // ── 初始化 ────────────────────────────────────────────
        void New();

        // ── IEventHandler ─────────────────────────────────────
        bool OnInput(const InputEvent& e) override;

        // ── 数据访问 ──────────────────────────────────────────
        Scene&         GetScene()        { return *m_scene; }
        const Scene&   GetScene()  const { return *m_scene; }

        Editor&        GetEditor()       { return *m_editor; }
        const  Editor& GetEditor() const { return *m_editor; }

        CommandStack&       GetCommandStack()       { return *m_cmdStack; }
        const CommandStack& GetCommandStack() const { return *m_cmdStack; }

        LayerManager&       GetLayerManager()       { return m_scene->GetLayerManager(); }
        const LayerManager& GetLayerManager() const { return m_scene->GetLayerManager(); }

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
        bool Save(const std::filesystem::path& path);
        bool Save();
        bool SaveAs();
        bool Load(const std::filesystem::path& path);
        bool Open();

        const std::filesystem::path& GetPath() const { return m_currentPath; }
        bool HasPath() const { return !m_currentPath.empty(); }
        bool IsDirty() const { return m_scene && m_scene->IsDirty(); }

    private:
        bool CreateLayerAndActivate();
        bool CycleActiveLayer(int direction);
        bool ToggleActiveLayerVisibility();
        bool ToggleActiveLayerLock();
        bool DeleteActiveLayer();
        void RefreshEditorAfterLayerChange(bool cancelActiveTool = false);

        bool PromptSavePath(std::filesystem::path& path) const;
        bool PromptOpenPath(std::filesystem::path& path) const;
        void ResetEditorState();

        std::unique_ptr<Scene>        m_scene;
        std::unique_ptr<CommandStack> m_cmdStack;
        std::unique_ptr<Editor>       m_editor;
        std::filesystem::path         m_currentPath;
    };
} 
