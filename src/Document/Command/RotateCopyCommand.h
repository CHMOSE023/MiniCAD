#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;

    class RotateCopyCommand : public ICommand
    {
    public:
        RotateCopyCommand(std::vector<Object::ObjectID> sourceIds, Math::Point3 pivot, double angle);

        bool        Execute(Scene& scene) override;
        void        Undo   (Scene& scene) override;
        std::string GetName() const override { return "RotateCopy"; }

        const std::vector<Object::ObjectID>& GetNewIds() const { return m_newIds; }

    private:
        std::vector<Object::ObjectID> m_sourceIds;
        Math::Point3                  m_pivot;
        double                        m_angle = 0.0;
        std::vector<Object::ObjectID> m_newIds;
        bool                          m_executed = false;
    };
}