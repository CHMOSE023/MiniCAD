#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <memory>

namespace MiniCAD
{
    // 倒角（Chamfer）的计算结果。first / second 是修剪或延伸后的两条直线（保持原 ID 与属性），
    // line 是新生成的倒角线（ID 为 0，由调用方分配；属性取自第一条线），两个距离都为 0 时为空。
    struct ChamferResult
    {
        std::unique_ptr<LineEntity> first;
        std::unique_ptr<LineEntity> second;
        std::unique_ptr<LineEntity> line;
    };

    // 计算两条直线之间的倒角：从两条线（延长线）的交点出发，沿点选的一侧分别量出距离 d1（第一条线）、
    // d2（第二条线），两条线修剪 / 延伸到这两个点，再连一条倒角线。
    //   · pickA / pickB 是点选位置，决定保留哪一侧
    //   · d1、d2 必须同时 > 0（生成倒角线）或同时为 0（只把两条线修剪 / 延伸到交点）
    //   · 只支持直线；两条平行线、同一个对象、距离超过保留一侧的长度、距离为负：返回 false，out 不变
    bool ComputeChamfer(const Entity& a, const Math::Point3& pickA,
                        const Entity& b, const Math::Point3& pickB,
                        double d1, double d2, ChamferResult& out);
}
