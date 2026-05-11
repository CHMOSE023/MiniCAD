#pragma once 
#include "../Math/Point3.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Constants.hpp"
#include "../Math/MathUtils.hpp"
#include "AABB.hpp" 
#include <algorithm>
#include <cmath> 
namespace MiniCAD
{
    struct Line
    {
        Math::Point3 Start;
        Math::Point3 End;
         
        Line() = default;

        constexpr Line(const Math::Point3& start, const Math::Point3& end)
            : Start(start)
            , End(end) 
        {
        } 
		
        Math::Vec3 Vector() const     // 从 Start 指向 End 的向量
        {
            return End - Start;
        }
         
		Math::Vec3 Direction() const // 归一化的方向向量
        {
            return Vector().Normalized();
        }
         
		double Length() const       // 线段长度（如果是无限直线则没有意义）
        {  
            return Vector().Length();
        }
         
		double LengthSq() const    // 线段长度的平方（如果是无限直线则没有意义）
        {
            return Vector().LengthSq();
        }
         
		Math::Point3 Midpoint() const  // 线段中点坐标（如果是无限直线则没有意义）
        {
            return {
                (Start.x + End.x) * 0.5,
                (Start.y + End.y) * 0.5,
                (Start.z + End.z) * 0.5
            };
        }
         
        Math::Point3 PointAt(double t) const // P(t) = Start + t*(End-Start) 参数 t 对应的点坐标
        {
            return Start + Vector() * t;
        }
         
		double ProjectParam(const Math::Point3& p) const  // 计算点 p 在直线上的投影参数 t，使得 PointAt(t) 是 p 在直线上的投影点
        {
            Math::Vec3 v = Vector();

            double lenSq = v.LengthSq();

            if (lenSq < Math::LengthEPS)
                return 0.0;

            double t = Math::Dot(p - Start, v) / lenSq;
  
            return t;
        }
         
		Math::Point3 ClosestPoint(const Math::Point3& p) const  // 计算点 p 在直线上的最近点坐标（如果是线段则限制在 [Start, End] 上）
        {
            return PointAt(ProjectParam(p));
        }
         
		double DistanceToPoint(const  Math::Point3& p) const    // 计算点 p 到直线的距离（如果是线段则限制在 [Start, End] 上）
        {
            return (p - ClosestPoint(p)).Length();
        } 

		AABB GetBounds() const  // 计算线段的轴对齐包围盒 
        {
            return {
                {
                    std::min(Start.x, End.x),
                    std::min(Start.y, End.y),
                    std::min(Start.z, End.z)
                },
                {
                    std::max(Start.x, End.x),
                    std::max(Start.y, End.y),
                    std::max(Start.z, End.z)
                }
            };
        } 

		bool IsValid() const   // 检查线段是否有效 
        {
            return Vector().LengthSq() > Math::LengthEPS;
        }
    };
}
