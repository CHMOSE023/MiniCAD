#pragma once
#include "Core/Types/Vec2.hpp"

namespace MiniGUI
{
    class Node;

    // 布局引擎接口：两阶段布局
    //   Measure：自下而上计算容器期望尺寸（含内边距）
    //   Arrange：自上而下，根据容器已确定的尺寸设置每个子节点的 bounds
    // 自研 Flexbox 满足不了需求时，可以换成 Yoga 等实现
    class ILayoutEngine
    {
    public:
        virtual ~ILayoutEngine() = default;

        // available 为可用空间（可能是无穷大），返回容器内容所需尺寸
        virtual Vec2 MeasureChildren(Node& container, Vec2 available) = 0;

        // pixelScale 用于把子节点边缘对齐到物理像素，避免模糊
        virtual void ArrangeChildren(Node& container, float pixelScale) = 0;
    };
}
