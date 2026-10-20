#pragma once
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Vec3.hpp"
#include "Solid/ISolidKernel.h"
#include <cstdint>
#include <memory>
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
        Geometry,   // 只用于端口：Vector / Curve / Curve3D / Body
    };

    using CurvePtr   = std::shared_ptr<const Entity>;
    using Curve3DPtr = std::shared_ptr<const Solid::NurbsCurve>;

    struct Value
    {
        std::variant<std::monostate, double, int64_t, bool, std::string, Math::Vec3, CurvePtr, Solid::BodyPtr, Curve3DPtr> Data;

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
        return t == DataType::Vector || t == DataType::Curve || t == DataType::Curve3D || t == DataType::Body || t == DataType::Geometry;
    }
}
