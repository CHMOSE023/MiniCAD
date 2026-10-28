#include "Widgets/DockSpace.h"
#include "Widgets/FloatingPanel.h"
#include "Widgets/Panel.h"
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

        // 卡片样式：用背景色盖住矩形的四个角，留下圆角（内容节点各自画的直角背景也一并修成圆角）
        void MaskCorners(DrawList& dl, const Rect& r, float rad, ColorRef bg)
        {
            constexpr float kPi = 3.14159265f;
            struct Corner { Vec2 corner, center; float a0; };
            const Corner corners[4] =
            {
                { r.min,                  { r.min.x + rad, r.min.y + rad }, kPi },          // 左上：从左边到上边
                { { r.max.x, r.min.y },   { r.max.x - rad, r.min.y + rad }, kPi * 1.5f },   // 右上
                { r.max,                  { r.max.x - rad, r.max.y - rad }, 0.0f },         // 右下
                { { r.min.x, r.max.y },   { r.min.x + rad, r.max.y - rad }, kPi * 0.5f },   // 左下
            };
            for (const Corner& c : corners)
            {
                dl.PathClear();
                dl.PathLineTo(c.corner);
                dl.PathArcTo(c.center, rad, c.a0, c.a0 + kPi * 0.5f, 8);
                dl.PathFillConvex(bg);
            }
        }

        void DrawDropPreview(DrawList& dl, const Rect& preview)
        {
            dl.AddRectFilled(preview.Deflated(2.0f), Theme::AccentSoft, Theme::RadiusSm);
            dl.AddRect(preview.Deflated(2.0f), Theme::Accent, Theme::RadiusSm, 2.0f);
        }

        // 光标旁显示被拖动的面板名称（拖的是浮动窗口时窗口本身就跟着光标，不画）
        void DrawDragGhost(DrawList& dl, UIContext& ctx, Vec2 pos, const std::string& title)
        {
            TextParams p = TabText(Theme::Text);
            const float w = ctx.GetTextSystem().Measure(title, p).x + kTabPad * 2.0f;
            const Rect ghost{ pos.x + 12.0f, pos.y + 8.0f, pos.x + 12.0f + w, pos.y + 8.0f + 24.0f };
            dl.AddRectFilled(ghost, Theme::Panel, Theme::RadiusSm);
            dl.AddRect(ghost, Theme::Accent, Theme::RadiusSm, 1.0f);
            ctx.GetTextSystem().Draw(dl, Rect{ ghost.min.x + kTabPad, ghost.min.y, ghost.max.x, ghost.max.y }, title, p);
        }
    }

    // =========================================================
    // 浮动窗口的内容：那个窗口的布局树的标签组与分隔条（紧贴样式）。停靠区建好交给宿主放进浮动窗口
    // =========================================================
    class DockFloatView : public Node
    {
    public:
        DockFloatView(DockSpace* dock, DockSpace::Float* f) : m_dock(dock), m_float(f) { SetClipChildren(true); }

        DockSpace::Float* GetFloat() const { return m_float; }

    protected:
        void OnLayout() override
        {
            const UIContext* ctx = GetContext();
            if (m_float->root)
                DockSpace::LayoutTree(m_float->root.get(), Rect{ { 0.0f, 0.0f }, GetSize() }, DockSpace::kSplitterSize,
                                      ctx ? ctx->GetPixelScale() : 1.0f);
            DockSpace::PlaceNodes(this, DockSpace::kSplitterSize);
        }

        void OnPaintOverlay(DrawList& dl, const Rect& r) override
        {
            const auto& drag = m_dock->m_drag;
            if (!drag.active)
                return;
            if (drag.target.valid && drag.target.container == m_float)
                DrawDropPreview(dl, Rect{ drag.target.preview.min + r.min, drag.target.preview.max + r.min });
            if (!drag.external && drag.source == m_float)
                if (UIContext* ctx = GetContext())
                    DrawDragGhost(dl, *ctx, m_dock->DockToFloat(m_float, drag.pos), m_dock->m_panels.at(drag.panel).title);
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            // 这个窗口里的标签拖出后：本节点持有捕获（系统的捕获仍在这个窗口），把移动与松开换算后转给停靠区
            const auto& drag = m_dock->m_drag;
            if (!drag.torn || drag.source != m_float || e.phase != EventPhase::Target)
                return;
            DockSpace* dock = m_dock;           // 下面的调用可能销毁本节点（推迟销毁），之后不再访问成员
            const Vec2 pos  = dock->FloatToDock(m_float, e.position);
            switch (e.type)
            {
            case PointerEventType::Move:
                dock->UpdateDrag(pos);
                e.handled = true;
                break;
            case PointerEventType::Up:
                if (e.button != MouseButton::Left)
                    break;
                e.handled = true;
                if (!dock->EndDrag(pos, false))     // 没停靠：留在松开的地方，算一次布局变化（保存位置）
                    dock->LayoutChanged();
                break;
            case PointerEventType::Cancel:
                dock->EndDrag(pos, true);
                dock->LayoutChanged();              // 已经浮出了
                break;
            default:
                break;
            }
        }

    private:
        DockSpace*        m_dock;
        DockSpace::Float* m_float;
    };

    // =========================================================
    // 标签组：标题栏（标签）+ 当前面板。view 不为空时在浮动窗口里
    // =========================================================
    class DockGroup : public Node
    {
    public:
        DockGroup(DockSpace* dock, DockSpace::Tree* leaf, DockFloatView* view) : m_dock(dock), m_leaf(leaf), m_view(view) { SetClipChildren(true); }

        DockSpace::Tree* GetLeaf() const { return m_leaf; }

        bool  HasHeader() const { return m_dock->LeafHasHeader(m_leaf); }
        float HeaderHeight() const { return HasHeader() ? DockSpace::kHeaderHeight : 0.0f; }

        // 停靠区叠在绘图区上（SolidCraft 的浮动布局）、卡片样式：只对停靠区本身；浮动窗口里是紧贴的面板
        bool Overlay()   const { return !m_view && m_dock->IsFloating(); }
        bool CardStyle() const { return !m_view && m_dock->IsCardStyle(); }

        // 浮动停靠区里没有标题栏的组（文档区）：透明、点击穿透，露出下面的绘图区
        bool IsHole() const { return Overlay() && !HasHeader(); }

        // 浮动时卡片四角不能用背景色盖（下面是绘图区），内容向内缩一点，露出卡片自己画的圆角
        static constexpr float kFloatInset = 3.0f;

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
            float x = Overlay() ? 0.0f : 2.0f;      // 浮动卡片：第一个标签贴着卡片的圆角
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
            SetHitTestVisible(!IsHole());
            Rect content{ 0.0f, HeaderHeight(), size.x, std::max(size.y, HeaderHeight()) };
            if (Overlay() && !IsHole())
                content = Rect{ kFloatInset, content.min.y, std::max(kFloatInset, size.x - kFloatInset), std::max(content.min.y, size.y - kFloatInset) };
            for (const auto& c : GetChildren())
                c->SetBounds(content);
        }

        // 当前面板内容的底色：卡片和激活的标签都用它，标签与内容连成一片
        ColorRef BodyColor() const
        {
            for (const auto& c : GetChildren())
                if (const auto* p = dynamic_cast<const Panel*>(c.get()); p && p->GetBackground() != ColorRef(Colors::Transparent))
                    return p->GetBackground();
            return Theme::Panel;
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            if (IsHole())
                return;
            const bool     floating = Overlay();
            const ColorRef body     = BodyColor();
            dl.AddRectFilled(r, body, floating ? Theme::RadiusLg : 0.0f);
            if (!HasHeader())
                return;
            UIContext* ctx = GetContext();
            const Rect header{ r.min.x, r.min.y, r.max.x, r.min.y + DockSpace::kHeaderHeight };
            // 标题条比内容深一级，激活的标签填内容色并盖住底边的分隔线，看上去与内容连成一片
            const ColorRef strip = body == ColorRef(Theme::PanelAlt) ? Theme::Background : Theme::PanelAlt;
            if (floating)
            {
                dl.AddRectFilled(header, strip, Theme::RadiusLg);
                dl.AddRectFilled(Rect{ header.min.x, header.min.y + Theme::RadiusLg, header.max.x, header.max.y }, strip);
            }
            else
            {
                dl.AddRectFilled(header, strip);
            }
            dl.AddRectFilled(Rect{ header.min.x, header.max.y - 1.0f, header.max.x, header.max.y }, Theme::BorderSubtle);

            const auto tabs = Tabs();
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                const TabGeom& t      = tabs[i];
                const bool     active = static_cast<int>(i) == m_leaf->active;
                const bool     hover  = static_cast<int>(i) == m_hoverTab;
                const Rect     tab{ r.min.x + t.x0, header.min.y, r.min.x + t.x1, header.max.y - (active ? 0.0f : 1.0f) };
                // 标签上面两个角是圆的（与卡片、按钮的圆角风格一致），下边与内容连成一片；
                // 浮动卡片的上角更圆：贴着卡片角的标签（第一个的左上、顶到右边的右上）用卡片的圆角
                const float rl = floating && i == 0 ? Theme::RadiusLg : Theme::RadiusMd;
                const float rr = floating && t.x1 >= GetSize().x - 0.5f ? Theme::RadiusLg : Theme::RadiusMd;
                auto fillTab = [&](ColorRef color)
                {
                    constexpr float kPi = 3.14159265f;
                    dl.PathClear();
                    dl.PathLineTo({ tab.min.x, tab.max.y });
                    dl.PathArcTo({ tab.min.x + rl, tab.min.y + rl }, rl, kPi, kPi * 1.5f, 8);          // 左上：从左边到上边
                    dl.PathArcTo({ tab.max.x - rr, tab.min.y + rr }, rr, kPi * 1.5f, kPi * 2.0f, 8);   // 右上：从上边到右边
                    dl.PathLineTo({ tab.max.x, tab.max.y });
                    dl.PathFillConvex(color);
                };
                if (active)
                {
                    fillTab(body);
                    // 顶上的指示线：两头收进圆角以内；浮动卡片的边框画在最上层，指示线下移 1px 不被它盖住
                    const float top = floating ? 1.0f : 0.0f;
                    dl.AddRectFilled(Rect{ tab.min.x + rl, tab.min.y + top, tab.max.x - rr, tab.min.y + top + 2.0f }, Theme::Accent, 1.0f);
                }
                else if (hover)
                {
                    fillTab(Theme::RowHover);
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
                        dl.AddRectFilled(cr, Theme::ControlHover, Theme::RadiusSm);
                    dl.AddCross(cr.Center(), 3.5f, Theme::TextDim, 1.3f);
                }
            }
        }

        void OnPaintOverlay(DrawList& dl, const Rect& r) override
        {
            if (!CardStyle() || IsHole())
                return;
            if (!Overlay())
                MaskCorners(dl, r, Theme::RadiusLg, Theme::Background);
            dl.AddRect(r, Theme::BorderSubtle, Theme::RadiusLg, 1.0f);
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
                            m_dock->BeginDrag(m_leaf->panels[static_cast<size_t>(m_pressTab)], DockPos(e.position));
                        }
                    }
                    else if (m_dock->IsDragging())
                    {
                        m_dock->UpdateDrag(DockPos(e.position));    // 拖出所在的窗口时浮出，本节点随之摘下
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
                if (e.clickCount == 2 && !(tabs[static_cast<size_t>(tab)].closable && tabs[static_cast<size_t>(tab)].close.Contains(e.localPosition)))
                {
                    // 双击标签：停靠区里的浮出成浮动窗口，放在原标签组的位置上（略向内缩）；浮动窗口里的回到停靠区。
                    // 会重建标签组并销毁本节点，之后不再访问成员
                    e.handled = true;
                    m_pressTab = -1;
                    const std::string id = m_leaf->panels[static_cast<size_t>(tab)];     // 复制：浮出会释放 m_leaf
                    if (m_view)
                    {
                        m_dock->RedockPanel(id);
                        return;
                    }
                    const Rect  r    = GetScreenBounds();
                    const Vec2  size{ std::clamp(r.Width(),  DockSpace::kFloatMinW, DockSpace::kFloatMaxW),
                                      std::clamp(r.Height(), DockSpace::kFloatMinH, DockSpace::kFloatMaxH) };
                    m_dock->FloatPanel(id, Rect{ r.min + Vec2{ 24.0f, 24.0f }, r.min + Vec2{ 24.0f, 24.0f } + size });
                    return;
                }
                m_pressTab     = tab;
                m_pressPos     = e.position;
                m_pressOnClose = tabs[static_cast<size_t>(tab)].closable && tabs[static_cast<size_t>(tab)].close.Contains(e.localPosition);
                m_dragStarted  = false;
                if (ctx)
                    ctx->SetCapture(this);
                if (!m_pressOnClose && tab != m_leaf->active)
                    m_dock->SelectTab(m_leaf, tab);
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
                        m_dock->EndDrag(DockPos(e.position), false);
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
                    m_dock->EndDrag(DockPos(e.position), true);
                m_pressTab    = -1;
                m_dragStarted = false;
                return;
            default:
                break;
            }
        }

    private:
        // 本窗口坐标 → 停靠区所在窗口的坐标（浮动窗口里的标签组经宿主换算）
        Vec2 DockPos(Vec2 p) const { return m_view ? m_dock->FloatToDock(m_view->GetFloat(), p) : p; }

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
        DockFloatView*   m_view;
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
        DockSplitter(DockSpace* dock, DockSpace::Tree* split, size_t index, DockFloatView* view)
            : m_dock(dock), m_split(split), m_index(index), m_view(view) {}

        DockSpace::Tree* GetSplit() const { return m_split; }
        size_t           GetIndex() const { return m_index; }

        CursorShape GetCursor(Vec2) const override { return m_split->horizontal ? CursorShape::SizeWE : CursorShape::SizeNS; }

    protected:
        void OnPaint(DrawList& dl, const Rect& r) override
        {
            // 节点比分界线宽（热区叠在两侧面板上），平时只画中间的分界线，悬停 / 拖动时画一条稍宽的强调色
            const bool  hot = m_dragging || IsHovered();
            if (!m_view && m_dock->IsCardStyle() && !hot)
                return;                         // 卡片样式：平时缝里什么也不画
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
            if (m_view)
                m_view->InvalidateLayout();
            else
                m_dock->InvalidateLayout();
        }

        DockSpace*       m_dock;
        DockSpace::Tree* m_split;
        size_t           m_index;
        DockFloatView*   m_view;
        bool             m_dragging = false;
        float            m_start = 0.0f, m_a = 0.0f, m_b = 0.0f;
    };

    // =========================================================
    // 默认的浮动宿主：在停靠区所在的窗口里开 FloatingPanel 弹层（非模态，叠在界面之上，只能在窗口内浮动）
    // =========================================================
    class InWindowFloatHost : public IDockFloatHost
    {
    public:
        ~InWindowFloatHost() override
        {
            // 停靠区先于弹层销毁时（一般不会：UiLayout 重建前会 TakeAllPanels）：弹层的回调不能再指向停靠区
            auto panels = std::move(m_panels);
            for (auto& [id, fp] : panels)
            {
                Detach(fp);
                fp->Close();
            }
        }

        static void Detach(FloatingPanel* fp)
        {
            fp->SetOnClosed(nullptr);
            fp->SetOnClose(nullptr);
            fp->SetOnRedock(nullptr);
            fp->SetOnMoved(nullptr);
            fp->SetOnMaximize(nullptr);
            fp->SetOnActivate(nullptr);
            fp->SetOnDrag(nullptr);
        }

        bool Open(DockSpace& dock, const std::string& id, const std::string& title, std::unique_ptr<Node>& content, const Rect& rect) override
        {
            UIContext* ctx = dock.GetContext();
            if (!ctx || m_panels.count(id))
                return false;
            auto panel = std::make_unique<FloatingPanel>(title);
            FloatingPanel* fp = panel.get();
            fp->SetContent(std::move(content));
            fp->SetRect(rect);
            DockSpace* d = &dock;
            fp->SetOnClose   ([d, id] { d->CloseFloat(id); });
            fp->SetOnRedock  ([d, id] { d->RedockFloat(id); });
            fp->SetOnMoved   ([d]     { d->LayoutChanged(); });
            fp->SetOnMaximize([d, fp] { fp->SetMaximized(!fp->IsPanelMaximized()); d->LayoutChanged(); });   // 铺满窗口 / 还原
            fp->SetOnActivate([ctx, fp, d, id] { ctx->RaisePopup(fp); d->RaiseFloat(id); });
            fp->SetOnDrag([d, id](FloatingPanel::DragPhase phase, Vec2 pos)
            {
                switch (phase)
                {
                case FloatingPanel::DragPhase::Begin:  d->BeginFloatDrag(id, pos);   break;
                case FloatingPanel::DragPhase::Update: d->UpdateFloatDrag(pos);      break;
                case FloatingPanel::DragPhase::End:    d->EndFloatDrag(pos, false);  break;   // 停靠成功时本弹层已被 Close
                case FloatingPanel::DragPhase::Cancel: d->EndFloatDrag(pos, true);   break;
                }
            });
            // 弹层被别处关闭（例如 UIContext 销毁时 CloseAllPopups）：忘掉它，内容随弹层销毁
            fp->SetOnClosed([this, id] { m_panels.erase(id); });
            m_panels[id] = fp;
            ctx->OpenPopup(std::move(panel), /*takeFocus=*/false);
            return true;
        }

        std::unique_ptr<Node> Close(const std::string& id) override
        {
            auto it = m_panels.find(id);
            if (it == m_panels.end())
                return nullptr;
            FloatingPanel* fp = it->second;
            m_panels.erase(it);
            std::unique_ptr<Node> content = fp->TakeContent();
            Detach(fp);
            fp->Close();            // 推迟销毁：可能正在它自己的事件处理函数里
            return content;
        }

        Rect GetRect(const std::string& id) const override
        {
            const auto it = m_panels.find(id);
            return it == m_panels.end() ? Rect{} : it->second->GetRect();
        }

        void SetTitle(const std::string& id, const std::string& title) override
        {
            if (auto it = m_panels.find(id); it != m_panels.end())
                it->second->SetTitle(title);
        }

        void SetRect(const std::string& id, const Rect& rect) override
        {
            if (auto it = m_panels.find(id); it != m_panels.end())
                it->second->SetRect(rect);
        }

        void Activate(const std::string& id) override
        {
            const auto it = m_panels.find(id);
            if (it == m_panels.end())
                return;
            if (UIContext* ctx = it->second->GetContext())
                ctx->RaisePopup(it->second);
        }

        bool IsFloatMaximized(const std::string& id) const override
        {
            const auto it = m_panels.find(id);
            return it != m_panels.end() && it->second->IsPanelMaximized();
        }

        void SetFloatMaximized(const std::string& id, bool maximized) override
        {
            if (auto it = m_panels.find(id); it != m_panels.end())
                it->second->SetMaximized(maximized);
        }

        Rect GetRestoreRect(const std::string& id) const override
        {
            const auto it = m_panels.find(id);
            return it == m_panels.end() ? Rect{} : it->second->GetNormalRect();
        }

    private:
        std::map<std::string, FloatingPanel*> m_panels;
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
        // 还开着的浮动窗口：从宿主取回内容（里面的面板随它一起销毁），宿主关掉窗口
        for (auto& f : m_floats)
            if (f->open)
                CloseHostWindow(*f);
        m_floats.clear();
    }

    void DockSpace::AddPanel(const std::string& id, std::string title, std::unique_ptr<Node> content, DockPanelOptions options)
    {
        if (m_panels.count(id))
        {
            if (FindLeaf(id))
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
        return leaf && static_cast<size_t>(leaf->active) < leaf->panels.size() && leaf->panels[static_cast<size_t>(leaf->active)] == id;
    }

    void DockSpace::SetPanelTitle(const std::string& id, std::string title)
    {
        if (auto it = m_panels.find(id); it != m_panels.end())
        {
            it->second.title = std::move(title);
            Invalidate();
            InvalidateViews();
            if (Float* f = FindFloat(id))
                UpdateFloatTitle(f);
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
        ClearTrees();       // 内容摘回 Panel::owned，浮动窗口关掉
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
        if (m_root)
            if (Tree* t = FindLeaf(m_root.get(), panelId))
                return t;
        for (const auto& f : m_floats)
            if (f->root)
                if (Tree* t = FindLeaf(f->root.get(), panelId))
                    return t;
        return nullptr;
    }

    DockSpace::Tree* DockSpace::FindLeaf(Tree* node, const std::string& panelId)
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

    DockSpace::Tree* DockSpace::LeafAt(Tree* node, Vec2 local)
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

    DockSpace::Tree* DockSpace::TopOf(Tree* node)
    {
        while (node && node->parent)
            node = node->parent;
        return node;
    }

    DockSpace::Tree* DockSpace::FirstLeaf(Tree* node)
    {
        while (node && node->split && !node->children.empty())
            node = node->children.front().get();
        return node;
    }

    void DockSpace::CollectPanels(const Tree* node, std::vector<std::string>& out)
    {
        if (!node)
            return;
        if (!node->split)
        {
            out.insert(out.end(), node->panels.begin(), node->panels.end());
            return;
        }
        for (const auto& c : node->children)
            CollectPanels(c.get(), out);
    }

    std::string DockSpace::Signature(const Tree* node)
    {
        if (!node)
            return {};
        // 含节点序号：标签组 / 分隔条节点指向这些 Tree，换了节点（即使结构一样）也要重建
        std::string s = std::to_string(node->serial);
        if (!node->split)
        {
            s += "[";
            for (const std::string& id : node->panels)
                s += id + ",";
            return s + "]";
        }
        s += node->horizontal ? "r(" : "c(";
        for (const auto& c : node->children)
            s += Signature(c.get());
        return s + ")";
    }

    DockSpace::Float* DockSpace::FloatOf(const Tree* node) const
    {
        if (!node)
            return nullptr;
        while (node->parent)
            node = node->parent;
        for (const auto& f : m_floats)
            if (f->root.get() == node)
                return f.get();
        return nullptr;
    }

    DockSpace::Float* DockSpace::FindFloatById(const std::string& floatId) const
    {
        for (const auto& f : m_floats)
            if (f->id == floatId)
                return f.get();
        return nullptr;
    }

    bool DockSpace::HasFloat(const Float* f) const
    {
        return f && std::any_of(m_floats.begin(), m_floats.end(), [f](const auto& p) { return p.get() == f; });
    }

    std::unique_ptr<DockSpace::Tree>& DockSpace::RootSlot(Tree* top)
    {
        for (auto& f : m_floats)
            if (f->root.get() == top)
                return f->root;
        return m_root;
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

    bool DockSpace::LeafHasHeader(const Tree* leaf) const
    {
        if (!AcceptsTabs(leaf))
            return false;
        // 浮动窗口里只有一个面板：窗口自己的标题栏就够了，面板铺满
        return !(leaf->panels.size() == 1 && !leaf->parent && FloatOf(leaf));
    }

    void DockSpace::SetParents(Tree* node, Tree* parent)
    {
        node->parent = parent;
        for (const auto& c : node->children)
            SetParents(c.get(), node);
    }

    void DockSpace::FixParents()
    {
        if (m_root)
            SetParents(m_root.get(), nullptr);
        for (auto& f : m_floats)
            if (f->root)
                SetParents(f->root.get(), nullptr);
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
                RootSlot(node) = std::move(only);
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
            RootSlot(leaf).reset();     // 浮动窗口的树空了：Rebuild 时关掉窗口
            return;
        }
        auto& siblings = parent->children;
        siblings.erase(std::find_if(siblings.begin(), siblings.end(), [leaf](const auto& c) { return c.get() == leaf; }));
        Collapse(parent);
        FixParents();
    }

    void DockSpace::RememberPosition(const std::string& panelId)
    {
        // 只记在停靠区里的位置：浮动窗口之间搬来搬去不影响"回到停靠区"的去处
        Tree*  leaf = FindLeaf(panelId);
        if (!leaf || FloatOf(leaf))
            return;
        Panel& p = m_panels.at(panelId);
        p.lastTarget.clear();

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

    bool DockSpace::InsertTree(std::unique_ptr<Tree>& sub, Tree* target, DockSide side, float size, int tabIndex, Float* container)
    {
        if (!sub)
            return false;
        std::unique_ptr<Tree>& root = target ? RootSlot(TopOf(target)) : (container ? container->root : m_root);
        if (!root)
        {
            sub->size = 0.0f;
            root = std::move(sub);
            FixParents();
            return true;
        }

        if (side == DockSide::Center)
        {
            if (!target || !AcceptsTabs(target))
                return false;
            // 搬来的面板都成为目标组的标签，原来显示的那个成为当前标签
            std::vector<std::string> ids;
            CollectPanels(sub.get(), ids);
            const Tree* first = FirstLeaf(sub.get());
            const std::string current = first && static_cast<size_t>(first->active) < first->panels.size()
                                      ? first->panels[static_cast<size_t>(first->active)] : std::string{};
            const int index = tabIndex < 0 || tabIndex > static_cast<int>(target->panels.size())
                            ? static_cast<int>(target->panels.size()) : tabIndex;
            target->panels.insert(target->panels.begin() + index, ids.begin(), ids.end());
            const auto pos = std::find(ids.begin(), ids.end(), current);
            target->active = index + (pos == ids.end() ? 0 : static_cast<int>(pos - ids.begin()));
            sub.reset();
            return true;
        }

        const bool horizontal = IsHorizontalSide(side);
        const bool leading    = IsLeadingSide(side);

        // 整个容器（停靠区或浮动窗口）的一侧
        if (!target)
        {
            const Vec2  whole  = container ? (container->view ? container->view->GetSize() : container->rect.Size()) : GetSize();
            const float extent = horizontal ? whole.x : whole.y;
            sub->size = size > 0.0f ? size : std::min(kRootSize, extent > 0.0f ? extent * 0.4f : kRootSize);
            if (root->split && root->horizontal == horizontal)
            {
                auto& ch = root->children;
                ch.insert(leading ? ch.begin() : ch.end(), std::move(sub));
            }
            else
            {
                auto split = std::make_unique<Tree>();
                split->split      = true;
                split->horizontal = horizontal;
                root->size        = 0.0f;       // 原来的整体占剩余空间
                split->children.push_back(std::move(root));
                split->children.insert(leading ? split->children.begin() : split->children.end(), std::move(sub));
                root = std::move(split);
            }
            FixParents();
            return true;
        }

        // 目标标签组的一侧
        const float targetExtent = horizontal ? target->rect.Width() : target->rect.Height();
        sub->size = size > 0.0f ? size : (targetExtent > 0.0f ? std::floor(targetExtent * 0.5f) : kRootSize);

        Tree* parent = target->parent;
        if (parent && parent->horizontal == horizontal)
        {
            auto& ch = parent->children;
            auto  it = std::find_if(ch.begin(), ch.end(), [target](const auto& c) { return c.get() == target; });
            if (target->size > 0.0f)
                target->size = std::max(kMinPanel, target->size - sub->size);     // 两者共享原来的尺寸
            ch.insert(leading ? it : it + 1, std::move(sub));
            FixParents();
            return true;
        }

        // 目标所在分割方向不同（或目标是根）：用一个新分割替换目标
        auto split = std::make_unique<Tree>();
        split->split      = true;
        split->horizontal = horizontal;
        split->size       = target->size;
        std::unique_ptr<Tree>* slot = nullptr;
        if (!parent)
            slot = &root;
        else
            slot = &*std::find_if(parent->children.begin(), parent->children.end(), [target](const auto& c) { return c.get() == target; });
        std::unique_ptr<Tree> old = std::move(*slot);
        old->size = 0.0f;
        split->children.push_back(std::move(old));
        split->children.insert(leading ? split->children.begin() : split->children.end(), std::move(sub));
        *slot = std::move(split);
        FixParents();
        return true;
    }

    bool DockSpace::InsertPanel(const std::string& id, Tree* target, DockSide side, float size, int tabIndex, Float* container)
    {
        auto leaf = std::make_unique<Tree>();
        leaf->panels.push_back(id);
        return InsertTree(leaf, target, side, size, tabIndex, container);
    }

    // =========================================================
    // 停靠操作
    // =========================================================
    bool DockSpace::Dock(const std::string& panelId, const std::string& target, DockSide side, float size)
    {
        const std::string id = panelId;     // 复制：引用可能来自即将重建的标签组
        if (!m_panels.count(id) || id == target)
            return false;
        Float* container = nullptr;
        if (!target.empty())
        {
            const Tree* t = FindLeaf(target);
            if (!t || (t == FindLeaf(id) && t->panels.size() == 1))
                return false;
            container = FloatOf(t);
        }

        if (container && !FindFloat(id))
            RememberPosition(id);           // 从停靠区进浮动窗口：记下在停靠区里的位置
        RemoveFromTree(id);
        Tree* targetLeaf = target.empty() ? nullptr : FindLeaf(target);
        const bool ok = InsertPanel(id, targetLeaf, side, size, -1, container);
        if (!ok)
            InsertPanel(id, nullptr, DockSide::Right, size, -1, nullptr);     // 无法合并（例如目标是文档区）：退回到右侧
        if (!FindFloat(id))
            m_panels[id].wasFloating = false;
        Rebuild();
        return ok;
    }

    void DockSpace::ClosePanel(const std::string& panelId)
    {
        const std::string id = panelId;     // 复制：引用可能来自即将销毁的浮动窗口回调或标签组
        auto it = m_panels.find(id);
        if (it == m_panels.end() || !IsPanelVisible(id) || !it->second.options.closable)
            return;
        Panel& p = it->second;
        if (Float* f = FindFloat(id))
        {
            // 在浮动窗口里：记下窗口；还有别的面板时记下其中一个（同组的优先），ShowPanel 时合并回去
            std::vector<std::string> ids;
            CollectPanels(f->root.get(), ids);
            p.wasFloating = true;
            p.floatRect   = FloatRestoreRect(*f);
            p.floatTarget.clear();
            for (const std::string& other : FindLeaf(id)->panels)
                if (other != id)
                {
                    p.floatTarget = other;
                    break;
                }
            for (const std::string& other : ids)
                if (p.floatTarget.empty() && other != id)
                    p.floatTarget = other;
        }
        else
        {
            RememberPosition(id);
            p.wasFloating = false;
        }
        RemoveFromTree(id);
        Rebuild();
        LayoutChanged();
    }

    void DockSpace::DockToRemembered(const std::string& id)
    {
        const Panel& p = m_panels.at(id);
        if (!p.lastTarget.empty() && m_root && FindLeaf(m_root.get(), p.lastTarget))
            Dock(id, p.lastTarget, p.lastSide, p.lastSize);
        else
            Dock(id, {}, DockSide::Right, 260.0f);
    }

    void DockSpace::ShowPanel(const std::string& panelId)
    {
        const std::string id = panelId;
        auto it = m_panels.find(id);
        if (it == m_panels.end())
            return;
        if (!IsPanelVisible(id))
        {
            const Panel& p = it->second;
            if (p.wasFloating)
            {
                // 关闭前在浮动窗口里：那个窗口还开着就回去当它的标签，否则在原处重新浮出
                if (!p.floatTarget.empty() && FindFloat(p.floatTarget))
                {
                    const std::string target = p.floatTarget;
                    Dock(id, target, DockSide::Center);
                    LayoutChanged();
                    ActivatePanel(id);
                    return;
                }
                if (FloatPanel(id, p.floatRect))
                    return;
            }
            DockToRemembered(id);
            LayoutChanged();
        }
        ActivatePanel(id);
    }

    void DockSpace::ActivatePanel(const std::string& id)
    {
        Tree* leaf = FindLeaf(id);
        if (!leaf)
            return;
        SelectTab(leaf, static_cast<int>(std::find(leaf->panels.begin(), leaf->panels.end(), id) - leaf->panels.begin()));
        if (Float* f = FloatOf(leaf); f && f->open)
        {
            f->host->Activate(f->id);
            RaiseFloat(f->id);
        }
    }

    void DockSpace::SelectTab(Tree* leaf, int index)
    {
        if (leaf->active == index)
            return;
        leaf->active = index;
        for (size_t i = 0; i < leaf->panels.size(); ++i)
            m_panels.at(leaf->panels[i]).node->SetVisible(static_cast<int>(i) == index);
        Float* f = FloatOf(leaf);
        Node*  container = f && f->view ? static_cast<Node*>(f->view) : this;
        container->InvalidateLayout();
        container->Invalidate();
        if (f)
            UpdateFloatTitle(f);
        LayoutChanged();
    }

    void DockSpace::LayoutChanged()
    {
        if (m_quietLayout)
            return;
        if (m_onLayoutChanged)
        {
            auto cb = m_onLayoutChanged;
            cb();
        }
    }

    // =========================================================
    // 浮动窗口
    // =========================================================
    IDockFloatHost& DockSpace::FloatHost()
    {
        if (m_floatHost)
            return *m_floatHost;
        if (!m_defaultFloatHost)
            m_defaultFloatHost = std::make_unique<InWindowFloatHost>();
        return *m_defaultFloatHost;
    }

    void DockSpace::SetFloatHost(IDockFloatHost* host)
    {
        if (host == m_floatHost)
            return;
        // 已经开着的浮动窗口先从旧宿主取回，再交给新宿主重新打开（布局树与里面的面板不动）
        std::vector<Float*> reopen;
        for (auto& f : m_floats)
        {
            if (f->open)
            {
                f->owned = CloseHostWindow(*f);
                reopen.push_back(f.get());
            }
        }
        m_floatHost = host;
        if (!GetContext())
            return;         // 还没挂到界面上：第一次布局时再开
        for (Float* f : reopen)
            if (HasFloat(f) && !OpenFloat(f))
                RedockFloat(f->id);
    }

    bool DockSpace::IsPanelFloating(const std::string& id) const
    {
        return FindFloat(id) != nullptr;
    }

    Rect DockSpace::GetFloatingRect(const std::string& id) const
    {
        const Float* f = FindFloat(id);
        if (!f)
            return {};
        if (!f->open)
            return f->rect;
        const Rect live = f->host->GetRect(f->id);
        return live.IsEmpty() ? f->rect : live;
    }

    size_t DockSpace::GetFloatCount() const
    {
        return static_cast<size_t>(std::count_if(m_floats.begin(), m_floats.end(), [](const auto& f) { return f->root != nullptr; }));
    }

    std::vector<std::string> DockSpace::GetFloatPanels(const std::string& id) const
    {
        std::vector<std::string> ids;
        if (const Float* f = FindFloat(id))
            CollectPanels(f->root.get(), ids);
        return ids;
    }

    DockSpace::Float* DockSpace::NewFloat(const Rect& rect, std::unique_ptr<Tree> root)
    {
        auto f   = std::make_unique<Float>();
        f->id    = "float" + std::to_string(m_nextFloat++);
        f->rect  = rect;
        f->root  = std::move(root);
        if (f->root)
            f->root->size = 0.0f;
        auto view = std::make_unique<DockFloatView>(this, f.get());
        f->view  = view.get();
        f->owned = std::move(view);
        Float* raw = f.get();
        m_floats.push_back(std::move(f));
        FixParents();
        return raw;
    }

    bool DockSpace::OpenFloat(Float* f)
    {
        if (f->open)
            return true;
        if (!f->owned || !f->root)
            return false;
        IDockFloatHost* host = &FloatHost();
        bool ok = host->Open(*this, f->id, FloatTitle(*f), f->owned, f->rect);
        if (!ok && m_floatHost)
        {
            // 平台宿主建不了独立窗口：退回窗口内浮动
            if (!m_defaultFloatHost)
                m_defaultFloatHost = std::make_unique<InWindowFloatHost>();
            host = m_defaultFloatHost.get();
            ok   = host->Open(*this, f->id, FloatTitle(*f), f->owned, f->rect);
        }
        if (!ok)
            return false;
        f->open = true;
        f->host = host;
        if (f->maximized)
            host->SetFloatMaximized(f->id, true);
        return true;
    }

    std::unique_ptr<Node> DockSpace::CloseHostWindow(Float& f)
    {
        if (!f.open)
            return std::move(f.owned);
        const Rect r = f.host->GetRestoreRect(f.id);
        if (!r.IsEmpty())
            f.rect = r;
        f.maximized = f.host->IsFloatMaximized(f.id);
        std::unique_ptr<Node> view = f.host->Close(f.id);
        f.open = false;
        f.host = nullptr;
        return view;
    }

    void DockSpace::OpenPendingFloats()
    {
        if (!GetContext())
            return;
        std::vector<Float*> pending;
        for (auto& f : m_floats)
            if (!f->open && f->root)
                pending.push_back(f.get());
        for (Float* f : pending)
            if (HasFloat(f) && !f->open && !OpenFloat(f))
                RedockFloat(f->id);         // 开不了：面板回停靠区
    }

    void DockSpace::ClearTrees()
    {
        m_root.reset();
        for (auto& f : m_floats)
            f->root.reset();
        Rebuild();          // 内容摘回 Panel::owned，浮动窗口都关掉
    }

    Rect DockSpace::FloatRestoreRect(const Float& f) const
    {
        if (f.open)
        {
            const Rect r = f.host->GetRestoreRect(f.id);
            if (!r.IsEmpty())
                return r;
        }
        return f.rect;
    }

    std::string DockSpace::FloatTitle(const Float& f) const
    {
        // 第一个标签组的当前标签（只有一个面板时就是它）
        const Tree* leaf = FirstLeaf(f.root.get());
        if (!leaf || leaf->panels.empty())
            return {};
        return m_panels.at(leaf->panels[static_cast<size_t>(std::clamp(leaf->active, 0, static_cast<int>(leaf->panels.size()) - 1))]).title;
    }

    void DockSpace::UpdateFloatTitle(Float* f)
    {
        if (f && f->open && f->root)
            f->host->SetTitle(f->id, FloatTitle(*f));
    }

    Vec2 DockSpace::FloatToDock(const Float* f, Vec2 floatPos) const
    {
        return f && f->open ? f->host->ToDock(f->id, floatPos) : floatPos;
    }

    Vec2 DockSpace::DockToFloat(const Float* f, Vec2 dockPos) const
    {
        return f && f->open ? f->host->FromDock(f->id, dockPos) : dockPos;
    }

    bool DockSpace::InContainer(const Float* c, Vec2 windowPos) const
    {
        if (c)
            return HasFloat(c) && c->open && c->host->GetRect(c->id).Contains(windowPos);
        const Vec2 local = ToLocal(windowPos);
        const Vec2 size  = GetSize();
        return local.x >= 0.0f && local.y >= 0.0f && local.x < size.x && local.y < size.y;
    }

    void DockSpace::InvalidateViews()
    {
        for (auto& f : m_floats)
            if (f->view)
                f->view->Invalidate();
    }

    bool DockSpace::FloatPanel(const std::string& panelId, const Rect& windowRect)
    {
        const std::string id = panelId;     // 复制：调用方的引用可能指向即将释放的标签组
        auto it = m_panels.find(id);
        if (it == m_panels.end() || !it->second.options.showHeader)
            return false;
        if (Float* cur = FindFloat(id))
        {
            std::vector<std::string> ids;
            CollectPanels(cur->root.get(), ids);
            if (ids.size() == 1)
            {
                // 已经独占一个浮动窗口：只置前
                if (cur->open)
                {
                    cur->host->Activate(cur->id);
                    RaiseFloat(cur->id);
                }
                return true;
            }
        }
        if (m_drag.active)
            EndDrag(m_drag.pos, true);

        RememberPosition(id);           // 在停靠区里时记下位置
        RemoveFromTree(id);
        auto leaf = std::make_unique<Tree>();
        leaf->panels.push_back(id);
        Float* f = NewFloat(windowRect, std::move(leaf));
        Rebuild();                      // 内容放进新浮动窗口的标签组
        if (!GetContext())
            return true;                // 还没挂到界面上：第一次布局时再开浮动窗口
        if (!OpenFloat(f))
        {
            DockToRemembered(id);
            return false;
        }
        LayoutChanged();
        return true;
    }

    void DockSpace::RedockPanel(const std::string& panelId)
    {
        const std::string id = panelId;     // 复制：引用可能来自即将销毁的浮动窗口回调或标签组
        if (!IsPanelFloating(id))
            return;
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        DockToRemembered(id);
        ActivatePanel(id);
        LayoutChanged();
    }

    void DockSpace::RedockFloat(const std::string& floatId)
    {
        Float* f = FindFloatById(floatId);
        if (!f || !f->root)
            return;
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        std::vector<std::string> ids;
        CollectPanels(f->root.get(), ids);
        const Tree* first = FirstLeaf(f->root.get());
        const std::string current = first->panels.empty() ? std::string{}
                                  : first->panels[static_cast<size_t>(std::clamp(first->active, 0, static_cast<int>(first->panels.size()) - 1))];
        // 各回各的位置；没有记录的跟着前一个放进同一组（不各占一列）。窗口在最后一个面板搬走时关掉，之后不再访问 f
        m_quietLayout = true;
        std::string prev;
        for (const std::string& id : ids)
        {
            const Panel& p = m_panels.at(id);
            if (!p.lastTarget.empty() && m_root && FindLeaf(m_root.get(), p.lastTarget))
                Dock(id, p.lastTarget, p.lastSide, p.lastSize);
            else if (!prev.empty())
                Dock(id, prev, DockSide::Center);
            else
                Dock(id, {}, DockSide::Right, 260.0f);
            prev = id;
        }
        if (!current.empty())
            ActivatePanel(current);
        m_quietLayout = false;
        LayoutChanged();
    }

    void DockSpace::CloseFloat(const std::string& floatId)
    {
        Float* f = FindFloatById(floatId);
        if (!f || !f->root)
            return;
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        std::vector<std::string> ids;
        CollectPanels(f->root.get(), ids);
        const Rect rect = FloatRestoreRect(*f);
        // 关闭里面的面板，各自记下这个窗口和同窗口的另一个面板：先 ShowPanel 的在原处重新浮出，后面的合并进去
        std::vector<std::string> keep;      // 不可关闭的：回停靠区
        for (const std::string& id : ids)
        {
            Panel& p = m_panels.at(id);
            if (!p.options.closable)
            {
                keep.push_back(id);
                continue;
            }
            p.wasFloating = true;
            p.floatRect   = rect;
            p.floatTarget.clear();
            for (const std::string& other : ids)
                if (other != id && m_panels.at(other).options.closable)
                {
                    p.floatTarget = other;
                    break;
                }
            RemoveFromTree(id);
        }
        for (const std::string& id : keep)
            DockToRemembered(id);
        Rebuild();
        LayoutChanged();
    }

    void DockSpace::RaiseFloat(const std::string& floatId)
    {
        auto it = std::find_if(m_floats.begin(), m_floats.end(), [&](const auto& f) { return f->id == floatId; });
        if (it != m_floats.end())
            std::rotate(it, it + 1, m_floats.end());
    }

    void DockSpace::BeginFloatDrag(const std::string& floatId, Vec2 windowPos)
    {
        Float* f = FindFloatById(floatId);
        if (!f || !f->root)
            return;
        if (m_drag.active)
            EndDrag(m_drag.pos, true);
        RaiseFloat(floatId);
        const Tree* leaf = FirstLeaf(f->root.get());
        if (leaf->panels.empty())
            return;
        BeginDrag(leaf->panels[static_cast<size_t>(std::clamp(leaf->active, 0, static_cast<int>(leaf->panels.size()) - 1))],
                  windowPos, /*external=*/true);
    }

    void DockSpace::UpdateFloatDrag(Vec2 windowPos)
    {
        if (m_drag.active && m_drag.external)
            UpdateDrag(windowPos);
    }

    bool DockSpace::EndFloatDrag(Vec2 windowPos, bool cancel)
    {
        if (!m_drag.active || !m_drag.external)
            return false;
        return EndDrag(windowPos, cancel);
    }

    // =========================================================
    // 布局
    // =========================================================
    void DockSpace::LayoutTree(Tree* node, const Rect& rect, float gap, float scale)
    {
        node->rect = rect;
        if (!node->split || node->children.empty())
            return;

        auto snap = [scale](float v) { return std::round(v * scale) / scale; };

        const size_t n     = node->children.size();
        const bool   horiz = node->horizontal;
        const float  total = (horiz ? rect.Width() : rect.Height()) - gap * static_cast<float>(n - 1);

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
            LayoutTree(node->children[i].get(), r, gap, scale);
            pos = b + gap;
        }
    }

    void DockSpace::PlaceNodes(Node* container, float gap)
    {
        for (const auto& c : container->GetChildren())
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
                const float pad = std::max(1.0f, (kSplitterHit - gap) * 0.5f);
                sp->SetBounds(split->horizontal ? Rect{ a.max.x - pad, split->rect.min.y, b.min.x + pad, split->rect.max.y }
                                                : Rect{ split->rect.min.x, a.max.y - pad, split->rect.max.x, b.min.y + pad });
            }
        }
    }

    void DockSpace::OnLayout()
    {
        OpenPendingFloats();
        if (m_root)
        {
            const Edges& pad  = GetLayoutStyle().padding;
            const Vec2   size = GetSize();
            LayoutTree(m_root.get(), Rect{ pad.left, pad.top, std::max(pad.left, size.x - pad.right), std::max(pad.top, size.y - pad.bottom) },
                       SplitterSize(), GetContext() ? GetContext()->GetPixelScale() : 1.0f);
        }
        PlaceNodes(this, SplitterSize());
    }

    void DockSpace::DiscardChildren(Node* container)
    {
        // 推迟销毁：可能正在执行它们自己的事件处理函数
        UIContext* ctx = container->GetContext() ? container->GetContext() : GetContext();
        while (!container->GetChildren().empty())
        {
            std::unique_ptr<Node> old = container->RemoveChild(container->GetChildren().back().get());
            if (ctx)
                ctx->DeferDelete(std::move(old));
        }
    }

    void DockSpace::Rebuild()
    {
        // 结构没变的浮动窗口不动：内容留在原处（焦点、悬停、正在编辑的文字不受别处改动的影响）
        std::vector<const Float*> keep;
        for (const auto& f : m_floats)
            if (f->root && f->view && !f->view->GetChildren().empty() && f->built == Signature(f->root.get()))
                keep.push_back(f.get());
        auto kept = [&keep](const Float* f) { return f && std::find(keep.begin(), keep.end(), f) != keep.end(); };

        // 1. 面板内容从旧的标签组上摘下（不销毁）
        for (auto& [id, p] : m_panels)
        {
            if (p.owned || !p.node || !p.node->GetParent() || kept(FindFloat(id)))
                continue;
            p.owned = p.node->GetParent()->RemoveChild(p.node);
        }
        // 2. 旧的标签组和分隔条推迟销毁
        DiscardChildren(this);
        for (auto& f : m_floats)
            if (f->view && !kept(f.get()))
                DiscardChildren(f->view);
        // 3. 空了的浮动窗口关掉
        UIContext* ctx = GetContext();
        for (auto it = m_floats.begin(); it != m_floats.end();)
        {
            Float& f = **it;
            if (f.root)
            {
                ++it;
                continue;
            }
            std::unique_ptr<Node> view = CloseHostWindow(f);
            if (m_drag.source == &f)
                m_drag.source = nullptr;
            if (m_drag.target.container == &f)
                m_drag.target = {};
            if (ctx)
                ctx->DeferDelete(std::move(view));
            it = m_floats.erase(it);
        }
        // 4. 按树重建
        FixParents();
        if (m_root)
            BuildNodes(this, nullptr, m_root.get());
        for (auto& f : m_floats)
        {
            if (kept(f.get()))
                continue;
            BuildNodes(f->view, f->view, f->root.get());
            f->built = Signature(f->root.get());
            f->view->InvalidateLayout();
            UpdateFloatTitle(f.get());
        }
        InvalidateLayout();
    }

    void DockSpace::BuildNodes(Node* container, DockFloatView* view, Tree* node)
    {
        if (!node->split)
        {
            node->active = std::clamp(node->active, 0, std::max(0, static_cast<int>(node->panels.size()) - 1));
            auto* group = container->AddChild<DockGroup>(this, node, view);
            for (size_t i = 0; i < node->panels.size(); ++i)
            {
                Panel& p = m_panels.at(node->panels[i]);
                if (p.owned)
                    group->AddChild(std::move(p.owned));
                p.node->SetVisible(static_cast<int>(i) == node->active);
            }
            return;
        }
        for (const auto& c : node->children)
            BuildNodes(container, view, c.get());
        for (size_t i = 0; i + 1 < node->children.size(); ++i)
            container->AddChild<DockSplitter>(this, node, i, view);
    }

    Rect DockSpace::GetPanelRect(const std::string& id) const
    {
        if (!IsPanelActive(id))
            return {};
        const Node* content = m_panels.at(id).node;
        return content->GetParent() ? content->GetScreenBounds() : Rect{};
    }

    // =========================================================
    // 拖动
    // =========================================================
    void DockSpace::BeginDrag(const std::string& panelId, Vec2 windowPos, bool external)
    {
        m_drag          = {};
        m_drag.active   = true;
        m_drag.external = external;
        m_drag.source   = external ? nullptr : FindFloat(panelId);
        m_drag.panel    = panelId;
        m_drag.pos      = windowPos;
        if (UIContext* ctx = GetContext())
        {
            ShortcutTable::Options opt;
            opt.allowInTextInput = true;
            m_drag.escShortcut = ctx->GetShortcuts().Register(Key::Escape, 0, [this]
            {
                const bool torn = m_drag.torn;      // 拖出后已经浮出：取消只是不再停靠，窗口留在原地
                EndDrag(m_drag.pos, true);
                if (torn)
                    LayoutChanged();
            }, opt);
        }
        UpdateDrag(windowPos);
    }

    void DockSpace::UpdateDrag(Vec2 windowPos)
    {
        if (!m_drag.active)
            return;
        // 拖动的标签离开所在的停靠区 / 浮动窗口：马上浮出，浮动窗口随后跟着光标走（不必等松开才看到窗口）
        if (!m_drag.external && !InContainer(m_drag.source, windowPos) && m_panels.at(m_drag.panel).options.showHeader)
        {
            TearOff(windowPos);
            return;
        }
        if (m_drag.torn)
            MoveFloat(m_drag.panel, Rect{ windowPos - m_drag.grab, windowPos - m_drag.grab + m_drag.size });
        m_drag.pos    = windowPos;
        m_drag.target = ComputeDrop(windowPos);
        Invalidate();
        InvalidateViews();
    }

    void DockSpace::TearOff(Vec2 windowPos)
    {
        // 尺寸取自原标签组，标题栏落在光标下。浮出会重建标签组，持有捕获的标签条随之摘下：按下的那个窗口
        //（停靠区自己或原浮动窗口的内容）接过捕获，接着按"拖浮动窗口"处理（经过停靠区或别的浮动窗口画落点，
        // 松开在上面停靠过去，松开在外面留在那里）
        const std::string panel  = m_drag.panel;
        Float*            source = m_drag.source;
        const Tree* src = FindLeaf(panel);
        const Vec2  dim{ std::clamp(src ? src->rect.Width()  : 0.0f, kFloatMinW, kFloatMaxW),
                         std::clamp(src ? src->rect.Height() : 0.0f, kFloatMinH, kFloatMaxH) };
        const Vec2  grab{ 40.0f, kHeaderHeight * 0.5f };
        // 里面先结束本次标签拖动。布局变化等松开时再通知一次（浮出时窗口位置还在变）
        m_quietLayout = true;
        const bool floated = FloatPanel(panel, Rect{ windowPos - grab, windowPos - grab + dim });
        m_quietLayout = false;
        if (!floated || !IsPanelFloating(panel))
            return;
        Node* holder = this;
        if (source)
            holder = HasFloat(source) && source->view ? static_cast<Node*>(source->view) : nullptr;
        UIContext* ctx = holder ? holder->GetContext() : nullptr;
        if (!ctx)
            return;
        BeginDrag(panel, windowPos, /*external=*/true);
        m_drag.torn   = true;
        m_drag.source = source;
        m_drag.grab   = grab;
        m_drag.size   = dim;
        ctx->SetCapture(holder);
    }

    void DockSpace::MoveFloat(const std::string& panelId, const Rect& rect)
    {
        Float* f = FindFloat(panelId);
        if (!f)
            return;
        if (f->open)
            f->host->SetRect(f->id, rect);
        else
            f->rect = rect;
    }

    void DockSpace::OnPointerEvent(PointerEvent& e)
    {
        // 停靠区里的标签拖出后由停靠区持有捕获（浮动窗口里的由那个窗口的 DockFloatView 持有）
        if (!m_drag.torn || m_drag.source || e.phase != EventPhase::Target)
            return;
        switch (e.type)
        {
        case PointerEventType::Move:
            UpdateDrag(e.position);
            e.handled = true;
            break;
        case PointerEventType::Up:
            if (e.button != MouseButton::Left)
                break;
            e.handled = true;
            if (!EndDrag(e.position, false))        // 没停靠回去：留在松开的地方，算一次布局变化（保存位置）
                LayoutChanged();
            break;
        case PointerEventType::Cancel:
            EndDrag(e.position, true);
            LayoutChanged();                        // 已经浮出了
            break;
        default:
            break;
        }
    }

    bool DockSpace::EndDrag(Vec2 windowPos, bool cancel)
    {
        if (!m_drag.active)
            return false;
        if (m_drag.escShortcut)
        {
            if (UIContext* ctx = GetContext())
                ctx->GetShortcuts().Unregister(m_drag.escShortcut);
        }
        const DropTarget  target   = cancel ? DropTarget{} : ComputeDrop(windowPos);
        const std::string panel    = m_drag.panel;
        const bool        external = m_drag.external;
        Float*            source   = m_drag.source;
        if (m_drag.torn)
        {
            Node* holder = source ? (HasFloat(source) ? static_cast<Node*>(source->view) : nullptr) : static_cast<Node*>(this);
            if (holder && holder->HasCapture())
                holder->GetContext()->ReleaseCapture();
        }
        m_drag = {};
        Invalidate();
        InvalidateViews();
        if (!target.valid)
        {
            // 标签松开在所在的停靠区 / 浮动窗口之外：浮出成浮动窗口（尺寸取自原标签组，标签落在光标下）
            if (!cancel && !external && !InContainer(source, windowPos) && m_panels.at(panel).options.showHeader)
            {
                const Tree* src = FindLeaf(panel);
                const Vec2  dim{ std::clamp(src ? src->rect.Width()  : 0.0f, kFloatMinW, kFloatMaxW),
                                 std::clamp(src ? src->rect.Height() : 0.0f, kFloatMinH, kFloatMaxH) };
                const Vec2  origin = windowPos - Vec2{ 40.0f, kHeaderHeight * 0.5f };
                FloatPanel(panel, Rect{ origin, origin + dim });
            }
            return false;
        }

        // 目标组用其中一个面板标识（移除源面板后树结构会变，组指针可能失效）；浮动窗口的指针不变（空了的到 Rebuild 才删）
        Float*      container = target.container;
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

        if (external)
        {
            // 拖的是整个浮动窗口：里面的面板一起过去（合并为标签时都成为目标组的标签，拆分时整棵树放过去）。原窗口空了，Rebuild 时关掉
            Float* moving = FindFloat(panel);
            if (!moving || moving == container)
                return false;
            std::unique_ptr<Tree> sub = std::move(moving->root);
            std::vector<std::string> ids;
            CollectPanels(sub.get(), ids);
            Tree* leaf = targetPanel.empty() ? nullptr : FindLeaf(targetPanel);
            if (!InsertTree(sub, leaf, target.side, size, target.index, container))
                InsertTree(sub, nullptr, DockSide::Right, 0.0f, -1, nullptr);
            for (const std::string& id : ids)
                if (!FindFloat(id))
                    m_panels.at(id).wasFloating = false;
            Rebuild();
            LayoutChanged();
            return true;
        }

        // 合并到同一组：只调整标签顺序
        Tree* sourceLeaf = FindLeaf(panel);
        if (target.side == DockSide::Center && target.leaf == sourceLeaf)
        {
            auto& ps = sourceLeaf->panels;
            const int from = static_cast<int>(std::find(ps.begin(), ps.end(), panel) - ps.begin());
            int to = target.index < 0 ? static_cast<int>(ps.size()) - 1 : target.index;
            if (to > from)
                --to;
            ps.erase(ps.begin() + from);
            ps.insert(ps.begin() + std::clamp(to, 0, static_cast<int>(ps.size())), panel);
            sourceLeaf->active = static_cast<int>(std::find(ps.begin(), ps.end(), panel) - ps.begin());
            Rebuild();
            LayoutChanged();
            return true;
        }

        if (container && !FindFloat(panel))
            RememberPosition(panel);        // 从停靠区进浮动窗口：记下在停靠区里的位置
        RemoveFromTree(panel);
        Tree* leaf = targetPanel.empty() ? nullptr : FindLeaf(targetPanel);
        if (!InsertPanel(panel, leaf, target.side, size, target.index, container))
            InsertPanel(panel, nullptr, DockSide::Right, 0.0f, -1, nullptr);
        if (!FindFloat(panel))
            m_panels.at(panel).wasFloating = false;
        Rebuild();
        LayoutChanged();
        return true;
    }

    DockSpace::DropTarget DockSpace::ComputeDrop(Vec2 windowPos) const
    {
        // 浮动窗口在停靠区之上，从最上面的开始看；拖整个浮动窗口时跳过它自己
        const Float* dragged = m_drag.external ? FindFloat(m_drag.panel) : nullptr;
        for (auto it = m_floats.rbegin(); it != m_floats.rend(); ++it)
        {
            Float* f = it->get();
            if (f == dragged || !f->open || !f->root || !f->view)
                continue;
            if (!f->host->GetRect(f->id).Contains(windowPos))
                continue;
            const Vec2 local = f->view->ToLocal(DockToFloat(f, windowPos));
            if (local.y >= 0.0f)
                return ComputeDropIn(f, local);
            // 浮动窗口的标题栏：合并为标签（放进第一个标签组的末尾）
            DropTarget  t;
            Tree*       leaf   = FirstLeaf(f->root.get());
            const Tree* source = FindLeaf(m_drag.panel);
            if (!AcceptsTabs(leaf) || (leaf == source && source->panels.size() == 1))
                return t;
            t.valid     = true;
            t.container = f;
            t.leaf      = leaf;
            t.side      = DockSide::Center;
            t.preview   = leaf->rect;
            return t;
        }
        return ComputeDropIn(nullptr, ToLocal(windowPos));
    }

    DockSpace::DropTarget DockSpace::ComputeDropIn(Float* container, Vec2 local) const
    {
        DropTarget t;
        t.container = container;
        const Vec2 size = container ? container->view->GetSize() : GetSize();
        if (local.x < 0.0f || local.y < 0.0f || local.x >= size.x || local.y >= size.y)
            return t;

        Tree*       root   = container ? container->root.get() : m_root.get();
        const Tree* source = FindLeaf(m_drag.panel);
        const bool  alone  = source && source->panels.size() == 1;

        Tree* leaf = root ? LeafAt(root, local) : nullptr;
        const Node* nodes = container ? static_cast<const Node*>(container->view) : this;

        // 标题栏：合并为标签（按位置插入）；优先于边缘判断（顶部组的标题栏就在停靠区上边缘）
        const bool header = leaf && LeafHasHeader(leaf) && local.y < leaf->rect.min.y + kHeaderHeight;
        if (header)
        {
            t.valid   = true;
            t.leaf    = leaf;
            t.side    = DockSide::Center;
            t.preview = leaf->rect;
            // 插入位置：光标所在标签的左半边插在它前面，右半边插在后面
            for (const auto& c : nodes->GetChildren())
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

        // 贴近容器边缘：停靠到整体的一侧
        const float edge = kEdgeDrop;
        DockSide    side = DockSide::Center;
        if (local.x < edge)               side = DockSide::Left;
        else if (local.x >= size.x - edge) side = DockSide::Right;
        else if (local.y < edge)           side = DockSide::Top;
        else if (local.y >= size.y - edge) side = DockSide::Bottom;
        if (side != DockSide::Center && !(alone && root == source))
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
        const DropTarget& t = m_drag.target;
        if (!t.container)
        {
            const Vec2 o = GetScreenBounds().min;
            out = Rect{ t.preview.min + o, t.preview.max + o };
            return true;
        }
        if (!HasFloat(t.container) || !t.container->view)
            return false;
        const Vec2 o = t.container->view->GetScreenBounds().min;
        out = Rect{ FloatToDock(t.container, t.preview.min + o), FloatToDock(t.container, t.preview.max + o) };
        return true;
    }

    void DockSpace::SetFloating(bool floating)
    {
        m_floating = floating;
        SetHitTestVisible(!floating);       // 缝里点击穿透到绘图区；卡片、分隔条是子节点，照常命中
        InvalidateLayout();
        Invalidate();
    }

    void DockSpace::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        if (IsCardStyle() && !m_floating)
            dl.AddRectFilled(screenRect, Theme::Background);       // 卡片之间的缝（浮动时透明，露出绘图区）
    }

    void DockSpace::OnPaintOverlay(DrawList& dl, const Rect& screenRect)
    {
        if (!m_drag.active)
            return;
        // 落点在浮动窗口里时由那个窗口的 DockFloatView 画
        if (m_drag.target.valid && !m_drag.target.container)
            DrawDropPreview(dl, Rect{ m_drag.target.preview.min + screenRect.min, m_drag.target.preview.max + screenRect.min });
        UIContext* ctx = GetContext();
        if (ctx && !m_drag.external && !m_drag.source)
            DrawDragGhost(dl, *ctx, m_drag.pos, m_panels.at(m_drag.panel).title);
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
        JsonValue v = m_root ? SaveTree(m_root.get()) : JsonValue::MakeObject();
        JsonValue floating = JsonValue::MakeArray();
        for (const auto& f : m_floats)
        {
            if (!f->root)
                continue;
            const Tree* root = f->root.get();
            JsonValue e;
            if (!root->split && root->panels.size() == 1)
            {
                e = JsonValue::MakeObject();
                e.Set("panel", root->panels.front());
            }
            else
            {
                e = SaveTree(root);
            }
            const Rect r = FloatRestoreRect(*f);
            JsonValue rect = JsonValue::MakeArray();
            rect.Push(std::round(r.min.x));
            rect.Push(std::round(r.min.y));
            rect.Push(std::round(r.Width()));
            rect.Push(std::round(r.Height()));
            e.Set("rect", std::move(rect));
            if (f->open ? f->host->IsFloatMaximized(f->id) : f->maximized)
                e.Set("maximized", true);
            floating.Push(std::move(e));
        }
        if (floating.Size() > 0)
            v.Set("floatingPanels", std::move(floating));
        return v;
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
        ClearTrees();
        std::vector<std::string> used;
        const JsonValue* floating = layout.Find("floatingPanels");
        const bool hasTree = layout.Find("tabs") || layout.Find("split");
        m_root = hasTree ? LoadTree(layout, w, used) : nullptr;
        if (layout.Size() > 0 && !hasTree && !floating)
            w.push_back("dock：节点缺少 tabs 或 split");

        // 浮动窗口：一个面板写 "panel"，几个面板写这个窗口的布局树；没挂到界面上时先记下，第一次布局时再开
        if (floating)
        {
            for (const JsonValue& f : floating->GetArray())
            {
                std::unique_ptr<Tree> tree;
                if (f.Find("tabs") || f.Find("split"))
                {
                    tree = LoadTree(f, w, used);
                }
                else
                {
                    const std::string id = f["panel"].AsString();
                    if (!m_panels.count(id))
                    {
                        w.push_back("dock：没有名为 \"" + id + "\" 的面板");
                        continue;
                    }
                    if (std::find(used.begin(), used.end(), id) != used.end())
                    {
                        w.push_back("dock：面板 \"" + id + "\" 被引用了多次");
                        continue;
                    }
                    if (!m_panels.at(id).options.showHeader)
                        continue;       // 没有标题栏的面板不能浮出
                    used.push_back(id);
                    tree = std::make_unique<Tree>();
                    tree->panels.push_back(id);
                }
                if (!tree)
                    continue;
                const auto& r = f["rect"].GetArray();
                Rect rect{ 0.0f, 0.0f, 320.0f, 240.0f };
                if (r.size() == 4)
                {
                    rect.min = { static_cast<float>(r[0].AsNumber(0.0)), static_cast<float>(r[1].AsNumber(0.0)) };
                    rect.max = rect.min + Vec2{ static_cast<float>(r[2].AsNumber(320.0)), static_cast<float>(r[3].AsNumber(240.0)) };
                }
                NewFloat(rect, std::move(tree))->maximized = f["maximized"].AsBool(false);
            }
        }
        Rebuild();
        OpenPendingFloats();
        return true;
    }
}
