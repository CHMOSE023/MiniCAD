#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <memory>

namespace MiniCAD
{
    // 圆角（Fillet）的计算结果。first / second 为修剪或延伸后的两个对象（保持原 ID 与属性）；
    // 对象不需要修改时（例如圆）为空。arc 是新生成的圆角弧，半径为 0 时为空。
    struct FilletResult
    {
        std::unique_ptr<Entity>   first;
        std::unique_ptr<Entity>   second;
        std::unique_ptr<ArcEntity> arc;          // ID 为 0，由调用方分配；属性取自第一个对象
    };

    // 计算 a、b 之间半径为 radius 的圆角。pickA / pickB 是用户点选两个对象时的位置，
    // 用来决定保留哪一侧（保留靠近点选位置的那一段）以及在多个可能的圆角里选哪一个。
    //
    //   · 支持直线、圆弧、圆（圆不会被修剪）；其余类型返回 false
    //   · radius = 0：只支持两条直线，两条线延伸 / 修剪到交点，不生成圆弧
    //   · 两条平行直线、圆角半径过大找不到合适的圆角、同一个对象：返回 false，out 不变
    bool ComputeFillet(const Entity& a, const Math::Point3& pickA,
                       const Entity& b, const Math::Point3& pickB,
                       double radius, FilletResult& out);
}
