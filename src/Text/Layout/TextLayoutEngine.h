#pragma once
#include <string>
#include <vector>

#include "../Font/IFont.h"
#include "../Glyph/Glyph.h"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    class TextLayoutEngine
    {
    public:
        enum class HAlign
        {
            Left,
            Center,
            Right
        };

        struct GlyphInstance
        {
            const Glyph* m_glyph = nullptr;

            Math::Point3 m_position;
            double       m_rotation = 0.0;
            double       m_scale = 1.0;
        };

        struct LayoutResult
        {
            std::vector<GlyphInstance> m_glyphs;
        };

    public:
        LayoutResult Layout(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double rotation,
            double boxWidth,
            HAlign align = HAlign::Left);

    private:
        void BreakLines(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double boxWidth,
            std::vector<std::string>& outLines);

        double ComputeLineWidth(const std::string& line,
            IFont* font,
            double widthFactor);
    };
}
