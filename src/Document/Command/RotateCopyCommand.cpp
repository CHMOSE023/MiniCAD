#include "RotateCopyCommand.h"
#include "Scene/Scene.h"
#include "Document/DimAssoc.h"
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
            // AddEntity/RemoveEntity 已按实体标脏，无需全局失效
            DimAssoc::RemapCopies(scene, m_sourceIds, m_newIds);   // 一起复制的标注改为关联到副本
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

        DimAssoc::RemapCopies(scene, m_sourceIds, m_newIds);   // 一起复制的标注改为关联到副本

        m_executed = true;
        // AddEntity/RemoveEntity 已按实体标脏，无需全局失效
        return !m_newIds.empty();
    }

    void RotateCopyCommand::Undo(Scene& scene)
    {
        for (auto id : m_newIds)
            scene.RemoveEntity(id);
        // AddEntity/RemoveEntity 已按实体标脏，无需全局失效
    }
}
