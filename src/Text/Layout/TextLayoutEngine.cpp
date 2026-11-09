#include "TextLayoutEngine.h"
#include "Core/TextCodes.h"
#include <algorithm>
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

        // 下划线 / 上划线 / 删除线：开关跨行保持，每行在行尾收口、下一行从行首续上。
        // 纵向位置相对基线（× 字高），与 AutoCAD 一致；横向从开处的步进位置画到关处
        static constexpr double kDecorationY[3] = { -0.2, 1.2, 0.5 };
        bool   decorOn[3]     = { false, false, false };
        double decorStartX[3] = { 0.0, 0.0, 0.0 };
        auto closeDecoration = [&](int k, double x)
        {
            if (x > decorStartX[k])
                result.m_decorations.push_back({ decorStartX[k], x, cursorY + kDecorationY[k] * height });
        };

        for (const auto& line : lines)
        {
            auto codepoints = DecodeLine(line);

            double lineWidth = 0.0;
            for (auto cp : codepoints)
                if (!IsDecorationToggle(cp))
                    lineWidth += font->GetAdvance(cp) * scale * widthFactor;

            double cursorX = 0.0;

            if (align == HAlign::Center)
                cursorX = -lineWidth * 0.5;
            else if (align == HAlign::Right)
                cursorX = -lineWidth;

            for (double& x : decorStartX) x = cursorX;
            for (auto cp : codepoints)
            {
                if (IsDecorationToggle(cp))
                {
                    const int k = static_cast<int>(cp) - 1;
                    if (decorOn[k]) closeDecoration(k, cursorX);
                    else decorStartX[k] = cursorX;
                    decorOn[k] = !decorOn[k];
                    continue;
                }

                GlyphInstance instance;
                instance.m_glyph = &font->GetGlyph(cp);
                instance.m_scale = scale;
                instance.m_rotation = rotation;
                instance.m_widthFactor = widthFactor;
                instance.m_position = Math::Point3(cursorX, cursorY, 0.0);

                result.m_glyphs.push_back(instance);

                cursorX += font->GetAdvance(cp) * scale * widthFactor;
            }
            for (int k = 0; k < 3; ++k)
                if (decorOn[k]) closeDecoration(k, cursorX);

            cursorY -= lineHeight;
        }

        return result;
    }

    namespace
    {
        bool IsCjkBreakable(uint32_t cp)
        {
            return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFF60);
        }
        uint32_t GlyphCodeOf(uint32_t cp) { return cp == 0xA0 ? uint32_t(' ') : cp; }   // 不换行空格按空格画

        // 堆叠的竖向位置（AutoCAD 2020 炸开实测），以下半基线为 0、两半都用当前字高 h：
        // 上半基线 = (下半非空 ? h : 0) + h/3，横线在上半基线下 h/6；斜线分数上半基线 = h，上下两半左右错开
        struct StackGeometry
        {
            double top = 0.0;      // 上半基线
            double anchor = 0.0;   // \A1 居中时对准行中线的位置（横线处）
            double ascent = 0.0;   // 顶：上半为空时就是上半基线
        };
        StackGeometry StackGeometryOf(const MTextRun& run, double h)
        {
            StackGeometry g;
            g.top    = run.stackType == '#' ? h : (run.bottom.empty() ? 0.0 : h) + h / 3.0;
            g.anchor = g.top - h / 6.0;
            g.ascent = g.top + (run.text.empty() ? 0.0 : h);
            return g;
        }
    }

    TextLayoutEngine::LayoutResult TextLayoutEngine::LayoutRich(const MTextContent& content, const std::vector<RunStyle>& styles,
                                                                const RichOptions& o)
    {
        LayoutResult result;
        const auto& runs = content.runs;
        const double base = o.height > 0.0 ? o.height : 1.0;

        struct Item { uint32_t cp = 0; int run = -1; double adv = 0.0; bool stack = false; double shift = 0.0; };
        struct Line
        {
            size_t begin = 0, end = 0; int para = -1; double width = 0.0, maxH = 0.0, indent = 0.0;
            double ascent = 0.0, descent = 0.0;
        };
        std::vector<Item> items;
        std::vector<Line> lines;

        auto usable = [&](size_t r) { return r < styles.size() && styles[r].font; };
        auto scaleOf = [&](size_t r, double h)
        {
            const double fh = styles[r].font->GetHeight();
            return h / (fh > 0.0 ? fh : 1.0);
        };
        auto advanceOf = [&](size_t r, uint32_t cp, double h)
        {
            return styles[r].font->GetAdvance(GlyphCodeOf(cp)) * scaleOf(r, h) * styles[r].widthFactor * runs[r].tracking;
        };
        auto textWidth = [&](size_t r, const std::string& t, double h)
        {
            double w = 0.0;
            for (uint32_t cp : DecodeLine(t))
                if (!IsDecorationToggle(cp)) w += advanceOf(r, cp, h);
            return w;
        };
        auto isSpace = [](uint32_t cp) { return cp == ' ' || cp == '\t'; };

        // 一段（两个换段之间）的字符折成行：与 BreakLines 同样只在词边界、汉字之间折，放不下的长词溢出栏宽
        auto wrap = [&](size_t pb, size_t pe, int para)
        {
            const MTextRun* pr = para >= 0 ? &runs[para] : nullptr;
            const double indFirst = pr ? (pr->indentLeft + pr->indentFirst) * base : 0.0;
            const double indRest  = pr ? pr->indentLeft * base : 0.0;
            const double paraH    = pr ? pr->height * base : base;
            if (pb == pe)
            {
                lines.push_back({ pb, pe, para, 0.0, paraH, indFirst });
                return;
            }
            bool first = true;
            for (size_t start = pb; start < pe; first = false)
            {
                const double indent = first ? indFirst : indRest;
                const double avail  = o.boxWidth > 0.0 ? o.boxWidth - indent : 0.0;
                double w = 0.0;
                size_t end = start;
                for (; end < pe; ++end)
                {
                    if (avail > 0.0 && end > start && w + items[end].adv > avail)
                    {
                        size_t brk = 0;
                        for (size_t q = end; q > start && !brk; --q)
                        {
                            const Item& prev = items[q - 1];
                            if (!prev.stack && (isSpace(prev.cp) || prev.cp == '-')) brk = q;
                            else if (!items[q].stack && IsCjkBreakable(items[q].cp)) brk = q;
                        }
                        if (brk > start) { end = brk; break; }
                    }
                    w += items[end].adv;
                }
                Line line{ start, end, para, 0.0, 0.0, indent };
                size_t visible = end;
                if (end < pe)
                    while (visible > start && !items[visible - 1].stack && isSpace(items[visible - 1].cp)) --visible;
                for (size_t k = start; k < visible; ++k) line.width += items[k].adv;
                for (size_t k = start; k < end; ++k) line.maxH = std::max(line.maxH, runs[items[k].run].height * base);
                if (line.maxH <= 0.0) line.maxH = paraH;
                lines.push_back(line);
                start = end;
                if (end < pe)
                    while (start < pe && !items[start].stack && isSpace(items[start].cp)) ++start;
            }
        };

        size_t paraBegin = 0;
        int    paraRun   = -1;
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const MTextRun& run = runs[r];
            if (paraRun < 0) paraRun = static_cast<int>(r);
            if (run.kind == MTextRun::Kind::Break)
            {
                wrap(paraBegin, items.size(), paraRun);
                paraBegin = items.size();
                paraRun   = -1;
                continue;
            }
            if (!usable(r)) continue;
            const double h = run.height * base;
            if (run.kind == MTextRun::Kind::Stack)
            {
                const double topW = textWidth(r, run.text, h), botW = textWidth(r, run.bottom, h);
                items.push_back({ 0, static_cast<int>(r), run.stackType == '#' ? topW + botW : std::max(topW, botW), true });
                continue;
            }
            for (uint32_t cp : DecodeLine(run.text))
                if (!IsDecorationToggle(cp))
                    items.push_back({ cp, static_cast<int>(r), advanceOf(r, cp, h), false });
        }
        if (paraBegin < items.size() || paraRun >= 0 || lines.empty())
            wrap(paraBegin, items.size(), paraRun);

        // 字顶：SHX 按名义字高；TrueType 按字形的实际顶边（宋体的汉字约高出 16%），不低于名义字高
        auto topOf = [&](size_t r, uint32_t cp, double h)
        {
            const Glyph& gl = styles[r].font->GetGlyph(GlyphCodeOf(cp));
            return gl.Filled ? std::max(h, gl.MaxY * scaleOf(r, h)) : h;
        };
        auto textTop = [&](size_t r, const std::string& t, double h)
        {
            double top = 0.0;
            for (uint32_t cp : DecodeLine(t))
                if (!IsDecorationToggle(cp)) top = std::max(top, topOf(r, cp, h));
            return top;
        };

        // 各字相对行基线的竖向偏移（\A），以及每行的上升、下沉
        for (Line& line : lines)
        {
            bool hasStack = false;
            line.ascent  = line.begin == line.end ? line.maxH : 0.0;
            line.descent = 0.0;
            for (size_t k = line.begin; k < line.end; ++k)
            {
                Item&           it  = items[k];
                const MTextRun& run = runs[static_cast<size_t>(it.run)];
                const double    h   = run.height * base;
                if (it.stack)
                {
                    const StackGeometry g = StackGeometryOf(run, h);
                    it.shift = run.valign == 1 ? line.maxH * 0.5 - g.anchor : run.valign == 2 ? line.maxH - g.ascent : 0.0;
                    line.ascent  = std::max(line.ascent, it.shift + g.top + (run.text.empty() ? 0.0 : textTop(static_cast<size_t>(it.run), run.text, h)));
                    line.descent = hasStack ? std::max(line.descent, -it.shift) : -it.shift;
                    hasStack = true;
                }
                else
                {
                    it.shift = run.valign == 1 ? (line.maxH - h) * 0.5 : run.valign == 2 ? line.maxH - h : 0.0;
                    line.ascent = std::max(line.ascent, it.shift + topOf(static_cast<size_t>(it.run), it.cp, h));
                }
            }
            if (!hasStack) line.descent = line.maxH / 3.0;
        }

        double blockW = 0.0;
        for (const Line& line : lines) blockW = std::max(blockW, line.indent + line.width);
        result.m_width = blockW;
        if (o.boxWidth > 0.0) blockW = o.boxWidth;

        auto placeText = [&](size_t r, const std::string& t, double h, double x, double y)
        {
            const double s = scaleOf(r, h);
            for (uint32_t cp : DecodeLine(t))
            {
                if (IsDecorationToggle(cp)) continue;
                GlyphInstance g;
                g.m_glyph       = &styles[r].font->GetGlyph(GlyphCodeOf(cp));
                g.m_scale       = s;
                g.m_position    = Math::Point3(x, y, 0.0);
                g.m_widthFactor = styles[r].widthFactor;
                g.m_shear       = styles[r].shear;
                g.m_run         = static_cast<int>(r);
                result.m_glyphs.push_back(g);
                x += advanceOf(r, cp, h);
            }
        };

        const double spacing = o.lineSpacing > 0.0 ? o.lineSpacing : 1.0;
        double y = 0.0;
        for (size_t li = 0; li < lines.size(); ++li)
        {
            const Line& line = lines[li];
            if (li == 0)
            {
                const double ascent = o.exact ? base : line.ascent;
                y = base - ascent;
                result.m_height = ascent;
            }
            else
            {
                const Line&  prev  = lines[li - 1];
                const double pitch = MTextLinePitch(base, spacing, o.exact, prev.maxH, prev.descent, line.ascent);
                y -= pitch;
                result.m_height += pitch;
            }

            const int    paraAlign = line.para >= 0 ? runs[line.para].align : -1;
            const HAlign align = paraAlign == 1 ? HAlign::Center : paraAlign == 2 ? HAlign::Right : paraAlign == 0 ? HAlign::Left : o.justify;
            const double slack = blockW - line.indent - line.width;
            double x = line.indent + (align == HAlign::Center ? slack * 0.5 : align == HAlign::Right ? slack : 0.0);

            int lastDecor[3] = { -1, -1, -1 };
            for (size_t k = line.begin; k < line.end; ++k)
            {
                const Item&     it  = items[k];
                const size_t    r   = static_cast<size_t>(it.run);
                const MTextRun& run = runs[r];
                const double    h   = run.height * base;
                const double    yb  = y + it.shift;
                if (it.stack)
                {
                    const StackGeometry g = StackGeometryOf(run, h);
                    const double topW = textWidth(r, run.text, h), botW = textWidth(r, run.bottom, h);
                    if (run.stackType == '#')
                    {
                        placeText(r, run.text, h, x, yb + g.top);
                        placeText(r, run.bottom, h, x + topW, yb);
                        if (!run.text.empty() && !run.bottom.empty())
                            result.m_decorations.push_back({ x + 0.26 * h, x + topW + 0.41 * h, yb + 0.18 * h, static_cast<int>(r), 1.64 * h });
                    }
                    else
                    {
                        const bool tol = run.stackType == '^';
                        placeText(r, run.text, h, tol ? x : x + (it.adv - topW) * 0.5, yb + g.top);
                        placeText(r, run.bottom, h, tol ? x : x + (it.adv - botW) * 0.5, yb);
                        if (run.stackType == '/' && !run.text.empty() && !run.bottom.empty())
                            result.m_decorations.push_back({ x, x + it.adv, yb + g.anchor, static_cast<int>(r) });
                    }
                }
                else
                {
                    GlyphInstance g;
                    g.m_glyph       = &styles[r].font->GetGlyph(GlyphCodeOf(it.cp));
                    g.m_scale       = scaleOf(r, h);
                    g.m_position    = Math::Point3(x, yb, 0.0);
                    g.m_widthFactor = styles[r].widthFactor;
                    g.m_shear       = styles[r].shear;
                    g.m_run         = static_cast<int>(r);
                    result.m_glyphs.push_back(g);
                }
                // 下划线 / 上划线 / 删除线：与 Layout 同样在基线下 0.2、上 1.2、上 0.5 倍字高；首尾相接、同一段的连成一条
                const bool on[3] = { run.underline, run.overline, run.strike };
                static constexpr double kDecorationY[3] = { -0.2, 1.2, 0.5 };
                for (int d = 0; d < 3; ++d)
                {
                    if (!on[d]) { lastDecor[d] = -1; continue; }
                    const double dy = yb + kDecorationY[d] * h;
                    if (lastDecor[d] >= 0)
                    {
                        DecorationLine& prev = result.m_decorations[static_cast<size_t>(lastDecor[d])];
                        if (prev.m_run == it.run && std::abs(prev.m_y - dy) < 1e-12 && std::abs(prev.m_x1 - x) < 1e-9 * (1.0 + std::abs(x)))
                        {
                            prev.m_x1 = x + it.adv;
                            continue;
                        }
                    }
                    result.m_decorations.push_back({ x, x + it.adv, dy, it.run });
                    lastDecor[d] = static_cast<int>(result.m_decorations.size() - 1);
                }
                x += it.adv;
            }
        }
        return result;
    }

    // 多行文字排版后的宽度（最宽一行，世界单位）。
    // 与 Layout() 共用 BreakLines + GetAdvance × scale × widthFactor ⇒ 量与画一致。
    double TextLayoutEngine::MeasureWidth(const std::string& text,
        IFont* font,
        double height,
        double widthFactor,
        double boxWidth)
    {
        if (!font || text.empty())
            return 0.0;

        std::vector<std::string> lines;
        BreakLines(text, font, height, widthFactor, boxWidth, lines);

        double fontH = font->GetHeight();
        double scale = height * ((fontH > 0.0) ? 1.0 / fontH : 1.0);

        double maxW = 0.0;
        for (const auto& line : lines)
        {
            double w = 0.0;
            for (auto cp : DecodeLine(line))   // 与 Layout() 同样解析 %% 控制码与 \U+XXXX
                if (!IsDecorationToggle(cp))
                    w += font->GetAdvance(cp) * scale * widthFactor;
            maxW = std::max(maxW, w);
        }
        return maxW;
    }

    // 文字**墨迹**的左右边界（相对排版原点，世界单位）。
    //
    // 与 MeasureWidth 的差别很关键：MeasureWidth 是**步进宽度**（advance 之和），
    // 而字形轮廓通常比步进窄 —— 右留白（right side bearing）实测 45~133 单位，
    // 相当于 0.11~0.44 个字高。多重引线的基线要"画到文字外侧"，
    // 用步进边就会留出这段空隙。
    //
    // 实现与 Layout() 完全同构：同样的折行、同样的 scale、同样的 cursorX 推进，
    // 只是把每个字形的轮廓包围盒取出来 ⇒ 量与画必然一致。
    bool TextLayoutEngine::MeasureInkBounds(const std::string& text,
        IFont* font,
        double height,
        double widthFactor,
        double boxWidth,
        double& outMinX,
        double& outMaxX)
    {
        outMinX = 0.0;
        outMaxX = 0.0;
        if (!font || text.empty())
            return false;

        std::vector<std::string> lines;
        BreakLines(text, font, height, widthFactor, boxWidth, lines);

        double fontH = font->GetHeight();
        double scale = height * ((fontH > 0.0) ? 1.0 / fontH : 1.0);

        bool any = false;
        double lo = 1e300, hi = -1e300;

        for (const auto& line : lines)
        {
            auto codepoints = DecodeLine(line);
            double cursorX = 0.0;

            for (auto cp : codepoints)
            {
                if (IsDecorationToggle(cp))
                    continue;
                const Glyph& glyph = font->GetGlyph(cp);

                // 无轮廓（空格等）：不贡献墨迹，但仍要推进
                if (glyph.Lines.empty() && glyph.Triangles.empty())
                {
                    cursorX += font->GetAdvance(cp) * scale * widthFactor;
                    continue;
                }

                // 字形轮廓（flatten 后的线段 / 三角形）的 x 包围盒。
                // 优先用字形自带的 MinX/MaxX；为 0 时按几何现算。
                double gMinX = glyph.MinX, gMaxX = glyph.MaxX;
                if (gMinX == 0.0 && gMaxX == 0.0)
                {
                    bool first = true;
                    for (const auto& l : glyph.Lines)
                    {
                        const double xs[2] = { l.Start.x, l.End.x };
                        for (double x : xs)
                        {
                            if (first) { gMinX = gMaxX = x; first = false; }
                            else { gMinX = std::min(gMinX, x); gMaxX = std::max(gMaxX, x); }
                        }
                    }
                    for (const auto& t : glyph.Triangles)
                    {
                        const double xs[3] = { t.a.x, t.b.x, t.c.x };
                        for (double x : xs)
                        {
                            if (first) { gMinX = gMaxX = x; first = false; }
                            else { gMinX = std::min(gMinX, x); gMaxX = std::max(gMaxX, x); }
                        }
                    }
                    if (first)
                    {
                        cursorX += font->GetAdvance(cp) * scale * widthFactor;
                        continue;
                    }
                }

                // 与 Layout(): instance.m_position = (cursorX, cursorY)，
                // 字形局部坐标 × scale × widthFactor 后叠加（此处只看 x）。
                lo = std::min(lo, cursorX + gMinX * scale * widthFactor);
                hi = std::max(hi, cursorX + gMaxX * scale * widthFactor);
                any = true;

                cursorX += font->GetAdvance(cp) * scale * widthFactor;
            }
        }

        if (!any)
            return false;
        outMinX = lo;
        outMaxX = hi;
        return true;
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
                    // %%u 下划线 / %%o 上划线：开关（与多行文字的 \L \O 共用，见 Core/TextCodes.h）
                    case 'u': case 'U': cps.push_back((uint32_t)kUnderlineToggle); i = j + 1; continue;
                    case 'o': case 'O': cps.push_back((uint32_t)kOverlineToggle);  i = j + 1; continue;
                    default: break;
                    }
                }
                // 未知转义:原样输出一个 '%'
                cps.push_back((uint32_t)'%');
                i += 1;
                continue;
            }

            // \U+XXXX：代码页外的字符（单行文字、属性；多行文字已在 StripInlineCodes 里还原）
            if (char32_t ucp = 0; line[i] == '\\' && ParseUnicodeEscape(line, i, ucp))
            {
                if (ucp != 0)
                    cps.push_back(static_cast<uint32_t>(ucp));
                i += 7;
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
        // 实测（MTEXT 2992）：
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
            double advance = IsDecorationToggle(codepoint) ? 0.0 : font->GetAdvance(codepoint) * scale * widthFactor;

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
            if (!IsDecorationToggle(codepoint))
                width += font->GetAdvance(codepoint) * widthFactor;
        }

        return width;
    }
}
