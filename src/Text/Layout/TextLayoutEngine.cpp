#include "TextLayoutEngine.h"
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
            double lineWidth = ComputeLineWidth(line, font, widthFactor);

            double cursorX = 0.0;

            if (align == HAlign::Center)
                cursorX = -lineWidth * 0.5;
            else if (align == HAlign::Right)
                cursorX = -lineWidth;

            for (char c : line)
            {
                uint32_t codepoint = static_cast<uint8_t>(c);

                Glyph glyph = font->GetGlyph(codepoint);

                GlyphInstance instance;
                instance.m_glyph = new Glyph(glyph); // ⚠️ 简化（实际建议用 cache pointer）
                instance.m_scale = height;
                instance.m_rotation = rotation;

                // 基础位置（未旋转）
                instance.m_position = Math::Point3(cursorX, cursorY, 0.0);

                result.m_glyphs.push_back(instance);

                cursorX += font->GetAdvance(codepoint) * widthFactor * height;
            }

            cursorY -= lineHeight;
        }

        return result;
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
