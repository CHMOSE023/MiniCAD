#pragma once 
#include "../Math/Point3.hpp"
#include "../Math/Vec3.hpp"
#include <algorithm>
#include <limits>

namespace MiniCAD
{
    struct AABB
    {
        Math::Point3 Min;
        Math::Point3 Max;

        AABB() = default; 

        constexpr AABB(const Math::Point3& min, const Math::Point3& max) : Min(min), Max(max)
        {
        }
         
		static AABB Empty()  // 返回一个空的包围盒（Min 在正无穷，Max 在负无穷）
        {
            constexpr double inf = std::numeric_limits<double>::infinity();
            return { {inf,inf,inf },   {-inf,-inf,-inf } };
        } 

		Math::Point3 Center() const   // 盒心坐标
        {
            return {
                (Min.x + Max.x) * 0.5,
                (Min.y + Max.y) * 0.5,
                (Min.z + Max.z) * 0.5
            };
        }
         
		Math::Vec3 Extents() const   // 盒子半尺寸（从中心到任一面）
        {
            return {
                (Max.x - Min.x) * 0.5,
                (Max.y - Min.y) * 0.5,
                (Max.z - Min.z) * 0.5
            };
        }

		void Expand(const Math::Point3& p) // 扩展包围盒以包含点 p
        {
            Min.x = std::min(Min.x, p.x);
            Min.y = std::min(Min.y, p.y);
            Min.z = std::min(Min.z, p.z);

            Max.x = std::max(Max.x, p.x);
            Max.y = std::max(Max.y, p.y);
            Max.z = std::max(Max.z, p.z);
        }

		void Expand(const AABB& other)  // 扩展包围盒以包含另一个包围盒
        {
            Expand(other.Min);
            Expand(other.Max);
        }
         
		bool Contains(const Math::Point3& p) const // 判断点 p 是否在包围盒内（包含边界）
        {
            return
                p.x >= Min.x && p.x <= Max.x &&
                p.y >= Min.y && p.y <= Max.y &&
                p.z >= Min.z && p.z <= Max.z;
        }
         
		bool Intersects(const AABB& other) const   // 判断两个包围盒是否相交（包含边界）
        {
            return   
                Min.x <= other.Max.x && Max.x >= other.Min.x &&
                Min.y <= other.Max.y && Max.y >= other.Min.y &&
                Min.z <= other.Max.z && Max.z >= other.Min.z;
        }
         
		Math::Vec3 Size() const  // 包围盒尺寸（宽、高、深）
        {
            return { Max.x - Min.x,   Max.y - Min.y,    Max.z - Min.z };
        }
    };
}