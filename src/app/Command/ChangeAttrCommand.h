// ============================================================
// MiniCAD — app/Command/ChangeAttrCommand.h
// 职责：属性修改命令：记录前后 EntityAttr，Undo 还原
// 依赖：doc/UndoRedo/ICommand.h, app/Scene/Scene.h,
//       core/Entity/EntityAttr.h
// 约束：不依赖 render/ ui/
// ============================================================
#pragma once

#include "doc/UndoRedo/ICommand.h"
#include "app/Scene/Scene.h"
#include "core/Entity/EntityAttr.h" 
#include <string>
#include <core/Object/Object.hpp>

namespace MiniCAD {

class ChangeAttrCommand : public ICommand {
public:
    ChangeAttrCommand(Scene& scene, Object::ObjectID id,
                      const EntityAttr& newAttr);

    void Execute() override;
    void Undo()    override;
    std::string GetDescription() const override { return "Change Attribute"; }

private:
    Scene&           m_scene;
    Object::ObjectID m_entityId;
    EntityAttr       m_oldAttr;
    EntityAttr       m_newAttr;

    void ApplyAttr(const EntityAttr& attr);
};

} // namespace MiniCAD
