#pragma once
#include "App/Abstractions/ICommand.h"
#include "Core/Object/Object.hpp"
#include <vector>
#include <memory>

namespace MiniCAD
{
    // 批量删除：Execute 顺序删除，Undo 逆序还原
    // Ctrl+Z 调用一次 CommandStack::Undo 撤回整批
    class BatchDeleteCommand : public ICommand
    {
    public:
        explicit BatchDeleteCommand(std::vector<Object::ObjectID> ids)
            : m_ids(std::move(ids))
        {}

        void Execute(Scene& scene) override
        {
            // 顺序删除，保存实体所有权供 Undo
            m_saved.clear();
            for (auto id : m_ids)
            {
                auto entity = scene.RemoveEntity(id);
                if (entity) m_saved.push_back(std::move(entity));
            }
        }

        void Undo(Scene& scene) override
        {
            // 逆序还原，恢复原始顺序
            for (int i = (int)m_saved.size() - 1; i >= 0; --i)
                scene.AddEntity(std::move(m_saved[i]));
            m_saved.clear();
        }

        std::string GetName() const override { return "批量删除实体"; }

    private:
        std::vector<Object::ObjectID>        m_ids;
        std::vector<std::unique_ptr<Object>> m_saved;
    };
}
