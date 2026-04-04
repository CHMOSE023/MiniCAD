#pragma once
#include "App/Abstractions/ICommand.h"
#include "Core/Object/Object.hpp"
#include <vector>
#include <memory>

namespace MiniCAD
{
    // 批量添加：Execute 顺序添加，Undo 逆序删除
    // 用于需要一次性提交多个实体、但只占一个撤回步骤的场景
    class BatchAddCommand : public ICommand
    {
    public:
        explicit BatchAddCommand(std::vector<std::unique_ptr<Object>> entities)
            : m_entities(std::move(entities))
        {}

        void Execute(Scene& scene) override
        {
            m_ids.clear();
            for (auto& e : m_entities)
            {
                m_ids.push_back(e->GetID());
                scene.AddEntity(std::move(e));
            }
            m_entities.clear();
        }

        void Undo(Scene& scene) override
        {
            // 逆序取回所有权
            m_entities.clear();
            for (int i = (int)m_ids.size() - 1; i >= 0; --i)
            {
                auto entity = scene.RemoveEntity(m_ids[i]);
                if (entity) m_entities.push_back(std::move(entity));
            }
            // 恢复正向顺序，方便下次 Redo
            std::reverse(m_entities.begin(), m_entities.end());
        }

        std::string GetName() const override { return "批量添加实体"; }

    private:
        std::vector<std::unique_ptr<Object>> m_entities;
        std::vector<Object::ObjectID>        m_ids;
    };
}
