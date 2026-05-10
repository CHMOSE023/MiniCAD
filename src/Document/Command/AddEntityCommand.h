#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include <memory>

namespace MiniCAD
{
    class AddEntityCommand : public ICommand
    {
    public:
        explicit AddEntityCommand(std::unique_ptr<Object> entity) : m_entity(std::move(entity)) {}

        bool Execute(Scene& scene) override
        {
            if (!m_entity)
                return false;

            m_id = m_entity->GetID();
            scene.AddEntity(std::move(m_entity));
            return true;
        }

        void Undo(Scene& scene) override
        {
            m_entity = scene.RemoveEntity(m_id); // 取回所有权
        }

        std::string GetName() const override { return "添加实体"; }

    private:
        std::unique_ptr<Object> m_entity;
        Object::ObjectID        m_id = 0;
    };
}
