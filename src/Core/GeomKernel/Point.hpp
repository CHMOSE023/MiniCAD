#pragma once 
#include <DirectXMath.h>
#include "AABB.hpp"
 
namespace MiniCAD
{
    struct Point
    {
        Point() : Position(0.f, 0.f, 0.f) {}

        Point(const DirectX::XMFLOAT3& pos)
            : Position(pos)
        {
        }

        DirectX::XMFLOAT3 Position;

        // ─────────────────────────────
        // 基础访问
        // ───────────────────────────── 
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
