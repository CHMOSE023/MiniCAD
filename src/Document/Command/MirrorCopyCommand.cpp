#include "MirrorCopyCommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"

namespace MiniCAD
{
    MirrorCopyCommand::MirrorCopyCommand(std::vector<Object::ObjectID> sourceIds,  MirrorAxis axis)  : m_sourceIds(std::move(sourceIds)), m_axis(axis)
    {
    }

    bool MirrorCopyCommand::Execute(Scene& scene)
    {
        // Redo:沿用已分配的 ID
        if (m_executed && !m_newIds.empty())
        {
            for (size_t i = 0; i < m_sourceIds.size(); ++i)
            {
                auto* src = scene.GetEntity(m_sourceIds[i]);
                if (!src || !src->IsKindOf<Entity>()) continue;

                auto clone = static_cast<Entity*>(src)->Clone(m_newIds[i]);
                MirrorEntityInPlace(*clone, m_axis);
                scene.AddEntity(std::move(clone));   // 按你 Scene 接口调整
            }
            scene.MarkDirty();
            return true;
        }

        // 首次执行
        m_newIds.clear();
        m_newIds.reserve(m_sourceIds.size());

        for (auto srcId : m_sourceIds)
        {
            auto* src = scene.GetEntity(srcId);
            if (!src || !src->IsKindOf<Entity>()) continue;

            Object::ObjectID newId = scene.NextObjectID();   // 按你 Scene 接口调整
            auto clone = static_cast<Entity*>(src)->Clone(newId);
            MirrorEntityInPlace(*clone, m_axis);
            scene.AddEntity(std::move(clone));

            m_newIds.push_back(newId);
        }

        m_executed = true;
        scene.MarkDirty();
        return !m_newIds.empty();
    }

    void MirrorCopyCommand::Undo(Scene& scene)
    {
        for (auto id : m_newIds)
            scene.RemoveEntity(id);
        scene.MarkDirty();
    }
}
