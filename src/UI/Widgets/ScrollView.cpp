#include "Widgets/ScrollView.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace MiniGUI
{
    namespace
    {
        constexpr float kMinThumb = 24.0f;
    }

    ScrollView::ScrollView()
    {
        SetClipChildren(true);
    }

    Node* ScrollView::SetContent(std::unique_ptr<Node> content)
    {
        if (m_content)
            RemoveChild(m_content);
        m_content = AddChild(std::move(content));
        m_scroll  = {};
        return m_content;
    }

    void ScrollView::SetScrollAxes(bool horizontal, bool vertical)
    {
        m_hEnabled = horizontal;
        m_vEnabled = vertical;
        InvalidateLayout();
    }

    Vec2 ScrollView::GetMaxScroll() const
    {
        return { std::max(0.0f, m_contentSize.x - m_viewport.x), std::max(0.0f, m_contentSize.y - m_viewport.y) };
    }

    // =========================================================
    // 布局
    // =========================================================
    void ScrollView::SetHeaderHeight(float h)
    {
        m_headerHeight = h;
        InvalidateLayout();
    }

    Vec2 ScrollView::MeasureContent(Vec2 available)
    {
        if (!m_content)
            return { 0.0f, m_headerHeight };
        available.y -= m_headerHeight;
        const Vec2 m = m_content->Measure({ m_hEnabled ? kInfinity : available.x, m_vEnabled ? kInfinity : available.y });
        const bool needV = m_vEnabled && m.y > available.y;
        const float w = m_hEnabled ? std::min(m.x, available.x) : m.x + (needV ? kBarSize : 0.0f);
        const float h = m_vEnabled ? std::min(m.y, available.y) : m.y;
        return { w, h + m_headerHeight };
    }

    void ScrollView::OnLayout()
    {
        const Vec2 size{ GetSize().x, std::max(0.0f, GetSize().y - m_headerHeight) };
        if (!m_content)
        {
            m_viewport = size;
            m_contentSize = size;
            return;
        }

        // 先按"无滚动条"测量；出现竖直滚动条后内容宽度要让出滚动条，重新测量一次
        Vec2 m = m_content->Measure({ m_hEnabled ? kInfinity : size.x, m_vEnabled ? kInfinity : size.y });
        m_showV = m_vEnabled && m.y > size.y;
        m_showH = m_hEnabled && m.x > size.x - (m_showV ? kBarSize : 0.0f);
        if (m_showH && m_vEnabled && !m_showV && m.y > size.y - kBarSize)
            m_showV = true;
        if (m_showV && !m_hEnabled)
            m = m_content->Measure({ size.x - kBarSize, kInfinity });

        m_viewport = { std::max(0.0f, size.x - (m_showV ? kBarSize : 0.0f)),
                       std::max(0.0f, size.y - (m_showH ? kBarSize : 0.0f)) };
        m_contentSize = { m_hEnabled ? std::max(m.x, m_viewport.x) : m_viewport.x,
                          m_vEnabled ? std::max(m.y, m_viewport.y) : m_viewport.y };

        const Vec2 maxScroll = GetMaxScroll();
        m_scroll = { std::clamp(m_scroll.x, 0.0f, maxScroll.x), std::clamp(m_scroll.y, 0.0f, maxScroll.y) };
        ApplyScroll();
    }

    void ScrollView::ApplyScroll()
    {
        if (!m_content)
            return;
        // 滚动只改变内容的位置，尺寸不变，不会触发内容重新布局
        const Vec2 origin{ -std::round(m_scroll.x), m_headerHeight - std::round(m_scroll.y) };
        m_content->SetBounds(Rect{ origin, origin + m_contentSize });
    }

    void ScrollView::ScrollTo(Vec2 offset)
    {
        const Vec2 maxScroll = GetMaxScroll();
        offset = { std::clamp(offset.x, 0.0f, maxScroll.x), std::clamp(offset.y, 0.0f, maxScroll.y) };
        if (offset == m_scroll)
            return;
        m_scroll = offset;
        ApplyScroll();
        Invalidate();
        if (m_onScrolled)
            m_onScrolled(m_scroll);
    }

    void ScrollView::ScrollIntoView(const Rect& r)
    {
        Vec2 s = m_scroll;
        if (r.max.y > s.y + m_viewport.y) s.y = r.max.y - m_viewport.y;
        if (r.min.y < s.y)                s.y = r.min.y;
        if (r.max.x > s.x + m_viewport.x) s.x = r.max.x - m_viewport.x;
        if (r.min.x < s.x)                s.x = r.min.x;
        ScrollTo(s);
    }

    // =========================================================
    // 滚动条
    // =========================================================
    bool ScrollView::BarGeometryFor(Axis axis, BarGeometry& out) const
    {
        const Vec2 size = GetSize();
        if (axis == Axis::Vertical && m_showV)
        {
            out.track = Rect{ size.x - kBarSize, m_headerHeight, size.x, m_headerHeight + m_viewport.y };
            const float len  = out.track.Height();
            const float view = m_viewport.y, content = m_contentSize.y;
            const float th   = std::clamp(len * view / std::max(content, 1.0f), std::min(kMinThumb, len), len);
            const float maxS = std::max(content - view, 1.0f);
            const float y    = out.track.min.y + (len - th) * (m_scroll.y / maxS);   // 轨道已从表头下方开始
            out.thumb = Rect{ out.track.min.x + 2.0f, y + 2.0f, out.track.max.x - 2.0f, y + th - 2.0f };
            return true;
        }
        if (axis == Axis::Horizontal && m_showH)
        {
            out.track = Rect{ 0.0f, size.y - kBarSize, m_viewport.x, size.y };
            const float len  = out.track.Width();
            const float view = m_viewport.x, content = m_contentSize.x;
            const float tw   = std::clamp(len * view / std::max(content, 1.0f), std::min(kMinThumb, len), len);
            const float maxS = std::max(content - view, 1.0f);
            const float x    = out.track.min.x + (len - tw) * (m_scroll.x / maxS);
            out.thumb = Rect{ x + 2.0f, out.track.min.y + 2.0f, x + tw - 2.0f, out.track.max.y - 2.0f };
            return true;
        }
        return false;
    }

    ScrollView::Axis ScrollView::BarAt(Vec2 local) const
    {
        BarGeometry g;
        if (BarGeometryFor(Axis::Vertical, g) && g.track.Contains(local))
            return Axis::Vertical;
        if (BarGeometryFor(Axis::Horizontal, g) && g.track.Contains(local))
            return Axis::Horizontal;
        return Axis::None;
    }

    bool ScrollView::InterceptsHit(Vec2 local) const
    {
        // 滚动条区域由自己处理，不交给下面的内容
        return BarAt(local) != Axis::None;
    }

    void ScrollView::OnPaintOverlay(DrawList& dl, const Rect& r)
    {
        for (Axis axis : { Axis::Vertical, Axis::Horizontal })
        {
            BarGeometry g;
            if (!BarGeometryFor(axis, g))
                continue;
            const Rect track{ g.track.min + r.min, g.track.max + r.min };
            const Rect thumb{ g.thumb.min + r.min, g.thumb.max + r.min };
            const bool hot = m_hoverBar == axis || m_dragBar == axis;
            dl.AddRectFilled(track, ColorRef(ThemeColor::Background, 200));
            dl.AddRectFilled(thumb, hot ? Theme::ScrollThumbHover : Theme::ScrollThumb, 3.0f);
        }
        if (m_showV && m_showH)   // 右下角
        {
            const Vec2 size = GetSize();
            dl.AddRectFilled(Rect{ r.min.x + size.x - kBarSize, r.min.y + size.y - kBarSize, r.max.x, r.max.y }, ColorRef(ThemeColor::Background, 200));
        }
    }

    void ScrollView::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;

        switch (e.type)
        {
        case PointerEventType::Wheel:
        {
            // Shift+滚轮或只能水平滚动时，滚轮作用在水平方向
            const bool horizontal = e.wheelDelta.x != 0.0f || ((e.HasModifier(ModifierKey::Shift) || !m_vEnabled) && m_hEnabled);
            const float amount    = (e.wheelDelta.x != 0.0f ? e.wheelDelta.x : e.wheelDelta.y) * m_wheelStep;
            const Vec2  before    = m_scroll;
            if (horizontal)
                ScrollTo({ m_scroll.x - amount, m_scroll.y });
            else if (m_vEnabled)
                ScrollTo({ m_scroll.x, m_scroll.y - amount });
            // 真正滚动了才算处理；已经到头时让外层滚动视图接着滚
            if (m_scroll != before)
                e.handled = true;
            break;
        }

        case PointerEventType::Move:
        {
            if (m_dragBar != Axis::None)
            {
                BarGeometry g;
                if (!BarGeometryFor(m_dragBar, g))
                    break;
                const bool  v       = m_dragBar == Axis::Vertical;
                const float trackL  = v ? g.track.Height() : g.track.Width();
                const float thumbL  = v ? g.thumb.Height() + 4.0f : g.thumb.Width() + 4.0f;
                const float maxS    = v ? GetMaxScroll().y : GetMaxScroll().x;
                const float mouse   = v ? e.localPosition.y : e.localPosition.x;
                const float travel  = std::max(trackL - thumbL, 1.0f);
                const float scroll  = m_dragStartScroll + (mouse - m_dragStartMouse) * maxS / travel;
                ScrollTo(v ? Vec2{ m_scroll.x, scroll } : Vec2{ scroll, m_scroll.y });
                e.handled = true;
                break;
            }
            const Axis hover = e.phase == EventPhase::Target ? BarAt(e.localPosition) : Axis::None;
            if (hover != m_hoverBar)
            {
                m_hoverBar = hover;
                Invalidate();
            }
            break;
        }

        case PointerEventType::Leave:
            if (m_hoverBar != Axis::None)
            {
                m_hoverBar = Axis::None;
                Invalidate();
            }
            break;

        case PointerEventType::Down:
        {
            if (e.phase != EventPhase::Target || e.button != MouseButton::Left)
                break;
            const Axis axis = BarAt(e.localPosition);
            BarGeometry g;
            if (axis == Axis::None || !BarGeometryFor(axis, g))
                break;
            e.handled = true;

            const bool v = axis == Axis::Vertical;
            if (g.thumb.Deflated(-2.0f).Contains(e.localPosition))
            {
                m_dragBar         = axis;
                m_dragStartMouse  = v ? e.localPosition.y : e.localPosition.x;
                m_dragStartScroll = v ? m_scroll.y : m_scroll.x;
                GetContext()->SetCapture(this);
            }
            else
            {
                // 点在轨道上：朝点击方向翻一页
                if (v)
                    ScrollBy({ 0.0f, (e.localPosition.y < g.thumb.min.y ? -1.0f : 1.0f) * m_viewport.y * 0.9f });
                else
                    ScrollBy({ (e.localPosition.x < g.thumb.min.x ? -1.0f : 1.0f) * m_viewport.x * 0.9f, 0.0f });
            }
            Invalidate();
            break;
        }

        case PointerEventType::Up:
            if (m_dragBar != Axis::None)
            {
                m_dragBar = Axis::None;
                GetContext()->ReleaseCapture();
                e.handled = true;
                Invalidate();
            }
            break;

        case PointerEventType::Cancel:
            m_dragBar = Axis::None;
            break;

        default:
            break;
        }
    }
}
