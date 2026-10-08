#pragma once
#include "Core/Types/Vec2.hpp"
#include <algorithm>

namespace MiniGUI
{
    // 轴对齐矩形，用左上角 min、右下角 max 表示（y 轴向下）
    struct Rect
    {
        Vec2 min;
        Vec2 max;

        constexpr Rect() = default;
        constexpr Rect(Vec2 mn, Vec2 mx) : min(mn), max(mx) {}
        constexpr Rect(float x0, float y0, float x1, float y1) : min(x0, y0), max(x1, y1) {}

        static constexpr Rect FromXYWH(float x, float y, float w, float h) { return { x, y, x + w, y + h }; }

        constexpr float Width()  const { return max.x - min.x; }
        constexpr float Height() const { return max.y - min.y; }
        constexpr Vec2  Size()   const { return { Width(), Height() }; }
        constexpr Vec2  Center() const { return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f }; }
        constexpr bool  IsEmpty() const { return max.x <= min.x || max.y <= min.y; }

        constexpr bool Contains(Vec2 p) const
        {
            return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y;
        }

        constexpr bool Overlaps(const Rect& r) const
        {
            return r.min.x < max.x && r.max.x > min.x && r.min.y < max.y && r.max.y > min.y;
        }

        // 向内收缩（amount 为负时向外扩张）
        constexpr Rect Deflated(float amount) const
        {
            return { min.x + amount, min.y + amount, max.x - amount, max.y - amount };
        }

        Rect Intersected(const Rect& r) const
        {
            Rect o{ std::max(min.x, r.min.x), std::max(min.y, r.min.y),
                    std::min(max.x, r.max.x), std::min(max.y, r.max.y) };
            // 不相交时退化成零面积矩形，避免出现 max < min
            o.max.x = std::max(o.max.x, o.min.x);
            o.max.y = std::max(o.max.y, o.min.y);
            return o;
        }

        constexpr bool operator==(const Rect& r) const { return min == r.min && max == r.max; }
        constexpr bool operator!=(const Rect& r) const { return !(*this == r); }
    };

    // 整数像素矩形，用于纹理区域
    struct RectI
    {
        int x      = 0;
        int y      = 0;
        int width  = 0;
        int height = 0;
    };
}
