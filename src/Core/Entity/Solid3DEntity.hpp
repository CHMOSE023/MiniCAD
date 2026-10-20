#pragma once
#include "Entity.hpp"
#include "Core/Math/Mat4.hpp"
#include "Solid/ISolidKernel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace MiniCAD
{
    // 三维实体（对应 DXF 3DSOLID；MiniCAD 已有的 SolidEntity 是二维填充）。开发计划见 docs/Solid/PLAN.md。
    //
    // 几何是内核实体（BRep）加一个局部 → 世界的变换矩阵（行向量约定，P_world = P_local · Transform）：
    //   - 内核数据（Save 的字节）与局部坐标下的线框在创建时一次求出，之后不变，克隆时共享；
    //   - 移动 / 旋转 / 镜像 / 缩放只改矩阵，不调用内核，所以这些编辑很快、可直接撤销；
    //   - 读写保存字节、矩阵、线框和网格：没有内核（如网页版）时也能显示、移动、原样保存、导出 DWG / DXF（多面网格）。
    // 需要内核实体时（布尔、圆角等，M3 起）由 GetBody / GetWorldBody 从字节懒加载。
    //
    // 显示（第一期）：俯视线框，即实体所有边的折线，不画曲面轮廓线。
    class Solid3DEntity : public Entity
    {
    public:
        using Wire = std::vector<std::vector<Math::Point3>>;

        // 局部坐标下的三角网格，创建时由内核离散得到，用于导出 DWG / DXF（多面网格）。
        // 同一位置的顶点已合并；EdgeFlags 每个三角形一项，第 i 位表示第 i 条边（顶点 i → i+1）在内核的同一个面内部，
        // 导出时设为不可见，这样在 AutoCAD 里看到的线框与实体的边一致
        struct SurfaceMesh
        {
            std::vector<Math::Point3> Positions;
            std::vector<uint32_t>     Indices;     // 每 3 个一个三角形
            std::vector<uint8_t>      EdgeFlags;   // 与三角形一一对应

            bool   Empty() const { return Indices.empty(); }
            size_t TriangleCount() const { return Indices.size() / 3; }
        };

        // 内核网格（顶点按面拆分，带各面的法线）→ SurfaceMesh：先合并同位置顶点；
        // 一条边恰好被两个三角形共用、且两侧在两个端点处的法线相同，就在同一个面内部（或光滑相接处）
        static SurfaceMesh BuildSurfaceMesh(const Solid::Mesh& mesh)
        {
            SurfaceMesh out;
            const size_t triCount = mesh.TriangleCount();
            if (triCount == 0)
                return out;

            // 合并同位置顶点：按包围盒尺度的相对容差量化
            AABB box = AABB::Empty();
            for (const auto& p : mesh.Positions)
                box.Expand(p);
            const double size = std::max({ box.Max.x - box.Min.x, box.Max.y - box.Min.y, box.Max.z - box.Min.z, 1.0 });
            const double tol  = size * 1e-9;
            std::map<std::tuple<long long, long long, long long>, uint32_t> welded;
            std::vector<uint32_t> remap(mesh.Positions.size());
            for (size_t i = 0; i < mesh.Positions.size(); ++i)
            {
                const auto& p = mesh.Positions[i];
                const auto  k = std::make_tuple(std::llround(p.x / tol), std::llround(p.y / tol), std::llround(p.z / tol));
                auto [it, inserted] = welded.try_emplace(k, static_cast<uint32_t>(out.Positions.size()));
                if (inserted)
                    out.Positions.push_back(p);
                remap[i] = it->second;
            }

            // 每条边（合并后的顶点对）的使用者：三角形、该三角形在两个端点处的原始顶点
            struct Use { size_t Tri; int Side; uint32_t OrigLo, OrigHi; };
            std::map<std::pair<uint32_t, uint32_t>, std::vector<Use>> edges;
            std::vector<size_t> kept;
            for (size_t t = 0; t < triCount; ++t)
            {
                const uint32_t* o = &mesh.Indices[t * 3];
                if (remap[o[0]] == remap[o[1]] || remap[o[1]] == remap[o[2]] || remap[o[2]] == remap[o[0]])
                    continue;   // 合并后退化
                for (int i = 0; i < 3; ++i)
                {
                    uint32_t a = o[i], b = o[(i + 1) % 3];
                    if (remap[a] > remap[b])
                        std::swap(a, b);
                    edges[{ remap[a], remap[b] }].push_back({ kept.size(), i, a, b });
                }
                kept.push_back(t);
            }

            auto sameNormal = [&](uint32_t a, uint32_t b) {
                if (a >= mesh.Normals.size() || b >= mesh.Normals.size())
                    return false;
                const auto& n = mesh.Normals[a];
                const auto& m = mesh.Normals[b];
                return n.x * m.x + n.y * m.y + n.z * m.z > 1.0 - 1e-6;
            };
            out.EdgeFlags.assign(kept.size(), 0);
            for (const auto& [key, uses] : edges)
                if (uses.size() == 2 && sameNormal(uses[0].OrigLo, uses[1].OrigLo) && sameNormal(uses[0].OrigHi, uses[1].OrigHi))
                    for (const auto& u : uses)
                        out.EdgeFlags[u.Tri] |= static_cast<uint8_t>(1u << u.Side);

            for (size_t t : kept)
                for (int i = 0; i < 3; ++i)
                    out.Indices.push_back(remap[mesh.Indices[t * 3 + i]]);
            return out;
        }

        // 着色用的法线（M6）：每个三角形的每个角一条（与 Indices 一一对应），局部坐标、单位长度。
        // 绕一个顶点的三角形按“同一个面内部的边”（EdgeFlags）连成光滑的扇区，扇区内取三角形法线的面积加权平均：
        // 曲面上光滑过渡，棱边两侧保持锐利
        static std::vector<Math::Vec3> BuildCornerNormals(const SurfaceMesh& mesh)
        {
            const size_t triCount = mesh.TriangleCount();
            std::vector<Math::Vec3> faceNormals(triCount);
            for (size_t t = 0; t < triCount; ++t)
            {
                const auto& a = mesh.Positions[mesh.Indices[t * 3]];
                const Math::Vec3 u = mesh.Positions[mesh.Indices[t * 3 + 1]] - a;
                const Math::Vec3 w = mesh.Positions[mesh.Indices[t * 3 + 2]] - a;
                faceNormals[t] = { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };   // 长度 = 面积的两倍
            }

            // 并查集：元素为三角形的角 (t, i)，即 t * 3 + i
            std::vector<uint32_t> parent(triCount * 3);
            for (uint32_t i = 0; i < parent.size(); ++i)
                parent[i] = i;
            auto find = [&](uint32_t x) {
                while (parent[x] != x)
                    x = parent[x] = parent[parent[x]];
                return x;
            };
            auto corner = [&](size_t t, uint32_t v) -> uint32_t {
                for (uint32_t i = 0; i < 3; ++i)
                    if (mesh.Indices[t * 3 + i] == v)
                        return static_cast<uint32_t>(t * 3 + i);
                return UINT32_MAX;
            };
            // 光滑的边（两侧三角形在同一个面内）：两端的角各自合并
            std::map<std::pair<uint32_t, uint32_t>, size_t> firstUse;
            for (size_t t = 0; t < triCount; ++t)
                for (int i = 0; i < 3; ++i)
                {
                    if (t >= mesh.EdgeFlags.size() || !(mesh.EdgeFlags[t] & (1u << i)))
                        continue;
                    uint32_t a = mesh.Indices[t * 3 + i], b = mesh.Indices[t * 3 + (i + 1) % 3];
                    const auto key = std::make_pair(std::min(a, b), std::max(a, b));
                    auto [it, inserted] = firstUse.try_emplace(key, t);
                    if (inserted || it->second == t)
                        continue;
                    for (uint32_t v : { a, b })
                    {
                        const uint32_t c0 = corner(it->second, v), c1 = corner(t, v);
                        if (c0 != UINT32_MAX && c1 != UINT32_MAX)
                            parent[find(c0)] = find(c1);
                    }
                }

            std::vector<Math::Vec3> sum(parent.size());
            for (uint32_t c = 0; c < parent.size(); ++c)
                sum[find(c)] += faceNormals[c / 3];
            std::vector<Math::Vec3> normals(parent.size());
            for (uint32_t c = 0; c < parent.size(); ++c)
            {
                normals[c] = sum[find(c)].Normalized();
                if (normals[c].LengthSq() < 0.5)
                    normals[c] = faceNormals[c / 3].Normalized();
            }
            return normals;
        }

        // 同一个面内部的网格边（两侧三角形在同一个面里，见 EdgeFlags）：三维视图的轮廓线从这些边里找——
        // 两侧三角形一个朝向眼睛、一个背向眼睛的就是轮廓线（M6）
        struct SmoothEdge
        {
            uint32_t A, B;        // 两个端点（Positions 下标）
            uint32_t TriA, TriB;  // 两侧的三角形
        };
        static std::vector<SmoothEdge> BuildSmoothEdges(const SurfaceMesh& mesh)
        {
            std::vector<SmoothEdge> out;
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> firstUse;
            for (uint32_t t = 0; t < mesh.TriangleCount(); ++t)
                for (int i = 0; i < 3; ++i)
                {
                    if (t >= mesh.EdgeFlags.size() || !(mesh.EdgeFlags[t] & (1u << i)))
                        continue;
                    const uint32_t a = mesh.Indices[t * 3 + i], b = mesh.Indices[t * 3 + (i + 1) % 3];
                    const auto key = std::make_pair(std::min(a, b), std::max(a, b));
                    auto [it, inserted] = firstUse.try_emplace(key, t);
                    if (!inserted && it->second != t)
                        out.push_back({ key.first, key.second, it->second, t });
                }
            return out;
        }

        // 由内核实体创建：保存内核数据并离散出线框。失败返回空（原因见 kernel.LastError()）
        static std::unique_ptr<Solid3DEntity> Create(ObjectID id, Solid::ISolidKernel& kernel, const Solid::BodyPtr& body,
                                                     const Math::Mat4& transform = Math::Mat4::Identity())
        {
            if (!body)
                return nullptr;
            std::vector<uint8_t> bytes = kernel.Save(body);
            if (bytes.empty())
                return nullptr;
            Wire wire = std::move(kernel.Edges(body).Lines);
            auto e = std::make_unique<Solid3DEntity>(id, std::move(bytes), std::move(wire), transform, kernel.Bounds(body),
                                                     BuildSurfaceMesh(kernel.Tessellate(body)));
            e->m_data->Body = body;
            return e;
        }

        // 由保存的数据恢复（不需要内核）。localBounds 是局部坐标下实体的精确包围盒（由内核求出）：
        // 线框只有边，曲面会凸出到边以外（球只有接缝和赤道两条边），只用线框求的包围盒会偏小。
        // mesh 用于导出 DWG / DXF，旧数据没有时为空
        Solid3DEntity(ObjectID id, std::vector<uint8_t> bytes, Wire localWire, const Math::Mat4& transform,
                      const AABB& localBounds = AABB::Empty(), SurfaceMesh mesh = {})
            : Entity(id), m_data(std::make_shared<Data>()), m_transform(transform)
        {
            m_data->Bytes       = std::move(bytes);
            m_data->LocalWire   = std::move(localWire);
            m_data->LocalBounds = localBounds;
            m_data->Mesh        = std::move(mesh);
            UpdateWorldWire();
        }

        // ── 几何 ─────────────────────────────────────────────────────
        const std::vector<uint8_t>& GetBytes()     const { return m_data->Bytes; }
        const Wire&                 GetLocalWire() const { return m_data->LocalWire; }
        const Wire&                 GetWorldWire() const { return m_worldWire; }
        const AABB&                 GetLocalBounds() const { return m_data->LocalBounds; }   // 可能为空（旧数据）
        const SurfaceMesh&          GetLocalMesh()   const { return m_data->Mesh; }          // 可能为空（旧数据）
        // 面内部的网格边（见 BuildSmoothEdges），第一次调用时求出，克隆之间共享
        const std::vector<SmoothEdge>& GetLocalSmoothEdges() const
        {
            if (!m_data->SmoothEdgesBuilt)
            {
                m_data->SmoothEdges      = BuildSmoothEdges(m_data->Mesh);
                m_data->SmoothEdgesBuilt = true;
            }
            return m_data->SmoothEdges;
        }
        // 网格各三角形角的法线（局部坐标，见 BuildCornerNormals），第一次调用时求出，克隆之间共享
        const std::vector<Math::Vec3>& GetLocalCornerNormals() const
        {
            if (m_data->CornerNormals.size() != m_data->Mesh.Indices.size())
                m_data->CornerNormals = BuildCornerNormals(m_data->Mesh);
            return m_data->CornerNormals;
        }

        const Math::Mat4& GetTransform() const { return m_transform; }
        void SetTransform(const Math::Mat4& m)
        {
            m_transform = m;
            UpdateWorldWire();
        }
        // 在现有变换之后再施加 m（世界坐标下的变换）
        void ApplyTransform(const Math::Mat4& m) { SetTransform(m_transform * m); }

        // ── 编辑命令用的世界坐标变换（与 EntityTranslate / Rotate / Mirror / Scale 的点变换一致）──
        static Math::Mat4 MoveMatrix(const Math::Vec3& d) { return Math::Mat4::Translation(d); }

        // 绕过 pivot 的 Z 轴逆时针旋转 angle 弧度
        static Math::Mat4 RotateMatrix(const Math::Point3& pivot, double angle)
        {
            const Math::Vec3 p(pivot.x, pivot.y, pivot.z);
            return Math::Mat4::Translation(-p) * Math::Mat4::RotationZ(angle) * Math::Mat4::Translation(p);
        }

        // 以 base 为基点等比缩放
        static Math::Mat4 ScaleMatrix(const Math::Point3& base, double factor)
        {
            const Math::Vec3 b(base.x, base.y, base.z);
            return Math::Mat4::Translation(-b) * Math::Mat4::Scale({ factor, factor, factor }) * Math::Mat4::Translation(b);
        }

        // 以 XY 平面内过 p0、p1 的直线为镜像线（z 不变）；两点重合时返回单位矩阵
        static Math::Mat4 MirrorMatrix(const Math::Point3& p0, const Math::Point3& p1)
        {
            const double dx = p1.x - p0.x, dy = p1.y - p0.y, len = std::hypot(dx, dy);
            if (len < 1e-12)
                return Math::Mat4::Identity();
            const double ux = dx / len, uy = dy / len;
            Math::Mat4 r = Math::Mat4::Identity();   // 反射矩阵对称，行 / 列向量约定相同
            r.m[0] = 2 * ux * ux - 1;  r.m[1] = 2 * ux * uy;
            r.m[4] = 2 * ux * uy;      r.m[5] = 2 * uy * uy - 1;
            const Math::Vec3 p(p0.x, p0.y, 0.0);
            return Math::Mat4::Translation(-p) * r * Math::Mat4::Translation(p);
        }

        // 局部坐标下的内核实体；第一次调用时从字节加载，失败返回空
        Solid::BodyPtr GetBody(Solid::ISolidKernel& kernel) const
        {
            if (!m_data->Body)
                m_data->Body = kernel.Load(m_data->Bytes);
            return m_data->Body;
        }

        // 世界坐标下的内核实体（施加了变换）
        Solid::BodyPtr GetWorldBody(Solid::ISolidKernel& kernel) const
        {
            Solid::BodyPtr body = GetBody(kernel);
            if (!body || IsIdentity(m_transform))
                return body;
            return kernel.Transform(body, m_transform);
        }

        // ── Entity ───────────────────────────────────────────────────
        AABB GetBoundingBox() const override { return m_bounds; }

        void Draw(IDrawSink& sink, bool selected, bool hovered) const override
        {
            const auto color = selected ? IDrawSink::kSelectionColor : hovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
            for (const auto& line : m_worldWire)
                for (size_t i = 1; i < line.size(); ++i)
                    sink.DrawLine(line[i - 1], line[i], color, false);
        }

        std::unique_ptr<Entity> Clone(ObjectID id) const override
        {
            auto e = std::unique_ptr<Solid3DEntity>(new Solid3DEntity(id, m_data, m_transform));
            e->SetAttr(GetAttr());
            return e;
        }

        DECLARE_RUNTIME_TYPE(Solid3DEntity, Entity)

    protected:
        // 不随变换改变的部分，克隆之间共享（MeshEntity 也用它）（实体缓存在第一次需要时加载，仍视为不可变）
        struct Data
        {
            std::vector<uint8_t>   Bytes;
            Wire                   LocalWire;
            AABB                   LocalBounds = AABB::Empty();
            SurfaceMesh            Mesh;
            mutable Solid::BodyPtr Body;
            mutable std::vector<Math::Vec3> CornerNormals;   // 懒求出，见 GetLocalCornerNormals
            mutable std::vector<SmoothEdge> SmoothEdges;     // 懒求出，见 GetLocalSmoothEdges
            mutable bool                    SmoothEdgesBuilt = false;
        };

        Solid3DEntity(ObjectID id, std::shared_ptr<Data> data, const Math::Mat4& transform)
            : Entity(id), m_data(std::move(data)), m_transform(transform)
        {
            UpdateWorldWire();
        }

        static bool IsIdentity(const Math::Mat4& m)
        {
            const Math::Mat4 id = Math::Mat4::Identity();
            for (int i = 0; i < 16; ++i)
                if (std::abs(m.m[i] - id.m[i]) > 1e-15)
                    return false;
            return true;
        }

        // 世界线框与包围盒：线框各点，加上局部包围盒 8 个角点变换后的位置
        void UpdateWorldWire()
        {
            m_worldWire = m_data->LocalWire;
            m_bounds    = AABB::Empty();
            for (auto& line : m_worldWire)
                for (auto& p : line)
                {
                    p = m_transform.TransformPoint(p);
                    m_bounds.Expand(p);
                }
            const AABB& b = m_data->LocalBounds;
            if (b.Min.x <= b.Max.x && b.Min.y <= b.Max.y && b.Min.z <= b.Max.z)
                for (int i = 0; i < 8; ++i)
                    m_bounds.Expand(m_transform.TransformPoint({ (i & 1) ? b.Max.x : b.Min.x,
                                                                 (i & 2) ? b.Max.y : b.Min.y,
                                                                 (i & 4) ? b.Max.z : b.Min.z }));
        }

        std::shared_ptr<Data> m_data;
        Math::Mat4            m_transform;
        Wire                  m_worldWire;   // 世界坐标下的线框，随变换更新
        AABB                  m_bounds;
    };
}
