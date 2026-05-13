#pragma once 
#include "Point2.hpp" 
#include "Constants.hpp"
#include "Box2.hpp"

namespace MiniCAD::Math
{
    struct Circle2
    {
        Point2 Center;
        double Radius = 0.0;

        Circle2() = default;

        Circle2(const Point2& center, double radius)
            : Center(center)
            , Radius(radius)
        {}

        // 半径平方（避免 sqrt）
        double RadiusSq() const
        {
            return Radius * Radius;
        }

        // 面积（可选）
        double Area() const
        {
            return PI * Radius * Radius;
        }

        // 是否有效
        bool IsValid() const
        {
            return Radius >= 0.0;
        }

        // 包含点
        bool Contains(const Point2& p) const
        {
            double dx = p.x - Center.x;
            double dy = p.y - Center.y;
            return (dx * dx + dy * dy) <= Radius * Radius;
        }

        // 与点距离（带符号：<0 表示在圆内）
        double SignedDistance(const Point2& p) const
        {
            double dx = p.x - Center.x;
            double dy = p.y - Center.y;
            return std::sqrt(dx * dx + dy * dy) - Radius;
        }

        // 轴对齐包围盒（用于 selection / culling）
        Box2 Bounds() const
        {
            return Box2(
                { Center.x - Radius, Center.y - Radius },
                { Center.x + Radius, Center.y + Radius }
            );
        }

        // 扩展包围圆（包含点）
        void Expand(const Point2& p)
        {
            double dx = p.x - Center.x;
            double dy = p.y - Center.y;
            double d = std::sqrt(dx * dx + dy * dy);

            if (d > Radius)
                Radius = d;
        }

        // 扩展包围圆（合并点集）
        void Expand(const Point2* pts, size_t n)
        {
            for (size_t i = 0; i < n; ++i)
                Expand(pts[i]);
        }
    };
}