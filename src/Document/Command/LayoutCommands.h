#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // 图纸布局的新建 / 删除（可撤销）
    //
    // 布局按 OwnerKey 认（Scene::SetLayouts 保证非 0 且唯一），撤销 / 重做时下标可能已变，所以按键找。
    // 当前布局被删掉（含撤销新建）后由界面经 Document::ValidateActiveLayout 回到模型页。
    // =========================================================================

    // 新建布局：插到 index 处（index 越界时放到最后）
    class AddLayoutCommand : public ICommand
    {
    public:
        AddLayoutCommand(DrawingLayout layout, int index) : m_layout(std::move(layout)), m_index(index) {}

        bool Execute(Scene& scene) override
        {
            if (m_layout.Model)
                return false;
            if (m_layout.OwnerKey == 0 || scene.FindLayout(m_layout.OwnerKey) >= 0)
                m_layout.OwnerKey = scene.NewLayoutKey();
            std::vector<DrawingLayout> layouts = scene.Layouts();
            const int at = (m_index >= 1 && m_index <= static_cast<int>(layouts.size())) ? m_index : static_cast<int>(layouts.size());
            layouts.insert(layouts.begin() + at, m_layout);
            scene.SetLayouts(std::move(layouts));
            return true;
        }

        void Undo(Scene& scene) override
        {
            std::vector<DrawingLayout> layouts = scene.Layouts();
            const int i = scene.FindLayout(m_layout.OwnerKey);
            if (i < 0)
                return;
            layouts.erase(layouts.begin() + i);
            scene.SetLayouts(std::move(layouts));
        }

        std::string GetName() const override { return "新建布局"; }
        uint64_t    GetKey() const { return m_layout.OwnerKey; }    // 执行后有效

    private:
        DrawingLayout m_layout;
        int           m_index;
    };

    // 删除布局：连同它的图纸空间实体（视口、图框等）一起删除。模型页和最后一张图纸布局不能删
    class RemoveLayoutCommand : public ICommand
    {
    public:
        explicit RemoveLayoutCommand(uint64_t ownerKey) : m_key(ownerKey) {}

        bool Execute(Scene& scene) override
        {
            const int i = scene.FindLayout(m_key);
            if (i < 0 || PaperCount(scene) <= 1)
                return false;
            std::vector<DrawingLayout> layouts = scene.Layouts();
            m_index  = i;
            m_layout = layouts[static_cast<size_t>(i)];

            m_entities.clear();
            for (Object::ObjectID id : scene.GetAllIDs())
            {
                const Object* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>())
                    continue;
                const EntityAttr& attr = static_cast<const Entity&>(*obj).GetAttr();
                if (attr.InPaperSpace && attr.LayoutKey == m_key)
                    m_entities.push_back(scene.RemoveEntity(id));
            }

            layouts.erase(layouts.begin() + i);
            scene.SetLayouts(std::move(layouts));
            return true;
        }

        void Undo(Scene& scene) override
        {
            std::vector<DrawingLayout> layouts = scene.Layouts();
            const int at = std::min(m_index, static_cast<int>(layouts.size()));
            layouts.insert(layouts.begin() + at, m_layout);
            scene.SetLayouts(std::move(layouts));
            for (auto& e : m_entities)
                scene.AddEntity(std::move(e));
            m_entities.clear();
        }

        std::string GetName() const override { return "删除布局"; }

        static int PaperCount(const Scene& scene)
        {
            int n = 0;
            for (const DrawingLayout& l : scene.Layouts())
                n += l.Model ? 0 : 1;
            return n;
        }

    private:
        uint64_t                             m_key;
        int                                  m_index = 1;
        DrawingLayout                        m_layout;
        std::vector<std::unique_ptr<Object>> m_entities;   // 被删布局的图纸空间实体（撤销时放回）
    };
}
