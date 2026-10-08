#pragma once
#include "Core/Node.h"
#include <functional>
#include <optional>
#include <string_view>

namespace MiniGUI
{
    class Popup;
    class UIContext;

    // 解析 "#RRGGBB" / "RRGGBB" / "#RGB"
    std::optional<Color32> ParseHexColor(std::string_view text);

    // 颜色按钮：显示当前颜色，点击弹出调色板
    // 调色板：AutoCAD 标准色（1～9 号）、12 色相 × 5 明度的色板、灰度一行、十六进制输入
    class ColorButton : public Node
    {
    public:
        explicit ColorButton(Color32 color = Colors::White);

        void    SetColor(Color32 color) { m_color = color; Invalidate(); }
        Color32 GetColor() const { return m_color; }
        void    SetOnChanged(std::function<void(Color32)> cb) { m_onChanged = std::move(cb); }

        void Open();
        bool IsOpen() const { return m_popup != nullptr; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        void Choose(Color32 color);

        Color32 m_color;
        Popup*  m_popup = nullptr;
        std::function<void(Color32)> m_onChanged;
    };

    // 弹出调色板（ColorButton 点击后的同一个弹层），锚在 anchor（窗口坐标）下方；选中或输入十六进制后关闭并回调。
    // owner：点击它不会触发轻触关闭（例如表格里的颜色单元格所在的列表）
    Popup* ShowColorPalette(UIContext& ctx, const Rect& anchor, Color32 current,
                            std::function<void(Color32)> onChosen, Node* owner = nullptr);
}
