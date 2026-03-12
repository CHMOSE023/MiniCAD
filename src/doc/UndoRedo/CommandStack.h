// ============================================================
// MiniCAD — doc/UndoRedo/CommandStack.h
// 职责：命令栈：Push/Undo/Redo，默认容量 128 步，超限淘汰最旧
// 依赖：doc/UndoRedo/ICommand.h
// 约束：只持有 ICommand 指针；不感知具体命令
// ============================================================
#pragma once
#include "doc/UndoRedo/ICommand.h" 
#include <deque>
#include <memory> 
#include <string>

namespace MiniCAD {

class CommandStack {
public:
    static constexpr int DEFAULT_CAPACITY = 128;

    explicit CommandStack(int capacity = DEFAULT_CAPACITY);

    // 执行命令并推入栈；会清空 Redo 栈
    void Push(std::unique_ptr<ICommand> cmd);

    bool CanUndo() const { return m_undoTop > 0; }
    bool CanRedo() const { return m_undoTop < (int)m_stack.size(); }

    void Undo();
    void Redo();
    void Clear();

    int  UndoCount() const { return m_undoTop; }
    int  RedoCount() const { return (int)m_stack.size() - m_undoTop; }
    int  Capacity()  const { return m_capacity; }

    // 当前可 Undo 的最新命令描述
    std::string CurrentDescription() const;

private:  
    std::deque<std::unique_ptr<ICommand>> m_stack;
    int m_undoTop  = 0;   // 指向下一个 Redo 的位置（等于 undo 数量）
    int m_capacity;
};

} // namespace MiniCAD
