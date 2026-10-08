#include "Widgets/Menu.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr float kRowH       = Theme::RowH;
        constexpr float kSepH       = 9.0f;
        constexpr float kPad        = 4.0f;
        constexpr float kCheckCol   = 28.0f;    // 左侧勾选列
        constexpr float kArrowCol   = 24.0f;    // 右侧子菜单箭头列
        constexpr float kShortcutGap = 32.0f;

        TextParams ItemText(ColorRef color, TextAlign align = TextAlign::Start)
        {
            TextParams p;
            p.size   = Theme::FontSize;
            p.color  = color;
            p.hAlign = align;
            p.vAlign = TextAlign::Center;
            return p;
        }

        void DrawCheck(DrawList& dl, Vec2 c, ColorRef color)
        {
            const Vec2 pts[3] = { { c.x - 5.0f, c.y }, { c.x - 1.5f, c.y + 3.5f }, { c.x + 5.0f, c.y - 3.5f } };
            dl.AddPolyline(pts, color, false, 1.6f);
        }
    }

    // =========================================================
    // 助记符
    // =========================================================
    Mnemonic ParseMnemonic(const std::string& text)
    {
        Mnemonic m;
        m.display.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '&' && i + 1 < text.size())
            {
                if (text[i + 1] == '&')         // "&&" 表示字面的 &
                {
                    m.display.push_back('&');
                    ++i;
                    continue;
                }
                if (m.letter == 0)
                {
                    char c = text[i + 1];
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    m.letter      = c;
                    m.underlineAt = m.display.size();
                }
                continue;
            }
            m.display.push_back(text[i]);
        }
        return m;
    }

    void DrawMnemonicText(UIContext& ctx, DrawList& dl, const Rect& box, const Mnemonic& m, const TextParams& params)
    {
        TextSystem& text = ctx.GetTextSystem();
        text.Draw(dl, box, m.display, params);
        if (m.underlineAt == std::string::npos || m.underlineAt >= m.display.size())
            return;

        // 下划线位于助记字母下方：用前缀宽度定位
        const float x0 = box.min.x + text.Measure(std::string_view(m.display).substr(0, m.underlineAt), params).x;
        const float x1 = box.min.x + text.Measure(std::string_view(m.display).substr(0, m.underlineAt + 1), params).x;
        const float lh = text.GetLineHeight(params.size);
        const float y  = std::round(box.Center().y + lh * 0.5f - 3.0f);
        dl.AddRectFilled(Rect{ x0, y, x1, y + 1.0f }, params.color);
    }

    void AttachContextMenu(Node* node, std::function<std::vector<MenuItem>()> build)
    {
        node->SetContextMenuHandler([node, build](Vec2 pos)
        {
            UIContext* ctx = node->GetContext();
            if (!ctx || !build)
                return false;
            std::vector<MenuItem> items = build();
            if (items.empty())
                return false;
            ShowContextMenu(*ctx, std::move(items), pos);
            return true;
        });
    }

    // =========================================================
    // MenuPopup
    // =========================================================
    MenuPopup::MenuPopup(std::vector<MenuItem> items, MenuPopup* parentMenu, MenuBar* bar)
        : m_items(std::move(items))
        , m_parentMenu(parentMenu)
        , m_bar(bar)
    {
        // 动态状态在打开时求值
        for (MenuItem& item : m_items)
        {
            if (item.enabledIf) item.enabled = item.enabledIf();
            if (item.checkedIf) item.checked = item.checkedIf();
        }
        LayoutStyle s;
        s.padding = Edges::All(kPad);
        SetLayoutStyle(s);
    }

    float MenuPopup::RowHeight(const MenuItem& item) const
    {
        return item.separator ? kSepH : kRowH;
    }

    Rect MenuPopup::RowRect(int index) const
    {
        float y = kPad;
        for (int i = 0; i < index; ++i)
            y += RowHeight(m_items[static_cast<size_t>(i)]);
        return Rect{ kPad, y, GetSize().x - kPad, y + RowHeight(m_items[static_cast<size_t>(index)]) };
    }

    int MenuPopup::RowAt(float localY) const
    {
        float y = kPad;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
        {
            const float h = RowHeight(m_items[static_cast<size_t>(i)]);
            if (localY >= y && localY < y + h)
                return i;
            y += h;
        }
        return -1;
    }

    bool MenuPopup::Selectable(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return false;
        const MenuItem& item = m_items[static_cast<size_t>(index)];
        return !item.separator && item.enabled;
    }

    Vec2 MenuPopup::MeasureContent(Vec2 available)
    {
        (void)available;
        UIContext* ctx = GetContext();
        float textW = 80.0f, shortcutW = 0.0f, height = kPad * 2.0f;
        bool  hasSub = false;
        for (const MenuItem& item : m_items)
        {
            height += RowHeight(item);
            if (item.separator || !ctx)
                continue;
            textW = std::max(textW, ctx->GetTextSystem().Measure(ParseMnemonic(item.text).display, ItemText(Theme::Text)).x);
            if (!item.shortcut.empty())
                shortcutW = std::max(shortcutW, ctx->GetTextSystem().Measure(item.shortcut, ItemText(Theme::Text)).x);
            hasSub |= !item.submenu.empty();
        }
        (void)hasSub;
        const float width = kPad * 2.0f + kCheckCol + textW + (shortcutW > 0.0f ? kShortcutGap + shortcutW : 0.0f) + kArrowCol;
        return { std::max(width, 160.0f), height };
    }

    void MenuPopup::OnPaint(DrawList& dl, const Rect& r)
    {
        Popup::OnPaint(dl, r);
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        TextSystem& text = ctx->GetTextSystem();

        for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
        {
            const MenuItem& item = m_items[static_cast<size_t>(i)];
            const Rect lr  = RowRect(i);
            const Rect row{ lr.min + r.min, lr.max + r.min };
            if (item.separator)
            {
                const float y = std::round(row.Center().y);
                dl.AddRectFilled(Rect{ row.min.x + 6.0f, y, row.max.x - 6.0f, y + 1.0f }, Theme::BorderSubtle);
                continue;
            }

            const bool hot = (i == m_hover || i == m_submenuIndex) && item.enabled;
            if (hot)
                dl.AddRectFilled(row, Theme::Accent, 4.0f);

            const ColorRef color = !item.enabled ? Theme::TextDisabled : Theme::Text;
            if (item.checked)
                DrawCheck(dl, { row.min.x + kCheckCol * 0.5f, row.Center().y }, color);

            DrawMnemonicText(*ctx, dl, Rect{ row.min.x + kCheckCol, row.min.y, row.max.x - kArrowCol, row.max.y },
                             ParseMnemonic(item.text), ItemText(color));
            if (!item.shortcut.empty())
                text.Draw(dl, Rect{ row.min.x, row.min.y, row.max.x - kArrowCol, row.max.y }, item.shortcut,
                          ItemText(hot ? Theme::Text : Theme::TextDim, TextAlign::End));
            if (!item.submenu.empty())
            {
                const Vec2 c{ row.max.x - kArrowCol * 0.5f, row.Center().y };
                dl.AddTriangleFilled({ c.x - 2.0f, c.y - 4.0f }, { c.x + 3.0f, c.y }, { c.x - 2.0f, c.y + 4.0f }, color);
            }
        }
    }

    void MenuPopup::SetHover(int index)
    {
        if (index == m_hover)
            return;
        m_hover = index;
        Invalidate();
    }

    void MenuPopup::SelectFirst()
    {
        m_hover = -1;
        MoveHover(1);
    }

    void MenuPopup::MoveHover(int dir)
    {
        const int n = static_cast<int>(m_items.size());
        if (n == 0)
            return;
        // 没有悬停项时：向下从第一项开始，向上从最后一项开始
        int index = m_hover >= 0 ? m_hover : (dir > 0 ? -1 : n);
        for (int step = 0; step < n; ++step)
        {
            index = ((index + dir) % n + n) % n;
            if (Selectable(index))
            {
                SetHover(index);
                return;
            }
        }
    }

    MenuPopup* MenuPopup::RootMenu()
    {
        MenuPopup* m = this;
        while (m->m_parentMenu)
            m = m->m_parentMenu;
        return m;
    }

    void MenuPopup::OpenSubmenu(int index, bool focus)
    {
        if (m_submenu && m_submenuIndex == index)
        {
            if (focus)
            {
                m_submenu->Focus();
                m_submenu->SelectFirst();
            }
            return;
        }
        CloseSubmenu();

        UIContext* ctx = GetContext();
        if (!ctx || !Selectable(index) || m_items[static_cast<size_t>(index)].submenu.empty())
            return;

        const Rect lr     = RowRect(index);
        const Vec2 origin = GetScreenBounds().min;
        auto sub = std::make_unique<MenuPopup>(m_items[static_cast<size_t>(index)].submenu, this, m_bar);
        sub->SetAnchor(Rect{ lr.min + origin + Vec2{ 0.0f, 0.0f }, lr.max + origin + Vec2{ kPad, 0.0f } }, PopupPlacement::Right);
        sub->SetOwner(this);
        MenuPopup* raw = sub.get();
        raw->SetOnClosed([this, raw]
        {
            if (m_submenu == raw)
            {
                m_submenu      = nullptr;
                m_submenuIndex = -1;
                Invalidate();
            }
        });
        // 鼠标悬停打开的子菜单不抢焦点：方向键仍在当前菜单里移动
        ctx->OpenPopup(std::move(sub), focus);
        m_submenu      = raw;
        m_submenuIndex = index;
        if (focus)
            raw->SelectFirst();
        Invalidate();
    }

    void MenuPopup::CloseSubmenu()
    {
        if (m_submenu)
            m_submenu->Close();
    }

    bool MenuPopup::ActivateIndex(int index)
    {
        if (!Selectable(index))
            return false;
        const MenuItem& item = m_items[static_cast<size_t>(index)];
        if (!item.submenu.empty())
        {
            OpenSubmenu(index, true);
            return true;
        }

        // 先关闭整条菜单链，再执行动作（动作里可能打开对话框，需要焦点和弹层都已复位）
        auto action = item.action;
        MenuBar* bar = m_bar;
        RootMenu()->Close();
        if (bar && bar->IsKeyboardMode())
            bar->ExitKeyboardMode();     // 执行命令后退出菜单栏键盘模式
        if (bar)
            bar->Invalidate();
        if (action)
            action();
        return true;
    }

    void MenuPopup::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;
        const int row = RowAt(e.localPosition.y);

        switch (e.type)
        {
        case PointerEventType::Move:
        {
            // 鼠标移进子菜单时由子菜单接管键盘
            if (m_parentMenu && !HasFocus())
                Focus();
            const int hover = Selectable(row) ? row : -1;
            if (hover != m_hover)
            {
                SetHover(hover);
                if (hover >= 0 && !m_items[static_cast<size_t>(hover)].submenu.empty())
                    OpenSubmenu(hover, false);
                else if (hover >= 0)
                    CloseSubmenu();
            }
            e.handled = true;
            break;
        }
        case PointerEventType::Leave:
            if (!m_submenu)
                SetHover(-1);
            break;
        case PointerEventType::Down:
            m_pressed = row;
            e.handled = true;       // 点在菜单里（包括分隔线、禁用项）不关闭菜单
            break;
        case PointerEventType::Up:
            // 只执行"在本菜单里按下、在同一行抬起"的项：右键菜单弹出时的那次抬起不会误触发
            if (row >= 0 && row == m_pressed && e.button != MouseButton::Middle)
                ActivateIndex(row);
            m_pressed = -1;
            e.handled = true;
            break;
        default:
            break;
        }
    }

    void MenuPopup::OnKeyEvent(KeyEvent& e)
    {
        if (e.type == KeyEventType::Down && (e.phase == EventPhase::Target) && e.modifiers == 0)
        {
            switch (e.key)
            {
            case Key::Up:    MoveHover(-1); e.handled = true; return;
            case Key::Down:  MoveHover(1);  e.handled = true; return;
            case Key::Home:  m_hover = -1; MoveHover(1);  e.handled = true; return;
            case Key::End:   m_hover = -1; MoveHover(-1); e.handled = true; return;
            case Key::Enter:
            case Key::Space:
                ActivateIndex(m_hover);
                e.handled = true;
                return;
            case Key::Right:
                if (Selectable(m_hover) && !m_items[static_cast<size_t>(m_hover)].submenu.empty())
                    OpenSubmenu(m_hover, true);
                else if (m_bar)
                    m_bar->Navigate(1);
                e.handled = true;
                return;
            case Key::Left:
                if (m_parentMenu)
                    Close();                     // 关闭子菜单，焦点回到上一级
                else if (m_bar)
                    m_bar->Navigate(-1);
                e.handled = true;
                return;
            default:
                // 助记字母：直接执行对应的菜单项
                if (e.key >= Key::A && e.key <= Key::Z)
                {
                    const char letter = static_cast<char>('A' + (static_cast<int>(e.key) - static_cast<int>(Key::A)));
                    for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
                    {
                        if (Selectable(i) && ParseMnemonic(m_items[static_cast<size_t>(i)].text).letter == letter)
                        {
                            e.handled = true;
                            ActivateIndex(i);
                            return;
                        }
                    }
                }
                break;
            }
        }
        Popup::OnKeyEvent(e);   // Esc
    }

    // =========================================================
    // MenuBar
    // =========================================================
    MenuBar::MenuBar()
    {
        LayoutStyle s;
        s.height = 30.0f;
        s.shrink = 0.0f;
        SetLayoutStyle(s);
    }

    MenuBar::~MenuBar()
    {
        // 界面重建时旧菜单栏被销毁：不能让上下文继续指向它（UIContext 销毁节点树时仍然有效）
        if (m_registered && m_registered->GetMenuBarHost() == this)
            m_registered->SetMenuBarHost(nullptr);
    }

    void MenuBar::AddMenu(std::string title, std::vector<MenuItem> items)
    {
        Menu m;
        m.mnemonic = ParseMnemonic(title);
        m.title    = std::move(title);
        m.items    = std::move(items);
        m_menus.push_back(std::move(m));
        InvalidateLayout();
    }

    // =========================================================
    // 键盘：Alt / F10 / 助记符
    // =========================================================
    bool MenuBar::ActivateFromKeyboard()
    {
        // 已经在菜单栏里（或有菜单打开）时再按一次 Alt / F10：退出
        if (m_popup)
        {
            CloseMenu();
            ExitKeyboardMode();
            return true;
        }
        if (m_keyboardMode)
        {
            ExitKeyboardMode();
            return true;
        }
        if (m_menus.empty())
            return false;
        m_keyboardMode  = true;
        m_keyboardIndex = 0;
        SetFocusable(true);
        Focus(FocusReason::Keyboard);
        Invalidate();
        return true;
    }

    bool MenuBar::OpenByMnemonic(char letter)
    {
        for (int i = 0; i < static_cast<int>(m_menus.size()); ++i)
        {
            if (m_menus[static_cast<size_t>(i)].mnemonic.letter == letter)
            {
                OpenMenu(i, true);
                return true;
            }
        }
        return false;
    }

    void MenuBar::ExitKeyboardMode()
    {
        m_keyboardMode = false;
        if (HasFocus())
            GetContext()->RestoreFocusAfterMenuBar();
        SetFocusable(false);
        Invalidate();
    }

    void MenuBar::OnFocusChanged(bool focused)
    {
        if (focused)
        {
            // 菜单用 Esc 关闭后焦点回到菜单栏：保持键盘模式，高亮刚才的菜单
            m_keyboardMode = true;
            Invalidate();
        }
        else if (!m_popup)
        {
            m_keyboardMode = false;
            SetFocusable(false);
            Invalidate();
        }
    }

    void MenuBar::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || !m_keyboardMode)
            return;
        const int n = static_cast<int>(m_menus.size());
        const uint8_t alt = static_cast<uint8_t>(ModifierKey::Alt);
        switch (e.key)
        {
        case Key::Left:   m_keyboardIndex = (m_keyboardIndex + n - 1) % n; Invalidate(); break;
        case Key::Right:  m_keyboardIndex = (m_keyboardIndex + 1) % n;     Invalidate(); break;
        case Key::Down:
        case Key::Enter:
        case Key::Space:  OpenMenu(m_keyboardIndex, true); break;
        case Key::Escape: ExitKeyboardMode(); break;
        default:
            if (e.key >= Key::A && e.key <= Key::Z && (e.modifiers == 0 || e.modifiers == alt))
            {
                if (!OpenByMnemonic(static_cast<char>('A' + (static_cast<int>(e.key) - static_cast<int>(Key::A)))))
                    return;
                break;
            }
            return;
        }
        e.handled = true;
    }

    void MenuBar::Clear()
    {
        CloseMenu();
        m_menus.clear();
        InvalidateLayout();
    }

    Vec2 MenuBar::MeasureContent(Vec2 available)
    {
        (void)available;
        float w = 8.0f;
        if (UIContext* ctx = GetContext())
            for (const Menu& m : m_menus)
                w += ctx->GetTextSystem().Measure(m.mnemonic.display, ItemText(Theme::Text)).x + 20.0f;
        return { w, 30.0f };
    }

    void MenuBar::OnLayout()
    {
        UIContext* ctx = GetContext();
        // 第一次布局时注册为上下文的菜单栏（此时才知道所属的 UIContext）
        if (ctx && m_registered != ctx)
        {
            ctx->SetMenuBarHost(this);
            m_registered = ctx;
        }
        float x = 6.0f;
        for (Menu& m : m_menus)
        {
            const float w = ctx ? ctx->GetTextSystem().Measure(m.mnemonic.display, ItemText(Theme::Text)).x + 20.0f : 20.0f;
            m.x0 = x;
            m.x1 = x + w;
            x += w;
        }
    }

    int MenuBar::TitleAt(float localX) const
    {
        for (int i = 0; i < static_cast<int>(m_menus.size()); ++i)
            if (localX >= m_menus[static_cast<size_t>(i)].x0 && localX < m_menus[static_cast<size_t>(i)].x1)
                return i;
        return -1;
    }

    void MenuBar::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, Theme::Panel);
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        for (int i = 0; i < static_cast<int>(m_menus.size()); ++i)
        {
            const Menu& m = m_menus[static_cast<size_t>(i)];
            const Rect title{ r.min.x + m.x0, r.min.y + 3.0f, r.min.x + m.x1, r.max.y - 3.0f };
            if (i == m_open)
                dl.AddRectFilled(title, Theme::ControlPressed, 4.0f);
            else if (i == m_hover || (m_keyboardMode && i == m_keyboardIndex))
                dl.AddRectFilled(title, Theme::ControlHover, 4.0f);
            if (m_keyboardMode && !m_popup && i == m_keyboardIndex)
                dl.AddRect(title, Theme::Accent, 4.0f, 1.0f);
            DrawMnemonicText(*ctx, dl, Rect{ title.min.x + 10.0f, title.min.y, title.max.x, title.max.y }, m.mnemonic, ItemText(Theme::Text));
        }
    }

    void MenuBar::OpenMenu(int index, bool focusFirst)
    {
        UIContext* ctx = GetContext();
        if (!ctx || index < 0 || index >= static_cast<int>(m_menus.size()))
            return;
        if (index == m_open && m_popup)
            return;

        CloseMenu();

        const Menu& m     = m_menus[static_cast<size_t>(index)];
        const Rect screen = GetScreenBounds();
        auto popup = std::make_unique<MenuPopup>(m.items, nullptr, this);
        popup->SetAnchor(Rect{ screen.min.x + m.x0, screen.min.y, screen.min.x + m.x1, screen.max.y }, PopupPlacement::Below);
        popup->SetOwner(this);      // 点击菜单栏由菜单栏自己切换，不触发轻触关闭
        MenuPopup* raw = popup.get();
        raw->SetOnClosed([this, raw]
        {
            if (m_popup == raw)
            {
                m_popup = nullptr;
                m_open  = -1;
                Invalidate();
            }
        });
        // 先记下再打开：打开时焦点移到菜单，菜单栏据此判断还在菜单操作中
        m_popup = raw;
        m_open  = index;
        m_keyboardIndex = index;
        ctx->OpenPopup(std::move(popup));
        if (focusFirst)
            raw->SelectFirst();
        Invalidate();
    }

    void MenuBar::CloseMenu()
    {
        if (m_popup)
            m_popup->Close();
    }

    void MenuBar::Navigate(int dir)
    {
        const int n = static_cast<int>(m_menus.size());
        if (n == 0 || m_open < 0)
            return;
        OpenMenu((m_open + dir + n) % n, true);
    }

    void MenuBar::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;
        const int title = TitleAt(e.localPosition.x);

        switch (e.type)
        {
        case PointerEventType::Move:
            if (title != m_hover)
            {
                m_hover = title;
                Invalidate();
            }
            // 已有菜单打开时，悬停到其他标题直接切换
            if (m_open >= 0 && title >= 0 && title != m_open)
                OpenMenu(title, false);
            break;
        case PointerEventType::Leave:
            m_hover = -1;
            Invalidate();
            break;
        case PointerEventType::Down:
            if (e.button != MouseButton::Left || title < 0)
                break;
            if (title == m_open)
                CloseMenu();
            else
                OpenMenu(title, false);
            e.handled = true;
            break;
        default:
            break;
        }
    }

    MenuPopup* ShowContextMenu(UIContext& ctx, std::vector<MenuItem> items, Vec2 pos)
    {
        auto popup = std::make_unique<MenuPopup>(std::move(items));
        popup->SetPoint(pos);
        MenuPopup* raw = popup.get();
        ctx.OpenPopup(std::move(popup));
        return raw;
    }
}
