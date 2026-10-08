#pragma once
#include "Core/Node.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    struct MenuItem;

    // 标签页（多文档）：上方标签条 + 下方内容区（同一时间只显示选中页的内容节点）。
    // - 可关闭的标签显示 ×，中键点击也可关闭；未保存的标签显示 ●（悬停时变成 ×）
    // - 设置了 OnCloseRequested 时由调用方决定是否关闭（例如先询问是否保存），否则直接关闭
    // - "+" 按钮新建（OnNewTabRequested）；Ctrl+W / Ctrl+F4 关闭当前页；Ctrl+Tab / Ctrl+Shift+Tab 切换
    // - 右键标签：关闭、关闭其他、关闭右侧、关闭全部（可追加自定义项）
    // - 拖动标签调整顺序；标签太多时先等比缩窄，再放不下就滚动标签条，并用"⌄"按钮列出全部标签
    // - 内容节点可以为空：只用标签条（例如多个文档共用一个 CAD 视口），此时把高度设为 kStripHeight 即可
    class TabView : public Node
    {
    public:
        TabView();

        int  AddTab(std::string title, std::unique_ptr<Node> content, bool closable = false);

        template<typename T, typename... Args>
        T* AddTab(std::string title, bool closable, Args&&... args)
        {
            auto c = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = c.get();
            AddTab(std::move(title), std::move(c), closable);
            return raw;
        }

        void  RemoveTab(int index);
        void  MoveTab(int from, int to);
        int   GetTabCount() const { return static_cast<int>(m_tabs.size()); }
        Node* GetTabContent(int index) const;
        int   IndexOf(const Node* content) const;
        void  SetTabTitle(int index, std::string title);
        const std::string& GetTabTitle(int index) const;          // 越界时返回空字符串
        void  SetTabModified(int index, bool modified);
        bool  IsTabModified(int index) const { return index >= 0 && index < GetTabCount() && m_tabs[static_cast<size_t>(index)].modified; }
        // 宿主数据（例如文档指针），随标签一起移动
        void      SetTabData(int index, uintptr_t data) { if (index >= 0 && index < GetTabCount()) m_tabs[static_cast<size_t>(index)].data = data; }
        uintptr_t GetTabData(int index) const { return index >= 0 && index < GetTabCount() ? m_tabs[static_cast<size_t>(index)].data : 0; }
        int       FindTabData(uintptr_t data) const;      // 没有返回 -1

        void SetSelected(int index);
        int  GetSelected() const { return m_selected; }

        void RequestClose(int index);                   // 与点击 × 相同的流程

        void SetShowNewTabButton(bool show) { m_showNewButton = show; InvalidateLayout(); }
        void SetOnSelectionChanged(std::function<void(int)> cb) { m_onSelectionChanged = std::move(cb); }
        void SetOnCloseRequested(std::function<void(int)> cb)   { m_onCloseRequested = std::move(cb); }
        void SetOnNewTabRequested(std::function<void()> cb)     { m_onNewTabRequested = std::move(cb); }
        void SetOnTabMoved(std::function<void(int from, int to)> cb) { m_onTabMoved = std::move(cb); }
        void SetTabContextMenuExtra(std::function<std::vector<MenuItem>(int)> cb) { m_contextExtra = std::move(cb); }

        static constexpr float kStripHeight = 34.0f;

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnLayout() override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;
        bool OnContextMenu(Vec2 windowPos) override;

    private:
        struct Tab
        {
            std::string title;
            Node*       content  = nullptr;
            bool        closable = false;
            bool        modified = false;
            uintptr_t   data     = 0;
            float       x0 = 0.0f, x1 = 0.0f;       // 标签条坐标（未滚动）
        };

        enum class Hit { None, Tab, Close, NewButton, OverflowButton };

        void  LayoutTabs();
        void  EnsureSelectedVisible();
        Hit   HitAt(Vec2 local, int& tab) const;
        Rect  TabRect(const Tab& tab) const;         // 局部坐标（已滚动）
        Rect  CloseRect(const Tab& tab) const;
        Rect  NewButtonRect() const;
        Rect  OverflowButtonRect() const;
        void  ShowOverflowMenu();
        void  CloseOthers(int keep);
        void  CloseToRight(int index);

        std::vector<Tab> m_tabs;
        int   m_selected   = -1;
        int   m_hover      = -1;
        Hit   m_hoverHit   = Hit::None;
        bool  m_showNewButton = false;
        bool  m_overflow   = false;
        float m_stripScroll = 0.0f;
        float m_stripRight  = 0.0f;                   // 标签可用区域右边界
        int   m_contextTab = -1;

        // 拖动排序
        int   m_pressTab   = -1;
        float m_pressX     = 0.0f;
        bool  m_dragging   = false;

        std::function<void(int)> m_onSelectionChanged;
        std::function<void(int)> m_onCloseRequested;
        std::function<void()>    m_onNewTabRequested;
        std::function<void(int, int)> m_onTabMoved;
        std::function<std::vector<MenuItem>(int)> m_contextExtra;
    };
}
