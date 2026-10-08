#pragma once
#include "Core/Node.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include <string>

namespace MiniGUI
{
    // 文字标签：尺寸由文字决定（参与 Flexbox 测量），默认不接收点击（点击穿透到父节点）
    class Label : public Node
    {
    public:
        explicit Label(std::string text = {}, float fontSize = 14.0f, ColorRef color = Theme::Text);

        void SetText(std::string text);
        const std::string& GetText() const { return m_text; }

        void SetFontSize(float size);
        void SetFont(Font* font);
        void SetColor(ColorRef color);
        void SetAlign(TextAlign horizontal, TextAlign vertical = TextAlign::Start);
        void SetWrap(bool wrap);            // 超出宽度自动换行
        void SetEllipsis(bool ellipsis);    // 单行超出宽度时显示"…"

        const TextParams& GetParams() const { return m_params; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        std::string m_text;
        TextParams  m_params;
    };
}
