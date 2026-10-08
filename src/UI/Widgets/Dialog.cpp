#include "Widgets/Dialog.h"
#include "Widgets/Button.h"
#include "Widgets/Controls.h"
#include "Widgets/Label.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"

namespace MiniGUI
{
    // 标题栏：显示标题、× 按钮；按住拖动整个对话框
    class DialogTitleBar : public Node
    {
    public:
        explicit DialogTitleBar(Dialog* dialog) : m_dialog(dialog)
        {
            LayoutStyle s;
            s.height = 38.0f;
            s.shrink = 0.0f;
            SetLayoutStyle(s);
        }

    protected:
        Rect CloseRect() const
        {
            const Vec2 size = GetSize();
            return Rect{ size.x - 36.0f, 5.0f, size.x - 8.0f, size.y - 5.0f };
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            if (UIContext* ctx = GetContext())
            {
                TextParams p;
                p.size     = Theme::FontSize;
                p.color    = Theme::Text;
                p.vAlign   = TextAlign::Center;
                p.ellipsis = true;
                ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + 16.0f, r.min.y, r.max.x - 44.0f, r.max.y }, m_dialog->m_title, p);
            }

            const Rect cr{ CloseRect().min + r.min, CloseRect().max + r.min };
            if (m_closeHover)
                dl.AddRectFilled(cr, m_closePressed ? Theme::ControlPressed : Theme::ControlHover, 4.0f);
            const Vec2 c = cr.Center();
            dl.AddLine({ c.x - 5.0f, c.y - 5.0f }, { c.x + 5.0f, c.y + 5.0f }, Theme::TextDim, 1.5f);
            dl.AddLine({ c.x - 5.0f, c.y + 5.0f }, { c.x + 5.0f, c.y - 5.0f }, Theme::TextDim, 1.5f);
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase == EventPhase::Capture)
                return;
            const bool onClose = CloseRect().Contains(e.localPosition);
            switch (e.type)
            {
            case PointerEventType::Move:
                if (m_dragging)
                {
                    m_dialog->MoveTo(e.position - m_grab);
                    e.handled = true;
                }
                else if (onClose != m_closeHover)
                {
                    m_closeHover = onClose;
                    Invalidate();
                }
                break;
            case PointerEventType::Leave:
                m_closeHover = false;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.button != MouseButton::Left)
                    break;
                e.handled = true;
                GetContext()->SetCapture(this);
                if (onClose)
                {
                    m_closePressed = true;
                }
                else
                {
                    // 记住按下点相对对话框左上角的偏移，拖动时保持不变
                    m_dragging = true;
                    m_grab     = e.position - m_dialog->GetScreenBounds().min;
                }
                Invalidate();
                break;
            case PointerEventType::Up:
                if (e.button != MouseButton::Left)
                    break;
                GetContext()->ReleaseCapture();
                e.handled = true;
                if (m_closePressed && onClose)
                {
                    m_closePressed = false;
                    m_dialog->Cancel();
                    return;     // 对话框可能已经关闭
                }
                m_closePressed = false;
                m_dragging     = false;
                Invalidate();
                break;
            case PointerEventType::Cancel:
                m_dragging = m_closePressed = false;
                break;
            default:
                break;
            }
        }

    private:
        Dialog* m_dialog;
        bool    m_dragging     = false;
        bool    m_closeHover   = false;
        bool    m_closePressed = false;
        Vec2    m_grab;
    };

    // =========================================================
    // Dialog
    // =========================================================
    Dialog::Dialog(std::string title, bool modal)
        : m_title(std::move(title))
    {
        SetModal(modal);
        SetLightDismiss(false);
        SetCentered();

        LayoutStyle s;
        s.minWidth = 360.0f;
        SetLayoutStyle(s);

        PopupStyle ps;
        ps.rounding = 8.0f;
        SetStyle(ps);

        m_titleBar = AddChild<DialogTitleBar>(this);
        AddChild<Separator>();

        m_body = AddChild<Node>();
        {
            LayoutStyle bs;
            bs.padding = Edges::All(16.0f);
            bs.gap     = 10.0f;
            m_body->SetLayoutStyle(bs);
        }

        m_buttonRow = AddChild<Node>();
        {
            LayoutStyle rs;
            rs.direction = FlexDirection::Row;
            rs.justify   = Justify::End;
            rs.gap       = 8.0f;
            rs.padding   = Edges::Make(16.0f, 4.0f, 16.0f, 16.0f);
            m_buttonRow->SetLayoutStyle(rs);
        }
    }

    void Dialog::SetTitle(std::string title)
    {
        m_title = std::move(title);
        Invalidate();
    }

    Button* Dialog::AddButton(std::string text, std::function<void()> onClick, DialogButtonRole role)
    {
        Button* b = m_buttonRow->AddChild<Button>(std::move(text), std::move(onClick));
        b->EditLayoutStyle().minWidth = 80.0f;
        b->EditLayoutStyle().height   = Theme::ControlH;
        if (role == DialogButtonRole::Default)
        {
            b->SetStyle(ButtonStyle::Primary());
            m_default = b;
        }
        else if (role == DialogButtonRole::Cancel)
        {
            m_cancel = b;
        }
        return b;
    }

    void Dialog::Cancel()
    {
        if (m_cancel)
            m_cancel->Click();      // 与点击取消按钮完全一致
        else
            Close();
    }

    void Dialog::OnOpened()
    {
        // 常见写法是先打开再往内容区添加控件，所以初始焦点推迟到第一次布局完成后再设置
        m_pendingFocus = true;
    }

    void Dialog::OnLayout()
    {
        Popup::OnLayout();
        if (!m_pendingFocus)
            return;
        m_pendingFocus = false;
        // 打开后用户可能已经点到了别处（焦点不在对话框里就不抢）
        UIContext* ctx = GetContext();
        if (ctx && ctx->GetFocus() && !IsAncestorOf(ctx->GetFocus()))
            return;
        ApplyInitialFocus();
    }

    void Dialog::ApplyInitialFocus()
    {
        if (m_initialFocus && m_initialFocus->Focus())
            return;

        // 内容区第一个可聚焦控件，其次是默认按钮
        std::vector<Node*> stack{ m_body };
        while (!stack.empty())
        {
            Node* n = stack.back();
            stack.pop_back();
            if (n != m_body && n->Focus())
                return;
            const auto& children = n->GetChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(it->get());
        }
        if (m_default)
            m_default->Focus();
    }

    void Dialog::OnKeyEvent(KeyEvent& e)
    {
        if (e.type == KeyEventType::Down && e.modifiers == 0 &&
            (e.phase == EventPhase::Target || e.phase == EventPhase::Bubble))
        {
            UIContext* ctx   = GetContext();
            Node*      focus = ctx ? ctx->GetFocus() : nullptr;

            if (e.key == Key::Escape)
            {
                e.handled = true;
                Cancel();
                return;
            }
            // Enter：焦点在按钮上时由按钮自己处理（已在目标阶段处理掉），多行输入框里是换行
            if (e.key == Key::Enter && m_default && !dynamic_cast<Button*>(focus))
            {
                auto* tb = dynamic_cast<TextBox*>(focus);
                if (!tb || !tb->IsMultiline())
                {
                    e.handled = true;
                    m_default->Click();
                    return;
                }
            }
        }
        Popup::OnKeyEvent(e);
    }

    // =========================================================
    // 消息框
    // =========================================================
    Dialog* ShowMessageBox(UIContext& ctx, std::string title, std::string message,
                           std::vector<std::string> buttons, std::function<void(int)> onResult)
    {
        auto dialog = std::make_unique<Dialog>(std::move(title));
        Dialog* raw = dialog.get();

        Label* text = raw->GetBody()->AddChild<Label>(std::move(message));
        text->SetWrap(true);
        text->EditLayoutStyle().maxWidth = 440.0f;

        // 回调只触发一次：按钮点击、Esc、× 都走同一条路径
        auto result = std::make_shared<std::function<void(int)>>(std::move(onResult));
        const int count = static_cast<int>(buttons.size());
        for (int i = 0; i < count; ++i)
        {
            const DialogButtonRole role = i == 0 ? DialogButtonRole::Default
                                        : (i == count - 1 ? DialogButtonRole::Cancel : DialogButtonRole::Normal);
            raw->AddButton(buttons[static_cast<size_t>(i)], [raw, result, i]
            {
                auto cb = std::move(*result);
                *result = nullptr;
                raw->Close();
                if (cb)
                    cb(i);
            }, role);
        }
        if (count == 1)   // 只有一个按钮时它既是默认也是取消
            raw->SetOnClosed([result] { if (*result) { auto cb = std::move(*result); *result = nullptr; cb(0); } });

        ctx.OpenPopup(std::move(dialog));
        return raw;
    }
}
