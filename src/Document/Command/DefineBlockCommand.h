#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Scene/BlockTable.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 定义块(AutoCAD BLOCK):把选中实体收进新块定义,原位替换为一个块引用。
    //   · 块内子实体为原实体的克隆,坐标保持世界坐标,基点存于块定义;
    //     块引用插入点 = 基点、缩放 1、旋转 0,外观与原实体完全一致。
    //   · Undo 恢复原实体并移除块引用;块定义保留在块表中(BlockID 即表索引,
    //     不支持移除),Redo 复用该定义,不重复填充。
    class DefineBlockCommand : public ICommand
    {
    public:
        DefineBlockCommand(std::string name, const Math::Point3& basePoint,
                           std::vector<Object::ObjectID> ids)
            : m_name(std::move(name))
            , m_base(basePoint)
            , m_ids(std::move(ids))
        {}

        bool Execute(Scene& scene) override
        {
            auto& table = scene.GetBlockTable();

            // 首次执行:创建块定义并填充选中实体的克隆
            if (m_blockId == BlockTable::InvalidID)
            {
                std::vector<Entity*> src;
                for (auto id : m_ids)
                {
                    auto* obj = scene.GetEntity(id);
                    if (obj && obj->IsKindOf<Entity>())
                        src.push_back(static_cast<Entity*>(obj));
                }
                if (src.empty())
                    return false;

                // 调用方负责生成唯一块名;同名已存在视为失败,避免污染既有定义
                if (table.FindByName(m_name) != BlockTable::InvalidID)
                    return false;

                m_blockId = table.Create(m_name, m_base);
                BlockEntity* blk = table.Find(m_blockId);
                if (!blk)
                    return false;

                for (auto* e : src)
                    blk->AddEntity(e->Clone(e->GetID()));

                m_insertId = scene.NextObjectID();
            }

            // 移除原实体(所有权暂存,供 Undo 恢复)
            m_removed.clear();
            for (auto id : m_ids)
            {
                auto e = scene.RemoveEntity(id);
                if (e) m_removed.push_back(std::move(e));
            }

            // 原位添加块引用
            auto ins = std::make_unique<InsertEntity>(m_insertId, m_blockId, m_name, m_base);
            ins->SetBlock(table.Find(m_blockId));
            scene.AddEntity(std::move(ins));
            return true;
        }

        void Undo(Scene& scene) override
        {
            scene.RemoveEntity(m_insertId);   // 丢弃引用,Redo 时重建
            for (auto& e : m_removed)
                scene.AddEntity(std::move(e));
            m_removed.clear();
        }

        std::string GetName() const override { return "定义块 " + m_name; }

    private:
        std::string                          m_name;
        Math::Point3                         m_base;
        std::vector<Object::ObjectID>        m_ids;       // 被收进块的原实体
        BlockID                              m_blockId  = BlockTable::InvalidID;
        Object::ObjectID                     m_insertId = Object::InvalidID;
        std::vector<std::unique_ptr<Object>> m_removed;   // Execute 暂存,Undo 归还场景
    };
}
