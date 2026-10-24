#include "Widgets/Label.h"
#include "Core/UIContext.h"

namespace MiniGUI
{
    Label::Label(std::string text, float fontSize, ColorRef color)
        : m_text(std::move(text))
    {
        m_params.size  = fontSize;
        m_params.color = color;
        SetHitTestVisible(false);
    }

    void Label::SetText(std::string text)
    {
        if (text == m_text)
            return;
        m_text = std::move(text);
        InvalidateLayout();     // 文字变化可能改变尺寸
    }

    void Label::SetFontSize(float size)
    {
        m_params.size = size;
        InvalidateLayout();
    }

    void Label::SetFont(Font* font)
    {
        m_params.font = font;
        InvalidateLayout();
    }

    void Label::SetColor(ColorRef color)
    {
        m_params.color = color;
        Invalidate();
    }

    void Label::SetAlign(TextAlign horizontal, TextAlign vertical)
    {
        m_params.hAlign = horizontal;
        m_params.vAlign = vertical;
        Invalidate();
    }

    void Label::SetWrap(bool wrap)
    {
        m_params.wrap = wrap;
        InvalidateLayout();
    }

    void Label::SetEllipsis(bool ellipsis)
    {
        m_params.ellipsis = ellipsis;
        InvalidateLayout();
    }

    void Label::SetBold(bool bold)
    {
        if (m_bold == bold)
            return;
        m_bold = bold;
        InvalidateLayout();
    }

    Vec2 Label::MeasureContent(Vec2 available)
    {
        const Edges& pad = GetLayoutStyle().padding;
        UIContext* ctx = GetContext();
        if (!ctx)
            return { pad.Horizontal(), pad.Vertical() };

        const float maxWidth = available.x - pad.Horizontal();
        const Vec2  size     = ctx->GetTextSystem().Measure(m_text, m_params, maxWidth);
        return { size.x + pad.Horizontal() + (m_bold ? 1.0f : 0.0f), size.y + pad.Vertical() };
    }

    void Label::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        UIContext* ctx = GetContext();
        if (!ctx || m_text.empty())
            return;

        const Edges& pad = GetLayoutStyle().padding;
        const Rect box{ screenRect.min.x + pad.left, screenRect.min.y + pad.top,
                        screenRect.max.x - pad.right, screenRect.max.y - pad.bottom };

        TextParams params = m_params;
        if (!IsEnabled())
            params.color = ColorScaleAlpha(params.color, 0.45f);

        ctx->GetTextSystem().Draw(dl, box, m_text, params);
        if (m_bold)
            ctx->GetTextSystem().Draw(dl, Rect{ box.min.x + 0.7f, box.min.y, box.max.x + 0.7f, box.max.y }, m_text, params);
    }
}
