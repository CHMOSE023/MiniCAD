#pragma once
#include "Core/Object/Object.hpp" 
#include "Core/Math/Point3.hpp"
#include <cstdint>

namespace MiniCAD
{
	// 捕捉结果
    struct SnapResult
    {
        enum class Type : uint8_t
        {
            None,
            Endpoint,
            Midpoint,
            Nearest,
            Quadrant,        // 象限点
            Intersection,    // 两曲线交点
            Perpendicular,   // 自基点向曲线的垂足
            Grid,
        };
        Type              SnapType = Type::None;
        Math::Point3      WorldPos = {};
        Object::ObjectID  SourceID = Object::InvalidID;
        Object::ObjectID  SourceID2 = Object::InvalidID;   // 交点捕捉的另一条曲线

        bool IsValid() const { return SnapType != Type::None; }
    };
}
