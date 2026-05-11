#pragma once
#include "Core/Math/PackedTypes.hpp"

namespace MiniCAD 
{
    struct Vertex_P3_C4
    {
        Math::Float3 pos;
        Math::Float4 color;
    };

    struct Vertex_P3
    {
        Math::Float3 pos;
    };

    struct Vertex_P3_C4_UV
    {
        Math::Float3 pos;
        Math::Float4 color;
        Math::Float2 uv;
    };
}
