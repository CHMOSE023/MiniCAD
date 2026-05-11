#pragma once
#include "Core/Math/Mat4.hpp"

namespace MiniCAD
{
    struct Float4x4
    {
        float m[16] = {};

        static Float4x4 FromMat4(const Math::Mat4& mat)
        {
            Float4x4 r;
            for (int i = 0; i < 16; ++i)
                r.m[i] = static_cast<float>(mat.m[i]);
            return r;
        }
    };
}