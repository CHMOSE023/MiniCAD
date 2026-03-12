// ============================================================
// MiniCAD — app/Command/DeleteEntityCommand.cpp
// ============================================================
#include "app/Command/DeleteEntityCommand.h"
#include "app/Scene/Scene.h"
#include <core/Object/Object.hpp>

namespace MiniCAD {

DeleteEntityCommand::DeleteEntityCommand(Scene& scene, Object::ObjectID id)
    : m_scene(scene), m_entityId(id) {}

void DeleteEntityCommand::Execute() {
    m_entity = m_scene.RemoveEntity(m_entityId);
}

void DeleteEntityCommand::Undo() {
    if (m_entity) m_scene.AddEntity(std::move(m_entity));
}

} // namespace MiniCAD
