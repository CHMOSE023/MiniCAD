#pragma once
#include "Solid3DEntity.hpp"
#include <cmath>
#include <unordered_map>

namespace MiniCAD
{
    // 多面网格（docs/NodeGraph/PLAN.md 第 8 节，节点图“网格”的烘焙结果；对应 DWG / DXF 的多面网格 PolyfaceMesh）。
    //
    // 继承 Solid3DEntity：网格就是没有内核数据（GetBytes 为空、GetBody 返回空）的“三维实体”，
    // 移动 / 旋转 / 镜像 / 缩放、夹点、捕捉、着色、轮廓线、读写局部网格、导出多面网格都与三维实体相同；
    // 不同的是线框画全部网格边，导出时全部边可见，布尔等需要内核实体的命令跳过它。
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

        std::unique_ptr<Entity> Clone(ObjectID id) const override
        {
            auto e = std::unique_ptr<MeshEntity>(new MeshEntity(id, m_data, GetTransform()));
            e->SetAttr(GetAttr());
            return e;
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
    };
}
