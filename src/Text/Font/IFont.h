#pragma once
#include <cstdint>
#include "../Glyph/Glyph.h"

namespace MiniCAD
{
    class IFont
    {
    public:
        virtual ~IFont() = default;

        // 获取 glyph（统一输出线段几何）
        virtual Glyph GetGlyph(uint32_t codepoint) = 0;

        // 字符前进宽度（排版用）
        virtual double GetAdvance(uint32_t codepoint) = 0;

        // 字体高度（建议基准单位）
        virtual double GetHeight() const = 0;

        // 字体唯一ID（用于GlyphKey）
        virtual uint64_t GetFontId() const = 0;

        // 字体名称
        virtual const char* GetName() const = 0;

        virtual bool HasGlyph(uint32_t codepoint) = 0;   // 新增
    };
}
