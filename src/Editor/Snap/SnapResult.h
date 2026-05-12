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
            Quadrant,   // 象限点
            Grid,
        };
        Type              SnapType = Type::None;
        Math::Point3      WorldPos = {};
        Object::ObjectID  SourceID = Object::InvalidID;

        bool IsValid() const { return SnapType != Type::None; }
    };
}
