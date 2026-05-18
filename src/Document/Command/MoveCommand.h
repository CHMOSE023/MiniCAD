// Document/Command/MoveCommand.h
#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Document/Command/MoveEntityEntry.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Scene/Scene.h"
#include <vector>

namespace MiniCAD
{ 
    class Scene;

    class MoveCommand : public ICommand
    {
    public:
        MoveCommand(const std::vector<Object::ObjectID>& ids, const Math::Vec3& delta, Scene& scene);

        bool        Execute(Scene& scene) override;
        void        Undo(Scene& scene) override;
        std::string GetName() const override { return "Move"; }

    private:
        std::vector<MoveEntityEntry> m_entries;
        Math::Vec3                   m_delta;
    };

}
