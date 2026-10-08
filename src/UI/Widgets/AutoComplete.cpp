#include "Widgets/AutoComplete.h"
#include "Widgets/TextBox.h"
#include "Core/UIContext.h"
#include <algorithm>

namespace MiniGUI
{
    AutoComplete::AutoComplete(TextBox* box, Provider provider)
        : m_box(box)
        , m_provider(std::move(provider))
    {}

    AutoComplete::~AutoComplete()
    {
        Close();
    }

    int AutoComplete::GetCurrent() const
    {
        return m_list ? m_list->GetCurrent() : -1;
    }

    void AutoComplete::Close()
    {
        if (m_popup)
            m_popup->Close();
    }

    void AutoComplete::OnTextChanged()
    {
        if (!m_suppress)
            Refresh();
    }

    void AutoComplete::Refresh()
    {
        const std::string& text = m_box->GetText();
        m_items = (m_provider && !text.empty()) ? m_provider(text) : std::vector<ListItem>{};
        if (m_items.empty() || !m_box->HasFocus())
        {
            Close();
            return;
        }
        Show();
    }

    void AutoComplete::Show()
    {
        UIContext* ctx = m_box->GetContext();
        if (!ctx)
            return;

        const float rows = static_cast<float>(std::min<int>(m_maxVisible, static_cast<int>(m_items.size())));
        if (m_popup)
        {
            // 已打开：只更新列表内容和高度
            m_list->SetItems(m_items);
            m_list->EditLayoutStyle().height = m_list->GetRowHeight() * rows;
            if (m_selectFirst)
                m_list->SetCurrent(0);
            return;
        }

        const Rect anchor = m_box->GetScreenBounds();
        auto popup = std::make_unique<Popup>();
        popup->SetFocusable(false);             // 点击候选时焦点留在输入框
        popup->SetAnchor(anchor, PopupPlacement::Below);
        popup->SetOwner(m_box);

        m_list = popup->AddChild<ListView>();
        m_list->SetFocusable(false);
        m_list->SetItems(m_items);
        {
            LayoutStyle s;
            s.width  = std::max(anchor.Width() - 8.0f, 200.0f);
            s.height = m_list->GetRowHeight() * rows;
            m_list->SetLayoutStyle(s);
        }
        m_list->SetOnItemClicked([this](int i) { Accept(i); });
        if (m_selectFirst)
            m_list->SetCurrent(0);

        Popup* raw = popup.get();
        raw->SetOnClosed([this, raw]
        {
            if (m_popup == raw)
            {
                m_popup = nullptr;
                m_list  = nullptr;
            }
        });
        ctx->OpenPopup(std::move(popup), false);
        m_popup = raw;
    }

    void AutoComplete::Accept(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;
        const std::string text = m_items[static_cast<size_t>(index)].text;
        Close();

        // 替换文字时不重新查询，否则刚关闭的列表又会弹出来
        m_suppress = true;
        m_box->ReplaceAll(text);
        m_suppress = false;

        if (m_onAccepted)
        {
            auto cb = m_onAccepted;
            cb(text);
        }
    }

    bool AutoComplete::OnKey(KeyEvent& e)
    {
        if (!m_popup || e.type != KeyEventType::Down || e.modifiers != 0)
            return false;

        const int n   = static_cast<int>(m_items.size());
        const int cur = m_list->GetCurrent();
        switch (e.key)
        {
        case Key::Down:
            m_list->SetCurrent(cur < 0 ? 0 : std::min(cur + 1, n - 1));
            return true;
        case Key::Up:
            m_list->SetCurrent(cur < 0 ? n - 1 : std::max(cur - 1, 0));
            return true;
        case Key::Enter:
        case Key::Tab:
            if (cur < 0)
            {
                Close();
                return false;   // 没有选中候选：Enter 执行原文，Tab 切换焦点
            }
            Accept(cur);
            return true;
        case Key::Escape:
            Close();
            return true;
        default:
            return false;
        }
    }
}
