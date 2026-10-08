#pragma once
#include "Layout/ILayoutEngine.h"
#include "Layout/LayoutStyle.hpp"
#include "Core/Types/Rect.hpp"
#include <vector>

namespace MiniGUI
{
    // 自研的 Flexbox 子集（单行，不换行），支持方向、内边距、间距、
    // 固定/最小/最大尺寸、grow/shrink、主轴与交叉轴对齐、外边距、绝对定位
    class FlexLayout : public ILayoutEngine
    {
    public:
        static FlexLayout& Default();   // 未指定引擎时使用的共享实例

        virtual Vec2 MeasureChildren(Node& container, Vec2 available) override;
        virtual void ArrangeChildren(Node& container, float pixelScale) override;

    private:
        struct Item
        {
            Node* node        = nullptr;
            float basis       = 0.0f;   // 主轴基础尺寸
            float target      = 0.0f;   // 主轴最终尺寸
            float minMain     = 0.0f;
            float maxMain     = kInfinity;
            float marginMain  = 0.0f;   // 主轴两侧外边距之和
            bool  frozen      = false;
        };

        void ResolveFlexibleLengths(std::vector<Item>& items, float innerMain, float gap);
        void ArrangeAbsolute(Node& child, Vec2 containerSize, float pixelScale);
    };
}
