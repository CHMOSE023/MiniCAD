#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Editor/Grip/GripEditor.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Editor/Grip/GripType.h"
#include <memory>

namespace MiniCAD
{ 
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
                
                if (e.Kind == DragEntityEntry::Kind::Point)
                {
                    auto* pt = static_cast<PointEntity*>(obj);
                    const auto& p = useAfter ? e.AfterPoint : e.BeforePoint;
                    pt->SetPoint({ p });
                }

                if (e.Kind == DragEntityEntry::Kind::Circle)
                {
                    auto* circle = static_cast<CircleEntity*>(obj);
                    const auto& snap = useAfter ? e.AfterCircle : e.BeforeCircle;
                    circle->SetCircle({ snap.Center,snap.Radius });
                }

            }
        }
    };
}
