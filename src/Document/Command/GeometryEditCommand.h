#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // GeometryEditCommand —— 通用几何编辑命令
    //
    // 以「实体快照对 (before / after)」描述一次编辑，支持新增 / 删除 / 替换：
    //   before == nullptr → 新增（after 为新实体）
    //   after  == nullptr → 删除（before 为被删实体，供 Undo 还原）
    //   两者皆非空        → 替换几何甚至类型（同一 ObjectID，如 Circle→Arc）
    //
    // 每次应用都从快照 Clone 写回场景，命令自身始终保有快照，可反复 Undo/Redo。
    // Trim / Extend / Offset 等编辑工具统一用它提交，免去为每种操作各写一个命令。
    // =========================================================================
    class GeometryEditCommand : public ICommand
    {
    public:
        struct Item
        {
            Object::ObjectID        id = Object::InvalidID;
            std::unique_ptr<Entity> before;   // 可空
            std::unique_ptr<Entity> after;    // 可空
        };

        GeometryEditCommand(std::string name, std::vector<Item> items)
            : m_name(std::move(name)), m_items(std::move(items)) {}

        bool Execute(Scene& scene) override
        {
            if (m_items.empty()) return false;
            for (auto& it : m_items) ApplyOne(scene, it, /*useAfter=*/true);
            return true;
        }

        void Undo(Scene& scene) override
        {
            // 逆序还原，保证多项编辑互不干扰
            for (auto it = m_items.rbegin(); it != m_items.rend(); ++it)
                ApplyOne(scene, *it, /*useAfter=*/false);
        }

        std::string GetName() const override { return m_name; }

    private:
        static void ApplyOne(Scene& scene, Item& it, bool useAfter)
        {
            if (scene.Has(it.id)) scene.RemoveEntity(it.id);

            const Entity* snap = useAfter ? it.after.get() : it.before.get();
            if (snap) scene.AddEntity(snap->Clone(it.id));
        }

        std::string       m_name;
        std::vector<Item> m_items;
    };
}
