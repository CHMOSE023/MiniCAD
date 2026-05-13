#pragma once
#include "Core/Math/Point3.hpp"
#include <algorithm> 
#include "AABB.hpp"

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────────────────
    //  Rectangle  ── 矩形定义：由四个顶点 P1, P2, P3, P4 定义，按顺时针或逆时针顺序排列
    // ─────────────────────────────────────────────────────────────────────────
    struct Rectangle
    {
        Math::Point3 P1;    
        Math::Point3 P2;   
        Math::Point3 P3;   
        Math::Point3 P4;   

        // ── 构造 ─────────────────────────────────────────────────────────────
        Rectangle() = default;

        // 接受任意两个对角点，自动规范化
        Rectangle(const Math::Point3& a, const Math::Point3& b)
        {
            P1 = { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; // BottomLeft
            P3 = { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; // TopRight
            P2 = { P3.x, P1.y, P1.z }; // BottomRight
			P4 = { P1.x, P3.y, P3.z }; // TopLeft 
        }
          
        Rectangle(const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3, const Math::Point3& p4)
            : P1(p1), P2(p2), P3(p3), P4(p4)
        {
		}
          
        AABB GetBounds() const
        {
            auto Min = Math::Point3
            {
                std::min({ P1.x, P2.x, P3.x, P4.x }),
                std::min({ P1.y, P2.y, P3.y, P4.y }),
                std::min({ P1.z, P2.z, P3.z, P4.z })
			};

            auto Max = Math::Point3
            {
                std::max({ P1.x, P2.x, P3.x, P4.x }),
                std::max({ P1.y, P2.y, P3.y, P4.y }),
                std::max({ P1.z, P2.z, P3.z, P4.z })
			};

			return AABB(Min, Max);
        }
    };

} 
