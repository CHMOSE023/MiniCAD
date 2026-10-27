#include "Widgets/CommandConsole.h"
#include "Core/UIContext.h"
#include "Style/Theme.hpp"
#include "Widgets/AutoComplete.h"
#include "Widgets/Button.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"
#include "Widgets/TextBox.h"
#include <algorithm>
#include <cctype>

namespace MiniGUI
{
    namespace
    {
        bool IEquals(const std::string& a, const std::string& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
            {
                return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
            });
        }

        std::string Trim(const std::string& s)
        {
            const size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos)
                return {};
            return s.substr(b, s.find_last_not_of(" \t") - b + 1);
        }
    }

    // =========================================================
    // 输入框：空格等于回车，按键先交给命令行（历史、Esc）
    // =========================================================
    class CommandInput : public TextBox
    {
    public:
        explicit CommandInput(CommandConsole& owner) : m_owner(owner)
        {
            SetKeyPreview([this](KeyEvent& e) { return m_owner.OnInputKey(e); });
            SetOnSubmit([this](const std::string&) { m_owner.Submit(); });
        }

    protected:
        void OnTextInput(TextInputEvent& e) override
        {
            // 输入法确认的文字里的空格照常输入；单独敲的空格当作回车
            if (e.phase == EventPhase::Target && e.text == " ")
            {
                e.handled = true;
                m_owner.Submit();
                return;
            }
            TextBox::OnTextInput(e);
        }

    private:
        CommandConsole& m_owner;
    };

    namespace
    {
        constexpr const char* kIdlePrompt = "命令:";

        // 展开 / 收起回显：收起时尖朝上（回显在上方展开）
        class HistoryToggle : public Button
        {
        public:
            HistoryToggle(std::function<void()> onClick, const bool& expanded)
                : Button(std::move(onClick)), m_expanded(expanded)
            {
                SetFocusable(false);
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (IsPressedVisual())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 34), Theme::RadiusSm);
                else if (IsHovered())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 20), Theme::RadiusSm);
                const Vec2  c  = r.Center();
                const float dy = m_expanded ? 2.5f : -2.5f;
                dl.PathLineTo({ c.x - 5.0f, c.y - dy });
                dl.PathLineTo({ c.x,        c.y + dy });
                dl.PathLineTo({ c.x + 5.0f, c.y - dy });
                dl.PathStroke(IsHovered() ? Theme::Text : Theme::TextDim, false, 1.5f);
            }

        private:
            const bool& m_expanded;
        };

        // 输入框左边的"命令:"：点击也展开 / 收起回显
        class IdleLabel : public Label
        {
        public:
            IdleLabel(std::string text, std::function<void()> onClick)
                : Label(std::move(text), 13.0f, Theme::TextDim), m_onClick(std::move(onClick))
            {
                SetHitTestVisible(true);
            }

        protected:
            void OnPointerEvent(PointerEvent& e) override
            {
                if (e.phase != EventPhase::Target)
                    return;
                if (e.type == PointerEventType::Down && e.button == MouseButton::Left)
                {
                    e.handled = true;
                    if (m_onClick)
                        m_onClick();
                }
                else if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
                {
                    SetColor(e.type == PointerEventType::Enter ? Theme::Text : Theme::TextDim);
                }
            }

            CursorShape GetCursor(Vec2) const override { return CursorShape::Hand; }

        private:
            std::function<void()> m_onClick;
        };

        // 输入区：外观同输入框（底色、边框、获得焦点时强调色），里面是工具提示 + 无边框的输入框。
        // 点到提示文字上也把焦点交给输入框
        class CommandField : public Node
        {
        public:
            void SetInput(TextBox* input) { m_input = input; }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                const bool focused = m_input && m_input->HasFocus();
                const bool hovered = IsHovered() || (m_input && m_input->IsHovered());
                dl.AddRectFilled(r, Theme::Background, Theme::RadiusSm);
                dl.AddRect(r, focused ? Theme::Accent : hovered ? Theme::BorderHover : Theme::Border, Theme::RadiusSm, 1.0f);
            }

            void OnPointerEvent(PointerEvent& e) override
            {
                if (e.type == PointerEventType::Down && e.button == MouseButton::Left && !e.handled && m_input && !m_input->HasFocus())
                {
                    m_input->Focus();
                    e.handled = true;
                }
                if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
                    Invalidate();
            }

        private:
            TextBox* m_input = nullptr;
        };
    }

    // =========================================================
    // 命令行
    // =========================================================
    CommandConsole::CommandConsole()
    {
        LayoutStyle s;
        s.direction = FlexDirection::Column;
        SetLayoutStyle(s);

        m_log = AddChild<ListView>();
        m_log->EditLayoutStyle().grow = 1.0f;
        m_log->SetRowHeight(20.0f);
        m_log->SetFocusable(false);         // 点击回显不抢走输入框或绘图区的键盘焦点

        Panel* row = AddChild<Panel>(Theme::Panel);
        LayoutStyle rs;
        rs.direction  = FlexDirection::Row;
        rs.alignItems = Align::Center;
        rs.gap        = 6.0f;
        rs.padding    = Edges::Symmetric(8.0f, 3.0f);
        rs.shrink     = 0.0f;
        row->SetLayoutStyle(rs);

        m_idle = row->AddChild<IdleLabel>(kIdlePrompt, [this] { SetLogExpanded(!m_logExpanded); });
        m_idle->EditLayoutStyle().shrink = 0.0f;
        m_idle->SetTooltip("展开 / 收起命令历史");

        auto* field = row->AddChild<CommandField>();
        LayoutStyle fs;
        fs.direction  = FlexDirection::Row;
        fs.alignItems = Align::Center;
        fs.grow       = 1.0f;
        fs.minWidth   = 120.0f;
        fs.padding    = Edges::Make(8.0f, 0.0f, 0.0f, 0.0f);
        field->SetLayoutStyle(fs);

        m_prompt = field->AddChild<Label>("", 13.0f, Theme::Accent);
        m_prompt->SetEllipsis(true);
        m_prompt->EditLayoutStyle().shrink   = 1.0f;
        m_prompt->EditLayoutStyle().minWidth = 0.0f;
        m_prompt->EditLayoutStyle().maxWidth = 520.0f;
        m_prompt->SetVisible(false);

        m_input = field->AddChild<CommandInput>(*this);
        m_input->EditLayoutStyle().grow     = 1.0f;
        m_input->EditLayoutStyle().minWidth = 60.0f;
        TextBoxStyle ts;                    // 边框和底色由输入区画
        ts.background = ts.border = ts.borderHover = ts.borderFocus = ColorRef();
        ts.padding    = Edges::Make(4.0f, 5.0f, 8.0f, 5.0f);
        m_input->SetStyle(ts);
        m_placeholder = "输入命令（例如 L 直线、C 圆），空格或回车执行";
        m_input->SetPlaceholder(m_placeholder);
        field->SetInput(m_input);

        // 最右边：展开 / 收起回显
        m_toggle = row->AddChild<HistoryToggle>([this] { SetLogExpanded(!m_logExpanded); }, m_logExpanded);
        m_toggle->EditLayoutStyle().width  = 22.0f;
        m_toggle->EditLayoutStyle().height = 22.0f;
        m_toggle->EditLayoutStyle().shrink = 0.0f;
        m_toggle->SetTooltip("展开 / 收起命令历史");

        m_promptText = kIdlePrompt;
    }

    void CommandConsole::SetLogExpanded(bool expanded)
    {
        m_logExpanded = expanded;
        m_log->SetVisible(expanded);
        if (expanded && !m_lines.empty())
            m_log->EnsureVisible(static_cast<int>(m_lines.size()) - 1);
        m_toggle->Invalidate();
        InvalidateLayout();
    }

    TextBox* CommandConsole::GetInput() const
    {
        return m_input;
    }

    void CommandConsole::SetPrompt(std::string prompt)
    {
        // 空闲提示（"命令:"）只显示在输入框外面；工具提示放进输入框里，输入时也一直看得到
        const bool idle = prompt.empty() || prompt == kIdlePrompt;
        m_prompt->SetText(idle ? std::string() : prompt);
        m_prompt->SetVisible(!idle);
        m_input->SetPlaceholder(idle ? m_placeholder : std::string());
        m_promptText = std::move(prompt);
    }

    const std::string& CommandConsole::GetPrompt() const
    {
        return m_promptText;
    }

    void CommandConsole::SetLog(const std::vector<std::string>& lines)
    {
        if (lines == m_lines)
            return;
        m_lines = lines;
        m_log->SetItems(m_lines);
        if (!m_lines.empty())
            m_log->EnsureVisible(static_cast<int>(m_lines.size()) - 1);
    }

    bool CommandConsole::IsLogScrolledToEnd() const
    {
        const float maxScroll = std::max(0.0f, m_log->GetContentSize().y - m_log->GetViewportSize().y);
        return m_log->GetScrollOffset().y >= maxScroll - 0.5f;
    }

    void CommandConsole::SetCompletion(Completion completion)
    {
        AutoComplete* ac = m_input->EnableAutoComplete(std::move(completion));
        ac->SetSelectFirst(true);
        ac->SetMaxVisible(8);
        ac->SetOnAccepted([this](const std::string&) { Submit(); });     // 选中候选即执行
    }

    void CommandConsole::FocusInput()
    {
        m_input->Focus();
    }

    bool CommandConsole::HasInputFocus() const
    {
        return m_input->HasFocus();
    }

    void CommandConsole::SetInputText(std::string text)
    {
        m_input->SetText(std::move(text));
    }

    const std::string& CommandConsole::GetInputText() const
    {
        return m_input->GetText();
    }

    void CommandConsole::Submit()
    {
        // 候选列表打开且有选中项：执行选中的候选
        std::string text = Trim(m_input->GetText());
        if (AutoComplete* ac = m_input->GetAutoComplete(); ac && ac->IsOpen())
        {
            const int cur = ac->GetCurrent();
            if (cur >= 0 && cur < static_cast<int>(ac->GetSuggestions().size()))
                text = ac->GetSuggestions()[static_cast<size_t>(cur)].text;
            ac->Close();
        }

        if (!text.empty())
        {
            const auto it = std::find_if(m_history.begin(), m_history.end(), [&text](const std::string& h) { return IEquals(h, text); });
            if (it != m_history.end())
                m_history.erase(it);
            m_history.push_back(text);
        }
        m_historyPos = -1;
        m_input->SetText({});

        if (m_onSubmit)
        {
            auto cb = m_onSubmit;       // 回调里可能把焦点移走、重建界面
            cb(text);
        }
    }

    void CommandConsole::ShowHistory(int pos)
    {
        m_historyPos = pos;
        m_input->ReplaceAll(pos >= 0 ? m_history[static_cast<size_t>(pos)] : std::string());
        if (AutoComplete* ac = m_input->GetAutoComplete())
            ac->Close();                // 翻出来的历史直接执行，不弹候选
    }

    bool CommandConsole::OnInputKey(KeyEvent& e)
    {
        if (e.type != KeyEventType::Down || e.modifiers != 0)
            return false;

        // 候选列表打开时上下键已经被它处理，到这里说明没有候选
        switch (e.key)
        {
        case Key::Up:
            if (m_history.empty())
                return true;
            ShowHistory(m_historyPos < 0 ? static_cast<int>(m_history.size()) - 1 : std::max(m_historyPos - 1, 0));
            return true;
        case Key::Down:
            if (m_historyPos < 0)
                return true;
            ShowHistory(m_historyPos + 1 < static_cast<int>(m_history.size()) ? m_historyPos + 1 : -1);
            return true;
        case Key::Escape:
            m_historyPos = -1;
            m_input->SetText({});
            if (m_onEscape)
            {
                auto cb = m_onEscape;
                cb();
            }
            return true;
        default:
            return false;
        }
    }
}
