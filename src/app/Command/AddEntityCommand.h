// ============================================================
// MiniCAD — app/Command/AddEntityCommand.h
// 职责：添加实体命令：Execute 加入 Scene，Undo 移除
// 依赖：doc/UndoRedo/ICommand.h, app/Scene/Scene.h
// 约束：不依赖 render/ ui/；不持有 Scene 以外的全局状态
// ============================================================
#pragma once

#include "doc/UndoRedo/ICommand.h"
#include "app/Scene/Scene.h"
#include "core/Object/Object.hpp"
#include <memory>

namespace MiniCAD {

class AddEntityCommand : public ICommand {
public:
    AddEntityCommand(Scene& scene, std::unique_ptr<Object> entity);

    void Execute() override;
    void Undo()    override;
    std::string GetDescription() const override { return "Add Entity"; }

private:
    Scene&                  m_scene;
    std::unique_ptr<Object> m_entity;   // 在 Execute 后所有权转给 Scene；Undo 后转回
    Object::ObjectID        m_entityId = Object::INVALID_ID;
};

} // namespace MiniCAD
