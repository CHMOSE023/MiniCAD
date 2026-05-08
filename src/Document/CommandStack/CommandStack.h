#pragma once
#include "ICommand.h"
#include <stack>
#include <memory>
namespace MiniCAD
{
    class Scene;
    class CommandStack
    {
    public:
        void Execute(std::unique_ptr<ICommand> cmd, Scene& scene);
        void Undo(Scene& scene);
        void Redo(Scene& scene);

        // Push = 只入栈，不执行（例如:拖拽这种“已经发生”的操作）
        void Push(std::unique_ptr<ICommand> cmd);
        bool CanUndo() const { return !m_undoStack.empty(); }
        bool CanRedo() const { return !m_redoStack.empty(); }

        void Clear();

    private:
        std::stack<std::unique_ptr<ICommand>> m_undoStack;
        std::stack<std::unique_ptr<ICommand>> m_redoStack;

    };
}