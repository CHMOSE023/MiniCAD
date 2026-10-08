#include "Widgets/Splitter.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    Splitter::Splitter(Node* target, bool vertical, bool targetBefore)
        : m_target(target)
        , m_vertical(vertical)
        , m_targetBefore(targetBefore)
    {
        // 可拖动区域 6 像素宽，画出来只有中间 1 像素
        LayoutStyle s;
        if (vertical) s.width = 6.0f; else s.height = 6.0f;
        s.shrink = 0.0f;
        SetLayoutStyle(s);
    }

    void Splitter::OnPaint(DrawList& dl, const Rect& r)
    {
        const bool hot = IsHovered() || m_dragging;
        const float w  = hot ? 2.0f : 1.0f;
        const ColorRef color = hot ? Theme::Accent : Theme::BorderSubtle;
        if (m_vertical)
        {
            const float x = std::round(r.Center().x - w * 0.5f);
            dl.AddRectFilled(Rect{ x, r.min.y, x + w, r.max.y }, color);
        }
        else
        {
            const float y = std::round(r.Center().y - w * 0.5f);
            dl.AddRectFilled(Rect{ r.min.x, y, r.max.x, y + w }, color);
        }
    }

    void Splitter::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture || !m_target)
            return;
        const float pos = m_vertical ? e.position.x : e.position.y;

        switch (e.type)
        {
        case PointerEventType::Enter:
        case PointerEventType::Leave:
            Invalidate();
            break;
        case PointerEventType::Down:
            if (e.button == MouseButton::Left)
            {
                m_dragging  = true;
                m_startPos  = pos;
                m_startSize = m_vertical ? m_target->GetSize().x : m_target->GetSize().y;
                GetContext()->SetCapture(this);
                e.handled = true;
                Invalidate();
            }
            break;
        case PointerEventType::Move:
            if (m_dragging)
            {
                const float delta = (pos - m_startPos) * (m_targetBefore ? 1.0f : -1.0f);
                const float size  = std::clamp(std::round(m_startSize + delta), m_min, m_max);
                LayoutStyle& s = m_target->EditLayoutStyle();
                if (m_vertical) s.width = size; else s.height = size;
                if (m_onResized)
                    m_onResized(size);
                e.handled = true;
            }
            break;
        case PointerEventType::Up:
            if (m_dragging)
            {
                m_dragging = false;
                GetContext()->ReleaseCapture();
                e.handled = true;
                Invalidate();
            }
            break;
        case PointerEventType::Cancel:
            m_dragging = false;
            break;
        default:
            break;
        }
    }
}
