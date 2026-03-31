// App/Preview/PreviewPrimitive.hpp
#pragma once
#include <DirectXMath.h>
#include <vector>

namespace MiniCAD
{
    enum class PreviewPrimitiveType
    {
        LineList,   // 直线、折线、矩形、多边形
        LineStrip,  // 连续折线
    };

    struct PreviewPrimitive
    {
        PreviewPrimitiveType           Type = PreviewPrimitiveType::LineList;
        std::vector<DirectX::XMFLOAT3> Points;
        DirectX::XMFLOAT4              Color = { 0.5f, 0.5f, 0.5f, 1.f };
    };
}
