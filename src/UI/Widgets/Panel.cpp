#include "Widgets/Panel.h"
#include "Paint/DrawList.h"

namespace MiniGUI
{
    Panel::Panel(ColorRef background)
        : m_background(background)
    {}

    void Panel::SetBackground(ColorRef color)
    {
        if (m_background == color)
            return;
        m_background = color;
        Invalidate();
    }

    void Panel::SetBorder(ColorRef color, float thickness)
    {
        m_border          = color;
        m_borderThickness = thickness;
        Invalidate();
    }

    void Panel::SetRounding(float rounding)
    {
        m_rounding = rounding;
        Invalidate();
    }

    void Panel::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        dl.AddRectFilled(screenRect, m_background, m_rounding);
        if (m_border != ColorRef(Colors::Transparent) && m_borderThickness > 0.0f)
            dl.AddRect(screenRect, m_border, m_rounding, m_borderThickness);
    }
}
