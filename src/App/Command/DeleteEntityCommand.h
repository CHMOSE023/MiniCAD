#pragma once
#include "App/Abstractions/ICommand.h"
#include "Core/Object/Object.hpp"
#include <memory>

namespace MiniCAD
{

    class DeleteEntityCommand : public ICommand
    {
    public:
        explicit DeleteEntityCommand(Object::ObjectID id) : m_id(id) {}

        void Execute(Scene& scene) override
        {
            m_entity = scene.RemoveEntity(m_id); // 保存所有权供 Undo
        }

        void Undo(Scene& scene) override 
        {
            if (m_entity) scene.AddEntity(std::move(m_entity));
        }

        std::string GetName() const override { return "删除实体"; }

    private:
        Object::ObjectID        m_id;
        std::unique_ptr<Object> m_entity; // Undo 时暂存
    };

}
