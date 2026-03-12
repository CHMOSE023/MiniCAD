// ============================================================
// MiniCAD — app/Command/DeleteEntityCommand.h
// 职责：删除实体命令：Execute 移除，Undo 恢复快照
// 依赖：doc/UndoRedo/ICommand.h, app/Scene/Scene.h
// 约束：不依赖 render/ ui/
// ============================================================
#pragma once

#include "doc/UndoRedo/ICommand.h"
#include "app/Scene/Scene.h"
#include <memory> 
#include <string>
#include <core/Object/Object.hpp>

namespace MiniCAD {

class DeleteEntityCommand : public ICommand {
public:
    DeleteEntityCommand(Scene& scene, Object::ObjectID id);

    void Execute() override;
    void Undo()    override;
    void Redo()    override { Execute(); }
    std::string GetDescription() const override { return "Delete Entity"; }

private:
    Scene&                  m_scene;
    Object::ObjectID        m_entityId;
    std::unique_ptr<Object> m_entity;       // 持有被删除的实体（用于 Undo）
};

} // namespace MiniCAD
