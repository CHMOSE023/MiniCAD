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

        // 任意子命令失败则整批失败，已执行的部分逆序回滚
        bool Execute(Scene& scene) override
        {
            int executed = 0;
            for (auto& cmd : m_commands)
            {
                if (!cmd->Execute(scene))
                {
                    // 回滚已成功的部分
                    for (int i = executed - 1; i >= 0; --i)
                        m_commands[i]->Undo(scene);
                    return false;
                }
                ++executed;
            }
            return true;
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
