#pragma once
#include "Core/Node.h"
#include "Render/DrawData.hpp"
#include "Style/Theme.hpp"
#include <functional>
#include <string>

namespace MiniGUI
{
    // 按钮外观：各状态的颜色（引用主题槽位，切换主题时自动更新）。
    // 预设：默认（面板色）、Primary（强调色，对话框的默认按钮）、Flat（透明无边框，工具栏图标按钮）
    struct ButtonStyle
    {
        ColorRef normal    = Theme::Panel;
        ColorRef hover     = Theme::Control;
        ColorRef pressed   = Theme::ControlHover;
        ColorRef checked   = Theme::Accent;
        ColorRef disabled  = ColorRef(ThemeColor::Panel, 128);
        ColorRef border    = Theme::BorderSubtle;
        ColorRef focusRing = Theme::Accent;
        ColorRef text      = Theme::Text;           // SetText 创建的文字；SetStyle 时同步到已有文字
        float    rounding  = 4.0f;
        float    borderThickness = 1.0f;

        static ButtonStyle Primary()
        {
            ButtonStyle s;
            s.normal  = Theme::Accent;
            s.hover   = Theme::AccentHover;
            s.pressed = Theme::AccentPressed;
            s.border  = Theme::Accent;
            s.text    = Theme::TextOnAccent;
            return s;
        }

        static ButtonStyle Flat()
        {
            ButtonStyle s;
            s.normal          = Colors::Transparent;
            s.borderThickness = 0.0f;
            return s;
        }
    };

    class Label;
    class ImageView;

    // 按钮：常态 / 悬停 / 按下 / 选中 / 禁用，左键点击触发回调。
    // 按下后捕获指针：拖出按钮范围松开不触发，拖回来再松开仍然触发（与系统按钮一致）。
    // 内容（图标、文字）以子节点形式添加，子节点通常设为 SetHitTestVisible(false)。
    // 默认布局：水平排列、内容居中、左右内边距 12
    class Button : public Node
    {
    public:
        Button();
        explicit Button(std::function<void()> onClick);
        Button(std::string text, std::function<void()> onClick);

        void SetOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }
        void Click();       // 以代码触发点击（禁用时无效）

        // 设置按钮文字：第一次调用时创建一个 Label 子节点，之后只更新文字
        Label* SetText(std::string text);
        Label* GetLabel() const { return m_label; }

        // 设置图标（显示在文字之前）；size 为 0 时用纹理原始尺寸
        ImageView* SetIcon(TextureId texture, Vec2 size = { 16.0f, 16.0f });
        ImageView* SetIcon(const std::string& utf8Path, Vec2 size = { 16.0f, 16.0f });   // 按显示尺寸重采样，清晰

        void SetStyle(const ButtonStyle& style);
        const ButtonStyle& GetStyle() const { return m_style; }

        void SetChecked(bool checked);          // 切换类按钮（例如正交、捕捉开关）的选中状态
        bool IsChecked() const { return m_checked; }

        bool IsPressed() const { return m_pressed; }
        bool IsPressedVisual() const { return m_pressed && IsHovered(); }  // 按下且指针仍在按钮上

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;
        void OnEnabledChanged() override;

    private:
        std::function<void()> m_onClick;
        ButtonStyle           m_style;
        Label*                m_label   = nullptr;
        ImageView*            m_icon    = nullptr;
        bool                  m_pressed = false;
        bool                  m_checked = false;
    };
}
