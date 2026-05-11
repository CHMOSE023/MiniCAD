#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Constants.hpp"
#include "AABB.hpp"
#include <cmath>

namespace MiniCAD
{
    struct Circle
    {
        Math::Point3 Center;
        double       Radius = 0.0;

        Circle() = default;

        constexpr Circle(const Math::Point3& center, double radius)
            : Center(center)
            , Radius(radius)
        {
        }

        // 圆是否有效（半径大于容差）
        bool IsValid() const { return Radius > Math::LengthEPS; }

        // 圆上角度 angle（弧度）对应的点坐标
        Math::Point3 PointAt(double angle) const
        {
            return 
            {
                Center.x + std::cos(angle) * Radius,
                Center.y + std::sin(angle) * Radius,
                Center.z
            };
        }

        // 四个象限点：East / North / West / South
        Math::Point3 QuadrantE() const { return PointAt(0.0); }
        Math::Point3 QuadrantN() const { return PointAt(Math::PI * 0.5); }
        Math::Point3 QuadrantW() const { return PointAt(Math::PI); }
        Math::Point3 QuadrantS() const { return PointAt(Math::PI * 1.5); }

        // 点 p 在圆上的最近点
        Math::Point3 ClosestPoint(const Math::Point3& p) const
        {
            double dx = p.x - Center.x;
            double dy = p.y - Center.y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < Math::LengthEPS)
                return QuadrantE();   // 退化：p 在圆心，任意返回 East 点
            return 
            {
                Center.x + dx / dist * Radius,
                Center.y + dy / dist * Radius,
                Center.z
            };
        }

        // 点 p 到圆周的距离（非圆内距离，是到弧线的距离）
        double DistanceToPoint(const Math::Point3& p) const
        {
            double dx = p.x - Center.x;
            double dy = p.y - Center.y;
            return std::abs(std::sqrt(dx * dx + dy * dy) - Radius);
        }

        // 轴对齐包围盒
        AABB GetBounds() const
        {
            return 
            {
                { Center.x - Radius, Center.y - Radius, Center.z },
                { Center.x + Radius, Center.y + Radius, Center.z }
            };
        }
    };
}
