#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Editor/Grip/GripEditor.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <memory>

namespace MiniCAD
{
    struct DragEntityEntry
    {
        Object::ObjectID Id;

        enum class Kind
        {
            Line,
            Point
        } Kind;

        LineSegment BeforeLine;
        LineSegment AfterLine;

        Math::Point3  BeforePoint;
        Math::Point3  AfterPoint;
    };

    class DragEntitiesCommand : public ICommand
    {
    public:
        explicit DragEntitiesCommand(std::vector<DragEntityEntry> entries)
            : m_entries(std::move(entries)) {}

        bool Execute(Scene& scene) override
        {
            if (m_entries.empty())
                return false;

            Apply(scene, /*useAfter=*/true);
            return true;
        }

        void Undo(Scene& scene) override
        {
            Apply(scene, /*useAfter=*/false);
        }

        std::string GetName() const override { return "拖动对象"; }

    private:
        std::vector<DragEntityEntry> m_entries;

        void Apply(Scene& scene, bool useAfter)
        {
            for (auto& e : m_entries)
            {
                auto obj = scene.GetEntity(e.Id);
                if (!obj) continue;

                if (e.Kind == DragEntityEntry::Kind::Line)
                {
                    auto* line = static_cast<LineEntity*>(obj);
                    const auto& seg = useAfter ? e.AfterLine : e.BeforeLine;
                    line->SetLine({ seg.Start, seg.End });
                }
                else if (e.Kind == DragEntityEntry::Kind::Point)
                {
                    auto* pt = static_cast<PointEntity*>(obj);
                    const auto& p = useAfter ? e.AfterPoint : e.BeforePoint;
                    pt->SetPoint({ p });
                }
            }
        }
    };
}
