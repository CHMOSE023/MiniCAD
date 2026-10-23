#pragma once
#include "Solid3DEntity.hpp"
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MiniCAD
{
    // 多面网格（docs/NodeGraph/PLAN.md 第 8 节，节点图“网格”的烘焙结果；对应 DWG / DXF 的多面网格 PolyfaceMesh）。
    //
    // 继承 Solid3DEntity：网格就是没有内核数据（GetBytes 为空、GetBody 返回空）的“三维实体”，
    // 移动 / 旋转 / 镜像 / 缩放、夹点、捕捉、着色、轮廓线、读写局部网格、导出多面网格都与三维实体相同；
    // 不同的是线框画全部网格边，导出时全部边可见，布尔等需要内核实体的命令跳过它。
    //
    // 【SubD 附加字段】另有一组只服务于 DWG / DXF 的 MESH（AcDbSubDMesh）往返的字段：
    // 细分级别 / 混合折痕 / 版本 / 显式边表 / 折痕值 / 覆盖选项。
    // 它们不参与显示（AutoCAD 的“平滑度”要 Catmull-Clark 细分，本版不做），
    // 只保证**几何与元数据不丢** —— 另存后重开，值与原图一致。
    class MeshEntity : public Solid3DEntity
    {
    public:
        // 由三角网格（顶点共用、局部坐标）创建：两侧三角形的法线夹角小于 creaseDeg 的边算光滑（着色时平滑过渡、参与轮廓线）
        static std::unique_ptr<MeshEntity> FromMesh(ObjectID id, const Solid::Mesh& mesh, double creaseDeg = 30.0,
                                                    const Math::Mat4& transform = Math::Mat4::Identity())
        {
            SurfaceMesh sm;
            sm.Positions = mesh.Positions;
            sm.Indices   = mesh.Indices;
            return std::make_unique<MeshEntity>(id, WithCreases(std::move(sm), creaseDeg), transform);
        }

        // 由保存的数据恢复（EdgeFlags 为空时按 30° 重新求）
        MeshEntity(ObjectID id, SurfaceMesh mesh, const Math::Mat4& transform)
            : Solid3DEntity(id, {}, AllEdges(mesh), transform, BoundsOf(mesh),
                            mesh.EdgeFlags.size() == mesh.TriangleCount() ? mesh : WithCreases(mesh, 30.0))   // 各参数都只读 mesh：求值顺序无关
        {
        }

        // 世界坐标下的网格（顶点共用，法线为空）
        Solid::Mesh ToWorldMesh() const
        {
            Solid::Mesh m;
            const SurfaceMesh& sm = GetLocalMesh();
            for (const auto& p : sm.Positions)
                m.Positions.push_back(GetTransform().TransformPoint(p));
            m.Indices = sm.Indices;
            const double* a = GetTransform().m;
            if (a[0] * (a[5] * a[10] - a[6] * a[9]) - a[1] * (a[4] * a[10] - a[6] * a[8]) + a[2] * (a[4] * a[9] - a[5] * a[8]) < 0)
                for (size_t t = 0; t < m.TriangleCount(); ++t)
                    std::swap(m.Indices[t * 3 + 1], m.Indices[t * 3 + 2]);   // 镜像过：保持正面朝外
            return m;
        }

        // ── SubD 元数据（DWG / DXF 的 MESH，组码 71 / 72 / 91）─────────────
        // 只往返、不参与显示。见文件头说明。
        std::int16_t  GetSubDVersion()          const { return m_subDVersion; }
        void          SetSubDVersion(std::int16_t v)   { m_subDVersion = v; }
        bool          GetBlendCrease()          const { return m_blendCrease; }
        void          SetBlendCrease(bool b)           { m_blendCrease = b; }
        std::int32_t  GetSubdivisionLevel()     const { return m_subdivisionLevel; }
        void          SetSubdivisionLevel(std::int32_t l) { m_subdivisionLevel = l; }
        std::int32_t  GetOverrideOption()       const { return m_overrideOption; }
        void          SetOverrideOption(std::int32_t v) { m_overrideOption = v; }

        // 显式边表（组码 90 的边对）与折痕值（组码 140，每个 crease 对应同下标的边）。
        // 空表示“用面边界现算”（非 SubD 来源的网格，如节点图烘焙 / PolyfaceMesh）。
        const std::vector<std::pair<int, int>>& GetExplicitEdges() const { return m_explicitEdges; }
        void SetExplicitEdges(std::vector<std::pair<int, int>> e) { m_explicitEdges = std::move(e); }
        const std::vector<double>& GetCreaseValues() const { return m_creaseValues; }
        void SetCreaseValues(std::vector<double> c) { m_creaseValues = std::move(c); }

        std::unique_ptr<Entity> Clone(ObjectID id) const override
        {
            auto e = std::unique_ptr<MeshEntity>(new MeshEntity(id, m_data, GetTransform()));
            e->SetAttr(GetAttr());
            e->m_subDVersion      = m_subDVersion;
            e->m_blendCrease      = m_blendCrease;
            e->m_subdivisionLevel = m_subdivisionLevel;
            e->m_overrideOption   = m_overrideOption;
            e->m_explicitEdges    = m_explicitEdges;
            e->m_creaseValues     = m_creaseValues;
            return e;
        }

        // 线框绘制。**有显式边表时按它画**（SubD MESH 的原图边表），
        // 否则退回基类的"从三角形收边"（节点图烘焙 / PolyfaceMesh 来源）。
        //
        // 为什么必须覆盖：我们的 SurfaceMesh 只有三角形表达，导入时把 N 边面
        // 扇形拆成三角形，基类 AllEdges 从三角形收边 ⇒ **每条四边形面对角线
        // 也变成一条网格边**被画出来。实测 sample_AC1032.dwg：
        //     句柄 343：48 个四边形 ⇒ 多画 48 条对角线（132 → 180）
        //     句柄 380：64 个四边形 ⇒ 多画 64 条对角线（128 → 192）
        // 用户看到的就是"网格里冒出一堆多余的三角形"。
        //
        // 而原图的显式边表是权威的：实测两个网格的「面边界去重边集」
        // 与文件给的显式边表**完全相等**（V0.1.0 已核对），按它画即不重不漏。
        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                          : isHovered  ? IDrawSink::kHoverColor
                                                       : ResolveDrawColor(sink);

            if (!m_explicitEdges.empty())
            {
                const auto& pos = GetLocalMesh().Positions;
                const auto& xf  = GetTransform();
                const int n = static_cast<int>(pos.size());
                for (const auto& [a, b] : m_explicitEdges)
                {
                    // 越界索引跳过（损坏文件不能把绘制搞崩）
                    if (a < 0 || b < 0 || a >= n || b >= n)
                        continue;
                    sink.DrawLine(xf.TransformPoint(pos[static_cast<size_t>(a)]),
                                  xf.TransformPoint(pos[static_cast<size_t>(b)]),
                                  color, false);
                }
                return;
            }
            Solid3DEntity::Draw(sink, isSelected, isHovered);
        }

        DECLARE_RUNTIME_TYPE(MeshEntity, Solid3DEntity)

    private:
        MeshEntity(ObjectID id, std::shared_ptr<Data> data, const Math::Mat4& transform) : Solid3DEntity(id, std::move(data), transform) {}

        static AABB BoundsOf(const SurfaceMesh& m)
        {
            AABB b = AABB::Empty();
            for (const auto& p : m.Positions)
                b.Expand(p);
            return b;
        }

        // 线框：每条网格边一段
        static Wire AllEdges(const SurfaceMesh& m)
        {
            Wire wire;
            std::unordered_map<uint64_t, bool> seen;
            for (size_t t = 0; t < m.TriangleCount(); ++t)
                for (int i = 0; i < 3; ++i)
                {
                    uint32_t a = m.Indices[t * 3 + i], b = m.Indices[t * 3 + (i + 1) % 3];
                    if (a > b)
                        std::swap(a, b);
                    if (seen.emplace(uint64_t(a) << 32 | b, true).second)
                        wire.push_back({ m.Positions[a], m.Positions[b] });
                }
            return wire;
        }

        // EdgeFlags：恰好被两个三角形共用、两侧法线夹角小于 creaseDeg 的边为光滑边
        static SurfaceMesh WithCreases(SurfaceMesh m, double creaseDeg)
        {
            const size_t n = m.TriangleCount();
            m.EdgeFlags.assign(n, 0);
            std::vector<Math::Vec3> normals(n);
            for (size_t t = 0; t < n; ++t)
            {
                const auto& a = m.Positions[m.Indices[t * 3]];
                const Math::Vec3 c = Math::Cross(m.Positions[m.Indices[t * 3 + 1]] - a, m.Positions[m.Indices[t * 3 + 2]] - a);
                normals[t] = c.LengthSq() > 0 ? c.Normalized() : Math::Vec3{};
            }
            std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, int>>> edges;
            for (uint32_t t = 0; t < n; ++t)
                for (int i = 0; i < 3; ++i)
                {
                    uint32_t a = m.Indices[t * 3 + i], b = m.Indices[t * 3 + (i + 1) % 3];
                    if (a > b)
                        std::swap(a, b);
                    edges[uint64_t(a) << 32 | b].push_back({ t, i });
                }
            const double cosLimit = std::cos(creaseDeg * 3.14159265358979323846 / 180.0);
            for (const auto& [key, uses] : edges)
                if (uses.size() == 2 && Math::Dot(normals[uses[0].first], normals[uses[1].first]) >= cosLimit)
                    for (const auto& [t, side] : uses)
                        m.EdgeFlags[t] |= static_cast<uint8_t>(1u << side);
            return m;
        }

        // ── SubD 元数据（默认值 = “非 SubD 来源”，见文件头说明）───────────
        std::int16_t                        m_subDVersion      = 2;      // 组码 71
        bool                                m_blendCrease      = false;  // 组码 72
        std::int32_t                        m_subdivisionLevel = 0;      // 组码 91
        std::int32_t                        m_overrideOption   = 0;      // DWG 位流尾部
        std::vector<std::pair<int, int>>    m_explicitEdges;             // 组码 90 边对
        std::vector<double>                 m_creaseValues;              // 组码 140
    };
}
