#include "Widgets/NumberBox.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace MiniGUI
{
    // =========================================================
    // 表达式求值：递归下降
    //   expr   := term (('+' | '-') term)*
    //   term   := factor (('*' | '/') factor)*
    //   factor := ('+' | '-') factor | number | '(' expr ')'
    // =========================================================
    namespace
    {
        class ExprParser
        {
        public:
            explicit ExprParser(std::string_view s) : m_s(s) {}

            std::optional<double> Parse()
            {
                auto v = Expr();
                SkipSpace();
                if (!v || m_pos != m_s.size() || !std::isfinite(*v))
                    return std::nullopt;
                return v;
            }

        private:
            void SkipSpace() { while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t')) ++m_pos; }

            bool Accept(char c)
            {
                SkipSpace();
                if (m_pos < m_s.size() && m_s[m_pos] == c) { ++m_pos; return true; }
                return false;
            }

            std::optional<double> Expr()
            {
                auto v = Term();
                while (v)
                {
                    if (Accept('+'))      { auto r = Term(); if (!r) return std::nullopt; *v += *r; }
                    else if (Accept('-')) { auto r = Term(); if (!r) return std::nullopt; *v -= *r; }
                    else break;
                }
                return v;
            }

            std::optional<double> Term()
            {
                auto v = Factor();
                while (v)
                {
                    if (Accept('*'))      { auto r = Factor(); if (!r) return std::nullopt; *v *= *r; }
                    else if (Accept('/')) { auto r = Factor(); if (!r || *r == 0.0) return std::nullopt; *v /= *r; }
                    else break;
                }
                return v;
            }

            std::optional<double> Factor()
            {
                if (++m_depth > 64)    // 防止恶意输入导致栈溢出
                    return std::nullopt;
                std::optional<double> result;
                if (Accept('-'))      { auto v = Factor(); if (v) result = -*v; }
                else if (Accept('+')) { result = Factor(); }
                else if (Accept('('))
                {
                    auto v = Expr();
                    if (v && Accept(')'))
                        result = v;
                }
                else
                {
                    SkipSpace();
                    const std::string tail(m_s.substr(m_pos));
                    char* end = nullptr;
                    const double v = std::strtod(tail.c_str(), &end);
                    if (end != tail.c_str())
                    {
                        m_pos += static_cast<size_t>(end - tail.c_str());
                        result = v;
                    }
                }
                --m_depth;
                return result;
            }

            std::string_view m_s;
            size_t           m_pos   = 0;
            int              m_depth = 0;
        };
    }

    std::optional<double> EvaluateExpression(std::string_view text)
    {
        // 全角符号、逗号小数点也接受（中文输入法下常见）
        std::string s;
        size_t pos = 0;
        while (pos < text.size())
        {
            const auto c = static_cast<unsigned char>(text[pos]);
            if (c == 0xEF && pos + 2 < text.size())
            {
                const auto c1 = static_cast<unsigned char>(text[pos + 1]);
                const auto c2 = static_cast<unsigned char>(text[pos + 2]);
                // U+FF01..U+FF5E 全角 ASCII：EF BC 81 .. EF BD 9E
                if ((c1 == 0xBC && c2 >= 0x81) || (c1 == 0xBD && c2 <= 0x9E))
                {
                    const int code = (c1 == 0xBC ? c2 - 0x80 : c2 - 0x40) + 0x20;
                    s.push_back(static_cast<char>(code));
                    pos += 3;
                    continue;
                }
            }
            s.push_back(c == ',' ? '.' : static_cast<char>(c));
            ++pos;
        }
        if (s.empty())
            return std::nullopt;
        return ExprParser(s).Parse();
    }

    // =========================================================
    // 微调按钮：上下两半
    // =========================================================
    class SpinButtons : public Node
    {
    public:
        explicit SpinButtons(NumberBox* owner) : m_owner(owner)
        {
            LayoutStyle s;
            s.width  = 20.0f;
            s.shrink = 0.0f;
            SetLayoutStyle(s);
        }

    protected:
        int HalfAt(Vec2 local) const { return local.y < GetSize().y * 0.5f ? 1 : -1; }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            const float midY = std::round(r.Center().y);
            const Rect up{ r.min.x, r.min.y + 1.0f, r.max.x - 1.0f, midY };
            const Rect dn{ r.min.x, midY, r.max.x - 1.0f, r.max.y - 1.0f };
            if (m_hover != 0)
                dl.AddRectFilled(m_hover > 0 ? up : dn, m_pressed ? Theme::ControlPressed : Theme::ControlHover, 3.0f);

            const ColorRef c = IsEnabled() ? Theme::TextDim : Theme::TextDisabled;
            const Vec2 cu = up.Center(), cd = dn.Center();
            dl.AddTriangleFilled({ cu.x - 4.0f, cu.y + 2.0f }, { cu.x + 4.0f, cu.y + 2.0f }, { cu.x, cu.y - 2.0f }, c);
            dl.AddTriangleFilled({ cd.x - 4.0f, cd.y - 2.0f }, { cd.x + 4.0f, cd.y - 2.0f }, { cd.x, cd.y + 2.0f }, c);
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase == EventPhase::Capture)
                return;
            switch (e.type)
            {
            case PointerEventType::Move:
                if (HalfAt(e.localPosition) != m_hover) { m_hover = HalfAt(e.localPosition); Invalidate(); }
                break;
            case PointerEventType::Leave:
                m_hover = 0;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.button == MouseButton::Left)
                {
                    m_pressed = true;
                    m_owner->Commit();       // 先提交正在输入的文字，再在它的基础上加减
                    m_owner->StepBy(HalfAt(e.localPosition) * (e.HasModifier(ModifierKey::Shift) ? 10.0 : 1.0));
                    e.handled = true;
                    Invalidate();
                }
                break;
            case PointerEventType::Up:
                m_pressed = false;
                Invalidate();
                break;
            default:
                break;
            }
        }

    private:
        NumberBox* m_owner;
        int        m_hover   = 0;
        bool       m_pressed = false;
    };

    // =========================================================
    // NumberBox
    // =========================================================
    NumberBox::NumberBox(double value, double minValue, double maxValue, double step, int decimals)
        : m_value(value)
        , m_min(minValue)
        , m_max(maxValue)
        , m_step(step)
        , m_decimals(decimals)
    {
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Stretch;
        s.height     = Theme::ControlH;
        SetLayoutStyle(s);

        m_value = Normalize(value);
        m_box = AddChild<TextBox>(Format(m_value));
        {
            TextBoxStyle ts;
            ts.border      = Colors::Transparent;     // 外框由 NumberBox 画
            ts.borderHover = Colors::Transparent;
            ts.borderFocus = Colors::Transparent;
            ts.background  = Colors::Transparent;
            m_box->SetStyle(ts);
            LayoutStyle bs;
            bs.grow     = 1.0f;
            bs.minWidth = 40.0f;
            m_box->SetLayoutStyle(bs);
        }
        AddChild<SpinButtons>(this);

        m_box->SetOnSubmit([this](const std::string&) { Commit(); m_box->SelectAll(); });
        m_box->SetOnFocusChanged([this](bool focused)
        {
            if (!focused)
                Commit();
            Invalidate();
        });
        m_box->SetKeyPreview([this](KeyEvent& e)
        {
            if (e.modifiers & ~static_cast<uint8_t>(ModifierKey::Shift))
                return false;
            if (e.key != Key::Up && e.key != Key::Down)
                return false;
            Commit();
            StepBy((e.key == Key::Up ? 1.0 : -1.0) * (e.Shift() ? 10.0 : 1.0));
            m_box->SelectAll();
            return true;
        });
    }

    double NumberBox::Normalize(double value) const
    {
        value = std::fmin(std::fmax(value, m_min), m_max);
        const double scale = std::pow(10.0, m_decimals);
        return std::round(value * scale) / scale;
    }

    std::string NumberBox::Format(double value) const
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", m_decimals, value);
        std::string s = buf;
        if (!s.empty() && s[0] == '-' && std::strtod(s.c_str(), nullptr) == 0.0)
            s.erase(0, 1);   // 不显示"-0.00"
        return s;
    }

    void NumberBox::SetValue(double value)
    {
        Apply(value, false);
    }

    void NumberBox::SetRange(double minValue, double maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        Apply(m_value, false);
    }

    void NumberBox::SetDecimals(int decimals)
    {
        m_decimals = decimals;
        Apply(m_value, false);
    }

    void NumberBox::SetMixed()
    {
        m_mixed = true;
        m_box->ReplaceAll({});
        m_box->SetPlaceholder("*多种*");
    }

    void NumberBox::Apply(double value, bool notify)
    {
        if (m_mixed)
        {
            m_mixed = false;
            m_box->SetPlaceholder({});
        }
        value = Normalize(value);
        const bool changed = value != m_value;
        m_value = value;
        const std::string text = Format(value);
        if (m_box->GetText() != text)
            m_box->ReplaceAll(text);
        if (changed && notify && m_onChanged)
        {
            auto cb = m_onChanged;
            cb(m_value);
        }
    }

    void NumberBox::Commit()
    {
        if (auto v = EvaluateExpression(m_box->GetText()))
        {
            const bool   wasMixed = m_mixed;
            const double before   = m_value;
            Apply(*v, true);
            // 从"多种"改成与原值相同的值时 Apply 认为没有变化、不会通知，这里补发
            if (wasMixed && m_value == before && m_onChanged)
            {
                auto cb = m_onChanged;
                cb(m_value);
            }
        }
        else if (m_mixed)
        {
            m_box->ReplaceAll({});   // 保持"多种"
        }
        else
        {
            Apply(m_value, false);   // 非法输入：恢复原值
        }
    }

    void NumberBox::StepBy(double steps)
    {
        Apply(m_value + m_step * steps, true);
    }

    void NumberBox::OnPaint(DrawList& dl, const Rect& r)
    {
        const bool focused = m_box->HasFocus();
        dl.AddRectFilled(r, Theme::Background, Theme::Rounding);
        dl.AddRect(r, focused ? Theme::Accent : (IsHovered() ? Theme::BorderHover : Theme::Border), Theme::Rounding, 1.0f);
    }

    void NumberBox::OnPointerEvent(PointerEvent& e)
    {
        if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
            Invalidate();
        // 输入框获得焦点时滚轮调整数值
        if (e.type == PointerEventType::Wheel && e.phase != EventPhase::Capture && m_box->HasFocus() && e.wheelDelta.y != 0.0f)
        {
            Commit();
            StepBy(e.wheelDelta.y > 0.0f ? 1.0 : -1.0);
            e.handled = true;
        }
    }
}
