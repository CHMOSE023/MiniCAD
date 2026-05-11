#pragma once 
#include "../Math/Point3.hpp"
#include "AABB.hpp"
 
namespace MiniCAD
{
    struct Point
    {
        Math::Point3 Position; 

        Point() : Position(0.f, 0.f, 0.f) {}
        Point(const Math::Point3 pos) : Position(pos) {} 
         
        float X() const { return Position.x; }
        float Y() const { return Position.y; }
        float Z() const { return Position.z; } 

        AABB GetBounds() const
        {
            AABB box;
            box.Min = Position;
            box.Max = Position;
            return box;
        }
    };
}
