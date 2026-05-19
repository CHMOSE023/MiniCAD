#include "RotateCopyCommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Document/Command/EntityRotate.h"

namespace MiniCAD
{
    RotateCopyCommand::RotateCopyCommand(std::vector<Object::ObjectID> sourceIds, Math::Point3 pivot, double angle)
        : m_sourceIds(std::move(sourceIds))
        , m_pivot(pivot)
        , m_angle(angle)
    {
    }

    bool RotateCopyCommand::Execute(Scene& scene)
    {
        // Redo:沿用已分配 ID
        if (m_executed && !m_newIds.empty())
        {
            for (size_t i = 0; i < m_sourceIds.size(); ++i)
            {
                auto* src = scene.GetEntity(m_sourceIds[i]);
                if (!src || !src->IsKindOf<Entity>()) continue;

                auto clone = static_cast<Entity*>(src)->Clone(m_newIds[i]);
                RotateEntityInPlace(*clone, m_pivot, m_angle);
                scene.AddEntity(std::move(clone));
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

            Object::ObjectID newId = scene.NextObjectID();// Allocate ObjectId
            auto clone = static_cast<Entity*>(src)->Clone(newId);
            RotateEntityInPlace(*clone, m_pivot, m_angle);
            scene.AddEntity(std::move(clone));

            m_newIds.push_back(newId);
        }

        m_executed = true;
        scene.MarkDirty();
        return !m_newIds.empty();
    }

    void RotateCopyCommand::Undo(Scene& scene)
    {
        for (auto id : m_newIds)
            scene.RemoveEntity(id);
        scene.MarkDirty();
    }
}
