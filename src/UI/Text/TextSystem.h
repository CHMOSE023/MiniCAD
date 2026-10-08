#pragma once
#include "Style/ColorRef.hpp"
#include "Core/Types/Rect.hpp"
#include "Text/GlyphAtlas.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    class DrawList;
    class Font;
    class IRenderBackend;

    enum class TextAlign
    {
        Start,
        Center,
        End,
    };

    // 一段文字的排版与绘制参数
    struct TextParams
    {
        Font*     font      = nullptr;              // nullptr：默认字体
        float     size      = 14.0f;                // 字号（逻辑像素，按 em）
        ColorRef  color     = Colors::White;      // 可以是主题颜色
        TextAlign hAlign    = TextAlign::Start;
        TextAlign vAlign    = TextAlign::Start;
        bool      wrap      = false;                // 超出宽度自动换行（英文按空格，中文可在任意两字之间）
        bool      ellipsis  = false;                // 单行超出宽度时截断并加"…"（wrap 为 true 时忽略）
    };

    // 排版结果（逻辑像素，相对文字框左上角），供文本编辑定位光标、选区和点击位置。
    // 所有位置都是 UTF-8 字节偏移，总落在字符边界上
    struct TextLayout
    {
        struct Glyph
        {
            size_t offset;      // 字符在原文中的字节偏移
            float  x;           // 相对行首
            float  advance;
        };

        struct Line
        {
            size_t glyphBegin, glyphEnd;
            size_t byteBegin,  byteEnd;     // 行覆盖的字节范围（不含换行符）
            float  y;                       // 行顶
            float  width;                   // 含行尾空格
        };

        std::vector<Glyph> glyphs;
        std::vector<Line>  lines;
        float              lineHeight = 0.0f;

        size_t LineOf(size_t offset) const;                 // 自动换行处的偏移属于下一行
        float  CaretX(size_t line, size_t offset) const;
        size_t HitTest(Vec2 p) const;                       // 离 p 最近的字符边界
        size_t LineHitTest(size_t line, float x) const;
    };

    // 文字系统：字体、字形图集、排版、测量缓存。由 UIContext 持有。
    // 排版在物理像素下进行，字形原点对齐到物理像素，任何 DPI 缩放下文字都清晰
    class TextSystem
    {
    public:
        explicit TextSystem(IRenderBackend* backend);
        ~TextSystem();

        // ── 字体：第一个 AddFont 的字体是默认字体；主字体缺字时按顺序在后备字体里找 ──
        Font* AddFont(std::shared_ptr<Font> font);
        void  AddFallbackFont(std::shared_ptr<Font> font);
        Font* GetDefaultFont() const { return m_fonts.empty() ? nullptr : m_fonts.front().get(); }
        bool  HasFont() const { return !m_fonts.empty(); }

        void  SetPixelScale(float scale);
        float GetPixelScale() const { return m_pixelScale; }

        GlyphAtlas& GetAtlas() { return m_atlas; }

        // ── 度量（逻辑像素）─────────────────────────────────────
        float GetLineHeight(float size, Font* font = nullptr) const;

        // maxWidth 仅在 wrap 时影响结果；ellipsis 时宽度不超过 maxWidth
        Vec2  Measure(std::string_view text, const TextParams& params, float maxWidth = std::numeric_limits<float>::infinity());

        // 在 box 内按对齐方式绘制；wrap / ellipsis 以 box 宽度为限
        void  Draw(DrawList& dl, const Rect& box, std::string_view text, const TextParams& params);

        // 排版信息（不绘制）。与 Draw 使用同一套排版，box 宽度相同时结果一致；只支持左上对齐
        void  BuildLayout(std::string_view text, const TextParams& params, float maxWidth, TextLayout& out);

    private:
        struct ShapedGlyph
        {
            const Font* font;
            int         glyph;
            uint32_t    codepoint;
            float       x;          // 物理像素，相对行首
            float       advance;
            size_t      offset;     // 字节偏移
        };

        struct Line
        {
            size_t begin;
            size_t end;
            float  width;           // 物理像素，不含行尾空格
            size_t byteBegin = 0;
            size_t byteEnd   = 0;
        };

        // 排版结果写入成员缓冲 m_glyphs / m_lines（物理像素）
        void  Layout(std::string_view text, Font* primary, float pixelSize, float maxWidthPx);
        void  ApplyEllipsis(Font* primary, float pixelSize, float maxWidthPx);
        bool  ResolveGlyph(uint32_t cp, Font* primary, const Font*& font, int& glyph) const;
        float LineWidth(size_t begin, size_t end) const;
        Font* Resolve(Font* font) const { return font ? font : GetDefaultFont(); }

    private:
        GlyphAtlas                         m_atlas;
        std::vector<std::shared_ptr<Font>> m_fonts;       // [0] 为默认字体，其余为后备
        float                              m_pixelScale = 1.0f;

        std::vector<ShapedGlyph> m_glyphs;
        std::vector<Line>        m_lines;

        std::unordered_map<std::string, Vec2> m_measureCache;
    };
}
