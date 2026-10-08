#include "Widgets/CommandUI.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/Controls.h"
#include "Widgets/Label.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    // =========================================================
    // 菜单项
    // =========================================================
    MenuItem CommandMenuItem(CommandRegistry& commands, const std::string& id)
    {
        const Command* c = commands.Find(id);
        if (!c)
            return MenuItem("?" + id).Enabled(false);

        MenuItem item(commands.GetLabel(id), [&commands, id] { commands.Execute(id); }, c->shortcut);
        item.EnabledIf([&commands, id] { return commands.IsEnabled(id); });
        if (c->isChecked)
            item.CheckedIf([&commands, id] { return commands.IsChecked(id); });
        return item;
    }

    // =========================================================
    // ToolBar
    // =========================================================
    namespace
    {
        constexpr float kGap = 2.0f;

        bool IsAbsolutePath(const std::string& p)
        {
            return (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\'));
        }

        ButtonStyle ToolButtonStyle()
        {
            ButtonStyle s = ButtonStyle::Flat();
            s.checked = Theme::AccentSoft;
            return s;
        }
    }

    ToolBar::ToolBar(CommandRegistry& commands, bool vertical)
        : m_commands(commands)
        , m_vertical(vertical)
    {
        SetClipChildren(true);      // 折叠的按钮放在可见区域之外，被裁掉，也不会被命中
        EditLayoutStyle().padding = vertical ? Edges::Symmetric(3.0f, 4.0f) : Edges::Symmetric(4.0f, 3.0f);

        m_more = AddChild<Button>([this] { OpenOverflowMenu(); });
        m_more->SetStyle(ToolButtonStyle());
        m_more->SetFocusable(false);
        m_more->SetText(vertical ? "︾" : "»")->SetFontSize(14.0f);
        m_more->EditLayoutStyle().padding = Edges::Symmetric(6.0f, kButtonPadding);
        m_more->SetTooltip("更多命令");

        m_listener      = m_commands.AddListener([this] { RefreshState(); });
        m_registryAlive = m_commands.GetLifetimeToken();
    }

    ToolBar::~ToolBar()
    {
        if (!m_registryAlive.expired())
            m_commands.RemoveListener(m_listener);
    }

    Button* ToolBar::AddCommand(const std::string& id, bool showText)
    {
        Button* b = AddChild<Button>([this, id] { m_commands.Execute(id); });
        b->SetStyle(ToolButtonStyle());
        b->SetFocusable(false);

        const Command* c = m_commands.Find(id);
        if (!c)
        {
            b->SetText("?" + id)->SetFontSize(13.0f);
            b->SetEnabled(false);
            b->SetTooltip("没有名为 " + id + " 的命令");
        }
        else
        {
            if (!c->icon.empty())
            {
                const std::string path = IsAbsolutePath(c->icon) || m_iconDir.empty() ? c->icon : m_iconDir + "/" + c->icon;
                b->SetIcon(path, { m_iconSize, m_iconSize });
            }
            if (showText || c->icon.empty())
                b->SetText(CommandRegistry::StripMnemonic(m_commands.GetLabel(id)))->SetFontSize(13.0f);
            b->SetTooltip(m_commands.GetTooltip(id));
        }

        const bool iconOnly = c && !c->icon.empty() && !showText;
        b->EditLayoutStyle().padding = iconOnly ? Edges::All(kButtonPadding) : Edges::Symmetric(8.0f, kButtonPadding);
        m_items.push_back({ id, b });
        RefreshState();
        InvalidateLayout();
        return b;
    }

    void ToolBar::AddSeparator()
    {
        m_items.push_back({ {}, AddChild<Separator>(!m_vertical) });
        InvalidateLayout();
    }

    void ToolBar::Clear()
    {
        // 可能正在某个按钮自己的点击回调里（例如"重新加载界面"），推迟销毁
        UIContext* ctx = GetContext();
        for (const Item& item : m_items)
        {
            std::unique_ptr<Node> owned = RemoveChild(item.node);
            if (ctx)
                ctx->DeferDelete(std::move(owned));
        }
        m_items.clear();
        InvalidateLayout();
    }

    Button* ToolBar::FindButton(const std::string& id) const
    {
        for (const Item& item : m_items)
        {
            if (item.id == id)
                return static_cast<Button*>(item.node);
        }
        return nullptr;
    }

    void ToolBar::RefreshState()
    {
        for (const Item& item : m_items)
        {
            if (item.id.empty() || !m_commands.Find(item.id))
                continue;
            auto* b = static_cast<Button*>(item.node);
            b->SetEnabled(m_commands.IsEnabled(item.id));
            b->SetChecked(m_commands.IsChecked(item.id));
            b->SetTooltip(m_commands.GetTooltip(item.id));
        }
    }

    // ---------------------------------------------------------
    // 布局：沿主轴依次排列，放不下的从末尾开始折叠到 » 按钮
    // ---------------------------------------------------------
    Vec2 ToolBar::ItemSize(const Item& item)
    {
        const float button = m_iconSize + kButtonPadding * 2.0f;
        if (item.id.empty() && dynamic_cast<Separator*>(item.node))
            return m_vertical ? Vec2{ button * 0.7f, kSeparatorSize } : Vec2{ kSeparatorSize, button * 0.7f };
        const Vec2 s = item.node->Measure({ 1e6f, 1e6f });
        // 图标按钮至少是正方形；文字按钮与图标按钮同高（竖排时同宽）
        return { std::max(s.x, button), std::max(s.y, button) };
    }

    Vec2 ToolBar::MeasureContent(Vec2 available)
    {
        (void)available;
        const Edges& pad = GetLayoutStyle().padding;
        float main = 0.0f, cross = m_iconSize + kButtonPadding * 2.0f;
        for (const Item& item : m_items)
        {
            const Vec2 s = ItemSize(item);
            main += Main(s) + kGap;
            cross = std::max(cross, Cross(s));
        }
        if (!m_items.empty())
            main -= kGap;
        // 向上取整到物理像素：布局对齐像素时不会因为舍去零点几个像素而误判为放不下
        const float scale = GetContext() ? GetContext()->GetPixelScale() : 1.0f;
        main = std::ceil((main + (m_vertical ? pad.Vertical() : pad.Horizontal())) * scale - 0.01f) / scale
             - (m_vertical ? pad.Vertical() : pad.Horizontal());
        return m_vertical ? Vec2{ cross + pad.Horizontal(), main + pad.Vertical() }
                          : Vec2{ main + pad.Horizontal(), cross + pad.Vertical() };
    }

    void ToolBar::OnLayout()
    {
        const Edges& pad   = GetLayoutStyle().padding;
        const Vec2   size  = GetSize();
        const float  scale = GetContext() ? GetContext()->GetPixelScale() : 1.0f;
        auto snap = [scale](float v) { return std::round(v * scale) / scale; };

        const float mainStart = m_vertical ? pad.top : pad.left;
        const float mainEnd   = Main(size) - (m_vertical ? pad.bottom : pad.right);
        const float crossLo   = m_vertical ? pad.left : pad.top;
        const float crossHi   = Cross(size) - (m_vertical ? pad.right : pad.bottom);

        std::vector<Vec2> sizes;
        sizes.reserve(m_items.size());
        float total = 0.0f;
        for (const Item& item : m_items)
        {
            sizes.push_back(ItemSize(item));
            total += Main(sizes.back()) + kGap;
        }
        if (!m_items.empty())
            total -= kGap;

        const Vec2  moreSize = m_more->Measure({ 1e6f, 1e6f });
        const float slack    = 1.0f / scale;        // 像素对齐造成的误差
        const bool  overflow = total > mainEnd - mainStart + slack;
        const float limit    = overflow ? mainEnd - Main(moreSize) - kGap : mainEnd;

        auto place = [&](Node* node, float pos, Vec2 s)
        {
            const float c0 = crossLo + (crossHi - crossLo - Cross(s)) * 0.5f;
            const Rect r = m_vertical ? Rect{ snap(c0), snap(pos), snap(c0 + s.x), snap(pos + s.y) }
                                      : Rect{ snap(pos), snap(c0), snap(pos + s.x), snap(c0 + s.y) };
            node->SetBounds(r);
        };

        // 分隔线占一个格子，线本身 1 像素宽、居中
        auto placeItem = [&](size_t k, float at)
        {
            if (!m_items[k].id.empty())
            {
                place(m_items[k].node, at, sizes[k]);
                return;
            }
            const float line = 1.0f / scale;
            const Vec2  s    = m_vertical ? Vec2{ sizes[k].x, line } : Vec2{ line, sizes[k].y };
            place(m_items[k].node, at + std::floor((Main(sizes[k]) - 1.0f) * 0.5f), s);
        };

        // 放得下的项
        float  pos = mainStart;
        size_t i   = 0;
        for (; i < m_items.size(); ++i)
        {
            if (pos + Main(sizes[i]) > limit + slack)
                break;
            placeItem(i, pos);
            pos += Main(sizes[i]) + kGap;
        }
        // 折叠位置前紧挨着的分隔线也一起折叠
        while (overflow && i > 0 && m_items[i - 1].id.empty())
            --i;
        m_overflowFrom = overflow ? i : m_items.size();

        // 放不下的项移到可见区域之外（被裁剪、不会命中）
        const float outside = Main(size) + 1000.0f;
        for (size_t k = m_overflowFrom; k < m_items.size(); ++k)
            placeItem(k, outside);

        if (overflow)
            place(m_more, mainEnd - Main(moreSize), moreSize);
        else
            place(m_more, outside, moreSize);
    }

    void ToolBar::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        dl.AddRectFilled(screenRect, m_background);
    }

    void ToolBar::OpenOverflowMenu()
    {
        UIContext* ctx = GetContext();
        if (!ctx || m_overflowFrom >= m_items.size())
            return;

        std::vector<MenuItem> items;
        for (size_t k = m_overflowFrom; k < m_items.size(); ++k)
        {
            const std::string& id = m_items[k].id;
            if (id.empty())
            {
                if (!items.empty() && !items.back().separator)
                    items.push_back(MenuItem::Separator());
            }
            else
            {
                items.push_back(CommandMenuItem(m_commands, id));
            }
        }
        if (!items.empty() && items.back().separator)
            items.pop_back();

        auto popup = std::make_unique<MenuPopup>(std::move(items));
        popup->SetAnchor(m_more->GetScreenBounds(), m_vertical ? PopupPlacement::Right : PopupPlacement::Below);
        popup->SetOwner(m_more);
        ctx->OpenPopup(std::move(popup));
    }
}
