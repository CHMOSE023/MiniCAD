#pragma once 
#include "../Math/Point3.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Constants.hpp"   
 
namespace MiniCAD
{ 
    struct XLine // 无限直线
    {
        Math::Point3 Origin;
        Math::Vec3   Direction;

        XLine() = default;

        XLine(const Math::Point3& origin, const Math::Vec3& direction) : Origin(origin), Direction(direction) {}

        // 由两个点构造无限直线
        XLine(const Math::Point3& p0, const Math::Point3& p1) : Origin(p0), Direction(p1 - p0) {}

		double DirectionLength() const     // 方向向量的长度（ 
        {
            return Direction.Length();
        }
         
		double DirectionLengthSq() const  // 方向向量长度的平方 
        {
            return Direction.LengthSq();
        }
         
		bool IsValid() const              // 方向向量长度必须大于一个小的容差值
        { 
            return DirectionLengthSq() > Math::LengthEPS * Math::LengthEPS;
        }
         
		Math::Vec3 UnitDirection() const       // 归一化的方向向量（如果无效则返回零向量）
        {
            return Direction.Normalized();
        }
         
		Point3 PointAt(double t) const   // P(t) = Origin + t * Direction 参数 t 对应的点坐标
        {
            return Origin + Direction * t;
        }
         
		double ProjectParam(const Math::Point3& p) const   // 计算点 p 在直线上的投影参数 t，使得 PointAt(t) 是 p 在直线上的投影点
        {
            double lenSq = Direction.LengthSq();

            if (lenSq < Math::LengthEPS * Math::LengthEPS)
            {
                return 0.0;
            }

            return Math::Dot(p - Origin, Direction) / lenSq;
        }
         
		Math::Point3 ClosestPoint(const Math::Point3& p) const  // 计算点 p 在直线上的最近点坐标
        {
            return PointAt(ProjectParam(p));
        }
         
		double DistanceToPoint(const Math::Point3& p) const // 计算点 p 到直线的距离
        {
            return (p - ClosestPoint(p)).Length();
        }
    };
}
