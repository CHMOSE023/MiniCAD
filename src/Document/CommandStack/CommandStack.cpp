#include "CommandStack.h"
#include "ICommand.h"
#include "Scene/Scene.h"

namespace MiniCAD
{

    // Execute = 立即执行 + 入栈（用于点击式操作，如创建/删除）
    void CommandStack::Execute(std::unique_ptr<ICommand> cmd, Scene& scene)
    {
        cmd->Execute(scene);
        m_undoStack.push(std::move(cmd));
        // 新操作清空 Redo 栈
        while (!m_redoStack.empty())
            m_redoStack.pop();
    }

    // Push = 只入栈，不执行（例如:拖拽这种“已经发生”的操作）
    void CommandStack::Push(std::unique_ptr<ICommand> cmd)
    {
        m_undoStack.push(std::move(cmd));

        while (!m_redoStack.empty())
            m_redoStack.pop();
    }

    void CommandStack::Undo(Scene& scene)
    {
        if (!CanUndo()) return;
        auto cmd = std::move(m_undoStack.top());
        m_undoStack.pop();
        cmd->Undo(scene);
        m_redoStack.push(std::move(cmd));
    }

    void CommandStack::Redo(Scene& scene)
    {
        if (!CanRedo()) return;
        auto cmd = std::move(m_redoStack.top());
        m_redoStack.pop();
        cmd->Execute(scene);
        m_undoStack.push(std::move(cmd));
    }



    void CommandStack::Clear()
    {
        while (!m_undoStack.empty()) m_undoStack.pop();
        while (!m_redoStack.empty()) m_redoStack.pop();
    }

}
