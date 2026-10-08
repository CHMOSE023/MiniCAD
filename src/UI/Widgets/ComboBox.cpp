#include "Widgets/ComboBox.h"
#include "Widgets/ListView.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>

namespace MiniGUI
{
    namespace
    {
        // 下拉列表弹层：里面是一个 ListView，打开后焦点在列表上
        class ComboPopup : public Popup
        {
        public:
            explicit ComboPopup(ListView* list) : m_list(list) {}

        protected:
            void OnOpened() override
            {
                m_list->Focus();
            }

        private:
            ListView* m_list;
        };
    }

    ComboBox::ComboBox(std::vector<std::string> items, int selected)
        : m_items(std::move(items))
        , m_selected(selected)
    {
        SetFocusable(true);
    }

    ComboBox::~ComboBox() = default;

    void ComboBox::SetItems(std::vector<std::string> items)
    {
        Close();
        m_items = std::move(items);
        if (m_selected >= static_cast<int>(m_items.size()))
            m_selected = -1;
        InvalidateLayout();
    }

    void ComboBox::SetSelectedIndex(int index)
    {
        if (index < -1 || index >= static_cast<int>(m_items.size()))
            return;
        m_selected = index;
        Invalidate();
    }

    std::string ComboBox::GetSelectedText() const
    {
        return m_selected >= 0 ? m_items[static_cast<size_t>(m_selected)] : std::string{};
    }

    void ComboBox::Choose(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_items.size()) || index == m_selected)
            return;
        m_selected = index;
        Invalidate();
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb(index);
        }
    }

    // =========================================================
    // 展开 / 收起
    // =========================================================
    void ComboBox::Open()
    {
        UIContext* ctx = GetContext();
        if (!ctx || m_popup || m_items.empty())
            return;

        const Rect anchor = GetScreenBounds();
        auto list = std::make_unique<ListView>();
        ListView* listRaw = list.get();
        list->SetItems(m_items);
        if (m_itemPainter)
        {
            listRaw->SetItemPainter([this](DrawList& dl, const Rect& row, int index, bool)
            {
                m_itemPainter(dl, Rect{ row.min.x + 8.0f, row.min.y, row.max.x - 8.0f, row.max.y }, index, true);
            });
        }
        {
            LayoutStyle s;
            s.width     = std::max(anchor.Width(), m_dropDownWidth) - 8.0f;      // 弹层内边距各 4
            s.maxHeight = listRaw->GetRowHeight() * static_cast<float>(std::min<int>(m_maxVisible, static_cast<int>(m_items.size())));
            s.height    = s.maxHeight;
            list->SetLayoutStyle(s);
        }

        auto popup = std::make_unique<ComboPopup>(listRaw);
        popup->AddChild(std::move(list));
        popup->SetAnchor(anchor, PopupPlacement::Below);
        popup->SetOwner(this);          // 再点下拉框本身由下拉框切换
        Popup* raw = popup.get();
        raw->SetOnClosed([this, raw]
        {
            if (m_popup == raw)
            {
                m_popup = nullptr;
                Invalidate();
            }
        });

        listRaw->SetOnItemClicked([this](int i) { Choose(i); Close(); });
        listRaw->SetOnItemActivated([this](int i) { Choose(i); Close(); });

        ctx->OpenPopup(std::move(popup));
        m_popup = raw;
        if (m_selected >= 0)
            listRaw->SetCurrent(m_selected);
        Invalidate();
    }

    void ComboBox::Close()
    {
        if (m_popup)
            m_popup->Close();     // 焦点自动回到下拉框
    }

    // =========================================================
    // 绘制与输入
    // =========================================================
    Vec2 ComboBox::MeasureContent(Vec2 available)
    {
        (void)available;
        float w = 80.0f;
        if (UIContext* ctx = GetContext())
        {
            TextParams p;
            p.size = Theme::FontSize;
            for (const auto& item : m_items)
                w = std::max(w, ctx->GetTextSystem().Measure(item, p).x);
        }
        return { w + 16.0f + 24.0f, Theme::ControlH };
    }

    void ComboBox::OnPaint(DrawList& dl, const Rect& r)
    {
        const bool enabled = IsEnabled();
        const bool focused = HasFocus() || m_popup;
        dl.AddRectFilled(r, IsHovered() && enabled ? Theme::ControlHover : Theme::Control, Theme::Rounding);
        dl.AddRect(r, focused ? Theme::Accent : Theme::Border, Theme::Rounding, 1.0f);

        UIContext* ctx = GetContext();
        if (!ctx)
            return;

        if (m_itemPainter && m_selected >= 0)
        {
            const Rect box{ r.min.x + 8.0f, r.min.y, r.max.x - 26.0f, r.max.y };
            dl.PushClipRect(box);
            m_itemPainter(dl, box, m_selected, false);
            dl.PopClipRect();
        }

        TextParams p;
        p.size     = Theme::FontSize;
        p.vAlign   = TextAlign::Center;
        p.ellipsis = true;
        std::string text = GetSelectedText();
        p.color = enabled ? Theme::Text : Theme::TextDisabled;
        if (m_selected < 0)
        {
            text    = m_placeholder;
            p.color = Theme::TextDim;
        }
        if (!(m_itemPainter && m_selected >= 0))
            ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + 8.0f, r.min.y, r.max.x - 26.0f, r.max.y }, text, p);

        // 下拉箭头 ⌄
        const Vec2 c{ r.max.x - 14.0f, r.Center().y };
        const Vec2 chevron[3] = { { c.x - 4.0f, c.y - 2.0f }, { c.x, c.y + 2.0f }, { c.x + 4.0f, c.y - 2.0f } };
        dl.AddPolyline(chevron, enabled ? Theme::TextDim : Theme::TextDisabled, false, 1.5f);
    }

    void ComboBox::OnPointerEvent(PointerEvent& e)
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
                if (m_popup) Close(); else Open();
                e.handled = true;
            }
            break;
        case PointerEventType::Wheel:
            // 只在持有焦点时用滚轮切换，避免滚动面板时误改
            if (HasFocus() && !m_popup && e.wheelDelta.y != 0.0f)
            {
                Choose(std::clamp(m_selected + (e.wheelDelta.y > 0.0f ? -1 : 1), 0, static_cast<int>(m_items.size()) - 1));
                e.handled = true;
            }
            break;
        default:
            break;
        }
    }

    void ComboBox::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down)
            return;
        const int last = static_cast<int>(m_items.size()) - 1;
        if ((e.key == Key::Down && e.Alt()) || e.key == Key::F4 ||
            ((e.key == Key::Space || e.key == Key::Enter) && e.modifiers == 0))
        {
            Open();
            e.handled = true;
            return;
        }
        if (e.modifiers != 0 || last < 0)
            return;
        switch (e.key)
        {
        case Key::Up:   Choose(std::max(0, m_selected - 1));                   break;
        case Key::Down: Choose(std::min(last, m_selected < 0 ? 0 : m_selected + 1)); break;
        case Key::Home: Choose(0);                                              break;
        case Key::End:  Choose(last);                                           break;
        default: return;
        }
        e.handled = true;
    }
}
