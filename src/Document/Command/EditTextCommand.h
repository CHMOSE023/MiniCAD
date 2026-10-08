#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
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
            scene.MarkEntityDirty(m_id);   // 仅该实体重新细分
            return true;
        }

        void Undo(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<TextEntity>()) return;
            static_cast<TextEntity*>(obj)->SetText(m_before);
            scene.MarkEntityDirty(m_id);   // 仅该实体重新细分
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
            scene.MarkEntityDirty(m_id);   // 仅该实体重新细分
            return true;
        }

        void Undo(Scene& scene) override
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<MTextEntity>()) return;
            static_cast<MTextEntity*>(obj)->SetText(m_before);
            scene.MarkEntityDirty(m_id);   // 仅该实体重新细分
        }

        std::string GetName() const override { return "编辑多行文字"; }

    private:
        Object::ObjectID m_id;
        std::string      m_before;
        std::string      m_after;
    };

    // 编辑表格单元格文字
    class EditTableCellCommand : public ICommand
    {
    public:
        EditTableCellCommand(Object::ObjectID id, size_t row, size_t col, std::string before, std::string after)
            : m_id(id), m_row(row), m_col(col), m_before(std::move(before)), m_after(std::move(after)) {}

        bool Execute(Scene& scene) override
        {
            auto* t = Target(scene);
            if (!t) return false;
            t->SetCellText(m_row, m_col, m_after);
            scene.MarkEntityDirty(m_id);
            return true;
        }

        void Undo(Scene& scene) override
        {
            if (auto* t = Target(scene))
            {
                t->SetCellText(m_row, m_col, m_before);
                scene.MarkEntityDirty(m_id);
            }
        }

        std::string GetName() const override { return "编辑单元格"; }

    private:
        TableEntity* Target(Scene& scene) const
        {
            auto* obj = scene.GetEntity(m_id);
            if (!obj || !obj->IsKindOf<TableEntity>()) return nullptr;
            auto* t = static_cast<TableEntity*>(obj);
            return (m_row < t->RowCount() && m_col < t->ColCount()) ? t : nullptr;
        }

        Object::ObjectID m_id;
        size_t           m_row, m_col;
        std::string      m_before;
        std::string      m_after;
    };
}
