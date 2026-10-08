#include "CommandStack.h"
#include "ICommand.h"
#include "Scene/Scene.h"

namespace MiniCAD
{
    namespace
    {
        // 原命令 + 其生效后产生的附属改动:重做时先原命令后附属,撤销反之
        class CompositeCommand : public ICommand
        {
        public:
            CompositeCommand(std::unique_ptr<ICommand> main, std::unique_ptr<ICommand> follow)
                : m_main(std::move(main)), m_follow(std::move(follow)) {}

            bool Execute(Scene& scene) override
            {
                if (!m_main->Execute(scene)) return false;
                m_follow->Execute(scene);
                return true;
            }
            void Undo(Scene& scene) override
            {
                m_follow->Undo(scene);
                m_main->Undo(scene);
            }
            std::string GetName() const override { return m_main->GetName(); }

        private:
            std::unique_ptr<ICommand> m_main;
            std::unique_ptr<ICommand> m_follow;
        };
    }

    void CommandStack::PushNew(std::unique_ptr<ICommand> cmd)
    {
        if (m_postCommand)
            if (auto follow = m_postCommand())
                cmd = std::make_unique<CompositeCommand>(std::move(cmd), std::move(follow));

        m_undoStack.push(std::move(cmd));
        // 新操作清空 Redo 栈
        while (!m_redoStack.empty())
            m_redoStack.pop();
    }

    // Execute = 执行 + 入栈（用于点击式操作，如创建/删除）
    // 返回 false 表示命令自身校验失败，不入栈，Scene 保持原状
    bool CommandStack::Execute(std::unique_ptr<ICommand> cmd, Scene& scene)
    {
        if (!cmd)
            return false;

        if (!cmd->Execute(scene))
            return false;

        const bool mutates = cmd->AffectsDocument();
        PushNew(std::move(cmd));
        if (mutates)
            NotifyMutate();
        return true;
    }

    // Push = 只入栈，不执行（例如：拖拽这种"已经发生"的操作）
    void CommandStack::Push(std::unique_ptr<ICommand> cmd)
    {
        // 空命令不入栈,否则 Undo 时解引用空指针
        if (!cmd)
            return;

        const bool mutates = cmd->AffectsDocument();
        PushNew(std::move(cmd));
        if (mutates)
            NotifyMutate();
    }

    void CommandStack::Undo(Scene& scene)
    {
        if (!CanUndo()) return;
        auto cmd = std::move(m_undoStack.top());
        m_undoStack.pop();
        cmd->Undo(scene);
        const bool mutates = cmd->AffectsDocument();
        m_redoStack.push(std::move(cmd));
        if (mutates)
            NotifyMutate();
    }

    void CommandStack::Redo(Scene& scene)
    {
        if (!CanRedo()) return;
        auto cmd = std::move(m_redoStack.top());
        m_redoStack.pop();
        cmd->Execute(scene);
        const bool mutates = cmd->AffectsDocument();
        m_undoStack.push(std::move(cmd));
        if (mutates)
            NotifyMutate();
    }

    void CommandStack::Clear()
    {
        while (!m_undoStack.empty()) m_undoStack.pop();
        while (!m_redoStack.empty()) m_redoStack.pop();
    }

}
