#pragma once
#include "Core/Entity/Entity.hpp"
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // 属性描述表：特性面板「几何」特性的数据来源
    //
    // 每种实体一张表，每行描述一个可显示 / 可编辑的特性（圆心 X、半径、长度……）。
    // 表里只有语义，没有界面：单位换算（角度一律用度）、派生值（直径、长度）、
    // 合法性校验（半径 > 0）都写在 get / set 里。面板、命令、测试共用同一份。
    // =========================================================================
    enum class PropKind { Number, Angle, String };    // Angle：界面与 get / set 都用「度」

    using PropValue = std::variant<double, std::string>;

    struct PropertyDesc
    {
        const char* name;      // 显示名，同时是多选时求交集的键，例如 "圆心 X"
        PropKind    kind;
        std::function<PropValue(const Entity&)>        get;
        std::function<bool(Entity&, const PropValue&)> set;     // 返回 false：值非法，不修改；空：只读
    };

    // 实体对应的属性表；没有描述表的实体类型返回空表
    const std::vector<PropertyDesc>& PropertiesOf(const Entity& e);

    // 多选时的公共特性（各实体表按 名称 + 类型 求交集，顺序以第一个实体为准）
    struct CommonProperty
    {
        const char*              name = "";
        PropKind                 kind = PropKind::Number;
        bool                     editable = true;     // 所有选中实体的这一行都可写才可编辑
        std::optional<PropValue> value;               // 取值不一致时为 nullopt（面板显示「*多种*」）
    };
    std::vector<CommonProperty> CommonProperties(const std::vector<const Entity*>& entities);

    // 两个值是否相同（数值按容差比较）
    bool PropValueEqual(const PropValue& a, const PropValue& b);

    // 在 entity 的副本上尝试把名为 name 的特性改成 value：
    //   成功返回修改后的副本；特性不存在 / 只读 / 值非法 / 值没有变化返回 nullptr
    std::unique_ptr<Entity> ApplyProperty(const Entity& entity, const std::string& name, const PropValue& value);
}
