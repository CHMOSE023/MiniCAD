#include "Widgets/Controls.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr float kBoxSize = 16.0f;
        constexpr float kGap     = 8.0f;

        TextParams LabelParams(bool enabled)
        {
            TextParams p;
            p.size   = Theme::FontSize;
            p.color  = enabled ? Theme::Text : Theme::TextDisabled;
            p.vAlign = TextAlign::Center;
            return p;
        }

        bool FocusRingVisible(const Node& n)
        {
            UIContext* ctx = n.GetContext();
            return n.HasFocus() && ctx && ctx->IsFocusVisible();
        }
    }

    // =========================================================
    // Separator
    // =========================================================
    Separator::Separator(bool vertical, ColorRef color)
        : m_color(color)
    {
        LayoutStyle s;
        if (vertical) s.width = 1.0f; else s.height = 1.0f;
        s.shrink = 0.0f;
        SetLayoutStyle(s);
        SetHitTestVisible(false);
    }

    void Separator::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, m_color);
    }

    // =========================================================
    // ToggleBase
    // =========================================================
    ToggleBase::ToggleBase(std::string text)
        : m_text(std::move(text))
    {
        SetFocusable(true);
    }

    void ToggleBase::SetText(std::string text)
    {
        m_text = std::move(text);
        InvalidateLayout();
    }

    Vec2 ToggleBase::MeasureContent(Vec2 available)
    {
        (void)available;
        float w = kBoxSize;
        float h = 24.0f;
        if (UIContext* ctx = GetContext(); ctx && !m_text.empty())
        {
            const Vec2 t = ctx->GetTextSystem().Measure(m_text, LabelParams(true));
            w += kGap + t.x;
            h  = std::max(h, t.y);
        }
        return { w, h };
    }

    Rect ToggleBase::BoxRect(const Rect& r, float boxSize) const
    {
        const float y = std::round(r.Center().y - boxSize * 0.5f);
        return Rect::FromXYWH(r.min.x, y, boxSize, boxSize);
    }

    void ToggleBase::DrawLabelAndFocus(DrawList& dl, const Rect& r, float boxSize)
    {
        if (FocusRingVisible(*this))
            dl.AddRect(BoxRect(r, boxSize).Deflated(-3.0f), Theme::Accent, 5.0f, 1.5f);

        UIContext* ctx = GetContext();
        if (!ctx || m_text.empty())
            return;
        ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + boxSize + kGap, r.min.y, r.max.x, r.max.y }, m_text, LabelParams(IsEnabled()));
    }

    void ToggleBase::OnPointerEvent(PointerEvent& e)
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
                e.handled = true;
                Invalidate();
            }
            break;
        case PointerEventType::Up:
            if (e.button == MouseButton::Left && m_pressed)
            {
                m_pressed = false;
                GetContext()->ReleaseCapture();
                e.handled = true;
                Invalidate();
                if (IsHovered())
                    Activate();
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

    void ToggleBase::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase == EventPhase::Target && e.type == KeyEventType::Down && e.key == Key::Space && !e.repeat && e.modifiers == 0)
        {
            e.handled = true;
            Activate();
        }
    }

    // =========================================================
    // CheckBox
    // =========================================================
    CheckBox::CheckBox(std::string text, bool checked)
        : ToggleBase(std::move(text))
        , m_state(checked ? State::Checked : State::Unchecked)
    {}

    void CheckBox::SetState(State state)
    {
        if (m_state == state)
            return;
        m_state = state;
        Invalidate();
    }

    void CheckBox::Activate()
    {
        // 不确定状态点击后变为勾选
        SetState(m_state == State::Checked ? State::Unchecked : State::Checked);
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb(IsChecked());
        }
    }

    void CheckBox::OnPaint(DrawList& dl, const Rect& r)
    {
        const Rect box     = BoxRect(r, kBoxSize);
        const bool enabled = IsEnabled();
        const bool on      = m_state != State::Unchecked;

        ColorRef fill = on ? (IsHovered() ? Theme::AccentHover : Theme::Accent)
                          : (m_pressed ? Theme::ControlPressed : IsHovered() ? Theme::ControlHover : Theme::Control);
        if (!enabled)
            fill = ColorScaleAlpha(fill, 0.45f);
        dl.AddRectFilled(box, fill, 3.0f);
        if (!on)
            dl.AddRect(box, enabled ? Theme::Border : Theme::BorderSubtle, 3.0f, 1.0f);

        const ColorRef mark = enabled ? Theme::TextOnAccent : ColorScaleAlpha(Theme::TextOnAccent, 0.5f);
        if (m_state == State::Checked)
        {
            const Vec2 pts[3] = { { box.min.x + 3.5f, box.min.y + 8.0f },
                                  { box.min.x + 6.8f, box.min.y + 11.3f },
                                  { box.min.x + 12.5f, box.min.y + 4.8f } };
            dl.AddPolyline(pts, mark, false, 1.8f);
        }
        else if (m_state == State::Indeterminate)
        {
            dl.AddRectFilled(Rect{ box.min.x + 4.0f, box.min.y + 7.0f, box.max.x - 4.0f, box.min.y + 9.0f }, mark);
        }

        DrawLabelAndFocus(dl, r, kBoxSize);
    }

    // =========================================================
    // RadioGroup / RadioButton
    // =========================================================
    void RadioGroup::SetValue(int value)
    {
        if (value == m_value)
            return;
        m_value = value;
        for (RadioButton* b : m_buttons)
            b->Invalidate();
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb(value);
        }
    }

    RadioGroup::~RadioGroup()
    {
        for (RadioButton* b : m_buttons)
            b->m_group = nullptr;
    }

    void RadioGroup::Remove(RadioButton* b)
    {
        m_buttons.erase(std::remove(m_buttons.begin(), m_buttons.end(), b), m_buttons.end());
    }

    RadioButton::RadioButton(RadioGroup* group, int value, std::string text)
        : ToggleBase(std::move(text))
        , m_group(group)
        , m_value(value)
    {
        if (m_group)
            m_group->Add(this);
    }

    RadioButton::~RadioButton()
    {
        if (m_group)
            m_group->Remove(this);
    }

    bool RadioButton::IsSelected() const
    {
        return m_group && m_group->GetValue() == m_value;
    }

    void RadioButton::Activate()
    {
        if (m_group)
            m_group->SetValue(m_value);
    }

    void RadioButton::OnKeyEvent(KeyEvent& e)
    {
        ToggleBase::OnKeyEvent(e);
        if (e.handled || e.phase != EventPhase::Target || e.type != KeyEventType::Down || !m_group || e.modifiers != 0)
            return;

        // 方向键：在组内移动并选中（与系统单选按钮一致）
        int dir = 0;
        if (e.key == Key::Down || e.key == Key::Right) dir = 1;
        if (e.key == Key::Up   || e.key == Key::Left)  dir = -1;
        if (dir == 0)
            return;

        const auto& buttons = m_group->m_buttons;
        const auto  it      = std::find(buttons.begin(), buttons.end(), this);
        if (it == buttons.end())
            return;
        const int n = static_cast<int>(buttons.size());
        int index = static_cast<int>(it - buttons.begin());
        for (int step = 0; step < n; ++step)
        {
            index = (index + dir + n) % n;
            RadioButton* next = buttons[static_cast<size_t>(index)];
            if (next->CanTakeFocus())
            {
                next->Focus(FocusReason::Keyboard);
                next->Activate();
                break;
            }
        }
        e.handled = true;
    }

    void RadioButton::OnPaint(DrawList& dl, const Rect& r)
    {
        const Rect box     = BoxRect(r, kBoxSize);
        const Vec2 c       = box.Center();
        const bool enabled = IsEnabled();
        const bool on      = IsSelected();

        ColorRef fill = on ? (IsHovered() ? Theme::AccentHover : Theme::Accent)
                          : (m_pressed ? Theme::ControlPressed : IsHovered() ? Theme::ControlHover : Theme::Control);
        if (!enabled)
            fill = ColorScaleAlpha(fill, 0.45f);
        dl.AddCircleFilled(c, kBoxSize * 0.5f, fill);
        if (on)
            dl.AddCircleFilled(c, 3.5f, enabled ? Theme::TextOnAccent : ColorScaleAlpha(Theme::TextOnAccent, 0.5f));
        else
            dl.AddCircle(c, kBoxSize * 0.5f - 0.5f, enabled ? Theme::Border : Theme::BorderSubtle, 1.0f);

        if (FocusRingVisible(*this))
            dl.AddCircle(c, kBoxSize * 0.5f + 3.0f, Theme::Accent, 1.5f);
        UIContext* ctx = GetContext();
        if (ctx && !m_text.empty())
            ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + kBoxSize + kGap, r.min.y, r.max.x, r.max.y }, m_text, LabelParams(enabled));
    }

    // =========================================================
    // Slider
    // =========================================================
    namespace
    {
        constexpr float kSliderPad   = 8.0f;    // 两端留出滑块半径
        constexpr float kThumbRadius = 7.0f;
    }

    Slider::Slider(float minValue, float maxValue, float value)
        : m_min(minValue)
        , m_max(maxValue)
        , m_value(value)
    {
        SetFocusable(true);
        m_value = Snap(value);
    }

    void Slider::SetRange(float minValue, float maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        SetValue(m_value);
        Invalidate();
    }

    float Slider::Snap(float v) const
    {
        const float lo = std::min(m_min, m_max), hi = std::max(m_min, m_max);
        if (m_step > 0.0f)
            v = m_min + std::round((v - m_min) / m_step) * m_step;
        return std::clamp(v, lo, hi);
    }

    float Slider::Fraction() const
    {
        return m_max != m_min ? (m_value - m_min) / (m_max - m_min) : 0.0f;
    }

    void Slider::SetValue(float value)
    {
        value = Snap(value);
        if (value == m_value)
            return;
        m_value = value;
        Invalidate();
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb(m_value);
        }
    }

    void Slider::SetValueFromX(float localX)
    {
        const float w = std::max(GetSize().x - kSliderPad * 2.0f, 1.0f);
        const float t = std::clamp((localX - kSliderPad) / w, 0.0f, 1.0f);
        SetValue(m_min + (m_max - m_min) * t);
    }

    Vec2 Slider::MeasureContent(Vec2 available)
    {
        (void)available;
        return { 160.0f, 24.0f };
    }

    void Slider::OnPaint(DrawList& dl, const Rect& r)
    {
        const bool  enabled = IsEnabled();
        const float cy      = r.Center().y;
        const float x0      = r.min.x + kSliderPad;
        const float x1      = r.max.x - kSliderPad;
        const float xv      = x0 + (x1 - x0) * Fraction();

        dl.AddRectFilled(Rect{ x0, cy - 2.0f, x1, cy + 2.0f }, Theme::Control, 2.0f);
        dl.AddRectFilled(Rect{ x0, cy - 2.0f, xv, cy + 2.0f }, enabled ? Theme::Accent : Theme::TextDisabled, 2.0f);

        const bool hot = IsHovered() || m_dragging;
        dl.AddCircleFilled({ xv, cy }, kThumbRadius, enabled ? (hot ? Theme::Outline : Theme::Text) : Theme::TextDisabled);
        if (FocusRingVisible(*this))
            dl.AddCircle({ xv, cy }, kThumbRadius + 3.0f, Theme::Accent, 1.5f);
    }

    void Slider::OnPointerEvent(PointerEvent& e)
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
                m_dragging = true;
                GetContext()->SetCapture(this);
                SetValueFromX(e.localPosition.x);
                e.handled = true;
            }
            break;
        case PointerEventType::Move:
            if (m_dragging)
            {
                SetValueFromX(e.localPosition.x);
                e.handled = true;
            }
            break;
        case PointerEventType::Up:
            if (m_dragging && e.button == MouseButton::Left)
            {
                m_dragging = false;
                GetContext()->ReleaseCapture();
                e.handled = true;
                Invalidate();
            }
            break;
        case PointerEventType::Wheel:
            // 只在持有焦点时响应滚轮，避免滚动面板时误改数值
            if (HasFocus() && e.wheelDelta.y != 0.0f)
            {
                const float step = m_step > 0.0f ? m_step : (m_max - m_min) / 100.0f;
                SetValue(m_value + (e.wheelDelta.y > 0.0f ? step : -step));
                e.handled = true;
            }
            break;
        case PointerEventType::Cancel:
            m_dragging = false;
            break;
        default:
            break;
        }
    }

    void Slider::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.modifiers != 0)
            return;
        const float step = m_step > 0.0f ? m_step : (m_max - m_min) / 100.0f;
        const float page = std::max(step, (m_max - m_min) / 10.0f);
        switch (e.key)
        {
        case Key::Left:  case Key::Down:  SetValue(m_value - step); break;
        case Key::Right: case Key::Up:    SetValue(m_value + step); break;
        case Key::PageDown:               SetValue(m_value - page); break;
        case Key::PageUp:                 SetValue(m_value + page); break;
        case Key::Home:                   SetValue(m_min);          break;
        case Key::End:                    SetValue(m_max);          break;
        default: return;
        }
        e.handled = true;
    }

    // =========================================================
    // ProgressBar
    // =========================================================
    ProgressBar::ProgressBar(float value)
        : m_value(std::clamp(value, 0.0f, 1.0f))
    {
        SetHitTestVisible(false);
    }

    void ProgressBar::SetValue(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        if (value == m_value)
            return;
        m_value = value;
        Invalidate();
    }

    Vec2 ProgressBar::MeasureContent(Vec2 available)
    {
        (void)available;
        return { 160.0f, 6.0f };
    }

    void ProgressBar::OnPaint(DrawList& dl, const Rect& r)
    {
        const float radius = r.Height() * 0.5f;
        dl.AddRectFilled(r, Theme::Control, radius);
        if (m_indeterminate)
        {
            // 斜条纹，表示进度未知
            dl.PushClipRect(r);
            for (float x = r.min.x - r.Height(); x < r.max.x; x += 12.0f)
            {
                const Vec2 quad[4] = { { x, r.max.y }, { x + 6.0f, r.max.y }, { x + 6.0f + r.Height(), r.min.y }, { x + r.Height(), r.min.y } };
                dl.AddConvexPolyFilled(quad, Theme::Accent);
            }
            dl.PopClipRect();
            return;
        }
        if (m_value > 0.0f)
            dl.AddRectFilled(Rect{ r.min.x, r.min.y, r.min.x + std::max(r.Width() * m_value, r.Height()), r.max.y }, Theme::Accent, radius);
    }
}
