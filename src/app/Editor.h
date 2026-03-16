// ============================================================
// MiniCAD — app/Editor.h
// 职责：应用级单例，核心中介
// 依赖：app/Scene/Scene.h, doc/UndoRedo/CommandStack.h,
//       app/Tools/ITool.h, app/Picking/SelectionSet.h
// 约束：ui/ 层只能通过此接口操作；不持有 Renderer
// ============================================================
#pragma once

#include "app/Scene/Scene.h"
#include "app/Scene/LayerManager.h"
#include "doc/UndoRedo/CommandStack.h"
#include  "doc/UndoRedo/ICommand.h"
#include "app/Tools/ITool.h"
#include "app/Picking/SelectionSet.h"
#include "app/InputEvent.h"  
#include <memory>
#include <functional>

namespace MiniCAD {

    // ── 重绘请求回调（由 ui/ 层注入）────────────────────────────
    using RedrawCallback = std::function<void()>;

    // ═══════════════════════════════════════════════════════════
    //  Editor — 单例
    // ═══════════════════════════════════════════════════════════
    class Editor {
    public:
        static Editor& Instance();

        // ── 初始化 / 关闭 ────────────────────────────────────────
        void Initialize();
        void Shutdown();

        // ── 工具管理 ─────────────────────────────────────────────
        void   SetActiveTool(std::unique_ptr<ITool> tool);
        ITool* GetActiveTool() const { return m_activeTool.get(); }

        // ── 命令执行（所有数据修改必须走此接口）─────────────────
        // 注意：CommandStack::Push 会自动调用 Execute()
        void PushCommand(std::unique_ptr<ICommand> cmd);
        void Undo();
        void Redo();
        bool CanUndo() const { return m_commandStack.CanUndo(); }
        bool CanRedo() const { return m_commandStack.CanRedo(); }

        // ── 输入分发 ─────────────────────────────────────────────
        void HandleInput(const InputEvent& evt);

        // ── 渲染请求 ─────────────────────────────────────────────
        void RequestRedraw();
        void SetRedrawCallback(RedrawCallback cb) { m_redrawCb = std::move(cb); }

        // ★ 新增：选择集变化通知（修复 C2039）
        void SetSelectionChangedCallback(std::function<void()> cb) {
            m_selectionChangedCb = std::move(cb);
        }

        // ── 访问器 ───────────────────────────────────────────────
        Scene& GetScene() { return m_scene; }
        LayerManager& GetLayerManager() { return m_layerManager; }
        SelectionSet& GetSelectionSet() { return m_selectionSet; }
        CommandStack& GetCommandStack() { return m_commandStack; }

    private:
        Editor() = default;
        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;

        Scene            m_scene;
        LayerManager     m_layerManager;
        SelectionSet     m_selectionSet;
        CommandStack     m_commandStack;
        std::unique_ptr<ITool> m_activeTool;
        RedrawCallback   m_redrawCb;
        std::function<void()>  m_selectionChangedCb;
    };

} // namespace MiniCAD
