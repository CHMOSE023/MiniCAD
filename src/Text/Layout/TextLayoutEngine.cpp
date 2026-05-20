#include "TextLayoutEngine.h"
#include "Text/Font/Utf8Iterator.h"
#include <cmath>

namespace MiniCAD
{
    TextLayoutEngine::LayoutResult
        TextLayoutEngine::Layout(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double rotation,
            double boxWidth,
            HAlign align)
    {
        LayoutResult result;

        if (!font)
            return result;

        std::vector<std::string> lines;
        BreakLines(text, font, height, widthFactor, boxWidth, lines);

        double cursorY = 0.0;

        double lineHeight = font->GetHeight() * height;

        for (const auto& line : lines)
        {
            auto codepoints = DecodeLine(line);

            double lineWidth = 0.0;
            for (auto cp : codepoints)
                lineWidth += font->GetAdvance(cp) * widthFactor * height;

            double cursorX = 0.0;

            if (align == HAlign::Center)
                cursorX = -lineWidth * 0.5;
            else if (align == HAlign::Right)
                cursorX = -lineWidth;

            for (auto cp : codepoints)
            {
                Glyph glyph = font->GetGlyph(cp);

                GlyphInstance instance;
                instance.m_glyph = std::move(glyph);
                instance.m_scale = height;
                instance.m_rotation = rotation;
                instance.m_position = Math::Point3(cursorX, cursorY, 0.0);

                result.m_glyphs.push_back(instance);

                cursorX += font->GetAdvance(cp) * widthFactor * height;
            }

            cursorY -= lineHeight;
        }

        return result;
    }

    std::vector<uint32_t> TextLayoutEngine::DecodeLine(const std::string& line)
    {
        Utf8Iterator it(line); 

        std::vector<uint32_t> cps;
        while (it.HasNext())
        {
            uint32_t cp = it.Next();
            if (cp != 0)
                cps.push_back(cp);
        }
        return cps;
    }

    void TextLayoutEngine::BreakLines(const std::string& text,
        IFont* font,
        double height,
        double widthFactor,
        double boxWidth,
        std::vector<std::string>& outLines)
    {
        std::string current;

        double currentWidth = 0.0;

        for (char c : text)
        {
            if (c == '\n')
            {
                outLines.push_back(current);
                current.clear();
                currentWidth = 0.0;
                continue;
            }

            uint32_t codepoint = static_cast<uint8_t>(c);
            double advance = font->GetAdvance(codepoint)
                * widthFactor
                * height;

            if (boxWidth > 0.0 && currentWidth + advance > boxWidth)
            {
                outLines.push_back(current);
                current.clear();
                currentWidth = 0.0;
            }

            current.push_back(c);
            currentWidth += advance;
        }

        if (!current.empty())
            outLines.push_back(current);
    }

    double TextLayoutEngine::ComputeLineWidth(const std::string& line,
        IFont* font,
        double widthFactor)
    {
        double width = 0.0;

        for (char c : line)
        {
            uint32_t codepoint = static_cast<uint8_t>(c);
            width += font->GetAdvance(codepoint) * widthFactor;
        }

        return width;
    }
}
