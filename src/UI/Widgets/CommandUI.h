#pragma once
#include "Core/Node.h"
#include "Data/CommandRegistry.h"
#include "Widgets/Menu.h"
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Button;

    // 由命令生成菜单项：文字、快捷键提示、可用/勾选状态（打开菜单时求值）都取自命令。
    // 命令不存在时生成一个禁用项，文字为 "?id"，便于发现界面描述文件里的拼写错误
    MenuItem CommandMenuItem(CommandRegistry& commands, const std::string& id);

    // 工具栏：一排（或一列）命令按钮 + 分隔线。
    // - 按钮的图标、悬浮提示、可用、选中状态都取自命令；命令状态变化（CommandRegistry::NotifyStateChanged）时自动刷新
    // - 空间不够时，放不下的按钮折叠到末尾的 » 按钮里，点击后以菜单列出
    // - 按钮不参与键盘焦点：点击后键盘仍留在原处（例如 CAD 视口）
    class ToolBar : public Node
    {
    public:
        explicit ToolBar(CommandRegistry& commands, bool vertical = false);
        ~ToolBar() override;

        // 图标路径前缀（命令的 icon 为相对路径时拼在前面）
        void SetIconDir(std::string dir) { m_iconDir = std::move(dir); }
        void SetIconSize(float size)     { m_iconSize = size; InvalidateLayout(); }
        void SetBackground(ColorRef color) { m_background = color; Invalidate(); }

        // showText：图标旁边显示命令名称；没有图标的命令总是显示名称
        Button* AddCommand(const std::string& id, bool showText = false);
        void    AddSeparator();
        void    Clear();

        bool   IsVertical() const { return m_vertical; }
        size_t GetItemCount() const { return m_items.size(); }
        size_t GetOverflowCount() const { return m_overflowFrom < m_items.size() ? m_items.size() - m_overflowFrom : 0; }
        Button* GetMoreButton() const { return m_more; }
        Button* FindButton(const std::string& id) const;

        void RefreshState();    // 按命令状态更新按钮；一般不需要手动调用

        static constexpr float kButtonPadding = 5.0f;
        static constexpr float kSeparatorSize = 9.0f;

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnLayout() override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        struct Item
        {
            std::string id;                 // 空表示分隔线
            Node*       node = nullptr;
        };

        float  Main (Vec2 v) const { return m_vertical ? v.y : v.x; }
        float  Cross(Vec2 v) const { return m_vertical ? v.x : v.y; }
        Vec2   ItemSize(const Item& item);
        void   OpenOverflowMenu();

        CommandRegistry&                 m_commands;
        CommandRegistry::ListenerId      m_listener = 0;
        std::weak_ptr<void>              m_registryAlive;
        bool                             m_vertical = false;
        std::string                      m_iconDir;
        float                            m_iconSize = 20.0f;
        ColorRef                         m_background = Colors::Transparent;
        std::vector<Item>                m_items;
        Button*                          m_more = nullptr;
        size_t                           m_overflowFrom = 0;    // 从这一项开始放不下
    };
}
