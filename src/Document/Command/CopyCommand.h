#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Vec3.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;

    // 复制命令:把若干源对象按 delta 偏移,克隆成新对象加入 Scene
    //
    // - Execute(首次):为每个源对象克隆一份,平移 delta,分配新 ID,加入 Scene。
    //                 记录新 ID 用于 Undo / 后续 Redo
    // - Undo:        按记录的新 ID 从 Scene 删除
    // - Redo:        如果记录里已有 NewId,沿用同一 ID 重新加入(保持引用稳定);
    //                 否则按首次 Execute 流程克隆并分配新 ID
    class CopyCommand : public ICommand
    {
    public:
        CopyCommand(std::vector<Object::ObjectID> sourceIds, Math::Vec3 delta);

        bool        Execute(Scene& scene) override;
        void        Undo(Scene& scene) override;
        std::string GetName() const override { return "Copy"; }

        // 暴露给外部:Execute 后可读出新生成的 ID(供 CopyTool 选中它们/继续操作)
        const std::vector<Object::ObjectID>& GetNewIds() const { return m_newIds; }

    private:
        std::vector<Object::ObjectID> m_sourceIds;  // 源对象 ID
        Math::Vec3                    m_delta;      // 平移量
        std::vector<Object::ObjectID> m_newIds;     // 生成的新对象 ID(Execute 后填充)
        bool                          m_executed = false;
    };
}
