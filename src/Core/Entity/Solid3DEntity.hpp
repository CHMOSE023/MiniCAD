#pragma once
#include "Entity.hpp"
#include "Core/Math/Mat4.hpp"
#include "Solid/ISolidKernel.h"
#include <cmath>
#include <memory>
#include <vector>

namespace MiniCAD
{
    // 三维实体（对应 DXF 3DSOLID；MiniCAD 已有的 SolidEntity 是二维填充）。开发计划见 docs/Solid/PLAN.md。
    //
    // 几何是内核实体（BRep）加一个局部 → 世界的变换矩阵（行向量约定，P_world = P_local · Transform）：
    //   - 内核数据（Save 的字节）与局部坐标下的线框在创建时一次求出，之后不变，克隆时共享；
    //   - 移动 / 旋转 / 镜像 / 缩放只改矩阵，不调用内核，所以这些编辑很快、可直接撤销；
    //   - 读写保存字节、矩阵和线框：没有内核（如网页版）时也能显示、移动、原样保存。
    // 需要内核实体时（布尔、圆角等，M3 起）由 GetBody / GetWorldBody 从字节懒加载。
    //
    // 显示（第一期）：俯视线框，即实体所有边的折线，不画曲面轮廓线。
    class Solid3DEntity : public Entity
    {
    public:
        using Wire = std::vector<std::vector<Math::Point3>>;

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
            auto e = std::make_unique<Solid3DEntity>(id, std::move(bytes), std::move(wire), transform, kernel.Bounds(body));
            e->m_data->Body = body;
            return e;
        }

        // 由保存的数据恢复（不需要内核）。localBounds 是局部坐标下实体的精确包围盒（由内核求出）：
        // 线框只有边，曲面会凸出到边以外（球只有接缝和赤道两条边），只用线框求的包围盒会偏小
        Solid3DEntity(ObjectID id, std::vector<uint8_t> bytes, Wire localWire, const Math::Mat4& transform,
                      const AABB& localBounds = AABB::Empty())
            : Entity(id), m_data(std::make_shared<Data>()), m_transform(transform)
        {
            m_data->Bytes       = std::move(bytes);
            m_data->LocalWire   = std::move(localWire);
            m_data->LocalBounds = localBounds;
            UpdateWorldWire();
        }

        // ── 几何 ─────────────────────────────────────────────────────
        const std::vector<uint8_t>& GetBytes()     const { return m_data->Bytes; }
        const Wire&                 GetLocalWire() const { return m_data->LocalWire; }
        const Wire&                 GetWorldWire() const { return m_worldWire; }
        const AABB&                 GetLocalBounds() const { return m_data->LocalBounds; }   // 可能为空（旧数据）

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

    private:
        // 不随变换改变的部分，克隆之间共享（实体缓存在第一次需要时加载，仍视为不可变）
        struct Data
        {
            std::vector<uint8_t>   Bytes;
            Wire                   LocalWire;
            AABB                   LocalBounds = AABB::Empty();
            mutable Solid::BodyPtr Body;
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
