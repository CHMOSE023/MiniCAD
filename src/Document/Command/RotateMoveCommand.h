#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Document/Command/MoveEntityEntry.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;

    class RotateMoveCommand : public ICommand
    {
    public:
        RotateMoveCommand(const std::vector<Object::ObjectID>& ids, const Math::Point3& pivot, double angle, Scene& scene);

        bool        Execute(Scene& scene) override;
        void        Undo   (Scene& scene) override;
        std::string GetName() const override { return "Rotate"; }

    private:
        std::vector<MoveEntityEntry> m_entries;   // 复用 Before/After 快照
        Math::Point3                 m_pivot;
        double                       m_angle = 0.0;
    };
}
