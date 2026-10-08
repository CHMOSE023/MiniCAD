#pragma once
#include "Core/Math/Color4.hpp"

namespace MiniCAD
{
    class Entity;
    class Overlay;

    // 把任意实体的轮廓绘制到 Overlay(幽灵预览用)。
    // 覆盖常用实体类型;未识别类型回退为包围盒矩形。
    void DrawEntityToOverlay(Overlay& overlay, const Entity& entity, const Math::Color4& color);
}
