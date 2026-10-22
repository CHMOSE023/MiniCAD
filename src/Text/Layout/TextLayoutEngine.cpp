#include "TextLayoutEngine.h"
#include <cmath>

namespace MiniCAD
{
    namespace
    {
        // 自 s[i] 解码一个完整 UTF-8 码点并前进 i;非法序列按单字节跳过返回 U+FFFD。
        // 与 DecodeLine 内部解码逻辑一致,供折行/量宽按「字符」而非「字节」推进。
        uint32_t DecodeOneUtf8(const std::string& s, size_t& i)
        {
            const size_t n = s.size();
            const unsigned char c = (unsigned char)s[i];
            uint32_t cp; int len;
            if      (c < 0x80)        { cp = c;        len = 1; }
            else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
            else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
            else if ((c >> 3) == 0x1E){ cp = c & 0x07; len = 4; }
            else                      { i += 1;        return 0xFFFD; }

            if (i + len > n) { i = n; return 0xFFFD; }
            for (int k = 1; k < len; ++k)
            {
                unsigned char cc = (unsigned char)s[i + k];
                if ((cc >> 6) != 0x2) { i += 1; return 0xFFFD; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            i += len;
            return cp;
        }
    }
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
                GlyphInstance instance;
                instance.m_glyph = &font->GetGlyph(cp);
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

        // 按 UTF-8 码点(而非字节)推进:一个汉字只量一次宽度,
        // 且折行点不会落在多字节序列中间。
        //
        // ── 折行规则（与 AutoCAD 一致）────────────────────────────────
        // AutoCAD 的 MTEXT 只在**词边界**折行：遇到放不下时，回退到本行内
        // 最后一个可折位置（空格 / 连字符之后，或汉字之间）再断行。
        // **不含空格的超长 token 不会被拆开**，而是溢出栏宽。
        //
        // 早期实现是"逐字符折行"（放不下就在当前字符前断），于是
        // 「420114001037GB10066」这种无空格的编号被强行拆成 4 行 ——
        // 用户报的「文字被强行换行」就是这个。
        // 实测（铂尔曼核位红线图改.dwg，MTEXT 2992）：
        //   41(栏宽)=53.926  字高=15  文本=18 字符无空格  排版总宽=166.364
        //   逐字符折行 → 4 行（错）；词边界折行 → 1 行（与 CAD 一致）
        const size_t n = text.size();

        // 在 s 里找最后一个可折位置（返回断点字节下标；0 = 无可折点）
        //   可折点：空格、制表符、连字符之**后**；或两个 CJK/全角字符之**间**
        auto lastBreakPos = [](const std::string& s) -> size_t
        {
            // 从后往前扫，找第一个"其**后**可以断"的位置
            size_t pos = s.size();
            while (pos > 0)
            {
                // 找 s 中最后一个码点的起点
                size_t q = pos - 1;
                while (q > 0 && (static_cast<unsigned char>(s[q]) & 0xC0) == 0x80) --q;

                const unsigned char c0 = static_cast<unsigned char>(s[q]);
                // 空白 / 连字符：断在它**之后**（含该字符）
                if (c0 == ' ' || c0 == '\t' || c0 == '-')
                    return pos;

                // CJK / 全角：可与前一个字符之间断行（中文无空格也能折）
                uint32_t cp = 0;
                if (c0 < 0x80) cp = c0;
                else if ((c0 & 0xE0) == 0xC0) cp = c0 & 0x1F;
                else if ((c0 & 0xF0) == 0xE0)
                    cp = ((c0 & 0x0F) << 12) |
                         ((q + 1 < s.size() ? (static_cast<unsigned char>(s[q + 1]) & 0x3F) : 0) << 6) |
                          (q + 2 < s.size() ? (static_cast<unsigned char>(s[q + 2]) & 0x3F) : 0);
                else if ((c0 & 0xF8) == 0xF0)
                    cp = ((c0 & 0x07) << 18) |
                         ((q + 1 < s.size() ? (static_cast<unsigned char>(s[q + 1]) & 0x3F) : 0) << 12) |
                         ((q + 2 < s.size() ? (static_cast<unsigned char>(s[q + 2]) & 0x3F) : 0) << 6) |
                          (q + 3 < s.size() ? (static_cast<unsigned char>(s[q + 3]) & 0x3F) : 0);

                const bool cjk = (cp >= 0x2E80 && cp <= 0x9FFF) ||     // CJK 部首 / 汉字
                                 (cp >= 0xF900 && cp <= 0xFAFF) ||     // CJK 兼容
                                 (cp >= 0xFF00 && cp <= 0xFF60);       // 全角
                if (cjk && q > 0)
                    return q;   // 断在该全角字符**之前**

                pos = q;
            }
            return 0;
        };

        size_t i = 0;
        while (i < n)
        {
            if (text[i] == '\n')
            {
                outLines.push_back(current);
                current.clear();
                currentWidth = 0.0;
                ++i;
                continue;
            }

            const size_t start = i;
            uint32_t codepoint = DecodeOneUtf8(text, i);
            double advance = font->GetAdvance(codepoint) * scale * widthFactor;

            // 放不下：先尝试**词边界**折行；找不到可折点就整体溢出（不拆 token）
            if (boxWidth > 0.0 && !current.empty() && currentWidth + advance > boxWidth)
            {
                const size_t brk = lastBreakPos(current);
                if (brk > 0 && brk < current.size())
                {
                    // 本行断在 brk 处，余下部分移到下一行
                    std::string rest = current.substr(brk);
                    current.resize(brk);
                    // 去掉行尾空白（AutoCAD 不在行首/行尾保留折行空白）
                    while (!current.empty() && (current.back() == ' ' || current.back() == '\t'))
                        current.pop_back();
                    outLines.push_back(current);

                    current = rest;
                    while (!current.empty() && (current.front() == ' ' || current.front() == '\t'))
                        current.erase(0, 1);
                    currentWidth = ComputeLineWidth(current, font, widthFactor) * scale;
                }
                // else：整行只有一个不可折的 token ⇒ 不拆，随它溢出栏宽
            }

            current.append(text, start, i - start);
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

        size_t i = 0;
        while (i < line.size())
        {
            uint32_t codepoint = DecodeOneUtf8(line, i);
            width += font->GetAdvance(codepoint) * widthFactor;
        }

        return width;
    }
}
