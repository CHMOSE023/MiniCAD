#pragma once
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Editor/Grip/GripEditor.h"
#include "Document/CommandStack/ICommand.h"
#include <memory>

namespace MiniCAD
{
    class DragLineCommand : public ICommand
    {
    public:
        DragLineCommand(Object::ObjectID id, const LineSegment& before, const LineSegment& after)
            : m_id(id)
            , m_before(before)
            , m_after(after)
        {
        }

        bool Execute(Scene& scene) override
        {
            auto* line = static_cast<LineEntity*>(scene.GetEntity(m_id));
            if (!line)
                return false;

            Apply(scene, m_after);
            return true;
        }

        void Undo(Scene& scene) override
        {
            Apply(scene, m_before);
        }

        std::string GetName() const override { return "拖动直线"; }

    private:
        Object::ObjectID m_id;
        LineSegment      m_before;
        LineSegment      m_after;

        void Apply(Scene& scene, const LineSegment& seg) const
        {
            auto* line = static_cast<LineEntity*>(scene.GetEntity(m_id));
            if (line)
            {
                line->SetLine({ seg.Start, seg.End });
            }
            scene.MarkDirty();
        }
    };

    // 多条线同时撤销，用于重叠夹点同时编辑的情况
    class DragMultiLineCommand : public ICommand
    {
    public:
        struct Entry
        {
            Object::ObjectID Id;
            LineSegment      Before;
            LineSegment      After;
        };

        explicit DragMultiLineCommand(std::vector<Entry> entries)
            : m_entries(std::move(entries)) {}

        bool Execute(Scene& scene) override
        {
            if (m_entries.empty())
                return false;

            for (auto& e : m_entries)
                Apply(scene, e.Id, e.After);
            scene.MarkDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            // 逆序撤销，保持操作对称性
            for (int i = (int)m_entries.size() - 1; i >= 0; --i)
                Apply(scene, m_entries[i].Id, m_entries[i].Before);
            scene.MarkDirty();
        }

        std::string GetName() const override { return "拖动直线"; }

    private:
        std::vector<Entry> m_entries;

        void Apply(Scene& scene, Object::ObjectID id, const LineSegment& seg) const
        {
            auto* line = static_cast<LineEntity*>(scene.GetEntity(id));
            if (line) line->SetLine({ seg.Start, seg.End });
        }
    };
}
