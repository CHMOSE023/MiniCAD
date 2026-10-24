#include "Widgets/Ribbon.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include "Widgets/Button.h"
#include "Widgets/CommandUI.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/Menu.h"
#include "Widgets/TextBox.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace MiniGUI
{
    namespace
    {
        constexpr float kPad        = 4.0f;     // 卡片内边距
        constexpr float kPanelPad   = 4.0f;     // 组内左右留白
        constexpr float kButtonGap  = 3.0f;
        constexpr float kLabelPad   = 10.0f;    // 按钮名称两侧
        constexpr float kIconSize   = 24.0f;
        constexpr float kLabelSize  = 11.5f;
        constexpr float kTabSize    = 13.0f;
        constexpr float kTabPadX    = 12.0f;
        constexpr float kSectionSz  = 11.5f;
        const std::string kEmpty;

        bool IsAbsolutePath(const std::string& p)
        {
            return (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\'));
        }

        TextParams Params(float size, ColorRef color, TextAlign h = TextAlign::Center, TextAlign v = TextAlign::Center)
        {
            TextParams p;
            p.size   = size;
            p.color  = color;
            p.hAlign = h;
            p.vAlign = v;
            return p;
        }

        // 第一个字符（UTF-8）：没有图标的命令画一个字块代替
        std::string FirstChar(const std::string& s)
        {
            if (s.empty())
                return {};
            const unsigned char c = static_cast<unsigned char>(s[0]);
            const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
            return s.substr(0, std::min(n, s.size()));
        }

        // ── 命令搜索 ────────────────────────────────────────────
        // UTF-8 → 码点，ASCII 字母转小写
        std::u32string Decode(std::string_view s)
        {
            std::u32string out;
            for (size_t i = 0; i < s.size();)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                n = std::min(n, s.size() - i);
                char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
                for (size_t k = 1; k < n; ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
                out.push_back(cp >= U'A' && cp <= U'Z' ? cp - U'A' + U'a' : cp);
                i += n;
            }
            return out;
        }

        bool IsWordChar(char32_t c)
        {
            return (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') || c >= 0x80;
        }

        // 与 SolidCraft 的 shortcut_box::score 相同：前缀 > 词首 > 包含 > 按顺序出现（间隔越少越高），0 表示不匹配
        int MatchScore(const std::u32string& q, const std::u32string& s)
        {
            if (q.empty())
                return 1;
            if (s.compare(0, q.size(), q) == 0)
                return 1000 - static_cast<int>(std::min<size_t>(s.size(), 500));
            for (size_t i = 1; i < s.size(); ++i)
                if (!IsWordChar(s[i - 1]) && s.compare(i, q.size(), q) == 0)
                    return 800;
            if (s.find(q) != std::u32string::npos)
                return 600;
            int gaps = 0;
            size_t j = 0;
            bool lastHit = true;
            for (char32_t qc : q)
            {
                bool found = false;
                for (; j < s.size(); ++j)
                {
                    if (s[j] == qc)
                    {
                        found = true;
                        ++j;
                        break;
                    }
                    if (lastHit)
                        ++gaps;
                    lastHit = false;
                }
                if (!found)
                    return 0;
                lastHit = true;
            }
            return std::max(300 - gaps * 20, 10);
        }

        // 搜索框：左侧画放大镜；获得焦点时变宽，失去焦点时清空并恢复宽度
        class SearchBox : public TextBox
        {
        public:
            SearchBox()
            {
                TextBoxStyle st;
                st.background = Theme::Control;
                st.border     = Theme::BorderSubtle;
                st.fontSize   = 12.5f;
                st.rounding   = Theme::RadiusMd;
                st.padding    = Edges{ 24.0f, 3.0f, 8.0f, 3.0f };
                SetStyle(st);
                EditLayoutStyle().width = Ribbon::kSearchW;
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                TextBox::OnPaint(dl, r);
                const Vec2     c{ r.min.x + 12.0f, r.Center().y - 1.0f };
                const ColorRef ink = HasFocus() ? Theme::Text : Theme::TextDim;
                dl.PathArcTo(c, 4.5f, 0.0f, 6.2831853f, 20);
                dl.PathStroke(ink, true, 1.4f);
                dl.PathLineTo({ c.x + 3.3f, c.y + 3.3f });
                dl.PathLineTo({ c.x + 6.5f, c.y + 6.5f });
                dl.PathStroke(ink, false, 1.6f);
            }

            void OnFocusChanged(bool focused) override
            {
                TextBox::OnFocusChanged(focused);
                EditLayoutStyle().width = focused ? Ribbon::kSearchOpenW : Ribbon::kSearchW;
                if (!focused && !GetText().empty())
                    SetText({});
                if (Node* p = GetParent())
                    p->InvalidateLayout();
            }
        };

        // 功能区按钮：图标在上（24），名称在下（最多两行）；自绘，状态取自命令
        class RibbonButton : public Button
        {
        public:
            RibbonButton(CommandRegistry& commands, std::string id, std::string iconPath)
                : Button([&commands, id] { commands.Execute(id); }), m_icon(std::move(iconPath))
            {
                SetFocusable(false);
                const Command* c = commands.Find(id);
                m_label = c ? CommandRegistry::StripMnemonic(commands.GetLabel(id)) : "?" + id;
                if (!m_label.empty() && m_label.size() >= 3 && m_label.compare(m_label.size() - 3, 3, "…") == 0)
                    m_label.erase(m_label.size() - 3);          // 按钮上不显示"…"
                SetTooltip(c ? commands.GetTooltip(id) : "没有名为 " + id + " 的命令");
                if (!c)
                    SetEnabled(false);
            }

            const std::string& Label() const { return m_label; }
            bool HasIcon() const { return !m_icon.empty(); }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                const bool enabled = IsEnabled();
                if (IsChecked())
                    dl.AddRectFilled(r, Theme::AccentSoft, Theme::RadiusMd);
                if (enabled && IsPressedVisual())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 34), Theme::RadiusMd);
                else if (enabled && IsHovered())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 20), Theme::RadiusMd);

                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect icon = Rect::FromXYWH(std::round(r.Center().x - kIconSize * 0.5f), r.min.y + 7.0f, kIconSize, kIconSize);
                TextureId tex = InvalidTextureId;
                if (!m_icon.empty())
                    tex = ctx->LoadTexture(m_icon, icon.Size());
                if (tex != InvalidTextureId)
                    dl.AddImage(tex, icon, { 0, 0 }, { 1, 1 }, enabled ? Colors::White : ColorRef(Color32(0x70FFFFFFu)));
                else
                {
                    // 没有图标：圆角字块
                    dl.AddRectFilled(icon.Deflated(1.0f), ColorRef(ThemeColor::Accent, enabled ? 70 : 30), Theme::RadiusMd);
                    ctx->GetTextSystem().Draw(dl, icon, FirstChar(m_label), Params(13.0f, enabled ? Theme::Text : Theme::TextDisabled));
                }
                const Rect label{ r.min.x + 2.0f, icon.max.y + 3.0f, r.max.x - 2.0f, r.max.y - 1.0f };
                TextParams p = Params(kLabelSize, enabled ? Theme::Text : Theme::TextDisabled, TextAlign::Center, TextAlign::Start);
                p.wrap = true;
                dl.PushClipRect(label);
                ctx->GetTextSystem().Draw(dl, label, m_label, p);
                dl.PopClipRect();
            }

        private:
            std::string m_icon;
            std::string m_label;
        };

        // 选项卡行右端的收起 / 展开按钮：展开时画 ⌃，收起时画 ⌄
        class ChevronButton : public Button
        {
        public:
            ChevronButton(std::function<void()> onClick, std::function<bool()> collapsed)
                : Button(std::move(onClick)), m_collapsed(std::move(collapsed))
            {
                SetFocusable(false);
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (IsPressedVisual())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 34), Theme::RadiusMd);
                else if (IsHovered())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 20), Theme::RadiusMd);
                const Vec2  c   = r.Center();
                const float dy  = m_collapsed && m_collapsed() ? 2.5f : -2.5f;     // 收起时尖朝下
                dl.PathLineTo({ c.x - 5.0f, c.y - dy });
                dl.PathLineTo({ c.x,        c.y + dy });
                dl.PathLineTo({ c.x + 5.0f, c.y - dy });
                dl.PathStroke(IsHovered() ? Theme::Text : Theme::TextDim, false, 1.5f);
            }

        private:
            std::function<bool()> m_collapsed;
        };

        // 折叠的组：组里第一个命令的图标 + 组名 + ▾，点击列出全组命令
        class CollapsedButton : public Button
        {
        public:
            CollapsedButton(std::string title, std::function<void()> onClick)
                : Button(std::move(onClick)), m_title(std::move(title))
            {
                SetFocusable(false);
                SetTooltip(m_title);
            }
            void SetIconIfEmpty(const std::string& path) { if (m_icon.empty()) m_icon = path; }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (IsPressedVisual())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 34), Theme::RadiusMd);
                else if (IsHovered())
                    dl.AddRectFilled(r, ColorRef(ThemeColor::Outline, 20), Theme::RadiusMd);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect icon = Rect::FromXYWH(std::round(r.Center().x - kIconSize * 0.5f), r.min.y + 10.0f, kIconSize, kIconSize);
                const TextureId tex = m_icon.empty() ? InvalidTextureId : ctx->LoadTexture(m_icon, icon.Size());
                if (tex != InvalidTextureId)
                    dl.AddImage(tex, icon);
                else
                {
                    dl.AddRectFilled(icon.Deflated(1.0f), ColorRef(ThemeColor::Accent, 70), Theme::RadiusMd);
                    ctx->GetTextSystem().Draw(dl, icon, FirstChar(m_title), Params(13.0f, Theme::Text));
                }
                const Rect label{ r.min.x + 2.0f, icon.max.y + 4.0f, r.max.x - 2.0f, icon.max.y + 22.0f };
                ctx->GetTextSystem().Draw(dl, label, m_title + " ▾", Params(kLabelSize, Theme::Text));
            }

        private:
            std::string m_title, m_icon;
        };
    }

    Ribbon::Ribbon(CommandRegistry& commands)
        : m_commands(commands)
    {
        SetClipChildren(true);
        m_collapseButton = AddChild<ChevronButton>([this] { SetCollapsed(!m_collapsed); }, [this] { return m_collapsed; });
        m_collapseButton->SetTooltip("收起功能区（双击选项卡也可以）");
        m_listener      = m_commands.AddListener([this] { RefreshState(); });
        m_registryAlive = m_commands.GetLifetimeToken();
    }

    Ribbon::~Ribbon()
    {
        if (!m_registryAlive.expired())
            m_commands.RemoveListener(m_listener);
    }

    int Ribbon::AddTab(std::string title)
    {
        m_tabs.push_back({ std::move(title), {} });
        InvalidateLayout();
        return GetTabCount() - 1;
    }

    int Ribbon::AddPanel(int tab, std::string title)
    {
        if (tab < 0 || tab >= GetTabCount())
            return -1;
        auto& panels = m_tabs[static_cast<size_t>(tab)].panels;
        panels.push_back({});
        Panel& p = panels.back();
        p.title = std::move(title);
        const int index = static_cast<int>(panels.size()) - 1;
        p.collapsed = AddChild<CollapsedButton>(p.title, [this, tab, index]
        {
            if (tab < GetTabCount() && index < static_cast<int>(m_tabs[static_cast<size_t>(tab)].panels.size()))
            {
                const Panel& panel = m_tabs[static_cast<size_t>(tab)].panels[static_cast<size_t>(index)];
                ShowPanelMenu(panel, panel.collapsed->GetScreenBounds(), panel.collapsed);
            }
        });
        p.collapsed->SetVisible(false);
        InvalidateLayout();
        return index;
    }

    void Ribbon::AddCommand(int tab, int panel, const std::string& id)
    {
        if (tab < 0 || tab >= GetTabCount() || panel < 0 || panel >= static_cast<int>(m_tabs[static_cast<size_t>(tab)].panels.size()))
            return;
        Panel& p = m_tabs[static_cast<size_t>(tab)].panels[static_cast<size_t>(panel)];
        std::string icon;
        if (const Command* c = m_commands.Find(id); c && !c->icon.empty())
            icon = IsAbsolutePath(c->icon) || m_iconDir.empty() ? c->icon : m_iconDir + "/" + c->icon;
        auto* b = AddChild<RibbonButton>(m_commands, id, icon);
        b->SetVisible(tab == m_selected);
        p.items.push_back({ id, b, 0.0f });
        // 折叠按钮用组里第一个有图标的命令的图标
        if (!icon.empty())
            static_cast<CollapsedButton*>(p.collapsed)->SetIconIfEmpty(icon);
        RefreshState();
        InvalidateLayout();
    }

    void Ribbon::AddHostNode(int tab, int panel, std::unique_ptr<Node> node)
    {
        if (!node || tab < 0 || tab >= GetTabCount() || panel < 0 || panel >= static_cast<int>(m_tabs[static_cast<size_t>(tab)].panels.size()))
            return;
        Node* n = AddChild(std::move(node));
        n->SetVisible(tab == m_selected);
        m_tabs[static_cast<size_t>(tab)].panels[static_cast<size_t>(panel)].items.push_back({ {}, n, 0.0f });
        InvalidateLayout();
    }

    void Ribbon::SetTrailing(std::unique_ptr<Node> node)
    {
        if (m_trailing)
            RemoveChild(m_trailing);
        m_search   = nullptr;
        m_trailing = node ? AddChild(std::move(node)) : nullptr;
        InvalidateLayout();
    }

    void Ribbon::EnableSearch(std::string placeholder)
    {
        auto box = std::make_unique<SearchBox>();
        TextBox* raw = box.get();
        raw->SetPlaceholder(std::move(placeholder));
        SetTrailing(std::move(box));
        m_search = raw;

        auto ids = std::make_shared<std::vector<std::string>>();      // 当前列表各行的命令（userData 是下标）
        AutoComplete* ac = raw->EnableAutoComplete([this, ids](const std::string& text)
        {
            *ids = SearchCommands(text, 40);
            std::map<std::string, std::string> places;
            for (const Tab& t : m_tabs)
                for (const Panel& p : t.panels)
                    for (const Item& it : p.items)
                        if (!it.id.empty())
                            places.emplace(it.id, t.title + " › " + p.title);
            std::vector<ListItem> items;
            for (size_t i = 0; i < ids->size(); ++i)
            {
                const std::string& id = (*ids)[i];
                ListItem li;
                li.text = CommandRegistry::StripMnemonic(m_commands.GetLabel(id));
                if (!m_commands.IsEnabled(id))
                    li.text += "（当前不可用）";
                const auto place = places.find(id);
                li.detail = place != places.end() ? place->second : std::string("文件菜单");
                if (const Command* c = m_commands.Find(id); c && !c->shortcut.empty())
                    li.detail += "   " + c->shortcut;
                li.userData = i;
                items.push_back(std::move(li));
            }
            return items;
        });
        ac->SetSelectFirst(true);
        ac->SetMaxVisible(12);
        ac->SetMinWidth(kSearchOpenW + 60.0f);
        ac->SetOnAcceptedItem([this, ids](const ListItem& item)
        {
            if (item.userData >= ids->size())
                return;
            const std::string id = (*ids)[static_cast<size_t>(item.userData)];
            FinishSearch();
            m_commands.Execute(id);
        });
        raw->SetKeyPreview([this](KeyEvent& e)
        {
            if (e.key == Key::Escape && e.modifiers == 0)
            {
                FinishSearch();
                return true;
            }
            return false;
        });
    }

    void Ribbon::FocusSearch()
    {
        if (!m_search)
            return;
        m_search->Focus();
        m_search->SelectAll();
    }

    void Ribbon::FinishSearch()
    {
        if (!m_search)
            return;
        if (m_onSearchDone)
            m_onSearchDone();                   // 焦点移走后 SearchBox 自己清空文字、恢复宽度
        else if (UIContext* ctx = GetContext())
            ctx->SetFocus(nullptr);
        if (m_search->HasFocus())
            m_search->SetText({});
    }

    std::vector<std::string> Ribbon::SearchCommands(const std::string& query, size_t limit) const
    {
        // 查询按空格分词，每个词都要匹配命令的名称、编号、提示或所在选项卡 / 组名之一；总分取各词的最低分
        std::vector<std::u32string> words;
        {
            std::u32string w;
            for (char32_t c : Decode(query))
            {
                if (c == U' ' || c == U'\t' || c == 0x3000)
                {
                    if (!w.empty())
                        words.push_back(std::move(w));
                    w.clear();
                }
                else
                    w.push_back(c);
            }
            if (!w.empty())
                words.push_back(std::move(w));
        }
        if (words.empty())
            return {};

        std::map<std::string, std::u32string> places;
        for (const Tab& t : m_tabs)
            for (const Panel& p : t.panels)
                for (const Item& it : p.items)
                    if (!it.id.empty())
                        places.emplace(it.id, Decode(t.title + " " + p.title));

        struct Hit { int score; bool enabled; size_t order; const std::string* id; };
        std::vector<Hit> hits;
        const std::vector<std::string>& ids = m_commands.GetIds();
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const std::string& id = ids[i];
            const Command* c = m_commands.Find(id);
            if (!c || c->label.empty())
                continue;
            const std::u32string label  = Decode(CommandRegistry::StripMnemonic(c->label));
            const std::u32string idText = Decode(id);
            const std::u32string tip    = Decode(c->tooltip);
            const auto place = places.find(id);

            int total = 1 << 30;
            for (const std::u32string& w : words)
            {
                int best = std::max(MatchScore(w, label), MatchScore(w, idText));
                if (tip.find(w) != std::u32string::npos)
                    best = std::max(best, 400);
                if (place != places.end() && place->second.find(w) != std::u32string::npos)
                    best = std::max(best, 500);
                total = std::min(total, best);
                if (total == 0)
                    break;
            }
            if (total > 0)
                hits.push_back({ total, m_commands.IsEnabled(id), i, &id });
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b)
        {
            if (a.enabled != b.enabled)
                return a.enabled;
            if (a.score != b.score)
                return a.score > b.score;
            return a.order < b.order;
        });
        std::vector<std::string> out;
        for (size_t i = 0; i < hits.size() && i < limit; ++i)
            out.push_back(*hits[i].id);
        return out;
    }

    void Ribbon::SelectTab(int index)
    {
        if (index < 0 || index >= GetTabCount() || index == m_selected)
            return;
        m_selected = index;
        for (int t = 0; t < GetTabCount(); ++t)
            for (Panel& p : m_tabs[static_cast<size_t>(t)].panels)
            {
                for (Item& it : p.items)
                    it.node->SetVisible(t == m_selected);
                p.collapsed->SetVisible(false);
            }
        InvalidateLayout();
        Invalidate();
    }

    void Ribbon::SetCollapsed(bool collapsed)
    {
        if (collapsed == m_collapsed)
            return;
        m_collapsed = collapsed;
        InvalidateLayout();
        Invalidate();
    }

    int Ribbon::FindTab(const std::string& title) const
    {
        for (int i = 0; i < GetTabCount(); ++i)
            if (m_tabs[static_cast<size_t>(i)].title == title)
                return i;
        return -1;
    }

    const std::string& Ribbon::GetTabTitle(int index) const
    {
        return index >= 0 && index < GetTabCount() ? m_tabs[static_cast<size_t>(index)].title : kEmpty;
    }

    void Ribbon::RefreshState()
    {
        for (Tab& t : m_tabs)
            for (Panel& p : t.panels)
                for (Item& it : p.items)
                {
                    if (it.id.empty() || !m_commands.Find(it.id))
                        continue;
                    auto* b = static_cast<Button*>(it.node);
                    b->SetEnabled(m_commands.IsEnabled(it.id));
                    b->SetChecked(m_commands.IsChecked(it.id));
                }
    }

    // =========================================================
    // 布局
    // =========================================================
    float Ribbon::ItemWidth(const Item& item, bool narrow) const
    {
        if (item.id.empty())
            return item.node->Measure({ 1e6f, kButtonH }).x;
        if (narrow)
            return kButtonMinW;
        UIContext* ctx = GetContext();
        const auto* b = static_cast<const RibbonButton*>(item.node);
        const float textW = ctx ? ctx->GetTextSystem().Measure(b->Label(), Params(kLabelSize, Theme::Text)).x : 40.0f;
        return std::clamp(std::ceil(textW) + kLabelPad, kButtonMinW, kButtonMaxW);
    }

    float Ribbon::PanelTitleMinW(const Panel& p) const
    {
        UIContext* ctx = GetContext();
        const float w = ctx ? ctx->GetTextSystem().Measure(p.title + " ▾", Params(kSectionSz, Theme::TextDim)).x : 40.0f;
        return std::ceil(w) + 16.0f;
    }

    bool Ribbon::HasOverflow(const Panel& p)
    {
        if (p.isCollapsed)
            return false;
        for (size_t i = p.shown; i < p.items.size(); ++i)
            if (!p.items[i].id.empty())
                return true;
        return false;
    }

    bool Ribbon::HasCommands(const Panel& p)
    {
        return std::any_of(p.items.begin(), p.items.end(), [](const Item& it) { return !it.id.empty(); });
    }

    float Ribbon::PanelWidth(const Panel& p, bool narrow, size_t shown) const
    {
        if (p.isCollapsed)
            return kCollapsedW;
        float w = kPanelPad * 2.0f;
        for (size_t i = 0; i < p.items.size() && i < shown; ++i)
            w += ItemWidth(p.items[i], narrow) + (i > 0 ? kButtonGap : 0.0f);
        return std::max(w, PanelTitleMinW(p));
    }

    Rect Ribbon::GroupsRect() const
    {
        const Vec2 size = GetSize();
        return Rect{ kPad, kTabRowH, size.x - kPad, kTabRowH + kGroupH };
    }

    Vec2 Ribbon::MeasureContent(Vec2 available)
    {
        const float w = std::isfinite(available.x) ? available.x : 800.0f;
        return { w, m_collapsed ? kTabRowH + 1.0f : kTabRowH + kGroupH + kPad };
    }

    void Ribbon::OnLayout()
    {
        UIContext* ctx = GetContext();
        const Vec2 size = GetSize();

        // 选项卡行
        float x = kPad + 2.0f;
        for (Tab& t : m_tabs)
        {
            const float w = (ctx ? ctx->GetTextSystem().Measure(t.title, Params(kTabSize, Theme::Text)).x : 30.0f) + kTabPadX * 2.0f;
            t.x0 = x;
            t.x1 = x + std::ceil(w);
            x = t.x1 + 2.0f;
        }
        float right = size.x - kPad - 2.0f;
        if (m_trailing)
        {
            const Vec2 m = m_trailing->Measure({ 320.0f, kTabRowH - 6.0f });
            const float w = std::min(m.x, std::max(0.0f, size.x - x - kPad * 2.0f - kChevronW - 4.0f));
            m_trailing->SetBounds(Rect::FromXYWH(right - w, 3.0f, w, kTabRowH - 6.0f));
            right -= w + 4.0f;
        }
        m_collapseButton->SetBounds(Rect::FromXYWH(right - kChevronW, 3.0f, kChevronW, kTabRowH - 6.0f));
        m_collapseButton->SetTooltip(m_collapsed ? "展开功能区（双击选项卡也可以）" : "收起功能区（双击选项卡也可以）");

        if (m_selected < 0 || m_selected >= GetTabCount())
            return;
        Tab& tab = m_tabs[static_cast<size_t>(m_selected)];
        if (m_collapsed)
        {
            for (Panel& p : tab.panels)
            {
                for (Item& it : p.items)
                    it.node->SetVisible(false);
                p.collapsed->SetVisible(false);
            }
            return;
        }
        const Rect groups = GroupsRect();
        const float room = groups.Width();

        // 1. 原始宽度；2. 按钮收窄；3. 从按钮最多的组（同样多时取右边的）起逐个收进下拉菜单，剩一个时整组折叠。
        // 只有宿主控件的组（例如图层下拉框）不收：折叠后没有菜单可列
        auto total = [&]
        {
            float w = 0.0f;
            for (const Panel& p : tab.panels)
                w += PanelWidth(p, m_narrow, p.shown) + 1.0f;
            return w;
        };
        for (Panel& p : tab.panels)
        {
            p.isCollapsed = false;
            p.shown       = p.items.size();
        }
        m_narrow = false;
        if (total() > room)
            m_narrow = true;
        while (total() > room)
        {
            Panel* pick = nullptr;
            for (Panel& p : tab.panels)
                if (!p.isCollapsed && HasCommands(p) && (!pick || p.shown >= pick->shown))
                    pick = &p;
            if (!pick)
                break;
            const bool hasHost = std::any_of(pick->items.begin(), pick->items.end(), [](const Item& it) { return it.id.empty(); });
            if (pick->shown > 1 && !(hasHost && pick->items[pick->shown - 1].id.empty()))
                --pick->shown;
            else
                pick->isCollapsed = true;
        }

        const float top = groups.min.y + 3.0f;
        x = groups.min.x;
        for (Panel& p : tab.panels)
        {
            p.x0 = x;
            if (p.isCollapsed)
            {
                for (Item& it : p.items)
                    it.node->SetVisible(false);
                p.collapsed->SetVisible(true);
                p.collapsed->SetBounds(Rect::FromXYWH(x + 2.0f, top, kCollapsedW - 4.0f, kGroupH - 6.0f));
                p.x1 = x + kCollapsedW;
            }
            else
            {
                p.collapsed->SetVisible(false);
                const float width = PanelWidth(p, m_narrow, p.shown);
                float content = 0.0f;
                for (size_t i = 0; i < p.items.size() && i < p.shown; ++i)
                    content += ItemWidth(p.items[i], m_narrow) + (i > 0 ? kButtonGap : 0.0f);
                float bx = x + std::max(kPanelPad, (width - content) * 0.5f);
                for (size_t i = 0; i < p.items.size(); ++i)
                {
                    Item& it = p.items[i];
                    it.node->SetVisible(i < p.shown);
                    if (i >= p.shown)
                        continue;
                    const float w = ItemWidth(it, m_narrow);
                    if (it.id.empty())
                    {
                        const Vec2 m = it.node->Measure({ w, kButtonH });
                        it.node->SetBounds(Rect::FromXYWH(bx, top + std::round((kButtonH - m.y) * 0.5f), w, m.y));
                    }
                    else
                        it.node->SetBounds(Rect::FromXYWH(bx, top, w, kButtonH));
                    bx += w + kButtonGap;
                }
                p.x1 = x + width;
            }
            x = p.x1 + 1.0f;
        }
    }

    // =========================================================
    // 绘制与输入
    // =========================================================
    void Ribbon::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, Theme::Panel, Theme::RadiusLg);
        dl.AddRect(r, Theme::BorderSubtle, Theme::RadiusLg, 1.0f);

        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        // 选项卡
        for (int i = 0; i < GetTabCount(); ++i)
        {
            const Tab& t = m_tabs[static_cast<size_t>(i)];
            const Rect tr{ r.min.x + t.x0, r.min.y + 4.0f, r.min.x + t.x1, r.min.y + kTabRowH - 3.0f };
            const bool sel = i == m_selected;
            if (sel)
                dl.AddRectFilled(tr, ColorRef(ThemeColor::Accent, 210), Theme::RadiusMd);
            else if (i == m_hoverTab)
                dl.AddRectFilled(tr, ColorRef(ThemeColor::Outline, 20), Theme::RadiusMd);
            ctx->GetTextSystem().Draw(dl, tr, t.title, Params(kTabSize, sel ? Theme::TextOnAccent : Theme::Text));
        }
        if (m_collapsed || m_selected < 0 || m_selected >= GetTabCount())
            return;
        dl.AddLine({ r.min.x + 1.0f, r.min.y + kTabRowH }, { r.max.x - 1.0f, r.min.y + kTabRowH }, Theme::BorderSubtle, 1.0f);

        const Tab& tab = m_tabs[static_cast<size_t>(m_selected)];
        const float sectionTop = r.min.y + kTabRowH + kGroupH - kSectionH;
        for (size_t i = 0; i < tab.panels.size(); ++i)
        {
            const Panel& p = tab.panels[i];
            if (!p.isCollapsed)
            {
                const Rect title{ r.min.x + p.x0 + 2.0f, sectionTop, r.min.x + p.x1 - 2.0f, sectionTop + kSectionH - 2.0f };
                if (static_cast<int>(i) == m_hoverTitle)
                    dl.AddRectFilled(title, ColorRef(ThemeColor::Outline, 16), Theme::RadiusSm);
                ctx->GetTextSystem().Draw(dl, title, HasOverflow(p) ? p.title + " ▾" : p.title, Params(kSectionSz, Theme::TextDim));
            }
            if (i + 1 < tab.panels.size())
            {
                const float sx = r.min.x + p.x1 + 0.5f;
                dl.AddLine({ sx, r.min.y + kTabRowH + 8.0f }, { sx, r.min.y + kTabRowH + kGroupH - 6.0f }, Theme::BorderSubtle, 1.0f);
            }
        }
    }

    int Ribbon::TabAt(Vec2 local) const
    {
        if (local.y < 0.0f || local.y >= kTabRowH)
            return -1;
        for (int i = 0; i < GetTabCount(); ++i)
            if (local.x >= m_tabs[static_cast<size_t>(i)].x0 && local.x < m_tabs[static_cast<size_t>(i)].x1)
                return i;
        return -1;
    }

    int Ribbon::PanelTitleAt(Vec2 local) const
    {
        if (m_collapsed || m_selected < 0 || m_selected >= GetTabCount())
            return -1;
        const float top = kTabRowH + kGroupH - kSectionH;
        if (local.y < top || local.y >= kTabRowH + kGroupH)
            return -1;
        const auto& panels = m_tabs[static_cast<size_t>(m_selected)].panels;
        for (size_t i = 0; i < panels.size(); ++i)
            if (HasOverflow(panels[i]) && local.x >= panels[i].x0 && local.x < panels[i].x1)
                return static_cast<int>(i);     // 没有收进下拉的命令时组名不可点
        return -1;
    }

    const Ribbon::Panel* Ribbon::CloseMenu()
    {
        const Panel* panel = nullptr;
        UIContext* ctx = GetContext();
        // 菜单可能已经被点外面、按 Esc 或执行命令关掉了：只有它还在最上面时才算打开着
        if (ctx && m_menu && ctx->GetTopPopup() == m_menu)
        {
            panel = m_menuPanel;
            ctx->ClosePopup(m_menu);
        }
        m_menu      = nullptr;
        m_menuPanel = nullptr;
        return panel;
    }

    void Ribbon::ShowPanelMenu(const Panel& p, const Rect& anchor, Node* owner)
    {
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        // 同一时间只开一个组下拉；再点同一个组是收起
        if (CloseMenu() == &p)
            return;
        // 只列出没显示出来的命令（收进下拉的、整组折叠时的全部）；功能区上已经看得到的不重复
        std::vector<MenuItem> items;
        for (size_t i = p.isCollapsed ? 0 : p.shown; i < p.items.size(); ++i)
            if (!p.items[i].id.empty())
                items.push_back(CommandMenuItem(m_commands, p.items[i].id));
        if (items.empty())
            return;
        auto popup = std::make_unique<MenuPopup>(std::move(items));
        popup->SetAnchor(anchor, PopupPlacement::Below);
        popup->SetOwner(owner);
        m_menu      = ctx->OpenPopup(std::move(popup));
        m_menuPanel = &p;
    }

    void Ribbon::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;
        switch (e.type)
        {
        case PointerEventType::Move:
        case PointerEventType::Enter:
        {
            const int tab = TabAt(e.localPosition), title = PanelTitleAt(e.localPosition);
            if (tab != m_hoverTab || title != m_hoverTitle)
            {
                m_hoverTab   = tab;
                m_hoverTitle = title;
                Invalidate();
            }
            break;
        }
        case PointerEventType::Leave:
            if (m_hoverTab >= 0 || m_hoverTitle >= 0)
            {
                m_hoverTab = m_hoverTitle = -1;
                Invalidate();
            }
            break;
        case PointerEventType::Down:
            if (e.button != MouseButton::Left)
                break;
            // 组下拉以整个功能区为所有者，点功能区不会自动关闭它：点别处（选项卡、按钮、其他组名）时先关掉。
            // 点的是同一个组名或它的折叠按钮时交给下面切换
            if (m_menuPanel)
            {
                const Panel* open = m_menuPanel;
                const bool onCollapsed = open->collapsed && open->collapsed->IsVisible() &&
                                         open->collapsed->GetScreenBounds().Contains(e.position);
                const int  title = PanelTitleAt(e.localPosition);
                const bool onTitle = title >= 0 && &m_tabs[static_cast<size_t>(m_selected)].panels[static_cast<size_t>(title)] == open;
                if (!onCollapsed && !onTitle)
                    CloseMenu();
            }
            if (const int tab = TabAt(e.localPosition); tab >= 0)
            {
                // 收起时单击任意选项卡就展开（同时切到它）；双击选项卡收起 / 展开。
                // 单击刚展开的那次双击不再收起，免得双击收起的功能区一闪又收回去
                if (e.clickCount == 2)
                {
                    if (!m_expandedByClick)
                        SetCollapsed(!m_collapsed);
                    m_expandedByClick = false;
                }
                else
                {
                    m_expandedByClick = m_collapsed;
                    SetCollapsed(false);
                    SelectTab(tab);
                }
                e.handled = true;
            }
            else if (const int title = PanelTitleAt(e.localPosition); title >= 0)
            {
                const Panel& p = m_tabs[static_cast<size_t>(m_selected)].panels[static_cast<size_t>(title)];
                const Rect b = GetScreenBounds();
                const float top = kTabRowH + kGroupH - kSectionH;
                ShowPanelMenu(p, Rect{ b.min.x + p.x0, b.min.y + top, b.min.x + p.x1, b.min.y + kTabRowH + kGroupH }, this);
                e.handled = true;
            }
            break;
        default:
            break;
        }
    }
}
