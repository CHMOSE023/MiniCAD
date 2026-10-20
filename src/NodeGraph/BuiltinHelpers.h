#pragma once
#include "NodeGraph/NodeType.h"
#include <string>
#include <utility>

// 内置节点注册用的简写（只在 BuiltinNodes*.cpp 里使用）
namespace MiniCAD::Nodes::Builtin
{
    inline PortSpec In(std::string name, DataType type, Value def = {}, std::string tip = {})
    {
        PortSpec p;
        p.Name    = std::move(name);
        p.Type    = type;
        p.Default = std::move(def);
        p.Tip     = std::move(tip);
        return p;
    }

    // 按列表访问的输入
    inline PortSpec InList(std::string name, DataType type, bool optional = false, std::string tip = {})
    {
        PortSpec p;
        p.Name     = std::move(name);
        p.Type     = type;
        p.Acc      = Access::List;
        p.Optional = optional;
        p.Tip      = std::move(tip);
        return p;
    }

    inline PortSpec Optional(PortSpec p)
    {
        p.Optional = true;
        return p;
    }

    inline PortSpec Out(std::string name, DataType type, std::string tip = {})
    {
        PortSpec p;
        p.Name = std::move(name);
        p.Type = type;
        p.Tip  = std::move(tip);
        return p;
    }

    // Body 端口的子类型提示（搜索排序与提示用，不限制连线）
    inline PortSpec SolidPort(PortSpec p)
    {
        p.Kind = BodyKind::Solid;
        return p;
    }

    inline PortSpec SurfacePort(PortSpec p)
    {
        p.Kind = BodyKind::Surface;
        return p;
    }

    inline ParamSpec Param(std::string name, Value def, ParamWidget widget = ParamWidget::Field)
    {
        ParamSpec p;
        p.Name    = std::move(name);
        p.Default = std::move(def);
        p.Widget  = widget;
        return p;
    }

    inline ParamSpec Choice(std::string name, std::vector<std::string> choices, int64_t def = 0)
    {
        ParamSpec p;
        p.Name    = std::move(name);
        p.Default = Value(def);
        p.Widget  = ParamWidget::Choice;
        p.Choices = std::move(choices);
        return p;
    }

    inline Math::Vec3 Vec(double x, double y, double z) { return Math::Vec3{ x, y, z }; }
}
