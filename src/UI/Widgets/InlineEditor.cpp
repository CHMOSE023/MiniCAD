#include "Widgets/InlineEditor.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"

namespace MiniGUI
{
    void InlineEditor::Begin(Node* host, Node* owner, const Rect& rect, const std::string& text, CommitFunc commit)
    {
        if (m_box)
            Commit();

        m_host   = host;
        m_owner  = owner;
        m_commit = std::move(commit);

        auto box = std::make_unique<TextBox>(text);
        TextBoxStyle style;
        style.padding     = Edges::Symmetric(5.0f, 2.0f);
        style.rounding    = 2.0f;
        style.border      = Theme::Accent;
        style.borderHover = Theme::Accent;
        box->SetStyle(style);

        // 绝对定位到行的位置：内容节点的 Flexbox 布局会按 left/top 放置它
        LayoutStyle s;
        s.position = PositionType::Absolute;
        s.left     = rect.min.x;
        s.top      = rect.min.y;
        s.width    = rect.Width();
        s.height   = rect.Height();
        box->SetLayoutStyle(s);

        m_box = static_cast<TextBox*>(host->AddChild(std::move(box)));
        m_box->SetOnSubmit([this](const std::string&) { Commit(); });
        m_box->SetKeyPreview([this](KeyEvent& e)
        {
            if (e.key == Key::Escape && e.modifiers == 0)
            {
                Cancel();
                return true;
            }
            return false;
        });
        m_box->SetOnFocusChanged([this](bool focused)
        {
            // 点到别处：提交（结束过程中输入框被移除导致的失焦除外）。
            // 此时正处于焦点切换过程中，不能再抢回焦点：被拒绝就直接取消
            if (!focused && !m_ending && m_box)
            {
                const std::string text = m_box->GetText();
                if (m_commit && !m_commit(text))
                    Cancel();
                else if (m_box)
                    End();
            }
        });

        if (UIContext* ctx = host->GetContext())
            ctx->Update();
        m_box->Focus();
        m_box->SelectAll();
    }

    bool InlineEditor::Commit()
    {
        if (!m_box || m_ending)
            return false;
        const std::string text = m_box->GetText();
        if (m_commit && !m_commit(text))
        {
            // 拒绝：继续编辑（输入框可能因为失焦而提交，重新聚焦）
            if (m_box)
            {
                m_box->Focus();
                m_box->SelectAll();
            }
            return false;
        }
        End();
        return true;
    }

    void InlineEditor::Cancel()
    {
        if (m_box)
            End();
    }

    void InlineEditor::End()
    {
        m_ending = true;
        TextBox* box = m_box;
        m_box = nullptr;
        UIContext* ctx = box->GetContext();
        const bool hadFocus = box->HasFocus();

        std::unique_ptr<Node> removed = m_host->RemoveChild(box);
        if (ctx)
            ctx->DeferDelete(std::move(removed));   // 可能正在输入框自己的回调里
        if (hadFocus && m_owner)
            m_owner->Focus();
        m_ending = false;
    }
}
