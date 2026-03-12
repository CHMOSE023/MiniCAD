// ============================================================
// MiniCAD — doc/UndoRedo/CommandStack.cpp
// 职责：CommandStack 实现
// 依赖：doc/UndoRedo/CommandStack.h
// 约束：不依赖 render/ ui/
// ============================================================
 
#include "doc/UndoRedo/CommandStack.h"  
#include <memory>
#include <string>

namespace MiniCAD {

CommandStack::CommandStack(int capacity) : m_capacity(capacity) {}

void CommandStack::Push(std::unique_ptr<ICommand> cmd) {
    // 清除 Redo 部分
    if (m_undoTop < (int)m_stack.size())
        m_stack.erase(m_stack.begin() + m_undoTop, m_stack.end());

    cmd->Execute();
    m_stack.push_back(std::move(cmd));
    ++m_undoTop;

    // 容量溢出：淘汰最旧命令
    while ((int)m_stack.size() > m_capacity) {
        m_stack.pop_front();
        --m_undoTop;
    }
}

void CommandStack::Undo() {
    if (!CanUndo()) return;
    --m_undoTop;
    m_stack[m_undoTop]->Undo();
}

void CommandStack::Redo() {
    if (!CanRedo()) return;
    m_stack[m_undoTop]->Redo();
    ++m_undoTop;
}

void CommandStack::Clear() {
    m_stack.clear();
    m_undoTop = 0;
}

std::string CommandStack::CurrentDescription() const {
    if (!CanUndo()) return "";
    return m_stack[m_undoTop - 1]->GetDescription();
}

} // namespace MiniCAD
