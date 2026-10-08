#pragma once
#include "Core/Node.h"
#include <functional>

namespace MiniGUI
{
    // 分隔条：放在两个兄弟节点之间，拖动调整其中一个（target）的宽度或高度。
    // vertical = true 时是竖直的条，左右拖动调整宽度；false 时是水平的条，上下拖动调整高度。
    // targetBefore：target 在分隔条之前（左边/上边），向右/下拖动时变大；否则相反
    class Splitter : public Node
    {
    public:
        Splitter(Node* target, bool vertical = true, bool targetBefore = true);

        void SetTarget(Node* target) { m_target = target; }
        void SetLimits(float minSize, float maxSize) { m_min = minSize; m_max = maxSize; }
        void SetOnResized(std::function<void(float)> cb) { m_onResized = std::move(cb); }

        CursorShape GetCursor(Vec2) const override { return m_vertical ? CursorShape::SizeWE : CursorShape::SizeNS; }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;

    private:
        Node* m_target;
        bool  m_vertical;
        bool  m_targetBefore;
        float m_min = 60.0f;
        float m_max = 2000.0f;

        bool  m_dragging  = false;
        float m_startPos  = 0.0f;
        float m_startSize = 0.0f;
        std::function<void(float)> m_onResized;
    };
}
