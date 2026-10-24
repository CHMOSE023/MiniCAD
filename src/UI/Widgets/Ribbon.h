#pragma once
#include "Core/Node.h"
#include "Data/CommandRegistry.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Popup;

    class Button;
    class TextBox;

    // 功能区（尺寸照 SolidCraft 的 ribbon：选项卡行 30、分组 86 = 按钮 60 + 组名 20，按钮宽 54–76）：
    // 上面一行选项卡，下面是当前选项卡的分组；每组是一排“图标在上、名称在下”的命令按钮，组名在底部，
    // 有命令收进下拉时组名带 ▾，点击列出没显示出来的命令。也可以把宿主的控件（例如图层下拉框）放进组里。
    // 宽度不够时（与 SolidCraft 相同）：先把按钮收窄到最小宽度；再从按钮最多的组（同样多时取右边的）起，
    // 一次一个把末尾的按钮收进组名的下拉菜单，减到一个时整组折叠成一个下拉按钮。
    // 按钮的图标、提示、可用 / 勾选状态取自命令，命令状态变化时自动刷新；按钮不参与键盘焦点。
    class Ribbon : public Node
    {
    public:
        explicit Ribbon(CommandRegistry& commands);
        ~Ribbon() override;

        void SetIconDir(std::string dir) { m_iconDir = std::move(dir); }

        int  AddTab(std::string title);                                         // 返回选项卡序号
        int  AddPanel(int tab, std::string title);                              // 返回组序号（在该选项卡内）
        void AddCommand(int tab, int panel, const std::string& id);
        void AddHostNode(int tab, int panel, std::unique_ptr<Node> node);       // 宿主控件（按自身尺寸，垂直居中）

        // 选项卡行右侧的控件，高度为选项卡行高；与命令搜索框互斥（后设的替换先设的）
        void SetTrailing(std::unique_ptr<Node> node);

        // 命令搜索框（照 SolidCraft：选项卡行右端，获得焦点时变宽）：按命令名称、编号、提示和所在选项卡 / 组名查找，
        // 结果列出命令与位置（“绘图 › 绘制”），↑↓ 选择、Enter 执行、Esc 关闭；执行或关闭后调用 onDone（宿主把焦点还给绘图区）
        void EnableSearch(std::string placeholder = "搜索命令");
        void FocusSearch();
        TextBox* GetSearchBox() const { return m_search; }
        void SetOnSearchDone(std::function<void()> cb) { m_onSearchDone = std::move(cb); }
        // 搜索结果（命令编号，按匹配程度排序；不可用的命令排在后面）。测试和宿主也可直接调用
        std::vector<std::string> SearchCommands(const std::string& query, size_t limit = 40) const;

        static constexpr float kSearchW      = 180.0f;
        static constexpr float kSearchOpenW  = 300.0f;

        int  GetTabCount() const { return static_cast<int>(m_tabs.size()); }
        int  GetSelectedTab() const { return m_selected; }
        void SelectTab(int index);
        int  FindTab(const std::string& title) const;
        const std::string& GetTabTitle(int index) const;

        // 收起：只留选项卡行（选项卡行右端的 ⌃ 按钮或双击选项卡切换，与 SolidCraft / AutoCAD 相同），给绘图区和停靠面板腾出高度
        void SetCollapsed(bool collapsed);
        bool IsCollapsed() const { return m_collapsed; }
        Button* GetCollapseButton() const { return m_collapseButton; }   // 选项卡行右端的 ⌃ / ⌄

        void RefreshState();    // 按命令状态更新按钮；一般不需要手动调用

        static constexpr float kTabRowH    = 30.0f;
        static constexpr float kGroupH     = 86.0f;
        static constexpr float kButtonH    = 60.0f;
        static constexpr float kSectionH   = 20.0f;
        static constexpr float kButtonMinW = 54.0f;
        static constexpr float kButtonMaxW = 76.0f;
        static constexpr float kCollapsedW = 72.0f;
        static constexpr float kChevronW   = 26.0f;     // 选项卡行右端的收起 / 展开按钮

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnLayout() override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;

    private:
        struct Item
        {
            std::string id;                 // 命令；为空时是宿主控件
            Node*       node  = nullptr;    // 按钮或宿主控件
            float       fullW = 0.0f;       // 按名称排版的宽度
        };
        struct Panel
        {
            std::string       title;
            std::vector<Item> items;
            Button*           collapsed = nullptr;     // 折叠时显示的下拉按钮
            // 布局结果（相对本控件）
            float x0 = 0.0f, x1 = 0.0f;
            bool  isCollapsed = false;
            size_t shown = 0;                   // 显示的项数，其余收进组名的下拉菜单
        };
        struct Tab
        {
            std::string        title;
            std::vector<Panel> panels;
            float x0 = 0.0f, x1 = 0.0f;      // 选项卡标题的位置
        };

        float ItemWidth(const Item& item, bool narrow) const;
        float PanelWidth(const Panel& p, bool narrow, size_t shown) const;
        static bool HasCommands(const Panel& p);
        static bool HasOverflow(const Panel& p);        // 有收进组名下拉的命令（组名显示 ▾、可点）
        float PanelTitleMinW(const Panel& p) const;
        void  ShowPanelMenu(const Panel& p, const Rect& anchor, Node* owner);
        const Panel* CloseMenu();                       // 关闭打开着的组下拉，返回它属于的组（没有打开时返回 nullptr）
        int   TabAt(Vec2 local) const;
        int   PanelTitleAt(Vec2 local) const;
        Rect  GroupsRect() const;
        void  FinishSearch();

        CommandRegistry&            m_commands;
        CommandRegistry::ListenerId m_listener = 0;
        std::weak_ptr<void>         m_registryAlive;
        std::string                 m_iconDir;
        std::vector<Tab>            m_tabs;
        int                         m_selected   = 0;
        int                         m_hoverTab   = -1;
        int                         m_hoverTitle = -1;
        bool                        m_narrow     = false;   // 布局结果：按钮收窄到最小宽度
        Node*                       m_trailing   = nullptr;
        TextBox*                    m_search     = nullptr;
        Button*                     m_collapseButton = nullptr;
        Popup*                      m_menu       = nullptr;   // 打开着的组下拉（只用来比较，弹层可能已被别处关闭）
        const Panel*                m_menuPanel  = nullptr;
        std::function<void()>       m_onSearchDone;
        bool                        m_collapsed  = false;
        bool                        m_expandedByClick = false;   // 上一次单击把收起的功能区展开了（随后的双击不再收起）
    };
}
