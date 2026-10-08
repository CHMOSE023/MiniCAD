#include "Core/Popup.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    Popup::Popup()
    {
        SetFocusable(true);   // 打开后持有焦点，接收 Esc 和方向键
        LayoutStyle s;
        s.padding = Edges::All(4.0f);
        SetLayoutStyle(s);
    }

    void Popup::SetAnchor(const Rect& windowRect, PopupPlacement placement)
    {
        m_anchor    = windowRect;
        m_placement = placement;
        RelayoutInLayer();
    }

    void Popup::SetPoint(Vec2 windowPos)
    {
        m_point     = windowPos;
        m_placement = PopupPlacement::AtPoint;
        RelayoutInLayer();
    }

    void Popup::SetCentered()
    {
        m_placement = PopupPlacement::Center;
        RelayoutInLayer();
    }

    void Popup::MoveTo(Vec2 windowPos)
    {
        m_point     = windowPos;
        m_placement = PopupPlacement::Manual;
        RelayoutInLayer();
    }

    void Popup::RelayoutInLayer()
    {
        // 位置由所在图层的 OnLayout 计算
        if (Node* layer = GetParent())
            layer->InvalidateLayout();
    }

    void Popup::Close()
    {
        if (UIContext* ctx = GetContext())
            ctx->ClosePopup(this);
    }

    void Popup::OnPaint(DrawList& dl, const Rect& r)
    {
        if (m_style.shadow)
        {
            // 柔和阴影：几层逐渐外扩、透明度很低的圆角矩形叠加，向下偏移 2 像素
            for (int i = 4; i >= 1; --i)
            {
                const float e = static_cast<float>(i) * 1.5f;
                dl.AddRectFilled(Rect{ r.min.x - e, r.min.y - e + 2.0f, r.max.x + e, r.max.y + e + 2.0f },
                                 ColorRef(ThemeColor::Shadow, 22), m_style.rounding + e);
            }
        }
        dl.AddRectFilled(r, m_style.background, m_style.rounding);
        dl.AddRect(r, m_style.border, m_style.rounding, 1.0f);
    }

    void Popup::OnKeyEvent(KeyEvent& e)
    {
        // 冒泡阶段处理：弹层内部的控件（例如输入框）先有机会处理 Esc
        if (e.type == KeyEventType::Down && e.key == Key::Escape && m_closeOnEscape &&
            (e.phase == EventPhase::Target || e.phase == EventPhase::Bubble))
        {
            e.handled = true;
            Close();
        }
    }

    // =========================================================
    // 放置
    // =========================================================
    Rect PlacePopup(Vec2 size, PopupPlacement placement, const Rect& anchor, Vec2 point, Vec2 display, float margin)
    {
        const float w = std::min(size.x, std::max(display.x - margin * 2.0f, 0.0f));
        const float h = std::min(size.y, std::max(display.y - margin * 2.0f, 0.0f));
        float x = 0.0f, y = 0.0f;

        switch (placement)
        {
        case PopupPlacement::Below:
            x = anchor.min.x;
            y = anchor.max.y + 2.0f;
            // 下方放不下而上方放得下：翻到上方
            if (y + h > display.y - margin && anchor.min.y - 2.0f - h >= margin)
                y = anchor.min.y - 2.0f - h;
            break;

        case PopupPlacement::Right:
            x = anchor.max.x;
            y = anchor.min.y - 4.0f;
            if (x + w > display.x - margin)
                x = anchor.min.x - w;
            break;

        case PopupPlacement::AtPoint:
            x = point.x;
            y = point.y;
            if (x + w > display.x - margin) x = point.x - w;
            if (y + h > display.y - margin) y = point.y - h;
            break;

        case PopupPlacement::Center:
            x = (display.x - w) * 0.5f;
            y = (display.y - h) * 0.5f;
            break;

        case PopupPlacement::Manual:
            x = point.x;
            y = point.y;
            break;
        }

        // 最后平移进窗口可见范围
        x = std::clamp(x, margin, std::max(margin, display.x - margin - w));
        y = std::clamp(y, margin, std::max(margin, display.y - margin - h));
        return Rect{ std::round(x), std::round(y), std::round(x) + w, std::round(y) + h };
    }

    void PopupLayer::OnLayout()
    {
        const Vec2 display = GetSize();
        for (const auto& child : GetChildren())
        {
            auto* popup = dynamic_cast<Popup*>(child.get());
            if (!popup)
            {
                child->SetBounds(Rect{ { 0, 0 }, display });   // 模态遮罩
                continue;
            }

            Vec2 size = popup->Measure(display);
            if (popup->m_matchAnchorWidth)
                size.x = std::max(size.x, popup->m_anchor.Width());
            if (popup->m_maxHeight > 0.0f)
                size.y = std::min(size.y, popup->m_maxHeight);

            popup->SetBounds(PlacePopup(size, popup->m_placement, popup->m_anchor, popup->m_point, display));
        }
    }

    void ModalBackdrop::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, Theme::ModalScrim);
    }

    void ModalBackdrop::OnPointerEvent(PointerEvent& e)
    {
        e.handled = true;   // 挡住下方主界面
    }
}
