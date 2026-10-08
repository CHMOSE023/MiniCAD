#pragma once
#include <functional>
#include <cstdint>

namespace MiniCAD
{
    // 字形数据（归一化到字高=1的坐标系，供纹理路径使用）
    struct GlyphInfo
    {
        float X0, Y0, X1, Y1; // 字形边界（单位：字高）
        float U0, V0, U1, V1; // 纹理 UV
        float AdvanceX;        // 水平步进（单位：字高）
    };

    // 纹理字形查询回调（由应用层注入：WebFontAtlas 路径）
    using GlyphProvider = std::function<bool(uint32_t cp, GlyphInfo& out, float& fallbackAdvance)>;
}
