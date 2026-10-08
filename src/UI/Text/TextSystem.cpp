#include "Text/TextSystem.h"
#include "Text/Font.h"
#include "Text/Utf8.hpp"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace MiniGUI
{
    namespace
    {
        constexpr size_t kMaxMeasureCache = 4096;
        constexpr uint32_t kEllipsis = 0x2026;   // "…"

        bool IsSpace(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000; }

        template<typename T>
        void AppendBytes(std::string& s, const T& v)
        {
            s.append(reinterpret_cast<const char*>(&v), sizeof(T));
        }
    }

    TextSystem::TextSystem(IRenderBackend* backend)
        : m_atlas(backend)
    {}

    TextSystem::~TextSystem() = default;

    Font* TextSystem::AddFont(std::shared_ptr<Font> font)
    {
        if (!font)
            return nullptr;
        m_fonts.push_back(std::move(font));
        m_measureCache.clear();
        return m_fonts.back().get();
    }

    void TextSystem::AddFallbackFont(std::shared_ptr<Font> font)
    {
        AddFont(std::move(font));
    }

    void TextSystem::SetPixelScale(float scale)
    {
        if (scale == m_pixelScale)
            return;
        m_pixelScale = scale;
        m_measureCache.clear();   // 测量结果按物理像素取整，与缩放系数相关
    }

    float TextSystem::GetLineHeight(float size, Font* font) const
    {
        const Font* f = Resolve(font);
        if (!f)
            return 0.0f;
        const float s  = f->ScaleForSize(size * m_pixelScale);
        const float lh = std::ceil(static_cast<float>(f->GetAscent() - f->GetDescent() + f->GetLineGap()) * s);
        return lh / m_pixelScale;
    }

    // =========================================================
    // 排版
    // =========================================================
    bool TextSystem::ResolveGlyph(uint32_t cp, Font* primary, const Font*& font, int& glyph) const
    {
        glyph = primary->FindGlyph(cp);
        if (glyph != 0)
        {
            font = primary;
            return true;
        }
        for (const auto& f : m_fonts)
        {
            if (f.get() == primary)
                continue;
            glyph = f->FindGlyph(cp);
            if (glyph != 0)
            {
                font = f.get();
                return true;
            }
        }
        font  = primary;   // 所有字体都没有：显示主字体的缺字方框
        glyph = 0;
        return false;
    }

    float TextSystem::LineWidth(size_t begin, size_t end) const
    {
        // 行尾空格不计入宽度
        while (end > begin && IsSpace(m_glyphs[end - 1].codepoint))
            --end;
        return end > begin ? m_glyphs[end - 1].x + m_glyphs[end - 1].advance : 0.0f;
    }

    void TextSystem::Layout(std::string_view text, Font* primary, float pixelSize, float maxWidthPx)
    {
        m_glyphs.clear();
        m_lines.clear();

        const bool wrap = std::isfinite(maxWidthPx);

        size_t      lineBegin      = 0;
        size_t      lineByteBegin  = 0;
        size_t      breakAt        = SIZE_MAX;   // 最近的换行机会：从这个字形开始换到下一行
        bool        breakAfterPrev = false;      // 上一个字符之后可以换行（空格、中文）
        uint32_t    prevCp         = 0;
        float       pen            = 0.0f;
        const Font* prevFont       = nullptr;
        int         prevGlyph      = -1;

        auto finishLine = [&](size_t end, size_t byteEnd)
        {
            Line line{ lineBegin, end, LineWidth(lineBegin, end) };
            line.byteBegin = lineByteBegin;
            line.byteEnd   = byteEnd;
            m_lines.push_back(line);
        };

        size_t pos = 0;
        while (pos < text.size())
        {
            const size_t offset = pos;
            uint32_t cp = DecodeUtf8(text, pos);
            if (cp == '\r')
                continue;
            if (cp == '\n')
            {
                finishLine(m_glyphs.size(), offset);
                lineBegin      = m_glyphs.size();
                lineByteBegin  = pos;
                breakAt        = SIZE_MAX;
                breakAfterPrev = false;
                pen            = 0.0f;
                prevGlyph      = -1;
                continue;
            }
            if (cp == '\t')
                cp = ' ';

            const Font* font  = nullptr;
            int         glyph = 0;
            ResolveGlyph(cp, primary, font, glyph);

            const float scale   = font->ScaleForSize(pixelSize);
            const float advance = static_cast<float>(font->GetAdvance(glyph)) * scale;
            if (prevFont == font && prevGlyph >= 0)
                pen += static_cast<float>(font->GetKerning(prevGlyph, glyph)) * scale;

            const bool space = IsSpace(cp);
            const bool cjk   = IsCJK(cp);
            // 换行机会（前一个字与当前字之间）：前一个是空格或汉字、或当前是汉字；
            // 但避尾标点（（「“等）后面、避头标点（。，）」等）前面不能断
            if (m_glyphs.size() > lineBegin && (breakAfterPrev || cjk) &&
                !IsNoBreakAfter(prevCp) && !IsNoBreakBefore(cp))
                breakAt = m_glyphs.size();

            // 超出宽度（空格允许悬挂在行尾）：在最近的换行机会处断开；没有机会就在当前字前断开
            if (wrap && !space && m_glyphs.size() > lineBegin && pen + advance > maxWidthPx)
            {
                const size_t split     = (breakAt != SIZE_MAX && breakAt > lineBegin) ? breakAt : m_glyphs.size();
                const size_t splitByte = split < m_glyphs.size() ? m_glyphs[split].offset : offset;
                finishLine(split, splitByte);

                const float shift = split < m_glyphs.size() ? m_glyphs[split].x : pen;
                for (size_t i = split; i < m_glyphs.size(); ++i)
                    m_glyphs[i].x -= shift;
                pen          -= shift;
                lineBegin     = split;
                lineByteBegin = splitByte;
                breakAt       = SIZE_MAX;
            }

            m_glyphs.push_back(ShapedGlyph{ font, glyph, cp, pen, advance, offset });
            pen           += advance;
            breakAfterPrev = space || cjk;
            prevCp         = cp;
            prevFont       = font;
            prevGlyph      = glyph;
        }
        finishLine(m_glyphs.size(), text.size());
    }

    void TextSystem::BuildLayout(std::string_view text, const TextParams& params, float maxWidth, TextLayout& out)
    {
        out.glyphs.clear();
        out.lines.clear();
        out.lineHeight = 0.0f;

        Font* font = Resolve(params.font);
        if (!font)
        {
            out.lines.push_back(TextLayout::Line{ 0, 0, 0, text.size(), 0.0f, 0.0f });
            return;
        }

        const float scale = m_pixelScale;
        Layout(text, font, params.size * scale,
               params.wrap && std::isfinite(maxWidth) ? maxWidth * scale : std::numeric_limits<float>::infinity());

        out.lineHeight = GetLineHeight(params.size, font);
        out.glyphs.reserve(m_glyphs.size());
        for (const ShapedGlyph& g : m_glyphs)
            out.glyphs.push_back(TextLayout::Glyph{ g.offset, std::round(g.x) / scale, g.advance / scale });

        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            const Line& l = m_lines[i];
            // 含行尾空格的宽度：光标可以停在空格之后
            const float width = l.end > l.begin ? out.glyphs[l.end - 1].x + out.glyphs[l.end - 1].advance : 0.0f;
            out.lines.push_back(TextLayout::Line{ l.begin, l.end, l.byteBegin, l.byteEnd,
                                                  out.lineHeight * static_cast<float>(i), width });
        }
    }

    // =========================================================
    // TextLayout：光标定位与点击测试
    // =========================================================
    size_t TextLayout::LineOf(size_t offset) const
    {
        // 最后一个 byteBegin <= offset 的行；自动换行处的偏移因此属于下一行（光标显示在下一行行首）
        size_t line = 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (lines[i].byteBegin <= offset)
                line = i;
            else
                break;
        }
        return line;
    }

    float TextLayout::CaretX(size_t line, size_t offset) const
    {
        if (line >= lines.size())
            return 0.0f;
        const Line& l = lines[line];
        for (size_t i = l.glyphBegin; i < l.glyphEnd; ++i)
        {
            if (glyphs[i].offset >= offset)
                return glyphs[i].x;
        }
        return l.width;
    }

    size_t TextLayout::LineHitTest(size_t line, float x) const
    {
        if (line >= lines.size())
            return 0;
        const Line& l = lines[line];
        for (size_t i = l.glyphBegin; i < l.glyphEnd; ++i)
        {
            // 点在字形左半边算字形之前，右半边算之后
            if (x < glyphs[i].x + glyphs[i].advance * 0.5f)
                return glyphs[i].offset;
        }
        return l.byteEnd;
    }

    size_t TextLayout::HitTest(Vec2 p) const
    {
        if (lines.empty())
            return 0;
        size_t line = 0;
        if (lineHeight > 0.0f && p.y > 0.0f)
            line = std::min(static_cast<size_t>(p.y / lineHeight), lines.size() - 1);
        return LineHitTest(line, p.x);
    }

    void TextSystem::ApplyEllipsis(Font* primary, float pixelSize, float maxWidthPx)
    {
        // 省略号字形；字体里没有"…"时用三个"."
        const Font* ellFont  = nullptr;
        int         ellGlyph = 0;
        int         ellCount = 1;
        if (!ResolveGlyph(kEllipsis, primary, ellFont, ellGlyph))
        {
            ResolveGlyph('.', primary, ellFont, ellGlyph);
            ellCount = 3;
        }
        const float ellAdvance = static_cast<float>(ellFont->GetAdvance(ellGlyph)) * ellFont->ScaleForSize(pixelSize);
        const float ellWidth   = ellAdvance * static_cast<float>(ellCount);

        std::vector<ShapedGlyph> glyphs;
        std::vector<Line>        lines;
        for (const Line& line : m_lines)
        {
            const size_t begin = glyphs.size();
            if (line.width <= maxWidthPx)
            {
                glyphs.insert(glyphs.end(), m_glyphs.begin() + line.begin, m_glyphs.begin() + line.end);
                lines.push_back(Line{ begin, glyphs.size(), line.width });
                continue;
            }

            // 保留能放下"前缀 + 省略号"的最长前缀，去掉前缀末尾的空格
            const float avail = maxWidthPx - ellWidth;
            size_t cut = line.begin;
            while (cut < line.end && m_glyphs[cut].x + m_glyphs[cut].advance <= avail)
                ++cut;
            while (cut > line.begin && IsSpace(m_glyphs[cut - 1].codepoint))
                --cut;

            glyphs.insert(glyphs.end(), m_glyphs.begin() + line.begin, m_glyphs.begin() + cut);
            float pen = cut > line.begin ? m_glyphs[cut - 1].x + m_glyphs[cut - 1].advance : 0.0f;
            for (int i = 0; i < ellCount; ++i)
            {
                glyphs.push_back(ShapedGlyph{ ellFont, ellGlyph, kEllipsis, pen, ellAdvance });
                pen += ellAdvance;
            }
            lines.push_back(Line{ begin, glyphs.size(), pen });
        }

        m_glyphs = std::move(glyphs);
        m_lines  = std::move(lines);
    }

    // =========================================================
    // 测量与绘制
    // =========================================================
    Vec2 TextSystem::Measure(std::string_view text, const TextParams& params, float maxWidth)
    {
        Font* font = Resolve(params.font);
        if (!font)
            return {};

        const bool  limitWidth = (params.wrap || params.ellipsis) && std::isfinite(maxWidth);
        const float widthKey   = limitWidth ? maxWidth : -1.0f;

        std::string key(text);
        key.push_back('\0');
        AppendBytes(key, font);
        AppendBytes(key, params.size);
        AppendBytes(key, widthKey);
        AppendBytes(key, m_pixelScale);
        key.push_back(static_cast<char>((params.wrap ? 1 : 0) | (params.ellipsis ? 2 : 0)));

        auto it = m_measureCache.find(key);
        if (it != m_measureCache.end())
            return it->second;

        const float pixelSize = params.size * m_pixelScale;
        Layout(text, font, pixelSize, (params.wrap && limitWidth) ? maxWidth * m_pixelScale : std::numeric_limits<float>::infinity());

        float width = 0.0f;
        for (const Line& line : m_lines)
            width = std::max(width, line.width);
        if (params.ellipsis && !params.wrap && limitWidth)
            width = std::min(width, maxWidth * m_pixelScale);

        const float lineHeight = GetLineHeight(params.size, font);
        const Vec2  result{ std::ceil(width) / m_pixelScale, lineHeight * static_cast<float>(m_lines.size()) };

        if (m_measureCache.size() >= kMaxMeasureCache)
            m_measureCache.clear();
        m_measureCache.emplace(std::move(key), result);
        return result;
    }

    void TextSystem::Draw(DrawList& dl, const Rect& box, std::string_view text, const TextParams& params)
    {
        Font* font = Resolve(params.font);
        const Color32 color = dl.Resolve(params.color);
        if (!font || text.empty() || ColorAlpha(color) == 0)
            return;

        const float scale     = m_pixelScale;
        const float pixelSize = params.size * scale;
        const float boxW      = box.Width() * scale;
        const float boxH      = box.Height() * scale;

        Layout(text, font, pixelSize, params.wrap ? boxW : std::numeric_limits<float>::infinity());
        if (params.ellipsis && !params.wrap)
            ApplyEllipsis(font, pixelSize, boxW);

        const float emScale    = font->ScaleForSize(pixelSize);
        const float ascent     = static_cast<float>(font->GetAscent()) * emScale;
        const float lineHeight = GetLineHeight(params.size, font) * scale;
        const float totalH     = lineHeight * static_cast<float>(m_lines.size());

        float top = box.min.y * scale;
        if (params.vAlign == TextAlign::Center) top += (boxH - totalH) * 0.5f;
        else if (params.vAlign == TextAlign::End) top += boxH - totalH;

        dl.PushTexture(m_atlas.GetTexture());
        for (size_t li = 0; li < m_lines.size(); ++li)
        {
            const Line& line = m_lines[li];

            float left = box.min.x * scale;
            if (params.hAlign == TextAlign::Center) left += (boxW - line.width) * 0.5f;
            else if (params.hAlign == TextAlign::End) left += boxW - line.width;

            // 行首和基线对齐到物理像素，每个字形的原点也取整：字形位图与像素一一对应，不会被插值模糊
            const float penX     = std::round(left);
            const float baseline = std::round(top + lineHeight * static_cast<float>(li) + ascent);

            for (size_t i = line.begin; i < line.end; ++i)
            {
                const ShapedGlyph& g = m_glyphs[i];
                const GlyphAtlas::Entry* e = m_atlas.GetGlyph(g.font, g.glyph, pixelSize, g.font->ScaleForSize(pixelSize));
                if (!e || e->rect.width == 0)
                    continue;

                const float x = penX + std::round(g.x) + static_cast<float>(e->offsetX);
                const float y = baseline + static_cast<float>(e->offsetY);
                const Rect  r{ x / scale, y / scale,
                               (x + static_cast<float>(e->rect.width)) / scale, (y + static_cast<float>(e->rect.height)) / scale };
                const Vec2  uv0 = m_atlas.ToUV(e->rect.x, e->rect.y);
                const Vec2  uv1 = m_atlas.ToUV(e->rect.x + e->rect.width, e->rect.y + e->rect.height);
                dl.PrimRectUV(r, uv0, uv1, color);
            }
        }
        dl.PopTexture();
    }
}
