#pragma once
#include "Document/CommandStack/ICommand.h"
#include <vector>
#include <memory>

namespace MiniCAD
{
    class BatchCommand : public ICommand
    {
    public:
        BatchCommand(std::vector<std::unique_ptr<ICommand>> cmds)
            : m_commands(std::move(cmds))
        {
        }

        void Execute(Scene& scene) override
        {
            for (auto& cmd : m_commands)
                cmd->Execute(scene);
        }

        void Undo(Scene& scene) override
        {
            // 逆序撤销
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                (*it)->Undo(scene);
        }

        std::string GetName() const override
        {
            return "批量修改";
        }

    private:
        std::vector<std::unique_ptr<ICommand>> m_commands;
    };
}
