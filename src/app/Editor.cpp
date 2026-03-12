// ============================================================
// MiniCAD — app/Editor.cpp
// 职责：Editor 单例实现
// 依赖：app/Editor.h
// 约束：不持有 Renderer；ui/ 层只能通过此接口操作
// ============================================================
#include "Editor.h"

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
    case InputEvent::Type::MouseDown:
        m_activeTool->OnMouseDown(evt.screenPos, evt.button);
        break;
    case InputEvent::Type::MouseMove:
        m_activeTool->OnMouseMove(evt.screenPos);
        break;
    case InputEvent::Type::MouseUp:
        m_activeTool->OnMouseUp(evt.screenPos, evt.button);
        break;
    case InputEvent::Type::KeyDown:
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
