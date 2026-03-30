#pragma once
#include "ICommand.h"
#include "Core/Object/Object.hpp"
#include <memory>

namespace MiniCAD
{
    class AddEntityCommand : public ICommand
    {
    public:
        explicit AddEntityCommand(std::unique_ptr<Object> entity)  : m_entity(std::move(entity)) {  }

        void Execute(Scene& scene) override 
        {
            m_id = m_entity->GetID();
            scene.AddEntity(std::move(m_entity));
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
