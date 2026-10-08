#pragma once
#include "Core/Node.h"
#include <functional>
#include <memory>

namespace MiniGUI
{
    // 滚动视图：包含一个内容节点，内容超出时显示滚动条（竖直 / 水平可分别开启）。
    // - 滚轮竖直滚动，Shift+滚轮水平滚动；已经滚到头时不处理，事件冒泡给外层滚动视图
    // - 拖动滑块、点击轨道翻页
    // - 竖直滚动时内容宽度等于视口宽度（出现竖直滚动条时自动让出宽度）；开启水平滚动后内容按自身宽度排布
    // - 内容节点可以只绘制与 DrawList 当前裁剪区相交的部分（ListView / TreeView 的虚拟化就是这样做的）
    class ScrollView : public Node
    {
    public:
        ScrollView();

        Node* SetContent(std::unique_ptr<Node> content);

        template<typename T, typename... Args>
        T* SetContent(Args&&... args)
        {
            auto c = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = c.get();
            SetContent(std::move(c));
            return raw;
        }

        Node* GetContent() const { return m_content; }

        void SetScrollAxes(bool horizontal, bool vertical);
        void SetWheelStep(float step) { m_wheelStep = step; }

        // 顶部固定区域（表头）：内容从它下面开始，竖直滚动时它不动，水平滚动时由子类按 GetScrollOffset().x 平移绘制
        void  SetHeaderHeight(float h);
        float GetHeaderHeight() const { return m_headerHeight; }

        Vec2 GetScrollOffset() const { return m_scroll; }
        Vec2 GetViewportSize() const { return m_viewport; }
        Vec2 GetContentSize()  const { return m_contentSize; }
        Vec2 GetMaxScroll()    const;

        void ScrollTo(Vec2 offset);
        void ScrollBy(Vec2 delta) { ScrollTo(m_scroll + delta); }
        void ScrollIntoView(const Rect& contentRect);       // 内容坐标

        void SetOnScrolled(std::function<void(Vec2)> cb) { m_onScrolled = std::move(cb); }

        static constexpr float kBarSize = 10.0f;

    protected:
        Vec2 MeasureContent(Vec2 available) override;       // 不超过可用空间：多出的部分靠滚动
        void OnLayout() override;
        void OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        bool InterceptsHit(Vec2 local) const override;

    private:
        enum class Axis { None, Vertical, Horizontal };

        struct BarGeometry
        {
            Rect track;
            Rect thumb;
        };

        bool        BarGeometryFor(Axis axis, BarGeometry& out) const;   // 局部坐标
        Axis        BarAt(Vec2 local) const;
        void        ApplyScroll();

    private:
        Node* m_content = nullptr;
        bool  m_hEnabled = false;
        bool  m_vEnabled = true;
        bool  m_showH    = false;
        bool  m_showV    = false;
        float m_wheelStep = 48.0f;
        float m_headerHeight = 0.0f;

        Vec2 m_scroll;
        Vec2 m_viewport;
        Vec2 m_contentSize;

        Axis  m_hoverBar  = Axis::None;
        Axis  m_dragBar   = Axis::None;
        float m_dragStartMouse  = 0.0f;
        float m_dragStartScroll = 0.0f;

        std::function<void(Vec2)> m_onScrolled;
    };
}
