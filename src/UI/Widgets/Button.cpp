#include "Widgets/Button.h"
#include "Widgets/ImageView.h"
#include "Widgets/Label.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"

namespace MiniGUI
{
    Button::Button()
    {
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.justify    = Justify::Center;
        s.alignItems = Align::Center;
        s.padding    = Edges::Symmetric(12.0f, 4.0f);
        SetLayoutStyle(s);
        SetFocusable(true);
    }

    Button::Button(std::function<void()> onClick)
        : Button()
    {
        m_onClick = std::move(onClick);
    }

    Button::Button(std::string text, std::function<void()> onClick)
        : Button(std::move(onClick))
    {
        SetText(std::move(text));
    }

    ImageView* Button::SetIcon(TextureId texture, Vec2 size)
    {
        if (!m_icon)
        {
            m_icon = static_cast<ImageView*>(InsertChild(0, std::make_unique<ImageView>(texture, size)));
            EditLayoutStyle().gap = 6.0f;
        }
        else
        {
            m_icon->SetTexture(texture, size);
        }
        return m_icon;
    }

    ImageView* Button::SetIcon(const std::string& utf8Path, Vec2 size)
    {
        ImageView* icon = SetIcon(InvalidTextureId, size);
        icon->SetImagePath(utf8Path, size);
        return icon;
    }

    Label* Button::SetText(std::string text)
    {
        if (!m_label)
            m_label = AddChild<Label>(std::move(text), Theme::FontSize, m_style.text);
        else
            m_label->SetText(std::move(text));
        return m_label;
    }

    void Button::SetStyle(const ButtonStyle& style)
    {
        m_style = style;
        if (m_label)
            m_label->SetColor(style.text);
        Invalidate();
    }

    void Button::SetChecked(bool checked)
    {
        if (m_checked == checked)
            return;
        m_checked = checked;
        Invalidate();
    }

    void Button::OnEnabledChanged()
    {
        m_pressed = false;
    }

    void Button::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        ColorRef fill = m_style.normal;
        if (!IsEnabled())
            fill = m_style.disabled;
        else if (IsPressedVisual())
            fill = m_style.pressed;
        else if (m_checked)
            fill = m_style.checked;
        else if (IsHovered())
            fill = m_style.hover;

        dl.AddRectFilled(screenRect, fill, m_style.rounding);
        if (m_style.borderThickness > 0.0f)
            dl.AddRect(screenRect, m_checked ? m_style.checked : m_style.border, m_style.rounding, m_style.borderThickness);

        // 焦点框：只在键盘导航获得焦点时显示（鼠标点击不显示，与系统行为一致）
        UIContext* ctx = GetContext();
        if (HasFocus() && ctx && ctx->IsFocusVisible())
            dl.AddRect(screenRect.Deflated(-2.0f), m_style.focusRing, m_style.rounding + 2.0f, 1.5f);
    }

    void Button::OnKeyEvent(KeyEvent& e)
    {
        // 获得焦点时 Space / Enter 触发点击
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.repeat || e.modifiers != 0)
            return;
        if (e.key != Key::Space && e.key != Key::Enter)
            return;

        e.handled = true;
        Click();
    }

    void Button::Click()
    {
        if (!IsEnabled() || !m_onClick)
            return;
        auto onClick = m_onClick;   // 回调里可能销毁按钮自身
        onClick();
    }

    void Button::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;

        switch (e.type)
        {
        case PointerEventType::Enter:
        case PointerEventType::Leave:
            Invalidate();
            break;

        case PointerEventType::Down:
            if (e.button == MouseButton::Left)
            {
                m_pressed = true;
                GetContext()->SetCapture(this);
                Invalidate();
                e.handled = true;
            }
            break;

        case PointerEventType::Up:
            if (e.button == MouseButton::Left && m_pressed)
            {
                const bool inside = IsHovered();
                m_pressed = false;
                GetContext()->ReleaseCapture();
                Invalidate();
                e.handled = true;

                // 回调放在最后，并复制一份：回调里可能销毁按钮自身
                if (inside && m_onClick)
                {
                    auto onClick = m_onClick;
                    onClick();
                }
            }
            break;

        case PointerEventType::Cancel:
            m_pressed = false;
            Invalidate();
            break;

        default:
            break;
        }
    }
}
