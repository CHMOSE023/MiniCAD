#pragma once
#include <algorithm>
#include "Point2.hpp"
 
namespace MiniCAD::Math
{
    struct Box2
    {
        Point2 Min;
        Point2 Max;

        Box2() = default;

        Box2(const Point2& minPt, const Point2& maxPt)
            : Min(minPt)
            , Max(maxPt)
        {
        }

        double Width() const
        {
            return Max.x - Min.x;
        }

        double Height() const
        {
            return Max.y - Min.y;
        }

        bool IsValid() const
        {
            return  Min.x <= Max.x &&  Min.y <= Max.y;
        }

        Point2 Center() const
        {
            return {
                (Min.x + Max.x) * 0.5,
                (Min.y + Max.y) * 0.5
            };
        }

        bool Contains(const Point2& p) const
        {
            return
                p.x >= Min.x &&
                p.x <= Max.x &&
                p.y >= Min.y &&
                p.y <= Max.y;
        }

        bool Intersects(const Box2& other) const
        {
            return !(
                Max.x < other.Min.x ||
                Min.x > other.Max.x ||
                Max.y < other.Min.y ||
                Min.y > other.Max.y
                );
        }

        void Expand(const Point2& p)
        {
            Min.x = std::min(Min.x, p.x);
            Min.y = std::min(Min.y, p.y);

            Max.x = std::max(Max.x, p.x);
            Max.y = std::max(Max.y, p.y);
        }
    };
}
