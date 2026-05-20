#pragma once
#include <cstdint>

namespace MiniCAD 
{
    struct GlyphKey
    {
        uint64_t FontId = 0;     // SHX/TTF唯一ID
        uint32_t Codepoint = 0;  // Unicode 或 SHX编码

        bool operator==(const GlyphKey& other) const
        {
            return FontId == other.FontId && Codepoint == other.Codepoint;
        }
    };

    template<>
    struct Hash< GlyphKey>
    {
        size_t operator()(const GlyphKey& k) const
        {
            return (std::hash<uint64_t>()(k.FontId) << 1) ^ std::hash<uint32_t>()(k.Codepoint);
        }
    };
}