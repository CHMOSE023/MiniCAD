#pragma once
#include "Core/Node.h"
#include "Style/ColorRef.hpp"

namespace MiniGUI
{
    // 面板：背景 + 可选边框 + 圆角，本身不处理输入，作为容器使用
    class Panel : public Node
    {
    public:
        explicit Panel(ColorRef background = Colors::Transparent);

        void SetBackground(ColorRef color);
        void SetBorder(ColorRef color, float thickness = 1.0f);
        void SetRounding(float rounding);

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        ColorRef m_background      = Colors::Transparent;
        ColorRef m_border          = Colors::Transparent;
        float   m_borderThickness = 1.0f;
        float   m_rounding        = 0.0f;
    };
}
