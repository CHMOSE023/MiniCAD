#pragma once
#include "Core/Popup.h"
#include "Core/UIContext.h"
#include <functional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class UIContext;
    class MenuBar;
    class DrawList;
    struct TextParams;

    // 助记符："文件(&F)" → 显示 "文件(F)"，助记字母 'F'，下划线位于显示文字的第 underlineAt 个字节
    struct Mnemonic
    {
        std::string display;
        char        letter      = 0;        // 大写；0 表示没有
        size_t      underlineAt = std::string::npos;
    };
    Mnemonic ParseMnemonic(const std::string& text);

    // 绘制带助记符下划线的文字（左对齐、竖直居中）
    void DrawMnemonicText(UIContext& ctx, DrawList& dl, const Rect& box, const Mnemonic& m, const TextParams& params);

    // 菜单项描述（数据）：菜单打开时按它生成界面。
    // enabledIf / checkedIf 在打开菜单时求值，适合绑定"当前有没有选中实体""正交是否打开"等动态状态
    struct MenuItem
    {
        std::string            text;
        std::string            shortcut;        // 只用于显示，快捷键本身在 ShortcutTable 注册
        std::function<void()>  action;
        std::vector<MenuItem>  submenu;
        bool                   separator = false;
        bool                   enabled   = true;
        bool                   checked   = false;
        std::function<bool()>  enabledIf;
        std::function<bool()>  checkedIf;

        MenuItem() = default;
        MenuItem(std::string text, std::function<void()> action = {}, std::string shortcut = {})
            : text(std::move(text)), shortcut(std::move(shortcut)), action(std::move(action)) {}

        static MenuItem Separator()                                     { MenuItem m; m.separator = true; return m; }
        static MenuItem Sub(std::string text, std::vector<MenuItem> items) { MenuItem m(std::move(text)); m.submenu = std::move(items); return m; }

        MenuItem& Checked(bool c)                     { checked = c; return *this; }
        MenuItem& Enabled(bool e)                     { enabled = e; return *this; }
        MenuItem& CheckedIf(std::function<bool()> f)  { checkedIf = std::move(f); return *this; }
        MenuItem& EnabledIf(std::function<bool()> f)  { enabledIf = std::move(f); return *this; }
    };

    // 弹出菜单：鼠标悬停打开子菜单，点击（抬起时）执行；
    // 键盘：上下移动、→ 打开子菜单、← 关闭子菜单（在菜单栏里则切换到相邻菜单）、Enter 执行、Esc 关闭
    class MenuPopup : public Popup
    {
    public:
        MenuPopup(std::vector<MenuItem> items, MenuPopup* parentMenu = nullptr, MenuBar* bar = nullptr);

        int  GetHover() const { return m_hover; }
        void SetHover(int index);
        void SelectFirst();
        bool ActivateIndex(int index);           // 返回是否执行了（禁用项、分隔线返回 false）
        const std::vector<MenuItem>& GetItems() const { return m_items; }
        MenuPopup* GetSubmenu() const { return m_submenu; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        float RowHeight(const MenuItem& item) const;
        int   RowAt(float localY) const;
        Rect  RowRect(int index) const;                  // 局部坐标
        bool  Selectable(int index) const;
        void  MoveHover(int dir);
        void  OpenSubmenu(int index, bool focus);
        void  CloseSubmenu();
        MenuPopup* RootMenu();

        std::vector<MenuItem> m_items;
        MenuPopup* m_parentMenu = nullptr;
        MenuBar*   m_bar        = nullptr;
        MenuPopup* m_submenu    = nullptr;
        int        m_submenuIndex = -1;
        int        m_hover      = -1;
        int        m_pressed    = -1;       // 在本菜单里按下的行：只有按下和抬起在同一行才执行
    };

    // 菜单栏：一行菜单标题；点击打开，已有菜单打开时悬停即切换。
    // 键盘：Alt 或 F10 进入菜单栏（← → 选择、↓ / Enter 打开、Esc 退出），Alt+助记字母直接打开。
    // 第一次布局时自动注册为 UIContext 的菜单栏
    class MenuBar : public Node, public IMenuBarHost
    {
    public:
        MenuBar();
        ~MenuBar() override;

        // ── IMenuBarHost ────────────────────────────────────────
        Node* GetMenuBarNode() override { return this; }
        bool  ActivateFromKeyboard() override;
        bool  OpenByMnemonic(char letter) override;
        bool  IsKeyboardMode() const { return m_keyboardMode; }

        void AddMenu(std::string title, std::vector<MenuItem> items);
        void Clear();

        size_t                       GetMenuCount() const                { return m_menus.size(); }
        const std::string&           GetMenuTitle(size_t index) const    { return m_menus[index].title; }
        const std::vector<MenuItem>& GetMenuItems(size_t index) const    { return m_menus[index].items; }

        int  GetOpenIndex() const { return m_open; }
        void OpenMenu(int index, bool focusFirst);
        void CloseMenu();
        MenuPopup* GetOpenPopup() const { return m_popup; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnLayout() override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;
        void OnFocusChanged(bool focused) override;

    private:
        friend class MenuPopup;
        void Navigate(int dir);                 // 键盘 ← → 在菜单之间切换
        int  TitleAt(float localX) const;
        void ExitKeyboardMode();

        struct Menu
        {
            std::string           title;
            Mnemonic              mnemonic;
            std::vector<MenuItem> items;
            float                 x0 = 0.0f, x1 = 0.0f;   // 标题的横向范围（局部坐标）
        };

        std::vector<Menu> m_menus;
        int        m_open  = -1;
        int        m_hover = -1;
        MenuPopup* m_popup = nullptr;
        bool       m_keyboardMode = false;
        int        m_keyboardIndex = 0;
        UIContext* m_registered   = nullptr;     // 已注册为哪个上下文的菜单栏（析构时注销）
    };

    // 在窗口坐标 pos 处弹出右键菜单
    MenuPopup* ShowContextMenu(UIContext& ctx, std::vector<MenuItem> items, Vec2 pos);

    // 给任意节点挂右键菜单：右键抬起、菜单键、Shift+F10 时调用 build 生成菜单项（每次打开时重新生成，状态总是最新）
    void AttachContextMenu(Node* node, std::function<std::vector<MenuItem>()> build);
}
