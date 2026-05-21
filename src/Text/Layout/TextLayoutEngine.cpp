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

        // SHX 字形坐标在 SHX 单位空间 (0..fontHeight)
        // scale 将其转换为世界坐标: worldCoord = shxCoord * scale
        double fontH     = font->GetHeight();
        double norm      = (fontH > 0.0) ? 1.0 / fontH : 1.0;
        double scale     = height * norm;      // SHX单位 → 世界单位
        double lineHeight = height;            // 一行占用的世界高度

        double cursorY = 0.0;

        for (const auto& line : lines)
        {
            auto codepoints = DecodeLine(line);

            double lineWidth = 0.0;
            for (auto cp : codepoints)
                lineWidth += font->GetAdvance(cp) * scale * widthFactor;

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
                instance.m_scale = scale;
                instance.m_rotation = rotation;
                instance.m_position = Math::Point3(cursorX, cursorY, 0.0);

                result.m_glyphs.push_back(instance);

                cursorX += font->GetAdvance(cp) * scale * widthFactor;
            }

            cursorY -= lineHeight;
        }

        return result;
    }

    std::vector<uint32_t> TextLayoutEngine::DecodeLine(const std::string& line)
    {
        std::vector<uint32_t> cps;

        const size_t n = line.size();
        size_t i = 0;

        // 当前位置解码一个 UTF-8 字符,返回码点并前进 i
        auto decodeUtf8 = [&]() -> uint32_t
        {
            const unsigned char c = (unsigned char)line[i];
            uint32_t cp; int len;
            if      (c < 0x80)        { cp = c;          len = 1; }
            else if ((c >> 5) == 0x6) { cp = c & 0x1F;   len = 2; }
            else if ((c >> 4) == 0xE) { cp = c & 0x0F;   len = 3; }
            else if ((c >> 3) == 0x1E){ cp = c & 0x07;   len = 4; }
            else                      { i += 1;          return 0xFFFD; }

            if (i + len > n) { i = n; return 0xFFFD; }
            for (int k = 1; k < len; ++k)
            {
                unsigned char cc = (unsigned char)line[i + k];
                if ((cc >> 6) != 0x2) { i += 1; return 0xFFFD; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            i += len;
            return cp;
        };

        while (i < n)
        {
            // AutoCAD 控制码: %%nnn / %%c / %%d / %%p / %%%
            if (line[i] == '%' && i + 1 < n && line[i + 1] == '%')
            {
                size_t j = i + 2;
                if (j < n)
                {
                    char c = line[j];

                    // %%nnn —— 最多三位十进制 → 字体内 shape 编号
                    if (c >= '0' && c <= '9')
                    {
                        uint32_t val = 0; int digits = 0;
                        while (j < n && digits < 3 && line[j] >= '0' && line[j] <= '9')
                        {
                            val = val * 10 + (uint32_t)(line[j] - '0');
                            ++j; ++digits;
                        }
                        cps.push_back(kRawShapeFlag | val);
                        i = j;
                        continue;
                    }

                    // %%d 度 / %%p 公差 / %%c 直径 → 对应 Unicode 符号编号
                    // (unifont 的 shape 编号即 Unicode 码点; raw flag 跳过 GBK 转码)
                    switch (c)
                    {
                    case 'd': case 'D': cps.push_back(kRawShapeFlag | 0x00B0u); i = j + 1; continue; // °
                    case 'p': case 'P': cps.push_back(kRawShapeFlag | 0x00B1u); i = j + 1; continue; // ±
                    case 'c': case 'C': cps.push_back(kRawShapeFlag | 0x2205u); i = j + 1; continue; // ∅
                    case '%':           cps.push_back((uint32_t)'%');          i = j + 1; continue;
                    default: break;
                    }
                }
                // 未知转义:原样输出一个 '%'
                cps.push_back((uint32_t)'%');
                i += 1;
                continue;
            }

            uint32_t cp = decodeUtf8();
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

        double fontH = font->GetHeight();
        double norm  = (fontH > 0.0) ? 1.0 / fontH : 1.0;
        double scale = height * norm;

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
            double advance = font->GetAdvance(codepoint) * scale * widthFactor;

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
