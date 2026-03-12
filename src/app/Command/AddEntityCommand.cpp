// ============================================================
// MiniCAD — app/Command/AddEntityCommand.cpp
// ============================================================
#include "app/Command/AddEntityCommand.h" 
#include <utility>
#include <core/Object/Object.hpp>
#include <memory>
#include <app/Scene/Scene.h>

namespace MiniCAD {

AddEntityCommand::AddEntityCommand(Scene& scene, std::unique_ptr<Object> entity)
    : m_scene(scene)
    , m_entity(std::move(entity))
{
    if (m_entity) m_entityId = m_entity->GetID();
}

void AddEntityCommand::Execute() {
    if (m_entity) {
        // 第一次执行：将所有权转给 Scene
        m_scene.AddEntity(std::move(m_entity));
    } else {
        // Redo 路径：entity 已在 Scene 里（由 Undo 拿回后再次 Execute）
        // 此处对 Redo 路径不适用默认 Execute()；子类应重写 Redo()
    }
}

void AddEntityCommand::Undo() {
    // 从 Scene 取回所有权
    m_entity = m_scene.RemoveEntity(m_entityId);
}

} // namespace MiniCAD
