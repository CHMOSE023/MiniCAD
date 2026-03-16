// ============================================================
// MiniCAD — app/Editor.cpp
// 职责：Editor 单例实现
// 依赖：app/Editor.h
// 约束：不持有 Renderer；ui/ 层只能通过此接口操作
// ============================================================
#include "Editor.h"
#include "InputEvent.h"
#include <doc/UndoRedo/ICommand.h>
#include <memory>
#include "Tools/ITool.h"

namespace MiniCAD {

    Editor& Editor::Instance() {
        static Editor inst;
        return inst;
    }

    void Editor::Initialize() {
        m_scene.SetDirtyCallback([this]() { RequestRedraw(); });
    }

    void Editor::Shutdown() {
        m_activeTool.reset();
        m_commandStack.Clear();
    }

    void Editor::SetActiveTool(std::unique_ptr<ITool> tool) {
        if (m_activeTool) m_activeTool->OnCancel();
        m_activeTool = std::move(tool);
    }

    void Editor::PushCommand(std::unique_ptr<ICommand> cmd) {
        m_commandStack.Push(std::move(cmd));
    }

    void Editor::Undo() {
        m_commandStack.Undo();
        RequestRedraw();
    }

    void Editor::Redo() {
        m_commandStack.Redo();
        RequestRedraw();
    }

    void Editor::HandleInput(const InputEvent& evt) {

        if (!m_activeTool) return;

        switch (evt.type) {
        case InputEventType::MOUSE_DOWN:
            m_activeTool->OnMouseDown(evt.screenPos, evt.button);
            break;
        case InputEventType::MOUSE_MOVE:
            m_activeTool->OnMouseMove(evt.screenPos);
            break;
        case InputEventType::MOUSE_UP:
            m_activeTool->OnMouseUp(evt.screenPos, evt.button);
            if (m_selectionChangedCb) // 鼠标抬起后选择集可能已变化，通知 PropertyPanel 刷新
                m_selectionChangedCb();
            break;
        case InputEventType::KEY_DOWN:
            if (evt.keyCode == KEY_ESCAPE) m_activeTool->OnCancel();
            else m_activeTool->OnKeyDown(evt.keyCode);
            break;
        default:
            break;
        }
    }

    void Editor::RequestRedraw() {
        if (m_redrawCb)
            m_redrawCb();
    }

} // namespace MiniCAD
