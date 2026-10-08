#pragma once
#include "Entity.hpp"
#include "BlockEntity.hpp"
#include "AttribEntity.hpp"
#include "Core/Draw/TransformDrawSink.hpp"
#include "Core/Math/Mat4.hpp"
#include "Core/Math/OCS.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace MiniCAD
{ 
    // 块引用(对应 DXF INSERT / MINSERT)。引用一个 BlockEntity,以
    // 插入点 + XYZ 缩放 + 绕 Z 旋转(均在 OCS 内)落位;可选行列阵列(MINSERT)。
    // 绘制时展开块子实体并施加插入变换,且对 ByBlock 属性做继承解析。
    class InsertEntity : public Entity
    { 
    public:
        using BlockID = uint64_t;
    public:
        InsertEntity(ObjectID id, BlockID blockId, const std::string& blockName,
                     const Math::Point3& position)
            : Entity(id)
            , m_blockId(blockId)
            , m_blockName(blockName)
            , m_position(position)
        {}

        // ── 引用的块 ─────────────────────────────────────────────────────────
        BlockID            GetBlockId()   const { return m_blockId; }
        const std::string& GetBlockName() const { return m_blockName; }

        // 解析(链接)到实际块定义。绘制需要它;由调用方在块表中查到后注入。
        void               SetBlock(const BlockEntity* blk) { m_block = blk; m_resolvedDirty = true; }
        const BlockEntity* GetBlock() const                 { return m_block; }

        // ── 插入变换(OCS 内)──────────────────────────────────────────────────
        const Math::Point3& GetPosition() const { return m_position; }     // 组码 10
        void                SetPosition(const Math::Point3& p) { m_position = p; }

        const Math::Vec3&   GetScale() const { return m_scale; }           // 组码 41/42/43
        void                SetScale(const Math::Vec3& s) { m_scale = s; }
        void                SetUniformScale(double s) { m_scale = { s, s, s }; }

        double GetRotation() const   { return m_rotation; }                // 组码 50(弧度)
        void   SetRotation(double r) { m_rotation = r; }

        // ── 阵列(MINSERT,组码 70/71 + 44/45)──────────────────────────────────
        int    GetColumnCount() const { return m_colCount; }
        int    GetRowCount()    const { return m_rowCount; }
        void   SetArray(int cols, int rows, double colSpacing, double rowSpacing)
        {
            m_colCount    = cols > 0 ? cols : 1;
            m_rowCount    = rows > 0 ? rows : 1;
            m_colSpacing  = colSpacing;
            m_rowSpacing  = rowSpacing;
        }
        double GetColumnSpacing() const { return m_colSpacing; }
        double GetRowSpacing()    const { return m_rowSpacing; }
        bool   IsArray() const          { return m_colCount > 1 || m_rowCount > 1; }

        // ── 随附属性 ATTRIB ──────────────────────────────────────────────────
        void AddAttrib(std::unique_ptr<AttribEntity> a) { if (a) m_attribs.push_back(std::move(a)); }
        const std::vector<std::unique_ptr<AttribEntity>>& GetAttribs() const { return m_attribs; }

        // 块/继承属性变更后需重建展开缓存时调用(SetBlock 已自动置位)。
        void MarkResolveDirty() { m_resolvedDirty = true; }

        // ── 单元(行 r / 列 c)的块坐标 → WCS 变换矩阵 ──────────────────────────
        Math::Mat4 CellMatrix(int col, int row) const
        {
            using namespace Math;

            // 阵列偏移在 OCS 内沿(旋转后的)列/行方向。
            const double c = std::cos(m_rotation), s = std::sin(m_rotation);
            const double ox = m_colSpacing * col, oy = m_rowSpacing * row;
            const Vec3 offset{ c * ox - s * oy, s * ox + c * oy, 0.0 };
            const Vec3 cellPos{ m_position.x + offset.x, m_position.y + offset.y, m_position.z };

            // 块坐标:先减基点 → 缩放 → 绕 Z 旋转 → 平移到单元插入点(均在 OCS)。
            // 本工程 Mat4 为行向量约定(P' = P·M),组合时最左矩阵最先作用,
            // 故按作用先后从左到右相乘:T(-base) → Scale → RotationZ → T(cellPos)。
            Mat4 base = m_block ? Mat4::Translation(Vec3{ -m_block->GetBasePoint().x,
                                                          -m_block->GetBasePoint().y,
                                                          -m_block->GetBasePoint().z })
                                : Mat4::Identity();
            Mat4 m = base
                   * Mat4::Scale(m_scale)
                   * Mat4::RotationZ(m_rotation)
                   * Mat4::Translation(cellPos);

            // OCS → WCS(默认 +Z 挤出时为单位阵,跳过):插入变换产生 OCS 坐标后再 →WCS,
            // 行向量约定下应放在最右(最后作用)。
            if (!HasDefaultExtrusion())
                m = m * OcsToWcsMatrix(GetOCS());
            return m;
        }

        // ── Entity 接口 ──────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            if (!m_block) return box;

            const AABB local = m_block->GetBoundingBox();
            // 块局部包围盒 8 角,逐单元变换后取并集。
            const Math::Point3 corners[8] = {
                { local.Min.x, local.Min.y, local.Min.z }, { local.Max.x, local.Min.y, local.Min.z },
                { local.Min.x, local.Max.y, local.Min.z }, { local.Max.x, local.Max.y, local.Min.z },
                { local.Min.x, local.Min.y, local.Max.z }, { local.Max.x, local.Min.y, local.Max.z },
                { local.Min.x, local.Max.y, local.Max.z }, { local.Max.x, local.Max.y, local.Max.z },
            };
            for (int r = 0; r < m_rowCount; ++r)
                for (int c = 0; c < m_colCount; ++c)
                {
                    const Math::Mat4 m = CellMatrix(c, r);
                    for (const auto& p : corners)
                        box.Expand(m * p);
                }
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_block) return;
            EnsureResolved();

            for (int r = 0; r < m_rowCount; ++r)
                for (int c = 0; c < m_colCount; ++c)
                {
                    TransformDrawSink xsink(sink, CellMatrix(c, r));
                    for (const auto& e : m_resolved)
                        e->Draw(xsink, isSelected, isHovered);
                    for (const auto& a : m_attribs)
                        a->Draw(xsink, isSelected, isHovered);
                }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<InsertEntity>(newId, m_blockId, m_blockName, m_position);
            e->SetAttr(GetAttr());
            e->m_block      = m_block;        // 共享解析指针(块定义由块表拥有)
            e->m_scale      = m_scale;
            e->m_rotation   = m_rotation;
            e->m_colCount   = m_colCount;
            e->m_rowCount   = m_rowCount;
            e->m_colSpacing = m_colSpacing;
            e->m_rowSpacing = m_rowSpacing;
            for (const auto& a : m_attribs)
                e->m_attribs.push_back(std::unique_ptr<AttribEntity>(
                    static_cast<AttribEntity*>(a->Clone(a->GetID()).release())));
            return e;
        }

        DECLARE_RUNTIME_TYPE(InsertEntity, Entity)

    private:
        // OCS 基 → WCS 的旋转矩阵(列为 Ux/Uy/Uz)。
        static Math::Mat4 OcsToWcsMatrix(const Math::OCS& ocs)
        {
            Math::Mat4 m = Math::Mat4::Identity();
            m.m[0] = ocs.Ux.x; m.m[4] = ocs.Uy.x; m.m[8]  = ocs.Uz.x;
            m.m[1] = ocs.Ux.y; m.m[5] = ocs.Uy.y; m.m[9]  = ocs.Uz.y;
            m.m[2] = ocs.Ux.z; m.m[6] = ocs.Uy.z; m.m[10] = ocs.Uz.z;
            return m;
        }

        // 把块子实体展开为「已解析」副本:ByBlock 颜色/线型/线宽继承本 INSERT 的有效属性。
        // 变换不在此烘焙(由 TransformDrawSink 在绘制时施加),故平移/旋转/缩放变化无需重建。
        void EnsureResolved() const
        {
            if (!m_resolvedDirty) return;
            m_resolved.clear();
            if (m_block)
            {
                const EntityAttr& host = GetAttr();
                for (const auto& src : m_block->GetEntities())
                {
                    auto clone = src->Clone(src->GetID());
                    EntityAttr a = clone->GetAttr();
                    if (a.Color.Method == ColorMethod::ByBlock) a.Color = host.Color;
                    if (a.Lineweight == Lineweight::ByBlock)     a.Lineweight = host.Lineweight;
                    if (a.LineType == LineTypeTable_ByBlockID)   a.LineType = host.LineType;
                    clone->SetAttr(a);
                    m_resolved.push_back(std::move(clone));
                }
            }
            m_resolvedDirty = false;
        }

        // 与 LineTypeTable::ByBlockID 约定一致(1 = ByBlock),避免在头文件引入块表依赖。
        static constexpr LineTypeID LineTypeTable_ByBlockID = 1;

        BlockID            m_blockId   = 0;
        std::string        m_blockName;
        const BlockEntity* m_block     = nullptr;   // 解析后的块定义(非拥有)

        Math::Point3       m_position{ 0, 0, 0 };
        Math::Vec3         m_scale{ 1, 1, 1 };
        double             m_rotation = 0.0;

        int                m_colCount   = 1;        // MINSERT 列数
        int                m_rowCount   = 1;        // MINSERT 行数
        double             m_colSpacing = 0.0;
        double             m_rowSpacing = 0.0;

        std::vector<std::unique_ptr<AttribEntity>> m_attribs;

        // 展开缓存(仅随块/继承属性变化重建)。
        mutable std::vector<std::unique_ptr<Entity>> m_resolved;
        mutable bool                                 m_resolvedDirty = true;
    };
}
