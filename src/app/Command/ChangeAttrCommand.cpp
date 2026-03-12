// ============================================================
// MiniCAD — app/Command/ChangeAttrCommand.cpp
// ============================================================
#include "app/Command/ChangeAttrCommand.h"
#include <app/Scene/Scene.h>
#include <core/Entity/EntityAttr.h>
#include <core/Entity/LineEntity.h>
#include <core/Object/Object.hpp>

namespace MiniCAD {

    ChangeAttrCommand::ChangeAttrCommand(Scene& scene, Object::ObjectID id, const EntityAttr& newAttr)
    : m_scene(scene), m_entityId(id), m_newAttr(newAttr)
{
    // 记录旧属性
    Object* obj = m_scene.GetEntity(id);
    if (obj && obj->IsKindOf<LineEntity>()) {
        m_oldAttr = static_cast<LineEntity*>(obj)->GetAttr();
    }
}

void ChangeAttrCommand::Execute() { ApplyAttr(m_newAttr); }
void ChangeAttrCommand::Undo()    { ApplyAttr(m_oldAttr); }

void ChangeAttrCommand::ApplyAttr(const EntityAttr& attr) {
    Object* obj = m_scene.GetEntity(m_entityId);
    if (!obj) return;
    if (obj->IsKindOf<LineEntity>()) {
        static_cast<LineEntity*>(obj)->SetAttr(attr);
        m_scene.MarkDirty();
    }
}

} // namespace MiniCAD
