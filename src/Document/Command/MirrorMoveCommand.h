#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Document/Command/MoveEntityEntry.h"   // 完全复用 Move 的 Entry 结构
#include "Document/Command/EntityMirror.h"
#include "Core/Object/Object.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;

    class MirrorMoveCommand : public ICommand
    {
    public:
        MirrorMoveCommand(const std::vector<Object::ObjectID>& ids,
            const MirrorAxis& axis,
            Scene& scene);

        bool        Execute(Scene& scene) override;
        void        Undo(Scene& scene) override;
        std::string GetName() const override { return "MirrorMove"; }

    private:
        std::vector<MoveEntityEntry> m_entries;   // 复用!Before/After 几何快照
        MirrorAxis                   m_axis;
    };
}
