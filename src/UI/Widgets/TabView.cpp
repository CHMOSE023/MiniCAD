#include "Widgets/TabView.h"
#include "Widgets/Menu.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr float kTabMin    = 120.0f;    // 缩窄的下限；再放不下就滚动标签条
        constexpr float kTabMax    = 220.0f;
        constexpr float kTabPad    = 14.0f;
        constexpr float kCloseW    = 20.0f;
        constexpr float kButtonW   = 28.0f;
        constexpr float kDragStart = 5.0f;

        TextParams TabText(ColorRef color)
        {
            TextParams p;
            p.size     = Theme::FontSize;
            p.color    = color;
            p.vAlign   = TextAlign::Center;
            p.ellipsis = true;
            return p;
        }
    }

    TabView::TabView()
    {
        SetFocusable(true);
    }

    // =========================================================
    // 标签管理
    // =========================================================
    int TabView::AddTab(std::string title, std::unique_ptr<Node> content, bool closable)
    {
        Tab tab;
        tab.title    = std::move(title);
        tab.closable = closable;
        if (content)
        {
            tab.content = AddChild(std::move(content));
            tab.content->SetVisible(false);
        }
        m_tabs.push_back(tab);
        const int index = static_cast<int>(m_tabs.size()) - 1;
        if (m_selected < 0)
            SetSelected(index);
        InvalidateLayout();
        return index;
    }

    void TabView::RemoveTab(int index)
    {
        if (index < 0 || index >= GetTabCount())
            return;
        Node* content = m_tabs[static_cast<size_t>(index)].content;
        m_tabs.erase(m_tabs.begin() + index);
        if (content)
            RemoveChild(content);

        // 关闭选中页后选中右边一页（没有就选左边）
        if (m_tabs.empty())
        {
            m_selected = -1;
            if (m_onSelectionChanged)
                m_onSelectionChanged(-1);
        }
        else if (index == m_selected)
        {
            m_selected = -1;
            SetSelected(std::min(index, GetTabCount() - 1));
        }
        else if (index < m_selected)
        {
            --m_selected;
        }
        m_hover = -1;
        InvalidateLayout();
    }

    void TabView::MoveTab(int from, int to)
    {
        const int n = GetTabCount();
        if (from < 0 || from >= n || to < 0 || to >= n || from == to)
            return;
        const Tab tab = m_tabs[static_cast<size_t>(from)];
        m_tabs.erase(m_tabs.begin() + from);
        m_tabs.insert(m_tabs.begin() + to, tab);

        // 选中页跟着移动
        if (m_selected == from)
            m_selected = to;
        else if (from < m_selected && to >= m_selected)
            --m_selected;
        else if (from > m_selected && to <= m_selected)
            ++m_selected;

        InvalidateLayout();
        if (m_onTabMoved)
            m_onTabMoved(from, to);
    }

    Node* TabView::GetTabContent(int index) const
    {
        return (index >= 0 && index < GetTabCount()) ? m_tabs[static_cast<size_t>(index)].content : nullptr;
    }

    int TabView::IndexOf(const Node* content) const
    {
        for (int i = 0; content && i < GetTabCount(); ++i)
            if (m_tabs[static_cast<size_t>(i)].content == content)
                return i;
        return -1;
    }

    int TabView::FindTabData(uintptr_t data) const
    {
        for (int i = 0; i < GetTabCount(); ++i)
            if (m_tabs[static_cast<size_t>(i)].data == data)
                return i;
        return -1;
    }

    const std::string& TabView::GetTabTitle(int index) const
    {
        static const std::string empty;
        return (index >= 0 && index < GetTabCount()) ? m_tabs[static_cast<size_t>(index)].title : empty;
    }

    void TabView::SetTabTitle(int index, std::string title)
    {
        if (index < 0 || index >= GetTabCount())
            return;
        m_tabs[static_cast<size_t>(index)].title = std::move(title);
        InvalidateLayout();
    }

    void TabView::SetTabModified(int index, bool modified)
    {
        if (index < 0 || index >= GetTabCount() || m_tabs[static_cast<size_t>(index)].modified == modified)
            return;
        m_tabs[static_cast<size_t>(index)].modified = modified;
        Invalidate();
    }

    void TabView::SetSelected(int index)
    {
        if (index < 0 || index >= GetTabCount() || index == m_selected)
            return;
        m_selected = index;
        for (int i = 0; i < GetTabCount(); ++i)
        {
            if (Node* c = m_tabs[static_cast<size_t>(i)].content)
                c->SetVisible(i == index);
        }
        EnsureSelectedVisible();
        Invalidate();
        if (m_onSelectionChanged)
        {
            auto cb = m_onSelectionChanged;
            cb(index);
        }
    }

    void TabView::RequestClose(int index)
    {
        if (index < 0 || index >= GetTabCount() || !m_tabs[static_cast<size_t>(index)].closable)
            return;
        if (m_onCloseRequested)
        {
            auto cb = m_onCloseRequested;
            cb(index);
        }
        else
        {
            RemoveTab(index);
        }
    }

    void TabView::CloseOthers(int keep)
    {
        // 从后往前，避免下标变化；调用方的 OnCloseRequested 可能拒绝关闭某些页
        Node* keepContent = GetTabContent(keep);
        for (int i = GetTabCount() - 1; i >= 0; --i)
            if (GetTabContent(i) != keepContent)
                RequestClose(i);
    }

    void TabView::CloseToRight(int index)
    {
        for (int i = GetTabCount() - 1; i > index; --i)
            RequestClose(i);
    }

    // =========================================================
    // 布局
    // =========================================================
    Vec2 TabView::MeasureContent(Vec2 available)
    {
        Vec2 content;
        if (Node* c = GetTabContent(m_selected))
            content = c->Measure({ available.x, available.y - kStripHeight });
        return { content.x, content.y + kStripHeight };
    }

    void TabView::LayoutTabs()
    {
        UIContext*  ctx   = GetContext();
        const float width = GetSize().x;

        // 先按文字求理想宽度
        std::vector<float> widths;
        float total = 0.0f;
        for (const Tab& t : m_tabs)
        {
            // 多留 4 像素：标签背景比标签格子窄 2 像素，文字刚好放下时不应出现省略号
            float w = kTabPad * 2.0f + 4.0f + (t.closable ? kCloseW : 0.0f);
            if (ctx)
                w += ctx->GetTextSystem().Measure(t.title, TabText(Theme::Text)).x;
            w = std::clamp(w, std::min(kTabMin, w), kTabMax);
            widths.push_back(w);
            total += w;
        }

        // 可用宽度：去掉"+"按钮；缩窄到下限还放不下时进入溢出模式（多出一个"⌄"按钮，标签条可滚动）
        float avail = width - 8.0f - (m_showNewButton ? kButtonW + 4.0f : 0.0f);
        float minTotal = 0.0f;
        for (float w : widths)
            minTotal += std::min(w, kTabMin);
        m_overflow = minTotal > avail;
        if (m_overflow)
            avail -= kButtonW + 4.0f;

        float scale = 1.0f;
        if (total > avail && total > 0.0f)
            scale = avail / total;

        float x = 4.0f;
        for (size_t i = 0; i < m_tabs.size(); ++i)
        {
            float w = std::floor(widths[i] * scale);
            w = std::max(w, std::min(kTabMin, widths[i]));
            m_tabs[i].x0 = x;
            m_tabs[i].x1 = x + w;
            x += w;
        }
        m_stripRight = 4.0f + avail;
        if (!m_overflow)
            m_stripScroll = 0.0f;
        EnsureSelectedVisible();
    }

    void TabView::EnsureSelectedVisible()
    {
        if (!m_overflow || m_selected < 0 || m_selected >= GetTabCount())
            return;
        const Tab& t = m_tabs[static_cast<size_t>(m_selected)];
        const float visibleW = m_stripRight - 4.0f;
        if (t.x1 - m_stripScroll > m_stripRight) m_stripScroll = t.x1 - m_stripRight;
        if (t.x0 - m_stripScroll < 4.0f)         m_stripScroll = t.x0 - 4.0f;
        const float maxScroll = std::max(0.0f, m_tabs.back().x1 - 4.0f - visibleW);
        m_stripScroll = std::clamp(m_stripScroll, 0.0f, maxScroll);
    }

    void TabView::OnLayout()
    {
        LayoutTabs();
        const Vec2 size = GetSize();
        const Rect content{ 0.0f, kStripHeight, size.x, std::max(size.y, kStripHeight) };
        for (const Tab& t : m_tabs)
        {
            if (t.content)
                t.content->SetBounds(content);
        }
    }

    Rect TabView::TabRect(const Tab& tab) const
    {
        return Rect{ tab.x0 - m_stripScroll, 4.0f, tab.x1 - m_stripScroll - 2.0f, kStripHeight };
    }

    Rect TabView::CloseRect(const Tab& tab) const
    {
        const Rect r = TabRect(tab);
        const float cy = kStripHeight * 0.5f + 2.0f;
        return Rect{ r.max.x - kCloseW - 4.0f, cy - 9.0f, r.max.x - 4.0f, cy + 9.0f };
    }

    Rect TabView::NewButtonRect() const
    {
        // 紧跟在最后一个标签后面；溢出时固定在标签区右侧
        float x = m_tabs.empty() ? 4.0f : m_tabs.back().x1 - m_stripScroll + 2.0f;
        x = std::min(x, m_stripRight + 2.0f);
        return Rect{ x, 6.0f, x + kButtonW, kStripHeight - 4.0f };
    }

    Rect TabView::OverflowButtonRect() const
    {
        const float x = GetSize().x - 4.0f - kButtonW;
        return Rect{ x, 6.0f, x + kButtonW, kStripHeight - 4.0f };
    }

    TabView::Hit TabView::HitAt(Vec2 local, int& tab) const
    {
        tab = -1;
        if (local.y < 0.0f || local.y >= kStripHeight)
            return Hit::None;
        if (m_overflow && OverflowButtonRect().Contains(local))
            return Hit::OverflowButton;
        if (m_showNewButton && NewButtonRect().Contains(local))
            return Hit::NewButton;
        if (local.x > m_stripRight + 2.0f)
            return Hit::None;
        for (int i = 0; i < GetTabCount(); ++i)
        {
            const Tab& t = m_tabs[static_cast<size_t>(i)];
            if (local.x >= t.x0 - m_stripScroll && local.x < t.x1 - m_stripScroll)
            {
                tab = i;
                return (t.closable && CloseRect(t).Contains(local)) ? Hit::Close : Hit::Tab;
            }
        }
        return Hit::None;
    }

    // =========================================================
    // 绘制
    // =========================================================
    void TabView::OnPaint(DrawList& dl, const Rect& r)
    {
        const Rect strip{ r.min.x, r.min.y, r.max.x, r.min.y + kStripHeight };
        dl.AddRectFilled(strip, Theme::Panel);
        dl.AddRectFilled(Rect{ strip.min.x, strip.max.y - 1.0f, strip.max.x, strip.max.y }, Theme::BorderSubtle);

        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        const bool focusRing = HasFocus() && ctx->IsFocusVisible();

        // 标签区裁剪：溢出滚动时不画到按钮上
        dl.PushClipRect(Rect{ strip.min.x, strip.min.y, r.min.x + m_stripRight + 2.0f, strip.max.y });
        for (int i = 0; i < GetTabCount(); ++i)
        {
            const Tab& t   = m_tabs[static_cast<size_t>(i)];
            const bool sel = i == m_selected;
            const Rect lr  = TabRect(t);
            const Rect tab{ lr.min + r.min, Vec2{ lr.max.x, lr.max.y - (sel ? 0.0f : 1.0f) } + r.min };

            if (sel)
            {
                dl.AddRectFilled(tab, Theme::Background, 5.0f);
                dl.AddRectFilled(Rect{ tab.min.x, tab.max.y - 5.0f, tab.max.x, tab.max.y }, Theme::Background);
                dl.AddRectFilled(Rect{ tab.min.x + 4.0f, tab.min.y, tab.max.x - 4.0f, tab.min.y + 2.0f }, Theme::Accent, 1.0f);
            }
            else if (i == m_hover)
            {
                dl.AddRectFilled(tab, Theme::RowHover, 5.0f);
            }
            if (m_dragging && i == m_pressTab)
                dl.AddRect(tab, Theme::Accent, 5.0f, 1.0f);
            if (focusRing && sel)
                dl.AddRect(tab.Deflated(1.0f), Theme::Accent, 5.0f, 1.0f);

            const float textRight = t.closable ? tab.max.x - kCloseW - 6.0f : tab.max.x - kTabPad;
            ctx->GetTextSystem().Draw(dl, Rect{ tab.min.x + kTabPad, tab.min.y, textRight, tab.max.y }, t.title,
                                      TabText(sel ? Theme::Text : Theme::TextDim));

            if (t.closable)
            {
                const Rect lc = CloseRect(t);
                const Rect cr{ lc.min + r.min, lc.max + r.min };
                const bool hoverTab   = i == m_hover;
                const bool hoverClose = hoverTab && m_hoverHit == Hit::Close;
                if (hoverClose)
                    dl.AddRectFilled(cr, Theme::ControlHover, 4.0f);
                // 未保存：平时显示圆点，悬停到标签上时显示 ×（与 VS Code 一致）
                if (t.modified && !hoverTab)
                    dl.AddCircleFilled(cr.Center(), 4.0f, sel ? Theme::Text : Theme::TextDim);
                else if (sel || hoverTab)
                    dl.AddCross(cr.Center(), 4.0f, Theme::TextDim, 1.4f);
            }
        }
        dl.PopClipRect();

        auto drawButton = [&](const Rect& lr, Hit hit)
        {
            const Rect b{ lr.min + r.min, lr.max + r.min };
            if (m_hoverHit == hit)
                dl.AddRectFilled(b, Theme::ControlHover, 4.0f);
            const Vec2 c = b.Center();
            if (hit == Hit::NewButton)
            {
                dl.AddLine({ c.x - 5.0f, c.y }, { c.x + 5.0f, c.y }, Theme::TextDim, 1.5f);
                dl.AddLine({ c.x, c.y - 5.0f }, { c.x, c.y + 5.0f }, Theme::TextDim, 1.5f);
            }
            else
            {
                const Vec2 chevron[3] = { { c.x - 4.0f, c.y - 2.0f }, { c.x, c.y + 2.0f }, { c.x + 4.0f, c.y - 2.0f } };
                dl.AddPolyline(chevron, Theme::TextDim, false, 1.5f);
            }
        };
        if (m_showNewButton)
            drawButton(NewButtonRect(), Hit::NewButton);
        if (m_overflow)
            drawButton(OverflowButtonRect(), Hit::OverflowButton);
    }

    // =========================================================
    // 输入
    // =========================================================
    void TabView::ShowOverflowMenu()
    {
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        std::vector<MenuItem> items;
        for (int i = 0; i < GetTabCount(); ++i)
            items.push_back(MenuItem(m_tabs[static_cast<size_t>(i)].title, [this, i] { SetSelected(i); }).Checked(i == m_selected));
        const Rect b = OverflowButtonRect();
        const Vec2 origin = GetScreenBounds().min;
        ShowContextMenu(*ctx, std::move(items), { origin.x + b.max.x - 200.0f, origin.y + b.max.y + 2.0f });
    }

    bool TabView::OnContextMenu(Vec2 windowPos)
    {
        UIContext* ctx = GetContext();
        const int tab = m_contextTab >= 0 ? m_contextTab : m_selected;
        m_contextTab = -1;
        if (!ctx || tab < 0)
            return Node::OnContextMenu(windowPos);

        const bool closable = m_tabs[static_cast<size_t>(tab)].closable;
        std::vector<MenuItem> items =
        {
            MenuItem("关闭(&C)", [this, tab] { RequestClose(tab); }, "Ctrl+W").Enabled(closable),
            MenuItem("关闭其他(&O)", [this, tab] { CloseOthers(tab); }).Enabled(GetTabCount() > 1),
            MenuItem("关闭右侧(&R)", [this, tab] { CloseToRight(tab); }).Enabled(tab + 1 < GetTabCount()),
            MenuItem("全部关闭(&A)", [this] { for (int i = GetTabCount() - 1; i >= 0; --i) RequestClose(i); }),
        };
        if (m_contextExtra)
        {
            std::vector<MenuItem> extra = m_contextExtra(tab);
            if (!extra.empty())
            {
                items.push_back(MenuItem::Separator());
                items.insert(items.end(), extra.begin(), extra.end());
            }
        }
        ShowContextMenu(*ctx, std::move(items), windowPos);
        return true;
    }

    void TabView::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;
        int tab = -1;
        const Hit hit = e.phase == EventPhase::Target ? HitAt(e.localPosition, tab) : Hit::None;

        switch (e.type)
        {
        case PointerEventType::Move:
            if (m_pressTab >= 0 && HasCapture())
            {
                if (!m_dragging && std::abs(e.localPosition.x - m_pressX) > kDragStart)
                    m_dragging = true;
                if (m_dragging)
                {
                    // 越过相邻标签的中线就交换位置
                    const float x = e.localPosition.x + m_stripScroll;
                    const Tab& cur = m_tabs[static_cast<size_t>(m_pressTab)];
                    if (m_pressTab > 0)
                    {
                        const Tab& left = m_tabs[static_cast<size_t>(m_pressTab - 1)];
                        if (x < (left.x0 + left.x1) * 0.5f) { MoveTab(m_pressTab, m_pressTab - 1); --m_pressTab; LayoutTabs(); }
                    }
                    if (m_pressTab + 1 < GetTabCount() && x > cur.x1)
                    {
                        const Tab& right = m_tabs[static_cast<size_t>(m_pressTab + 1)];
                        if (x > (right.x0 + right.x1) * 0.5f) { MoveTab(m_pressTab, m_pressTab + 1); ++m_pressTab; LayoutTabs(); }
                    }
                    Invalidate();
                }
                e.handled = true;
                break;
            }
            if (tab != m_hover || hit != m_hoverHit)
            {
                m_hover    = tab;
                m_hoverHit = hit;
                Invalidate();
            }
            break;

        case PointerEventType::Leave:
            m_hover    = -1;
            m_hoverHit = Hit::None;
            Invalidate();
            break;

        case PointerEventType::Down:
            if (hit == Hit::None)
                break;
            e.handled = true;
            if (e.button == MouseButton::Right)
            {
                m_contextTab = tab;
                if (tab >= 0)
                    SetSelected(tab);
                break;
            }
            if (e.button == MouseButton::Middle)
            {
                if (tab >= 0)
                    RequestClose(tab);
                break;
            }
            if (e.button != MouseButton::Left)
                break;
            if (hit == Hit::NewButton)
            {
                if (m_onNewTabRequested)
                {
                    auto cb = m_onNewTabRequested;
                    cb();
                }
            }
            else if (hit == Hit::OverflowButton)
            {
                ShowOverflowMenu();
            }
            else if (hit == Hit::Close)
            {
                RequestClose(tab);
            }
            else if (tab >= 0)
            {
                SetSelected(tab);
                m_pressTab = tab;
                m_pressX   = e.localPosition.x;
                m_dragging = false;
                GetContext()->SetCapture(this);
            }
            break;

        case PointerEventType::Up:
            if (m_pressTab >= 0)
            {
                m_pressTab = -1;
                m_dragging = false;
                GetContext()->ReleaseCapture();
                e.handled = true;
                Invalidate();
            }
            break;

        case PointerEventType::Wheel:
            // 溢出时在标签条上滚轮横向滚动
            if (m_overflow && hit != Hit::None)
            {
                const float maxScroll = std::max(0.0f, m_tabs.back().x1 - m_stripRight);
                m_stripScroll = std::clamp(m_stripScroll - (e.wheelDelta.y + e.wheelDelta.x) * 60.0f, 0.0f, maxScroll);
                e.handled = true;
                Invalidate();
            }
            break;

        case PointerEventType::Cancel:
            m_pressTab = -1;
            m_dragging = false;
            break;

        default:
            break;
        }
    }

    void TabView::OnKeyEvent(KeyEvent& e)
    {
        if (e.type != KeyEventType::Down || GetTabCount() == 0 || e.phase == EventPhase::Capture)
            return;
        const int n = GetTabCount();

        // 以下在焦点位于任何子控件时都有效（冒泡上来）
        if (e.key == Key::Tab && e.Ctrl() && !e.Alt())
        {
            SetSelected((m_selected + (e.Shift() ? n - 1 : 1)) % n);
            e.handled = true;
            return;
        }
        if (e.Ctrl() && !e.Shift() && !e.Alt() && (e.key == Key::W || e.key == Key::F4))
        {
            RequestClose(m_selected);
            e.handled = true;
            return;
        }
        // 标签条自身有焦点时 ← → 切换
        if (e.phase == EventPhase::Target && e.modifiers == 0 && (e.key == Key::Left || e.key == Key::Right))
        {
            SetSelected((m_selected + (e.key == Key::Left ? n - 1 : 1)) % n);
            e.handled = true;
        }
    }
}
