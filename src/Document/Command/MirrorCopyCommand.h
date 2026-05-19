#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Document/Command/EntityMirror.h"
#include "Core/Object/Object.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;

    class MirrorCopyCommand : public ICommand
    {
    public:
        MirrorCopyCommand(std::vector<Object::ObjectID> sourceIds, MirrorAxis axis);

        bool        Execute(Scene& scene) override;
        void        Undo(Scene& scene) override;
        std::string GetName() const override { return "MirrorCopy"; }

        const std::vector<Object::ObjectID>& GetNewIds() const { return m_newIds; }

    private:
        std::vector<Object::ObjectID> m_sourceIds;
        MirrorAxis                    m_axis;
        std::vector<Object::ObjectID> m_newIds;
        bool                          m_executed = false;
    };
}