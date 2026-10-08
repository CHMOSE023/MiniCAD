#include "Widgets/DockSpace.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr float kTabPad   = 12.0f;
        constexpr float kCloseW   = 16.0f;
        constexpr float kTabMaxW  = 200.0f;
        constexpr float kSideFrac = 0.3f;       // 标签组边缘多大比例算拆分到这一侧
        constexpr float kRootSize = 250.0f;     // 停靠到整体边缘时的默认尺寸

        TextParams TabText(ColorRef color)
        {
            TextParams p;
            p.size     = 13.0f;
            p.color    = color;
            p.vAlign   = TextAlign::Center;
            p.ellipsis = true;
            return p;
        }

        bool IsHorizontalSide(DockSide side) { return side == DockSide::Left || side == DockSide::Right; }
        bool IsLeadingSide(DockSide side)    { return side == DockSide::Left || side == DockSide::Top; }
    }

    // =========================================================
    // 标签组：标题栏（标签）+ 当前面板
    // =========================================================
    class DockGroup : public Node
    {
    public:
        DockGroup(DockSpace* dock, DockSpace::Tree* leaf) : m_dock(dock), m_leaf(leaf) { SetClipChildren(true); }

        DockSpace::Tree* GetLeaf() const { return m_leaf; }

        bool HasHeader() const
        {
            for (const std::string& id : m_leaf->panels)
            {
                if (!m_dock->m_panels.at(id).options.showHeader)
                    return false;
            }
            return true;
        }

        float HeaderHeight() const { return HasHeader() ? DockSpace::kHeaderHeight : 0.0f; }

        // 标签的横向范围（本地坐标）
        struct TabGeom { float x0, x1; Rect close; bool closable; };
        std::vector<TabGeom> Tabs() const
        {
            std::vector<TabGeom> tabs;
            UIContext* ctx = GetContext();
            if (!ctx || !HasHeader())
                return tabs;
            const float avail = GetSize().x - 4.0f;
            std::vector<float> widths;
            float total = 0.0f;
            for (const std::string& id : m_leaf->panels)
            {
                const auto& p = m_dock->m_panels.at(id);
                float w = ctx->GetTextSystem().Measure(p.title, TabText(Theme::Text)).x + kTabPad * 2.0f;
                if (p.options.closable)
                    w += kCloseW;
                w = std::min(w, kTabMaxW);
                widths.push_back(w);
                total += w;
            }
            const float scale = total > avail && total > 0.0f ? avail / total : 1.0f;     // 放不下时等比缩窄
            float x = 2.0f;
            for (size_t i = 0; i < widths.size(); ++i)
            {
                const float w  = std::floor(widths[i] * scale);
                const bool  cl = m_dock->m_panels.at(m_leaf->panels[i]).options.closable;
                const float cy = DockSpace::kHeaderHeight * 0.5f;
                tabs.push_back({ x, x + w, Rect{ x + w - kCloseW - 4.0f, cy - 8.0f, x + w - 4.0f, cy + 8.0f }, cl });
                x += w;
            }
            return tabs;
        }

        int TabAt(Vec2 local) const
        {
            if (local.y < 0.0f || local.y >= HeaderHeight())
                return -1;
            const auto tabs = Tabs();
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                if (local.x >= tabs[i].x0 && local.x < tabs[i].x1)
                    return static_cast<int>(i);
            }
            return -1;
        }

    protected:
        void OnLayout() override
        {
            const Vec2 size = GetSize();
            const Rect content{ 0.0f, HeaderHeight(), size.x, std::max(size.y, HeaderHeight()) };
            for (const auto& c : GetChildren())
                c->SetBounds(content);
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            dl.AddRectFilled(r, Theme::Panel);
            if (!HasHeader())
                return;
            UIContext* ctx = GetContext();
            const Rect header{ r.min.x, r.min.y, r.max.x, r.min.y + DockSpace::kHeaderHeight };
            dl.AddRectFilled(header, Theme::PanelAlt);
            dl.AddRectFilled(Rect{ header.min.x, header.max.y - 1.0f, header.max.x, header.max.y }, Theme::BorderSubtle);

            const auto tabs = Tabs();
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                const TabGeom& t      = tabs[i];
                const bool     active = static_cast<int>(i) == m_leaf->active;
                const bool     hover  = static_cast<int>(i) == m_hoverTab;
                const Rect     tab{ r.min.x + t.x0, header.min.y, r.min.x + t.x1, header.max.y - (active ? 0.0f : 1.0f) };
                if (active)
                {
                    dl.AddRectFilled(tab, Theme::Panel);
                    dl.AddRectFilled(Rect{ tab.min.x, tab.min.y, tab.max.x, tab.min.y + 2.0f }, Theme::Accent);
                }
                else if (hover)
                {
                    dl.AddRectFilled(tab, Theme::RowHover);
                }
                const std::string& title = m_dock->m_panels.at(m_leaf->panels[i]).title;
                const float textRight = t.closable ? tab.max.x - kCloseW - 4.0f : tab.max.x - kTabPad;
                if (ctx)
                    ctx->GetTextSystem().Draw(dl, Rect{ tab.min.x + kTabPad, tab.min.y, textRight, tab.max.y }, title,
                                              TabText(active ? Theme::Text : Theme::TextDim));
                if (t.closable && (active || hover))
                {
                    const Rect cr{ t.close.min + r.min, t.close.max + r.min };
                    if (hover && m_hoverClose)
                        dl.AddRectFilled(cr, Theme::ControlHover, 3.0f);
                    dl.AddCross(cr.Center(), 3.5f, Theme::TextDim, 1.3f);
                }
            }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase != EventPhase::Target)
                return;
            UIContext* ctx = GetContext();

            switch (e.type)
            {
            case PointerEventType::Move:
            {
                if (m_pressTab >= 0 && HasCapture())
                {
                    if (!m_dragStarted)
                    {
                        if ((e.position - m_pressPos).Length() >= DockSpace::kDragStart && !m_pressOnClose)
                        {
                            m_dragStarted = true;
                            m_dock->BeginDrag(m_leaf->panels[static_cast<size_t>(m_pressTab)], e.position);
                        }
                    }
                    else if (m_dock->IsDragging())
                    {
                        m_dock->UpdateDrag(e.position);
                    }
                    e.handled = true;
                    return;
                }
                UpdateHover(e.localPosition);
                break;
            }
            case PointerEventType::Leave:
                m_hoverTab   = -1;
                m_hoverClose = false;
                Invalidate();
                break;

            case PointerEventType::Down:
            {
                if (e.button != MouseButton::Left)
                    return;
                const int tab = TabAt(e.localPosition);
                if (tab < 0)
                    return;
                const auto tabs = Tabs();
                m_pressTab     = tab;
                m_pressPos     = e.position;
                m_pressOnClose = tabs[static_cast<size_t>(tab)].closable && tabs[static_cast<size_t>(tab)].close.Contains(e.localPosition);
                m_dragStarted  = false;
                if (ctx)
                    ctx->SetCapture(this);
                if (!m_pressOnClose && tab != m_leaf->active)
                    m_dock->ActivatePanel(m_leaf->panels[static_cast<size_t>(tab)]);
                e.handled = true;
                break;
            }
            case PointerEventType::Up:
            {
                if (e.button != MouseButton::Left || m_pressTab < 0)
                    return;
                const int  tab      = m_pressTab;
                const bool started  = m_dragStarted;
                const bool onClose  = m_pressOnClose;
                m_pressTab    = -1;
                m_dragStarted = false;
                e.handled     = true;
                // 下面的操作可能重建停靠区并销毁本节点（推迟销毁），调用之后不再访问成员
                if (started)
                {
                    if (m_dock->IsDragging())
                        m_dock->EndDrag(e.position, false);
                    return;
                }
                if (onClose)
                {
                    const auto tabs = Tabs();
                    if (static_cast<size_t>(tab) < tabs.size() && tabs[static_cast<size_t>(tab)].close.Contains(e.localPosition))
                        m_dock->ClosePanel(m_leaf->panels[static_cast<size_t>(tab)]);
                }
                return;
            }
            case PointerEventType::Cancel:
                if (m_dragStarted && m_dock->IsDragging())
                    m_dock->EndDrag(e.position, true);
                m_pressTab    = -1;
                m_dragStarted = false;
                return;
            default:
                break;
            }
        }

    private:
        void UpdateHover(Vec2 local)
        {
            const int  tab   = TabAt(local);
            bool       close = false;
            if (tab >= 0)
            {
                const auto tabs = Tabs();
                close = tabs[static_cast<size_t>(tab)].closable && tabs[static_cast<size_t>(tab)].close.Contains(local);
            }
            if (tab != m_hoverTab || close != m_hoverClose)
            {
                m_hoverTab   = tab;
                m_hoverClose = close;
                Invalidate();
            }
        }

        DockSpace*       m_dock;
        DockSpace::Tree* m_leaf;
        int              m_hoverTab     = -1;
        bool             m_hoverClose   = false;
        int              m_pressTab     = -1;
        bool             m_pressOnClose = false;
        bool             m_dragStarted  = false;
        Vec2             m_pressPos;
    };

    // =========================================================
    // 分隔条：调整分割中相邻两个子节点的尺寸
    // =========================================================
    class DockSplitter : public Node
    {
    public:
        DockSplitter(DockSpace* dock, DockSpace::Tree* split, size_t index) : m_dock(dock), m_split(split), m_index(index) {}

        DockSpace::Tree* GetSplit() const { return m_split; }
        size_t           GetIndex() const { return m_index; }

        CursorShape GetCursor(Vec2) const override { return m_split->horizontal ? CursorShape::SizeWE : CursorShape::SizeNS; }

    protected:
        void OnPaint(DrawList& dl, const Rect& r) override
        {
            // 节点比分界线宽（热区叠在两侧面板上），平时只画中间的分界线，悬停 / 拖动时画一条稍宽的强调色
            const bool  hot = m_dragging || IsHovered();
            const float w   = hot ? 3.0f : DockSpace::kSplitterSize;
            const Vec2  c   = r.Center();
            const Rect line = m_split->horizontal ? Rect{ c.x - w * 0.5f, r.min.y, c.x + w * 0.5f, r.max.y }
                                                  : Rect{ r.min.x, c.y - w * 0.5f, r.max.x, c.y + w * 0.5f };
            dl.AddRectFilled(line, hot ? Theme::Accent : Theme::BorderSubtle);
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase != EventPhase::Target)
                return;
            UIContext* ctx = GetContext();
            const float pos = m_split->horizontal ? e.position.x : e.position.y;
            switch (e.type)
            {
            case PointerEventType::Enter:
            case PointerEventType::Leave:
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.button != MouseButton::Left)
                    return;
                m_dragging = true;
                m_start    = pos;
                m_a = Extent(*m_split->children[m_index]);
                m_b = Extent(*m_split->children[m_index + 1]);
                if (ctx)
                    ctx->SetCapture(this);
                e.handled = true;
                break;
            case PointerEventType::Move:
                if (!m_dragging)
                    return;
                Apply(pos - m_start);
                e.handled = true;
                break;
            case PointerEventType::Up:
            case PointerEventType::Cancel:
                if (m_dragging)
                {
                    m_dragging = false;
                    Invalidate();
                    m_dock->LayoutChanged();
                }
                e.handled = true;
                break;
            default:
                break;
            }
        }

    private:
        float Extent(const DockSpace::Tree& t) const { return m_split->horizontal ? t.rect.Width() : t.rect.Height(); }

        void Apply(float delta)
        {
            // 两侧都不小于最小尺寸
            delta = std::clamp(delta, DockSpace::kMinPanel - m_a, m_b - DockSpace::kMinPanel);
            DockSpace::Tree& a = *m_split->children[m_index];
            DockSpace::Tree& b = *m_split->children[m_index + 1];
            if (a.size > 0.0f && b.size > 0.0f)
            {
                a.size = m_a + delta;
                b.size = m_b - delta;
            }
            else if (a.size > 0.0f)
            {
                a.size = m_a + delta;           // b 占剩余空间
            }
            else if (b.size > 0.0f)
            {
                b.size = m_b - delta;
            }
            else
            {
                a.size = m_a + delta;           // 都占剩余空间：前一个固定下来
            }
            m_dock->InvalidateLayout();
        }

        DockSpace*       m_dock;
        DockSpace::Tree* m_split;
        size_t           m_index;
        bool             m_dragging = false;
        float            m_start = 0.0f, m_a = 0.0f, m_b = 0.0f;
    };

    // =========================================================
    // DockSpace：面板
    // =========================================================
    DockSpace::DockSpace()
    {
        SetClipChildren(true);
    }

    DockSpace::~DockSpace()
    {
        if (m_drag.escShortcut)
        {
            if (UIContext* ctx = GetContext())
                ctx->GetShortcuts().Unregister(m_drag.escShortcut);
        }
    }

    void DockSpace::AddPanel(const std::string& id, std::string title, std::unique_ptr<Node> content, DockPanelOptions options)
    {
        if (m_panels.count(id))
        {
            const bool visible = IsPanelVisible(id);
            if (visible)
                RemoveFromTree(id);
            Rebuild();
        }
        Panel& p  = m_panels[id];
        p.title   = std::move(title);
        p.options = options;
        p.node    = content.get();
        p.owned   = std::move(content);
    }

    Node* DockSpace::GetPanelContent(const std::string& id) const
    {
        const auto it = m_panels.find(id);
        return it == m_panels.end() ? nullptr : it->second.node;
    }

    bool DockSpace::IsPanelVisible(const std::string& id) const
    {
        return FindLeaf(id) != nullptr;
    }

    bool DockSpace::IsPanelActive(const std::string& id) const
    {
        const Tree* leaf = FindLeaf(id);
        return leaf && leaf->panels[static_cast<size_t>(leaf->active)] == id;
    }

    void DockSpace::SetPanelTitle(const std::string& id, std::string title)
    {
        if (auto it = m_panels.find(id); it != m_panels.end())
        {
            it->second.title = std::move(title);
            for (const auto& c : GetChildren())
                c->Invalidate();
        }
    }

    std::vector<std::string> DockSpace::GetPanelIds() const
    {
        std::vector<std::string> ids;
        for (const auto& [id, p] : m_panels)
            ids.push_back(id);
        return ids;
    }

    std::map<std::string, std::unique_ptr<Node>> DockSpace::TakeAllPanels()
    {
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        m_root.reset();
        Rebuild();      // 内容摘回 Panel::owned
        std::map<std::string, std::unique_ptr<Node>> result;
        for (auto& [id, p] : m_panels)
        {
            if (p.owned)
                result[id] = std::move(p.owned);
        }
        m_panels.clear();
        return result;
    }

    // =========================================================
    // 树操作
    // =========================================================
    DockSpace::Tree* DockSpace::FindLeaf(const std::string& panelId) const
    {
        return m_root ? FindLeaf(m_root.get(), panelId) : nullptr;
    }

    DockSpace::Tree* DockSpace::FindLeaf(Tree* node, const std::string& panelId) const
    {
        if (!node->split)
            return std::find(node->panels.begin(), node->panels.end(), panelId) != node->panels.end() ? node : nullptr;
        for (const auto& c : node->children)
        {
            if (Tree* t = FindLeaf(c.get(), panelId))
                return t;
        }
        return nullptr;
    }

    DockSpace::Tree* DockSpace::LeafAt(Vec2 local) const
    {
        return m_root ? LeafAt(m_root.get(), local) : nullptr;
    }

    DockSpace::Tree* DockSpace::LeafAt(Tree* node, Vec2 local) const
    {
        if (!node->rect.Contains(local))
            return nullptr;
        if (!node->split)
            return node;
        for (const auto& c : node->children)
        {
            if (Tree* t = LeafAt(c.get(), local))
                return t;
        }
        return nullptr;
    }

    bool DockSpace::AcceptsTabs(const Tree* leaf) const
    {
        for (const std::string& id : leaf->panels)
        {
            if (!m_panels.at(id).options.showHeader)
                return false;
        }
        return true;
    }

    void DockSpace::SetParents(Tree* node, Tree* parent)
    {
        node->parent = parent;
        for (const auto& c : node->children)
            SetParents(c.get(), node);
    }

    void DockSpace::Collapse(Tree* node)
    {
        // 空分割删除；只剩一个子节点的分割用子节点替换（子节点继承分割的尺寸）
        while (node && node->split && node->children.size() <= 1)
        {
            Tree* parent = node->parent;
            std::unique_ptr<Tree> only;
            if (!node->children.empty())
            {
                only = std::move(node->children.front());
                only->size   = node->size;
                only->parent = parent;
            }
            if (!parent)
            {
                m_root = std::move(only);
                return;
            }
            auto& siblings = parent->children;
            auto  it = std::find_if(siblings.begin(), siblings.end(), [node](const auto& c) { return c.get() == node; });
            if (only)
                *it = std::move(only);
            else
                siblings.erase(it);
            node = parent;
        }
    }

    void DockSpace::RemoveFromTree(const std::string& panelId)
    {
        Tree* leaf = FindLeaf(panelId);
        if (!leaf)
            return;
        auto it = std::find(leaf->panels.begin(), leaf->panels.end(), panelId);
        const int index = static_cast<int>(it - leaf->panels.begin());
        leaf->panels.erase(it);
        if (leaf->active > index || leaf->active >= static_cast<int>(leaf->panels.size()))
            leaf->active = std::max(0, leaf->active - 1);
        if (!leaf->panels.empty())
            return;

        Tree* parent = leaf->parent;
        if (!parent)
        {
            m_root.reset();
            return;
        }
        auto& siblings = parent->children;
        siblings.erase(std::find_if(siblings.begin(), siblings.end(), [leaf](const auto& c) { return c.get() == leaf; }));
        Collapse(parent);
    }

    void DockSpace::RememberPosition(const std::string& panelId)
    {
        Tree*  leaf = FindLeaf(panelId);
        Panel& p    = m_panels.at(panelId);
        p.lastTarget.clear();
        if (!leaf)
            return;

        // 同组还有别的面板：回来时合并到它们中间
        for (const std::string& other : leaf->panels)
        {
            if (other != panelId)
            {
                p.lastTarget = other;
                p.lastSide   = DockSide::Center;
                return;
            }
        }

        Tree* parent = leaf->parent;
        if (!parent)
            return;
        auto& siblings = parent->children;
        const size_t i = static_cast<size_t>(std::find_if(siblings.begin(), siblings.end(),
                                                          [leaf](const auto& c) { return c.get() == leaf; }) - siblings.begin());
        // 优先记住后一个兄弟（自己在它的左 / 上侧），没有则记前一个
        const bool  useNext = i + 1 < siblings.size();
        const Tree* sib     = siblings[useNext ? i + 1 : i - 1].get();
        while (sib->split)
            sib = sib->children.front().get();
        p.lastTarget = sib->panels.front();
        p.lastSide   = parent->horizontal ? (useNext ? DockSide::Left : DockSide::Right)
                                          : (useNext ? DockSide::Top : DockSide::Bottom);
        p.lastSize   = parent->horizontal ? leaf->rect.Width() : leaf->rect.Height();
        if (p.lastSize <= 0.0f)
            p.lastSize = leaf->size;
    }

    bool DockSpace::InsertPanel(const std::string& id, Tree* target, DockSide side, float size, int tabIndex)
    {
        auto leaf = std::make_unique<Tree>();
        leaf->panels.push_back(id);

        if (!m_root)
        {
            m_root = std::move(leaf);
            return true;
        }

        if (side == DockSide::Center)
        {
            if (!target || !AcceptsTabs(target))
                return false;
            const int index = tabIndex < 0 || tabIndex > static_cast<int>(target->panels.size())
                            ? static_cast<int>(target->panels.size()) : tabIndex;
            target->panels.insert(target->panels.begin() + index, id);
            target->active = index;
            return true;
        }

        const bool horizontal = IsHorizontalSide(side);
        const bool leading    = IsLeadingSide(side);

        // 整个停靠区的一侧
        if (!target)
        {
            const float extent = horizontal ? GetSize().x : GetSize().y;
            leaf->size = size > 0.0f ? size : std::min(kRootSize, extent > 0.0f ? extent * 0.4f : kRootSize);
            if (m_root->split && m_root->horizontal == horizontal)
            {
                auto& ch = m_root->children;
                ch.insert(leading ? ch.begin() : ch.end(), std::move(leaf));
            }
            else
            {
                auto split = std::make_unique<Tree>();
                split->split      = true;
                split->horizontal = horizontal;
                m_root->size      = 0.0f;       // 原来的整体占剩余空间
                split->children.push_back(std::move(m_root));
                split->children.insert(leading ? split->children.begin() : split->children.end(), std::move(leaf));
                m_root = std::move(split);
            }
            return true;
        }

        // 目标标签组的一侧
        const float targetExtent = horizontal ? target->rect.Width() : target->rect.Height();
        leaf->size = size > 0.0f ? size : (targetExtent > 0.0f ? std::floor(targetExtent * 0.5f) : kRootSize);

        Tree* parent = target->parent;
        if (parent && parent->horizontal == horizontal)
        {
            auto& ch = parent->children;
            auto  it = std::find_if(ch.begin(), ch.end(), [target](const auto& c) { return c.get() == target; });
            if (target->size > 0.0f)
                target->size = std::max(kMinPanel, target->size - leaf->size);     // 两者共享原来的尺寸
            ch.insert(leading ? it : it + 1, std::move(leaf));
            return true;
        }

        // 目标所在分割方向不同（或目标是根）：用一个新分割替换目标
        auto split = std::make_unique<Tree>();
        split->split      = true;
        split->horizontal = horizontal;
        split->size       = target->size;
        std::unique_ptr<Tree>* slot = nullptr;
        if (!parent)
            slot = &m_root;
        else
            slot = &*std::find_if(parent->children.begin(), parent->children.end(), [target](const auto& c) { return c.get() == target; });
        std::unique_ptr<Tree> old = std::move(*slot);
        old->size = 0.0f;
        split->children.push_back(std::move(old));
        split->children.insert(leading ? split->children.begin() : split->children.end(), std::move(leaf));
        *slot = std::move(split);
        return true;
    }

    // =========================================================
    // 停靠操作
    // =========================================================
    bool DockSpace::Dock(const std::string& id, const std::string& target, DockSide side, float size)
    {
        if (!m_panels.count(id) || id == target)
            return false;
        if (!target.empty())
        {
            const Tree* t = FindLeaf(target);
            if (!t || (t == FindLeaf(id) && t->panels.size() == 1))
                return false;
        }

        RemoveFromTree(id);
        if (m_root)
            SetParents(m_root.get(), nullptr);
        Tree* targetLeaf = target.empty() ? nullptr : FindLeaf(target);
        const bool ok = InsertPanel(id, targetLeaf, side, size, -1);
        if (!ok)
            InsertPanel(id, nullptr, DockSide::Right, size, -1);     // 无法合并（例如目标是文档区）：退回到右侧
        Rebuild();
        return ok;
    }

    void DockSpace::ClosePanel(const std::string& id)
    {
        auto it = m_panels.find(id);
        if (it == m_panels.end() || !IsPanelVisible(id) || !it->second.options.closable)
            return;
        RememberPosition(id);
        RemoveFromTree(id);
        Rebuild();
        LayoutChanged();
    }

    void DockSpace::ShowPanel(const std::string& id)
    {
        auto it = m_panels.find(id);
        if (it == m_panels.end())
            return;
        if (!IsPanelVisible(id))
        {
            const Panel& p = it->second;
            if (!p.lastTarget.empty() && IsPanelVisible(p.lastTarget))
                Dock(id, p.lastTarget, p.lastSide, p.lastSize);
            else
                Dock(id, {}, DockSide::Right, 260.0f);
            LayoutChanged();
        }
        ActivatePanel(id);
    }

    void DockSpace::ActivatePanel(const std::string& id)
    {
        Tree* leaf = FindLeaf(id);
        if (!leaf)
            return;
        const int index = static_cast<int>(std::find(leaf->panels.begin(), leaf->panels.end(), id) - leaf->panels.begin());
        if (leaf->active == index)
            return;
        leaf->active = index;
        for (size_t i = 0; i < leaf->panels.size(); ++i)
            m_panels.at(leaf->panels[i]).node->SetVisible(static_cast<int>(i) == index);
        InvalidateLayout();
        for (const auto& c : GetChildren())
            c->Invalidate();
        LayoutChanged();
    }

    void DockSpace::LayoutChanged()
    {
        if (m_onLayoutChanged)
        {
            auto cb = m_onLayoutChanged;
            cb();
        }
    }

    // =========================================================
    // 布局
    // =========================================================
    void DockSpace::LayoutTree(Tree* node, const Rect& rect)
    {
        node->rect = rect;
        if (!node->split || node->children.empty())
            return;

        const float scale = GetContext() ? GetContext()->GetPixelScale() : 1.0f;
        auto snap = [scale](float v) { return std::round(v * scale) / scale; };

        const size_t n     = node->children.size();
        const bool   horiz = node->horizontal;
        const float  total = (horiz ? rect.Width() : rect.Height()) - kSplitterSize * static_cast<float>(n - 1);

        // 固定尺寸的子节点按 size，其余平分剩余空间；没有占剩余空间的子节点时最后一个占剩余空间
        std::vector<float> sizes(n, 0.0f);
        size_t growCount = 0;
        float  fixed     = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            const bool grow = node->children[i]->size <= 0.0f || (i + 1 == n && growCount == 0 &&
                              std::all_of(node->children.begin(), node->children.end(), [](const auto& c) { return c->size > 0.0f; }));
            if (grow)
                ++growCount;
            else
            {
                sizes[i] = std::max(kMinPanel, node->children[i]->size);
                fixed += sizes[i];
            }
        }
        const float growMin = kMinPanel * static_cast<float>(growCount);
        if (fixed > 0.0f && fixed > total - growMin)
        {
            // 放不下：固定尺寸的子节点按比例缩小
            const float s = std::max(0.0f, total - growMin) / fixed;
            for (size_t i = 0; i < n; ++i)
                sizes[i] *= s;
            fixed = std::max(0.0f, total - growMin);
        }
        const float each = growCount ? std::max(0.0f, total - fixed) / static_cast<float>(growCount) : 0.0f;

        float pos = horiz ? rect.min.x : rect.min.y;
        for (size_t i = 0; i < n; ++i)
        {
            const bool  grow = sizes[i] <= 0.0f;
            const float len  = grow ? each : sizes[i];
            const float a    = snap(pos);
            const float b    = i + 1 == n ? (horiz ? rect.max.x : rect.max.y) : snap(pos + len);
            const Rect  r    = horiz ? Rect{ a, rect.min.y, b, rect.max.y } : Rect{ rect.min.x, a, rect.max.x, b };
            LayoutTree(node->children[i].get(), r);
            pos = b + kSplitterSize;
        }
    }

    void DockSpace::OnLayout()
    {
        if (m_root)
            LayoutTree(m_root.get(), Rect{ { 0.0f, 0.0f }, GetSize() });

        for (const auto& c : GetChildren())
        {
            if (auto* g = dynamic_cast<DockGroup*>(c.get()))
            {
                g->SetBounds(g->GetLeaf()->rect);
            }
            else if (auto* sp = dynamic_cast<DockSplitter*>(c.get()))
            {
                const Tree* split = sp->GetSplit();
                const Rect& a = split->children[sp->GetIndex()]->rect;
                const Rect& b = split->children[sp->GetIndex() + 1]->rect;
                // 热区以分界线为中心向两侧扩展（分隔条节点在标签组之后添加，命中测试优先）
                const float pad = (kSplitterHit - kSplitterSize) * 0.5f;
                sp->SetBounds(split->horizontal ? Rect{ a.max.x - pad, split->rect.min.y, b.min.x + pad, split->rect.max.y }
                                                : Rect{ split->rect.min.x, a.max.y - pad, split->rect.max.x, b.min.y + pad });
            }
        }
    }

    void DockSpace::Rebuild()
    {
        UIContext* ctx = GetContext();
        // 1. 面板内容从旧的标签组上摘下（不销毁）
        for (auto& [id, p] : m_panels)
        {
            if (!p.owned && p.node && p.node->GetParent())
                p.owned = p.node->GetParent()->RemoveChild(p.node);
        }
        // 2. 旧的标签组和分隔条推迟销毁：可能正在执行它们自己的事件处理函数
        while (!GetChildren().empty())
        {
            std::unique_ptr<Node> old = RemoveChild(GetChildren().back().get());
            if (ctx)
                ctx->DeferDelete(std::move(old));
        }
        // 3. 按树重建
        if (m_root)
        {
            SetParents(m_root.get(), nullptr);
            BuildNodes(m_root.get());
        }
        InvalidateLayout();
    }

    void DockSpace::BuildNodes(Tree* node)
    {
        if (!node->split)
        {
            node->active = std::clamp(node->active, 0, std::max(0, static_cast<int>(node->panels.size()) - 1));
            auto* group = AddChild<DockGroup>(this, node);
            for (size_t i = 0; i < node->panels.size(); ++i)
            {
                Panel& p = m_panels.at(node->panels[i]);
                group->AddChild(std::move(p.owned));
                p.node->SetVisible(static_cast<int>(i) == node->active);
            }
            return;
        }
        for (const auto& c : node->children)
            BuildNodes(c.get());
        for (size_t i = 0; i + 1 < node->children.size(); ++i)
            AddChild<DockSplitter>(this, node, i);
    }

    Rect DockSpace::GetPanelRect(const std::string& id) const
    {
        const Tree* leaf = FindLeaf(id);
        if (!leaf || !IsPanelActive(id))
            return {};
        const Node* content = m_panels.at(id).node;
        return content->GetParent() ? content->GetScreenBounds() : Rect{};
    }

    // =========================================================
    // 拖动
    // =========================================================
    void DockSpace::BeginDrag(const std::string& panelId, Vec2 windowPos)
    {
        m_drag        = {};
        m_drag.active = true;
        m_drag.panel  = panelId;
        m_drag.pos    = windowPos;
        if (UIContext* ctx = GetContext())
        {
            ShortcutTable::Options opt;
            opt.allowInTextInput = true;
            m_drag.escShortcut = ctx->GetShortcuts().Register(Key::Escape, 0, [this] { EndDrag(m_drag.pos, true); }, opt);
        }
        UpdateDrag(windowPos);
    }

    void DockSpace::UpdateDrag(Vec2 windowPos)
    {
        if (!m_drag.active)
            return;
        m_drag.pos    = windowPos;
        m_drag.target = ComputeDrop(ToLocal(windowPos));
        Invalidate();
    }

    void DockSpace::EndDrag(Vec2 windowPos, bool cancel)
    {
        if (!m_drag.active)
            return;
        if (m_drag.escShortcut)
        {
            if (UIContext* ctx = GetContext())
                ctx->GetShortcuts().Unregister(m_drag.escShortcut);
        }
        const DropTarget  target = cancel ? DropTarget{} : ComputeDrop(ToLocal(windowPos));
        const std::string panel  = m_drag.panel;
        m_drag = {};
        Invalidate();
        if (!target.valid)
            return;

        // 合并到同一组：只调整标签顺序
        Tree* source = FindLeaf(panel);
        if (target.side == DockSide::Center && target.leaf == source)
        {
            auto& ps = source->panels;
            const int from = static_cast<int>(std::find(ps.begin(), ps.end(), panel) - ps.begin());
            int to = target.index < 0 ? static_cast<int>(ps.size()) - 1 : target.index;
            if (to > from)
                --to;
            ps.erase(ps.begin() + from);
            ps.insert(ps.begin() + std::clamp(to, 0, static_cast<int>(ps.size())), panel);
            source->active = static_cast<int>(std::find(ps.begin(), ps.end(), panel) - ps.begin());
            Rebuild();
            LayoutChanged();
            return;
        }

        // 目标组用其中一个面板标识（移除源面板后树结构会变，组指针可能失效）
        std::string targetPanel;
        if (target.leaf)
        {
            for (const std::string& id : target.leaf->panels)
            {
                if (id != panel)
                {
                    targetPanel = id;
                    break;
                }
            }
        }
        const float size = target.leaf ? 0.0f
                         : (IsHorizontalSide(target.side) ? target.preview.Width() : target.preview.Height());

        RemoveFromTree(panel);
        Tree* leaf = targetPanel.empty() ? nullptr : FindLeaf(targetPanel);
        if (m_root)
            SetParents(m_root.get(), nullptr);
        if (!InsertPanel(panel, leaf, target.side, size, target.index))
            InsertPanel(panel, nullptr, DockSide::Right, 0.0f, -1);
        Rebuild();
        LayoutChanged();
    }

    DockSpace::DropTarget DockSpace::ComputeDrop(Vec2 local) const
    {
        DropTarget t;
        const Vec2 size = GetSize();
        if (local.x < 0.0f || local.y < 0.0f || local.x >= size.x || local.y >= size.y)
            return t;

        const Tree* source = FindLeaf(m_drag.panel);
        const bool  alone  = source && source->panels.size() == 1;

        Tree* leaf = LeafAt(local);

        // 标题栏：合并为标签（按位置插入）；优先于边缘判断（顶部组的标题栏就在停靠区上边缘）
        const bool header = leaf && AcceptsTabs(leaf) && local.y < leaf->rect.min.y + kHeaderHeight;
        if (header)
        {
            t.valid   = true;
            t.leaf    = leaf;
            t.side    = DockSide::Center;
            t.preview = leaf->rect;
            // 插入位置：光标所在标签的左半边插在它前面，右半边插在后面
            for (const auto& c : GetChildren())
            {
                auto* g = dynamic_cast<DockGroup*>(c.get());
                if (!g || g->GetLeaf() != leaf)
                    continue;
                const auto tabs = g->Tabs();
                const float x = local.x - leaf->rect.min.x;
                t.index = static_cast<int>(tabs.size());
                for (size_t i = 0; i < tabs.size(); ++i)
                {
                    if (x < (tabs[i].x0 + tabs[i].x1) * 0.5f)
                    {
                        t.index = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (leaf == source && alone)
                t.valid = false;
            return t;
        }

        // 贴近停靠区边缘：停靠到整体的一侧
        const float edge = kEdgeDrop;
        DockSide    side = DockSide::Center;
        if (local.x < edge)               side = DockSide::Left;
        else if (local.x >= size.x - edge) side = DockSide::Right;
        else if (local.y < edge)           side = DockSide::Top;
        else if (local.y >= size.y - edge) side = DockSide::Bottom;
        if (side != DockSide::Center && !(alone && m_root.get() == source))
        {
            const float extent = IsHorizontalSide(side) ? size.x : size.y;
            const float s      = std::min(kRootSize, extent * 0.4f);
            t.valid   = true;
            t.leaf    = nullptr;
            t.side    = side;
            t.preview = side == DockSide::Left   ? Rect{ 0, 0, s, size.y }
                      : side == DockSide::Right  ? Rect{ size.x - s, 0, size.x, size.y }
                      : side == DockSide::Top    ? Rect{ 0, 0, size.x, s }
                                                 : Rect{ 0, size.y - s, size.x, size.y };
            return t;
        }

        if (!leaf)
            return t;
        const Rect& r = leaf->rect;

        // 内容区：靠近哪条边就拆到哪一侧，中间合并为标签
        const float rx = (local.x - r.min.x) / std::max(1.0f, r.Width());
        const float ry = (local.y - r.min.y) / std::max(1.0f, r.Height());
        const float d[4] = { rx, 1.0f - rx, ry, 1.0f - ry };
        const DockSide sides[4] = { DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom };
        const int nearest = static_cast<int>(std::min_element(d, d + 4) - d);
        side = (d[nearest] < kSideFrac || !AcceptsTabs(leaf)) ? sides[nearest] : DockSide::Center;

        if (leaf == source && (alone || side == DockSide::Center))
            return t;           // 拖回自己：没有变化

        t.valid = true;
        t.leaf  = leaf;
        t.side  = side;
        const float hw = std::floor(r.Width() * 0.5f), hh = std::floor(r.Height() * 0.5f);
        switch (side)
        {
        case DockSide::Left:   t.preview = Rect{ r.min.x, r.min.y, r.min.x + hw, r.max.y }; break;
        case DockSide::Right:  t.preview = Rect{ r.max.x - hw, r.min.y, r.max.x, r.max.y }; break;
        case DockSide::Top:    t.preview = Rect{ r.min.x, r.min.y, r.max.x, r.min.y + hh }; break;
        case DockSide::Bottom: t.preview = Rect{ r.min.x, r.max.y - hh, r.max.x, r.max.y }; break;
        default:               t.preview = r; break;
        }
        return t;
    }

    bool DockSpace::GetDropPreview(Rect& out) const
    {
        if (!m_drag.active || !m_drag.target.valid)
            return false;
        const Vec2 o = GetScreenBounds().min;
        out = Rect{ m_drag.target.preview.min + o, m_drag.target.preview.max + o };
        return true;
    }

    void DockSpace::OnPaintOverlay(DrawList& dl, const Rect& screenRect)
    {
        if (!m_drag.active)
            return;
        Rect preview;
        if (GetDropPreview(preview))
        {
            dl.AddRectFilled(preview.Deflated(2.0f), Theme::AccentSoft, 4.0f);
            dl.AddRect(preview.Deflated(2.0f), Theme::Accent, 4.0f, 2.0f);
        }
        // 光标旁显示被拖动的面板名称
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        const std::string& title = m_panels.at(m_drag.panel).title;
        TextParams p = TabText(Theme::Text);
        const float w = ctx->GetTextSystem().Measure(title, p).x + kTabPad * 2.0f;
        const Rect ghost{ m_drag.pos.x + 12.0f, m_drag.pos.y + 8.0f, m_drag.pos.x + 12.0f + w, m_drag.pos.y + 8.0f + 24.0f };
        (void)screenRect;
        dl.AddRectFilled(ghost, Theme::Panel, 4.0f);
        dl.AddRect(ghost, Theme::Accent, 4.0f, 1.0f);
        ctx->GetTextSystem().Draw(dl, Rect{ ghost.min.x + kTabPad, ghost.min.y, ghost.max.x, ghost.max.y }, title, p);
    }

    // =========================================================
    // 保存 / 加载
    // =========================================================
    JsonValue DockSpace::SaveTree(const Tree* node) const
    {
        JsonValue v = JsonValue::MakeObject();
        if (node->split)
        {
            v.Set("split", node->horizontal ? "row" : "column");
            if (node->size > 0.0f)
                v.Set("size", std::round(node->size));
            JsonValue children = JsonValue::MakeArray();
            for (const auto& c : node->children)
                children.Push(SaveTree(c.get()));
            v.Set("children", std::move(children));
            return v;
        }
        JsonValue tabs = JsonValue::MakeArray();
        for (const std::string& id : node->panels)
        {
            const DockPanelOptions& o = m_panels.at(id).options;
            if (o.closable && o.showHeader)
            {
                tabs.Push(id);
                continue;
            }
            JsonValue t = JsonValue::MakeObject();
            t.Set("panel", id);
            if (!o.showHeader) t.Set("header", false);
            if (!o.closable)   t.Set("closable", false);
            tabs.Push(std::move(t));
        }
        v.Set("tabs", std::move(tabs));
        if (node->active > 0)
            v.Set("active", node->active);
        if (node->size > 0.0f)
            v.Set("size", std::round(node->size));
        return v;
    }

    JsonValue DockSpace::SaveLayout() const
    {
        return m_root ? SaveTree(m_root.get()) : JsonValue::MakeObject();
    }

    std::unique_ptr<DockSpace::Tree> DockSpace::LoadTree(const JsonValue& v, std::vector<std::string>& warnings,
                                                         std::vector<std::string>& used)
    {
        if (!v.IsObject())
        {
            warnings.push_back("dock：节点应为对象 { \"tabs\": … } 或 { \"split\": … }");
            return nullptr;
        }
        auto node = std::make_unique<Tree>();
        node->size = static_cast<float>(v["size"].AsNumber(0.0));

        if (const JsonValue* tabs = v.Find("tabs"))
        {
            for (const JsonValue& t : tabs->GetArray())
            {
                const std::string id = t.IsString() ? t.AsString() : t["panel"].AsString();
                auto it = m_panels.find(id);
                if (it == m_panels.end())
                {
                    warnings.push_back("dock：没有名为 \"" + id + "\" 的面板");
                    continue;
                }
                if (std::find(used.begin(), used.end(), id) != used.end())
                {
                    warnings.push_back("dock：面板 \"" + id + "\" 被引用了多次");
                    continue;
                }
                if (t.IsObject())
                {
                    if (const JsonValue* ti = t.Find("title"))   it->second.title = ti->AsString();
                    if (const JsonValue* h = t.Find("header"))   it->second.options.showHeader = h->AsBool(true);
                    if (const JsonValue* c = t.Find("closable")) it->second.options.closable   = c->AsBool(true);
                }
                used.push_back(id);
                node->panels.push_back(id);
            }
            if (node->panels.empty())
                return nullptr;
            node->active = std::clamp(static_cast<int>(v["active"].AsNumber(0.0)), 0, static_cast<int>(node->panels.size()) - 1);
            return node;
        }

        if (const JsonValue* split = v.Find("split"))
        {
            const std::string& dir = split->AsString();
            if (dir != "row" && dir != "column")
                warnings.push_back("dock：split 应为 row 或 column");
            node->split      = true;
            node->horizontal = dir != "column";
            for (const JsonValue& c : v["children"].GetArray())
            {
                if (auto child = LoadTree(c, warnings, used))
                    node->children.push_back(std::move(child));
            }
            if (node->children.empty())
                return nullptr;
            if (node->children.size() == 1)
            {
                auto only = std::move(node->children.front());
                if (node->size > 0.0f)
                    only->size = node->size;
                return only;
            }
            return node;
        }

        warnings.push_back("dock：节点缺少 tabs 或 split");
        return nullptr;
    }

    bool DockSpace::LoadLayout(const JsonValue& layout, std::vector<std::string>* warnings)
    {
        std::vector<std::string> local;
        std::vector<std::string>& w = warnings ? *warnings : local;
        if (!layout.IsObject())
        {
            w.push_back("dock：布局应为对象");
            return false;
        }
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        std::vector<std::string> used;
        m_root = layout.Size() == 0 ? nullptr : LoadTree(layout, w, used);
        Rebuild();
        return true;
    }
}
