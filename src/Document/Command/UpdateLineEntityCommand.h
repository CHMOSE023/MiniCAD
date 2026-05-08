#pragma once
#include "App/Scene/Scene.h"
#include "App/CommandStack/ICommand.h"
#include "Core/Entity/EntityAttr.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Core/Entity/LineEntity.hpp"

namespace MiniCAD
{
    struct LineEntityState
    {
        Line       line;
        EntityAttr attr;
    };

    class UpdateLineEntityCommand : public ICommand
    {
    public:
        UpdateLineEntityCommand(Object::ObjectID id, const LineEntityState& before, const LineEntityState& after)
            : m_id(id)
            , m_before(before)
            , m_after(after)
        {
        }

        void Execute(Scene& scene) override
        {
            Apply(scene, m_after);
        }

        void Undo(Scene& scene) override
        {
            Apply(scene, m_before);
        }

        std::string GetName() const override
        {
            return "修改直线";
        }

    private:
        Object::ObjectID m_id;
        LineEntityState  m_before;
        LineEntityState  m_after;

        void Apply(Scene& scene, const LineEntityState& state) const
        {
            auto* obj = scene.GetEntity(m_id);

            if (!obj) return;

            if(obj->IsKindOf<LineEntity>())
            {
                auto* line = static_cast<LineEntity*>(obj);
                if (!line) return;

                line->SetLine(state.line);
                line->SetAttr(state.attr);
            }
           
 
        }
    };
}