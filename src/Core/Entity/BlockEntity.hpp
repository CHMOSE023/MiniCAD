#pragma once
#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include <string>
#include <vector>
#include <memory>

namespace MiniCAD
{
    // 块定义(对应 DXF BLOCK / BLOCK_RECORD)。
    //   · 名称:块名(块表内唯一,不区分大小写)。
    //   · 基点:插入基点(组码 10/20/30),INSERT 时与插入点对齐。
    //   · 子实体:块内几何,坐标位于「块定义坐标系」(以基点为参照)。
    // 本身不直接出现在模型空间,而是被 InsertEntity 引用后展开绘制。
    class BlockEntity : public Entity
    {
    public:
        // ── DXF 块标志(组码 70)位掩码 ───────────────────────────────────────
        enum Flags : uint16_t
        {
            None        = 0,
            Anonymous   = 1,   // *U 匿名块(由阵列/填充等自动生成)
            HasAttribs  = 2,   // 含非常量属性定义
            XRef        = 4,   // 外部参照
            XRefOverlay = 8,   // 外部参照叠加
        };

        BlockEntity(ObjectID id, std::string name,
                    const Math::Point3& basePoint = { 0, 0, 0 })
            : Entity(id)
            , m_name(std::move(name))
            , m_basePoint(basePoint)
        {}

        // ── 名称 / 基点 / 标志 / 说明 ─────────────────────────────────────────
        const std::string&  GetName()      const { return m_name; }
        void                SetName(std::string n) { m_name = std::move(n); }

        const Math::Point3& GetBasePoint() const { return m_basePoint; }
        void                SetBasePoint(const Math::Point3& p) { m_basePoint = p; }

        uint16_t GetFlags() const         { return m_flags; }
        void     SetFlags(uint16_t f)     { m_flags = f; }
        bool     IsAnonymous() const      { return (m_flags & Anonymous) != 0; }

        const std::string& GetDescription() const { return m_description; } // 组码 4
        void               SetDescription(std::string d) { m_description = std::move(d); }

        const std::string& GetXRefPath() const { return m_xrefPath; }       // 组码 1
        void               SetXRefPath(std::string p) { m_xrefPath = std::move(p); }

        // ── 子实体管理 ───────────────────────────────────────────────────────
        void AddEntity(std::unique_ptr<Entity> e)
        {
            if (e) m_entities.push_back(std::move(e));
        }

        const std::vector<std::unique_ptr<Entity>>& GetEntities() const { return m_entities; }
        std::vector<std::unique_ptr<Entity>>&       GetEntities()       { return m_entities; }

        size_t EntityCount() const { return m_entities.size(); }
        bool   IsEmpty()     const { return m_entities.empty(); }

        // ── Entity 接口 ──────────────────────────────────────────────────────
        // 包围盒为所有子实体并集(块定义坐标系)。
        AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            for (const auto& e : m_entities)
                box.Expand(e->GetBoundingBox());
            return box;
        }

        // 直接绘制块定义内容(块编辑器视图);模型空间中由 InsertEntity 施加变换后展开。
        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            for (const auto& e : m_entities)
                e->Draw(sink, isSelected, isHovered);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto blk = std::make_unique<BlockEntity>(newId, m_name, m_basePoint);
            blk->SetAttr(GetAttr());
            blk->m_flags       = m_flags;
            blk->m_description  = m_description;
            blk->m_xrefPath     = m_xrefPath;
            blk->m_entities.reserve(m_entities.size());
            for (const auto& e : m_entities)
                blk->m_entities.push_back(e->Clone(e->GetID()));
            return blk;
        }

        DECLARE_RUNTIME_TYPE(BlockEntity, Entity)

    private:
        std::string                          m_name;                  // 块名(组码 2)
        Math::Point3                         m_basePoint{ 0, 0, 0 };  // 插入基点(组码 10/20/30)
        uint16_t                             m_flags = None;          // 块标志(组码 70)
        std::string                          m_description;           // 块说明(组码 4)
        std::string                          m_xrefPath;              // 外部参照路径(组码 1)
        std::vector<std::unique_ptr<Entity>> m_entities;             // 块内子实体
    };
}
