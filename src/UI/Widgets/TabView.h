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
    // - 设置了 OnTabDragOut 时，标签一拖离标签条（上下超过 kDragOutDistance）就回调宿主（主窗口把文档拖出成独立窗口）；
    //   宿主接手后本控件保持捕获，把之后的移动 / 松开经 OnTornTabDrag 转给宿主（窗口跟着光标走，像 Visual Studio）；
    //   拖回时宿主用 DropIndexAt 找落点、SetDropIndicator 画插入标记
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
        void ClearSelection() { if (m_selected >= 0) { m_selected = -1; Invalidate(); } }   // 不选中任何标签（不触发回调）
        int  GetSelected() const { return m_selected; }

        void RequestClose(int index);                   // 与点击 × 相同的流程

        void SetShowNewTabButton(bool show) { m_showNewButton = show; InvalidateLayout(); }
        // 标题栏样式（放进无边框窗口的标题栏，SolidCraft 式）：不画标签条底色和底线，标签画成圆角块
        void SetBarStyle(bool bar) { m_barStyle = bar; Invalidate(); }
        bool IsEmptyStripAt(Vec2 local) const { int tab; return HitAt(local, tab) == Hit::None; }   // 标签条空白处（标题栏里可拖动窗口）
        Rect GetTabRect(int index) const;          // 标签的位置（局部坐标，布局之后有效；越界为空矩形）。自测用
void SetOnSelectionChanged(std::function<void(int)> cb) { m_onSelectionChanged = std::move(cb); }
        void SetOnCloseRequested(std::function<void(int)> cb)   { m_onCloseRequested = std::move(cb); }
        void SetOnNewTabRequested(std::function<void()> cb)     { m_onNewTabRequested = std::move(cb); }
        void SetOnTabMoved(std::function<void(int from, int to)> cb) { m_onTabMoved = std::move(cb); }
        void SetTabContextMenuExtra(std::function<std::vector<MenuItem>(int)> cb) { m_contextExtra = std::move(cb); }
        // 整个替换标签右键菜单（不要默认的「关闭…」几项，例如标签本身不能关闭时）；设置后忽略 Extra
        void SetTabContextMenu(std::function<std::vector<MenuItem>(int)> cb) { m_contextMenu = std::move(cb); }

        // 标签拖到标签条之外（上下超过 kDragOutDistance）时回调，windowPos 是当时的指针位置（窗口坐标）。
        // 返回 true 表示宿主接手（回调里可以移除该标签）：本控件不再排序，保持捕获，之后的移动、松开与取消经 OnTornTabDrag 上报；
        // 返回 false 时标签留在条里，拖回条内照常排序。没设置时标签只能在条内拖动排序
        enum class TornDragPhase { Move, End, Cancel };
        void SetOnTabDragOut(std::function<bool(int index, Vec2 windowPos)> cb) { m_onTabDragOut = std::move(cb); }
        void SetOnTornTabDrag(std::function<void(TornDragPhase, Vec2 windowPos)> cb) { m_onTornTabDrag = std::move(cb); }
        bool IsDraggingOut() const { return m_dragOut; }    // 正拖着标签在标签条之外，宿主没接手（测试用）
        bool IsTornDragging() const { return m_torn; }      // 宿主接手了拖出的标签，拖动还没结束（测试用）
// 外部拖回：窗口坐标落在标签条（上下各放宽 kDragOutDistance）上时返回放下后的标签序号（0..GetTabCount()），否则 -1
        int  DropIndexAt(Vec2 windowPos) const;
        // 画落点的插入标记（DropIndexAt 的结果；-1 清除）
        void SetDropIndicator(int index);
        int  GetDropIndicator() const { return m_dropIndicator; }

        static constexpr float kStripHeight     = 34.0f;
        static constexpr float kDragOutDistance = 24.0f;

        // 自然宽度取各标签的理想宽度之和（只用标签条、放在一行里和别的控件并排时用，例如状态栏左侧的模型 / 布局标签）
        void  SetFitToTabs(bool fit) { m_fitToTabs = fit; InvalidateLayout(); }
        float IdealStripWidth() const;

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
        bool  m_dragOut    = false;     // 指针已拖到标签条之外，宿主没接手
        bool  m_torn       = false;     // 宿主接手了拖出的标签：移动 / 松开转给 OnTornTabDrag

        bool                     m_barStyle = false;
        bool                     m_fitToTabs = false;
        int                      m_dropIndicator = -1;
        std::function<void(int)> m_onSelectionChanged;
        std::function<bool(int, Vec2)> m_onTabDragOut;
        std::function<void(TornDragPhase, Vec2)> m_onTornTabDrag;
std::function<void(int)> m_onCloseRequested;
        std::function<void()>    m_onNewTabRequested;
        std::function<void(int, int)> m_onTabMoved;
        std::function<std::vector<MenuItem>(int)> m_contextExtra;
        std::function<std::vector<MenuItem>(int)> m_contextMenu;     // 非空时整个替换默认菜单
    };
}
