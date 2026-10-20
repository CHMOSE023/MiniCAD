#pragma once
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Vec3.hpp"
#include "Solid/ISolidKernel.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// 节点图的数据类型与值（开发计划见 docs/NodeGraph/PLAN.md 第 3.1 节）。
// 值都不可变：曲线是不在场景里的曲线实体（共享、只读），空间曲线是 NurbsCurve 数据，实体与曲面是内核的 BodyPtr，
// 所以列表可以在节点之间随意复制、共享。
namespace MiniCAD
{
    class Entity;
}

namespace MiniCAD::Nodes
{
    enum class DataType : uint8_t
    {
        Any,        // 只用于端口：接受任何值
        Number,
        Integer,
        Boolean,
        Text,
        Vector,     // 点与向量不区分
        Curve,      // MiniCAD 的曲线实体：直线、圆弧、圆、多段线、矩形
        Body,       // 三维实体或开放曲面（ISolidKernel 的 BodyPtr，见 ISolidKernel::IsSolid）
        Curve3D,    // 空间 NURBS 曲线（Solid::NurbsCurve）；曲线实体可以转换过来
        Geometry,   // 只用于端口：Vector / Plane / Curve / Curve3D / Body
        Domain,     // 区间 [Min, Max]；数值转区间为 [0, x]
        Plane,      // 工作平面（原点 + 单位 X / Y 轴）；与点互转（原点 / 过该点的 XY 平面）
    };

    // 区间
    struct Domain
    {
        double Min = 0.0, Max = 1.0;

        double Length() const { return Max - Min; }
        double At(double t) const { return Min + (Max - Min) * t; }   // t ∈ [0, 1] → 区间内的值
    };

    // 平面：原点 + 互相垂直的单位轴 X、Y，法向 Z = X × Y（右手系）。平面坐标 (u, v, w) → 原点 + u·X + v·Y + w·Z
    struct Plane
    {
        Math::Vec3 Origin;
        Math::Vec3 X{ 1, 0, 0 };
        Math::Vec3 Y{ 0, 1, 0 };

        Math::Vec3 Normal() const;
        Math::Vec3 PointAt(double u, double v, double w = 0.0) const { return Origin + X * u + Y * v + Normal() * w; }
        Math::Vec3 Coordinates(const Math::Vec3& p) const;      // 世界点 → 平面坐标 (u, v, w)
        // 平面坐标系 → 世界坐标系的矩阵（行向量约定 P' = P · M：前三行是 X、Y、Z 轴，第四行是原点）
        Math::Mat4 ToWorld() const;
        // 由原点、法向（X 轴可选，不给或与法向平行时自动取）构造；法向为零返回空
        static std::optional<Plane> FromNormal(const Math::Vec3& origin, const Math::Vec3& normal, const Math::Vec3& xHint = {});
        // 由原点、X 方向、XY 平面内另一方向构造（Y 正交化）；共线返回空
        static std::optional<Plane> FromAxes(const Math::Vec3& origin, const Math::Vec3& x, const Math::Vec3& yHint);
    };

    using CurvePtr   = std::shared_ptr<const Entity>;
    using Curve3DPtr = std::shared_ptr<const Solid::NurbsCurve>;

    struct Value
    {
        std::variant<std::monostate, double, int64_t, bool, std::string, Math::Vec3, CurvePtr, Solid::BodyPtr, Curve3DPtr, Nodes::Domain, Nodes::Plane> Data;
        // 实体的角色（只用于 Body）：空心形体只用来切除实心（形体合并、剪切几何），预览为橙色线框、不烘焙（PLAN.md 第 5.2 节）
        bool Void = false;

        Value() = default;
        Value(double v)                 : Data(v) {}
        Value(int v)                    : Data(static_cast<int64_t>(v)) {}
        Value(int64_t v)                : Data(v) {}
        Value(bool v)                   : Data(v) {}
        Value(std::string v)            : Data(std::move(v)) {}
        Value(const char* v)            : Data(std::string(v)) {}
        Value(const Math::Vec3& v)      : Data(v) {}
        Value(CurvePtr v)               : Data(std::move(v)) {}
        Value(Solid::BodyPtr v)         : Data(std::move(v)) {}
        Value(Curve3DPtr v)             : Data(std::move(v)) {}
        Value(Solid::NurbsCurve v)      : Data(std::make_shared<const Solid::NurbsCurve>(std::move(v))) {}
        Value(const Nodes::Domain& v)   : Data(v) {}
        Value(const Nodes::Plane& v)    : Data(v) {}

        // 带空心标记的实体
        static Value VoidBody(Solid::BodyPtr b, bool isVoid = true)
        {
            Value v(std::move(b));
            v.Void = isVoid;
            return v;
        }

        bool IsEmpty() const { return std::holds_alternative<std::monostate>(Data); }
        // 值本身的类型（空值为 Any）
        DataType Type() const;

        double             AsNumber()  const { return std::get<double>(Data); }
        int64_t            AsInteger() const { return std::get<int64_t>(Data); }
        bool               AsBool()    const { return std::get<bool>(Data); }
        const std::string& AsText()    const { return std::get<std::string>(Data); }
        const Math::Vec3&  AsVector()  const { return std::get<Math::Vec3>(Data); }
        const CurvePtr&    AsCurve()   const { return std::get<CurvePtr>(Data); }
        const Solid::BodyPtr& AsBody() const { return std::get<Solid::BodyPtr>(Data); }
        const Curve3DPtr&  AsCurve3D() const { return std::get<Curve3DPtr>(Data); }
        const Nodes::Domain& AsDomain() const { return std::get<Nodes::Domain>(Data); }
        const Nodes::Plane&  AsPlane()  const { return std::get<Nodes::Plane>(Data); }
    };

    using DataList = std::vector<Value>;

    const char* TypeName(DataType t);           // 中文名，编辑器提示用
    const char* TypeKey(DataType t);            // 存档用的英文键
    bool        TypeFromKey(const std::string& key, DataType& out);

    // 类型级：from 类型的输出能否连到 to 类型的输入
    bool CanConvert(DataType from, DataType to);
    // 值级转换；失败返回 false（如文字解析不出数值）
    bool Convert(const Value& v, DataType to, Value& out);
    // 显示用的文字（面板节点、提示）
    std::string ToDisplayString(const Value& v);

    inline bool IsGeometryType(DataType t)
    {
        return t == DataType::Vector || t == DataType::Plane || t == DataType::Curve || t == DataType::Curve3D || t == DataType::Body ||
               t == DataType::Geometry;
    }
}
