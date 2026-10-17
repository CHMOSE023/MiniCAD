#pragma once
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 三维实体建模内核接口（开发计划见 docs/Solid/PLAN.md）。
// MiniCAD 只通过这个接口使用实体建模内核，接口里只出现 MiniCAD 自己的类型；
// 具体实现（如 SMLibKernel）的头文件与类型不进入这里。
namespace MiniCAD::Solid
{
    // 实体（BRep）。不可变：所有运算都返回新的实体，输入不变，因此可以在撤销栈、剪贴板等处共享。
    class IBody
    {
    public:
        virtual ~IBody() = default;
    };
    using BodyPtr = std::shared_ptr<const IBody>;

    // 闭合轮廓：位于 z = 常数的水平面上，用于拉伸、旋转。
    // 多段线顶点按 MiniCAD 的约定带凸度：Bulge 描述从本顶点到下一顶点的段，0 为直线，
    // tan(圆心角 / 4)，正值为逆时针圆弧；最后一个顶点连回第一个顶点（不要重复首点）。
    struct ProfileVertex
    {
        Math::Point3 Point;
        double       Bulge = 0.0;
    };

    struct Profile
    {
        std::vector<ProfileVertex> Vertices;   // 多段线轮廓（至少 2 个顶点，带凸度时 2 个也能闭合）
        bool         IsCircle = false;          // 整圆轮廓：用 Center、Radius，忽略 Vertices
        Math::Point3 Center;
        double       Radius   = 0.0;

        static Profile Circle(const Math::Point3& center, double radius)
        {
            Profile p;
            p.IsCircle = true;
            p.Center   = center;
            p.Radius   = radius;
            return p;
        }
    };

    enum class BooleanOp { Union, Difference, Intersection };

    // 离散化参数，含义同 SMLib：弦高为三角形到真实曲面的最大距离，0 表示不限制
    struct TessParams
    {
        double ChordHeight     = 0.0;
        double CurveAngleDeg   = 15.0;
        double SurfaceAngleDeg = 15.0;
    };

    // 三角网格：顶点按面拆分（同一位置在不同面上各有一个顶点，带各自的法线），
    // 三角形逆时针为外侧（右手定则，法线指向实体外）
    struct Mesh
    {
        std::vector<Math::Point3> Positions;
        std::vector<Math::Vec3>   Normals;     // 与 Positions 一一对应，单位长度
        std::vector<uint32_t>     Indices;     // 每 3 个一个三角形

        size_t TriangleCount() const { return Indices.size() / 3; }
    };

    // 一组折线。Ids 与 Lines 一一对应：Edges() 中是边的编号（同一实体内稳定，可用于 Fillet），其余为 -1
    struct Polylines
    {
        std::vector<std::vector<Math::Point3>> Lines;
        std::vector<int>                       Ids;
    };

    // 隐藏线消除的结果：沿视线方向看实体时可见 / 被遮挡的线（实体的边与曲面轮廓线），
    // 仍是原位置上的三维折线，投影到图纸平面由调用方完成
    struct HiddenLineResult
    {
        Polylines Visible;
        Polylines Hidden;
    };

    // 失败时返回空指针 / 空结果，原因由 LastError() 给出（下一次调用前有效）。
    // 实现不保证线程安全：同一内核对象只能在一个线程里使用。
    class ISolidKernel
    {
    public:
        virtual ~ISolidKernel() = default;

        virtual const char* Name() const = 0;
        virtual const std::string& LastError() const = 0;

        // ── 创建 ─────────────────────────────────────────────────────
        // 长方体：origin 为 x / y / z 最小的角点，三个边长都必须为正
        virtual BodyPtr Box(const Math::Point3& origin, double dx, double dy, double dz) = 0;
        // 圆柱：底面圆心、半径、高度（沿 +Z）
        virtual BodyPtr Cylinder(const Math::Point3& baseCenter, double radius, double height) = 0;
        virtual BodyPtr Sphere(const Math::Point3& center, double radius) = 0;
        // 拉伸：轮廓沿 +Z 拉伸 height（负值沿 -Z）
        virtual BodyPtr Extrude(const Profile& profile, double height) = 0;
        // 旋转：轮廓绕轴旋转 angleDeg 度（360 为整圈）；轴不能穿过轮廓内部
        virtual BodyPtr Revolve(const Profile& profile, const Math::Point3& axisPoint, const Math::Vec3& axisDir, double angleDeg) = 0;

        // ── 修改（都返回新实体）──────────────────────────────────────
        virtual BodyPtr Boolean(BooleanOp op, const BodyPtr& a, const BodyPtr& b) = 0;
        // 对 Edges() 给出编号的边倒圆角
        virtual BodyPtr Fillet(const BodyPtr& body, std::span<const int> edgeIds, double radius) = 0;
        // 任意仿射变换（行向量约定，与 Math::Mat4 相同）；非均匀缩放会把几何转成 NURBS
        virtual BodyPtr Transform(const BodyPtr& body, const Math::Mat4& m) = 0;

        // ── 显示与查询 ───────────────────────────────────────────────
        virtual Mesh      Tessellate(const BodyPtr& body, const TessParams& params = {}) = 0;
        // 所有边离散成折线，angleTolDeg 为相邻采样点切线的最大夹角
        virtual Polylines Edges(const BodyPtr& body, double angleTolDeg = 10.0) = 0;
        // 隐藏线消除：视线沿 viewDir（如俯视为 (0, 0, −1)）；光滑相接处的边与曲面接缝不输出
        virtual HiddenLineResult HiddenLines(const BodyPtr& body, const Math::Vec3& viewDir) = 0;
        // 与平面的截交线
        virtual Polylines Section(const BodyPtr& body, const Math::Point3& planePoint, const Math::Vec3& planeNormal) = 0;
        virtual double    Volume(const BodyPtr& body) = 0;   // 失败返回负数
        virtual AABB      Bounds(const BodyPtr& body) = 0;   // 失败返回 AABB::Empty()

        // ── 读写（内核自己的格式，用于 .mcad / JSON）────────────────────
        // Load 先校验数据（格式标记、长度、校验和），损坏的数据返回空指针，不会交给内核解析
        virtual std::vector<uint8_t> Save(const BodyPtr& body) = 0;   // 失败返回空
        virtual BodyPtr              Load(std::span<const uint8_t> bytes) = 0;
    };
}
