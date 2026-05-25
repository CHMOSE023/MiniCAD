#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Object/Object.hpp"
#include <string>

namespace MiniCAD
{
    class EditTextCommand : public ICommand
    {
    public:
        EditTextCommand(Object::ObjectID id, std::string before, std::string after)
            : m_id(id), m_before(std::move(before)), m_after(std::move(after)) {}

        bool Execute(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<TextEntity>()) return false;
            static_cast<TextEntity*>(obj)->SetText(m_after);
            scene.MarkDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<TextEntity>()) return;
            static_cast<TextEntity*>(obj)->SetText(m_before);
            scene.MarkDirty();
        }

        std::string GetName() const override { return "编辑文字"; }

    private:
        Object::ObjectID m_id;
        std::string      m_before;
        std::string      m_after;
    };

    class EditMTextCommand : public ICommand
    {
    public:
        EditMTextCommand(Object::ObjectID id, std::string before, std::string after)
            : m_id(id), m_before(std::move(before)), m_after(std::move(after)) {}

        bool Execute(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<MTextEntity>()) return false;
            static_cast<MTextEntity*>(obj)->SetText(m_after);
            scene.MarkDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<MTextEntity>()) return;
            static_cast<MTextEntity*>(obj)->SetText(m_before);
            scene.MarkDirty();
        }

        std::string GetName() const override { return "编辑多行文字"; }

    private:
        Object::ObjectID m_id;
        std::string      m_before;
        std::string      m_after;
    };
}
