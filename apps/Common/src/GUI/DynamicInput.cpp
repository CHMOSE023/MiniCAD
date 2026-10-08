// ── 动态输入（AutoCAD 式）：工具有锚点（橡皮筋阶段）时，光标旁显示长度 / 角度输入框与约束捕捉信息 ─────────
// 焦点平时留在绘图区；工具进行中按数字键时，焦点交给长度框，随后的字符自然落入输入框：
//   - 未键入时输入框实时显示当前长度 / 角度；敲下第一个字符即清空并锁定
//   - Tab 切换长度 / 角度；Enter / 空格提交：只填长度沿当前方向、只填角度取当前长度、都填按极坐标；
//     输入里有 "," "<" "@" 时按坐标处理（默认相对上一点，"#" 开头为绝对坐标）
//   - 字母不进输入框，交给工具作为选项键（如多段线的 A 切圆弧）
//   - Esc：有键入先清空；没有则交给工具（取消命令）
//   - 什么都没键入时 Enter 交给工具（例如结束多段线）
// 提交或取消后焦点回到绘图区
#include "GUI/MainFrame.h"
#include "Core/UIContext.h"
#include "Editor/Snap/SnapResult.h"
#include "Style/Theme.hpp"
#include "Widgets/CommandConsole.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"
#include "Widgets/TextBox.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;

        const char* SnapTypeName(SnapResult::Type t)
        {
            switch (t)
            {
            case SnapResult::Type::Endpoint:      return "端点";
            case SnapResult::Type::Midpoint:      return "中点";
            case SnapResult::Type::Nearest:       return "最近点";
            case SnapResult::Type::Quadrant:      return "象限点";
            case SnapResult::Type::Intersection:  return "交点";
            case SnapResult::Type::Perpendicular: return "垂足";
            case SnapResult::Type::Grid:          return "网格";
            default:                              return nullptr;
            }
        }

        bool ParseNumber(const std::string& s, double& out)
        {
            if (s.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end == s.c_str() + s.size() && std::isfinite(out);
        }

        constexpr float kOffset = 24.0f;      // 输入框相对光标的偏移（逻辑像素），避开十字光标
    }

    class DynamicInputBox;

    // 长度 / 角度输入框：只接受数字和坐标符号；字母交给工具；空格等于回车
    class DynField : public MiniGUI::TextBox
    {
    public:
        DynField(DynamicInputBox& owner, int index);

        bool typed = false;

    protected:
        void OnTextInput(MiniGUI::TextInputEvent& e) override;

    private:
        DynamicInputBox& m_owner;
        int              m_index;
    };

    class DynamicInputBox : public MiniGUI::Panel
    {
    public:
        struct Callbacks
        {
            std::function<void(bool, double, bool, double)> apply;        // 长度 / 角度
            std::function<bool(const std::string&)>          coordinate;   // 坐标文字
            std::function<void(MiniGUI::Key)>                forwardKey;   // 交给工具
            std::function<void()>                            done;         // 提交 / 取消后：焦点回到绘图区
        };

        explicit DynamicInputBox(Callbacks cb)
            : MiniGUI::Panel(MiniGUI::ColorRef(MiniGUI::ThemeColor::Panel, 235))
            , m_cb(std::move(cb))
        {
            using namespace MiniGUI;
            SetBorder(Theme::PopupBorder);
            SetRounding(3.0f);
            SetHitTestVisible(false);           // 只有输入框可以点击，背景不挡住绘图区

            LayoutStyle s;
            s.position   = PositionType::Absolute;
            s.left       = 0.0f;
            s.top        = 0.0f;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = 5.0f;
            s.padding    = Edges::Symmetric(6.0f, 3.0f);
            SetLayoutStyle(s);

            auto caption = [this](const char* text)
            {
                Label* l = AddChild<Label>(text, 12.0f, Theme::TextDim);
                l->SetHitTestVisible(false);
            };
            caption("长度");
            m_fields[0] = AddChild<DynField>(*this, 0);
            m_fields[0]->EditLayoutStyle().width = 86.0f;
            caption("角度");
            m_fields[1] = AddChild<DynField>(*this, 1);
            m_fields[1]->EditLayoutStyle().width = 66.0f;
            m_info = AddChild<Label>("", 12.0f, Theme::TextDim);
            m_info->SetHitTestVisible(false);
            SetVisible(false);
        }

        DynField* GetField(int i) const { return m_fields[i]; }
        bool HasFieldFocus() const { return m_fields[0]->HasFocus() || m_fields[1]->HasFocus(); }
        bool IsTyped() const       { return m_fields[0]->typed || m_fields[1]->typed; }
        void FocusLength()         { m_fields[0]->Focus(); }

        // 未键入的输入框跟随实时值
        void Update(const Editor::DynamicInputState& s)
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.3f", s.Length);
            SetLive(0, buf);
            std::snprintf(buf, sizeof(buf), "%.2f", s.AngleDeg);
            SetLive(1, buf);

            std::string info;
            if (s.OrthoOn)
                info = "正交";
            else if (s.PolarOn)
            {
                std::snprintf(buf, sizeof(buf), "极轴 %.0f°", s.PolarAngleDeg);
                info = buf;
            }
            if (const char* sn = SnapTypeName(s.SnapType))
                info += (info.empty() ? "" : "  ") + std::string("捕捉：") + sn;
            if (info != m_info->GetText())
                m_info->SetText(info);
            m_info->SetVisible(!info.empty());
        }

        void Reset()
        {
            for (DynField* f : m_fields)
                f->typed = false;
            m_live[0].clear();          // 下次 Update 时重新写入实时值
            m_live[1].clear();
        }

        void Submit()
        {
            const std::string len = m_fields[0]->typed ? m_fields[0]->GetText() : std::string();
            const std::string ang = m_fields[1]->typed ? m_fields[1]->GetText() : std::string();
            Reset();

            if (len.find_first_of(",<@#") != std::string::npos)
            {
                // 坐标：默认相对上一点（同 AutoCAD 动态输入），"#" 开头为绝对坐标
                std::string text = len;
                if (text[0] == '#')
                    text.erase(0, 1);
                else if (text[0] != '@')
                    text.insert(0, "@");
                m_cb.coordinate(text);
            }
            else
            {
                double l = 0.0, a = 0.0;
                const bool hasLen = ParseNumber(len, l) && l > 0.0;
                const bool hasAng = ParseNumber(ang, a);
                if (hasLen || hasAng)
                    m_cb.apply(hasLen, l, hasAng, a);
                else
                    m_cb.forwardKey(MiniGUI::Key::Enter);     // 什么都没键入：回车交给工具
            }
            m_cb.done();
        }

        void Escape()
        {
            if (IsTyped())
            {
                Reset();                // 第一次：清空键入，留在输入框
                return;
            }
            m_cb.forwardKey(MiniGUI::Key::Escape);
            Reset();
            m_cb.done();
        }

        bool OnFieldKey(int index, MiniGUI::KeyEvent& e)
        {
            using MiniGUI::Key;
            if (e.type != MiniGUI::KeyEventType::Down)
                return false;
            switch (e.key)
            {
            case Key::Enter:  Submit(); return true;
            case Key::Escape: Escape(); return true;
            case Key::Tab:    m_fields[1 - index]->Focus(); m_fields[1 - index]->SelectAll(); return true;
            case Key::Backspace:
            case Key::Delete:
                return !m_fields[index]->typed;     // 实时值不能删改，直接打字即可替换
            default:
                return false;
            }
        }

        void ForwardLetter(char c)
        {
            m_cb.forwardKey(MiniGUI::KeyFromLetter(static_cast<char>(std::toupper(static_cast<unsigned char>(c)))));
        }

    private:
        void SetLive(int i, const std::string& text)
        {
            DynField* f = m_fields[i];
            if (f->typed || text == m_live[i])
                return;
            m_live[i] = text;
            f->SetText(text);
        }

        Callbacks          m_cb;
        DynField*          m_fields[2] = {};
        MiniGUI::Label*    m_info = nullptr;
        std::string        m_live[2];
    };

    DynField::DynField(DynamicInputBox& owner, int index)
        : m_owner(owner)
        , m_index(index)
    {
        MiniGUI::TextBoxStyle st;
        st.fontSize = 13.0f;
        st.padding  = MiniGUI::Edges::Symmetric(5.0f, 2.0f);
        st.rounding = 2.0f;
        SetStyle(st);
        SetKeyPreview([this](MiniGUI::KeyEvent& e) { return m_owner.OnFieldKey(m_index, e); });
    }

    void DynField::OnTextInput(MiniGUI::TextInputEvent& e)
    {
        if (e.phase != MiniGUI::EventPhase::Target)
            return;
        e.handled = true;
        for (char c : e.text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isdigit(u) || c == '.' || c == '+' || c == '-' || c == ',' || c == '<' || c == '@' || c == '#')
            {
                if (!typed)
                {
                    SetText({});        // 第一个字符替换实时值
                    typed = true;
                }
                MiniGUI::TextInputEvent one;
                one.text = std::string(1, c);
                TextBox::OnTextInput(one);
            }
            else if (std::isalpha(u))
            {
                m_owner.ForwardLetter(c);
            }
            else if (c == ' ')
            {
                m_owner.Submit();
                return;                 // 提交后输入框已重置
            }
        }
    }

    // =========================================================
    // 宿主侧
    // =========================================================
    void MainFrame::SendKeyToEditor(MiniGUI::Key key)
    {
        MiniGUI::KeyEvent e;
        e.key  = key;
        e.type = MiniGUI::KeyEventType::Down;
        OnViewportKey(e);
        e.type = MiniGUI::KeyEventType::Up;          // 有的工具在随后的输入里才结束，按下、抬起都发
        OnViewportKey(e);
    }

    bool MainFrame::IsDynInputVisible() const
    {
        return m_dynInput && m_dynInput->IsVisible();
    }

    bool MainFrame::DynInputHasFocus() const
    {
        return m_dynInput && m_dynInput->HasFieldFocus();
    }

    std::string MainFrame::GetDynInputText(int field) const
    {
        return m_dynInput ? m_dynInput->GetField(field)->GetText() : std::string();
    }

    bool MainFrame::RouteKeyToDynamicInput(const MiniGUI::KeyEvent& e)
    {
        using MiniGUI::Key;
        const bool digit = e.key >= Key::Num0 && e.key <= Key::Num9;
        if (e.type != MiniGUI::KeyEventType::Down || !digit || e.Ctrl() || e.Alt()
            || !m_docManager.GetEditor().IsActiveTool())
            return false;

        if (m_dynInput && m_dynInput->IsVisible())
        {
            m_dynInput->FocusLength();
            return true;
        }
        // 还没有锚点（例如第一点）：数字进入命令行，按坐标输入（100,50）
        if (m_console && m_console->GetParent())
        {
            MiniGUI::DockSpace* dock = m_layout->GetDock("main");
            if (dock && dock->HasPanel("commandline"))
            {
                if (!dock->IsPanelVisible("commandline"))
                    return false;
                dock->ActivatePanel("commandline");
            }
            m_console->FocusInput();
            return true;
        }
        return false;
    }

    void MainFrame::SyncDynamicInput()
    {
        if (!m_dynInput)
        {
            DynamicInputBox::Callbacks cb;
            cb.apply = [this](bool hasLen, double len, bool hasAng, double ang)
            {
                m_docManager.GetEditor().ApplyDynamicInput(hasLen, len, hasAng, ang);
                NoteInput();
                StateChanged();
            };
            cb.coordinate = [this](const std::string& text)
            {
                const bool ok = m_docManager.GetEditor().SubmitCoordinateText(text);
                if (!ok)
                    m_docManager.GetEditor().GetCmdLine().Echo("无效的坐标：" + text);
                NoteInput();
                StateChanged();
                return ok;
            };
            cb.forwardKey = [this](MiniGUI::Key key) { SendKeyToEditor(key); };
            cb.done       = [this] { if (m_viewport->IsVisible()) m_viewport->Focus(); };
            // 放在文档区里、视口之后：绝对定位，盖在视口上
            m_dynInput = m_viewport->GetParent()->AddChild<DynamicInputBox>(std::move(cb));
        }

        const Document* doc = m_docManager.GetActive();
        const auto      dyn = doc ? m_docManager.GetEditor().GetDynamicInput() : Editor::DynamicInputState{};
        const bool focused  = m_dynInput->HasFieldFocus();
        const bool show     = dyn.Active && m_viewport->IsVisible() && (m_hovered || m_buttons != 0 || focused);
        if (!show)
        {
            if (m_dynInput->IsVisible())
            {
                m_dynInput->SetVisible(false);
                m_dynInput->Reset();
                if (focused)
                    m_viewport->Focus();
            }
            return;
        }

        m_dynInput->Update(dyn);
        m_dynInput->SetVisible(true);

        // 跟随光标（偏右下）；放不下时翻到光标左侧 / 上方
        const MiniGUI::Rect vb   = m_viewport->GetBounds();          // 文档区内的坐标
        const MiniGUI::Vec2 size = m_dynInput->GetSize();
        float x = vb.min.x + static_cast<float>(m_mouseX) / m_dpiScale + kOffset;
        float y = vb.min.y + static_cast<float>(m_mouseY) / m_dpiScale + kOffset;
        if (x + size.x > vb.max.x)
            x = std::max(vb.min.x, x - size.x - kOffset * 2.0f);
        if (y + size.y > vb.max.y)
            y = std::max(vb.min.y, y - size.y - kOffset * 2.0f);
        const MiniGUI::LayoutStyle& s = m_dynInput->GetLayoutStyle();
        if (std::abs(s.left - x) > 0.25f || std::abs(s.top - y) > 0.25f)
        {
            MiniGUI::LayoutStyle& e = m_dynInput->EditLayoutStyle();
            e.left = std::round(x);
            e.top  = std::round(y);
        }
    }
}
