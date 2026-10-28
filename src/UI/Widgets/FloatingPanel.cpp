#include "Widgets/FloatingPanel.h"
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
        constexpr float kTitlePad = 10.0f;
        constexpr float kCloseW   = 16.0f;

        // 内容容器：唯一的子节点铺满自己（忽略内容自己的布局样式，与停靠时 DockGroup 的做法一致）
        class FillNode : public Node
        {
        protected:
            void OnLayout() override
            {
                for (const auto& c : GetChildren())
                    c->SetBounds(Rect{ { 0.0f, 0.0f }, GetSize() });
            }
        };
    }

    // =========================================================
    // 标题栏：标题、最大化 / 还原、×；按住拖动（上报给面板），双击停靠回去
    // =========================================================
    class FloatingTitleBar : public Node
    {
    public:
        explicit FloatingTitleBar(FloatingPanel* panel) : m_panel(panel)
        {
            LayoutStyle s;
            s.height = FloatingPanel::kTitleHeight;
            s.shrink = 0.0f;
            SetLayoutStyle(s);
        }

    protected:
        enum class Button { None, Maximize, Close };

        Rect CloseRect() const
        {
            const Vec2  size = GetSize();
            const float cy   = std::floor(size.y * 0.5f);
            return Rect{ size.x - kCloseW - 8.0f, cy - 8.0f, size.x - 8.0f, cy + 8.0f };
        }

        Rect MaximizeRect() const
        {
            const Rect c = CloseRect();
            return Rect{ c.min.x - kCloseW - 6.0f, c.min.y, c.min.x - 6.0f, c.max.y };
        }

        bool HasMaximize() const { return m_panel->m_onMaximize != nullptr; }

        Button ButtonAt(Vec2 local) const
        {
            if (CloseRect().Contains(local))
                return Button::Close;
            if (HasMaximize() && MaximizeRect().Contains(local))
                return Button::Maximize;
            return Button::None;
        }

        // 按钮的底色：悬停时浅、按下时深，其余与标题栏相同
        ColorRef ButtonColor(Button b) const
        {
            if (m_hover != b)
                return Theme::PanelAlt;
            return m_press == b ? Theme::ControlPressed : Theme::ControlHover;
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            dl.AddRectFilled(r, Theme::PanelAlt);
            dl.AddRectFilled(Rect{ r.min.x, r.max.y - 1.0f, r.max.x, r.max.y }, Theme::BorderSubtle);
            const float textRight = (HasMaximize() ? MaximizeRect().min.x : CloseRect().min.x) - 4.0f;
            if (UIContext* ctx = GetContext())
            {
                TextParams p;
                p.size     = 13.0f;
                p.color    = Theme::Text;
                p.vAlign   = TextAlign::Center;
                p.ellipsis = true;
                ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + kTitlePad, r.min.y, r.min.x + textRight, r.max.y - 1.0f },
                                          m_panel->m_title, p);
            }
            const Rect cr{ CloseRect().min + r.min, CloseRect().max + r.min };
            if (m_hover == Button::Close)
                dl.AddRectFilled(cr, ButtonColor(Button::Close), Theme::RadiusSm);
            dl.AddCross(cr.Center(), 3.5f, Theme::TextDim, 1.3f);
            if (HasMaximize())
            {
                const Rect mr{ MaximizeRect().min + r.min, MaximizeRect().max + r.min };
                if (m_hover == Button::Maximize)
                    dl.AddRectFilled(mr, ButtonColor(Button::Maximize), Theme::RadiusSm);
                const Vec2 c = mr.Center();
                if (m_panel->m_maximized)
                {
                    // 还原：错开的两个方框，前面的盖住后面的
                    dl.AddRect(Rect{ c.x - 3.0f, c.y - 5.0f, c.x + 5.0f, c.y + 3.0f }, Theme::TextDim, 0.0f, 1.0f);
                    const Rect front{ c.x - 5.0f, c.y - 3.0f, c.x + 3.0f, c.y + 5.0f };
                    dl.AddRectFilled(front, ButtonColor(Button::Maximize));
                    dl.AddRect(front, Theme::TextDim, 0.0f, 1.0f);
                }
                else
                {
                    dl.AddRect(Rect{ c.x - 5.0f, c.y - 5.0f, c.x + 5.0f, c.y + 5.0f }, Theme::TextDim, 0.0f, 1.0f);
                }
            }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase != EventPhase::Target)
                return;
            UIContext*   ctx = GetContext();
            const Button at  = ButtonAt(e.localPosition);
            switch (e.type)
            {
            case PointerEventType::Move:
                if (m_pressed)
                {
                    if (!m_dragging)
                    {
                        if (m_press == Button::None && (e.position - m_pressPos).Length() >= FloatingPanel::kDragStart)
                        {
                            m_dragging = true;
                            RestoreForDrag();
                            if (auto cb = m_panel->m_onDrag)
                                cb(FloatingPanel::DragPhase::Begin, m_pressPos);
                        }
                    }
                    if (m_dragging)
                    {
                        if (!m_panel->m_frameless)
                            m_panel->MoveTo(e.position - m_grab);
                        if (auto cb = m_panel->m_onDrag)
                            cb(FloatingPanel::DragPhase::Update, e.position);
                    }
                    else if (m_press != Button::None)
                    {
                        // 按住按钮移出 / 移回：只在按钮上松开才算数
                        const Button hover = at == m_press ? m_press : Button::None;
                        if (hover != m_hover)
                        {
                            m_hover = hover;
                            Invalidate();
                        }
                    }
                    e.handled = true;
                }
                else if (at != m_hover)
                {
                    m_hover = at;
                    Invalidate();
                }
                break;

            case PointerEventType::Leave:
                m_hover = Button::None;
                Invalidate();
                break;

            case PointerEventType::Down:
                if (e.button != MouseButton::Left)
                    return;
                e.handled = true;
                if (e.clickCount == 2 && at == Button::None)
                {
                    // 双击标题栏：停靠回去（回调可能销毁本节点，之后不再访问成员）
                    m_pressed = false;
                    if (auto cb = m_panel->m_onRedock)       // 复制再调用：回调里可能清掉这些回调
                        cb();
                    return;
                }
                m_pressed  = true;
                m_press    = at;
                m_hover    = at;
                m_dragging = false;
                m_pressPos = e.position;
                m_grab     = e.position - m_panel->GetScreenBounds().min;
                if (ctx)
                    ctx->SetCapture(this);
                Invalidate();
                break;

            case PointerEventType::Up:
            {
                if (e.button != MouseButton::Left || !m_pressed)
                    return;
                const bool   dragging = m_dragging;
                const Button clicked  = m_press != Button::None && at == m_press ? m_press : Button::None;
                m_pressed = m_dragging = false;
                m_press   = Button::None;
                m_hover   = at;
                e.handled = true;
                if (ctx)
                    ctx->ReleaseCapture();
                Invalidate();
                // 下面的回调可能把面板停靠回去并销毁本节点：调用之后不再访问成员
                FloatingPanel* panel = m_panel;
                if (dragging)
                {
                    if (auto cb = panel->m_onDrag)
                        cb(FloatingPanel::DragPhase::End, e.position);
                    if (panel->IsOpen() || panel->m_frameless)
                        panel->Moved();
                    return;
                }
                if (clicked == Button::Close)
                {
                    if (auto cb = panel->m_onClose)
                        cb();
                }
                else if (clicked == Button::Maximize)
                {
                    if (auto cb = panel->m_onMaximize)
                        cb();
                }
                return;
            }

            case PointerEventType::Cancel:
                if (m_dragging)
                {
                    if (auto cb = m_panel->m_onDrag)
                        cb(FloatingPanel::DragPhase::Cancel, e.position);
                }
                m_pressed = m_dragging = false;
                m_press   = Button::None;
                Invalidate();
                return;

            default:
                break;
            }
        }

    private:
        // 窗口内最大化时拖标题栏：先还原，按下点按比例落在还原后的标题栏上（同系统窗口）。系统窗口由宿主还原
        void RestoreForDrag()
        {
            if (!m_panel->m_maximized || m_panel->m_frameless)
                return;
            const Rect  maxRect = m_panel->GetRect();
            const Rect  normal  = m_panel->m_normalRect;
            const float fx      = (m_pressPos.x - maxRect.min.x) / std::max(1.0f, maxRect.Width());
            m_grab = { std::floor(fx * normal.Width()), m_grab.y };
            m_panel->SetRect(Rect{ m_pressPos - m_grab, m_pressPos - m_grab + normal.Size() });
        }

        FloatingPanel* m_panel;
        bool   m_pressed  = false;
        bool   m_dragging = false;
        Button m_press    = Button::None;     // 按下时在哪个按钮上
        Button m_hover    = Button::None;
        Vec2   m_pressPos;
        Vec2   m_grab;        // 按下点相对面板左上角的偏移，拖动时保持不变
    };

    // =========================================================
    // FloatingPanel
    // =========================================================
    FloatingPanel::FloatingPanel(std::string title)
        : m_title(std::move(title))
    {
        SetModal(false);
        SetLightDismiss(false);
        SetCloseOnEscape(false);
        SetClipChildren(true);

        PopupStyle ps;
        ps.background = Theme::Panel;
        ps.border     = Theme::BorderSubtle;
        ps.rounding   = 0.0f;
        ps.shadow     = true;
        SetStyle(ps);

        LayoutStyle s;
        s.minWidth  = kMinWidth;
        s.minHeight = kMinHeight;
        SetLayoutStyle(s);

        m_titleBar = AddChild<FloatingTitleBar>(this);
        m_body     = AddChild<FillNode>();
        m_body->EditLayoutStyle().grow = 1.0f;
        SetRect(Rect{ 0.0f, 0.0f, 300.0f, 240.0f });
    }

    void FloatingPanel::SetTitle(std::string title)
    {
        m_title = std::move(title);
        m_titleBar->Invalidate();
    }

    void FloatingPanel::SetContent(std::unique_ptr<Node> content)
    {
        TakeContent();
        m_content = content.get();
        if (content)
        {
            content->SetVisible(true);
            m_body->AddChild(std::move(content));
        }
    }

    std::unique_ptr<Node> FloatingPanel::TakeContent()
    {
        std::unique_ptr<Node> out;
        if (m_content && m_content->GetParent() == m_body)
            out = m_body->RemoveChild(m_content);
        m_content = nullptr;
        return out;
    }

    void FloatingPanel::SetRect(const Rect& windowRect)
    {
        if (m_maximized && !m_frameless)
        {
            m_maximized = false;            // 窗口内最大化时被挪动：算还原
            m_titleBar->Invalidate();
        }
        ApplyRect(windowRect);
    }

    void FloatingPanel::ApplyRect(const Rect& windowRect)
    {
        m_rect = windowRect;
        LayoutStyle& s = EditLayoutStyle();
        s.width  = std::max(kMinWidth, windowRect.Width());
        s.height = std::max(kMinHeight, windowRect.Height());
        MoveTo(windowRect.min);
    }

    void FloatingPanel::SetMaximized(bool maximized)
    {
        if (maximized == m_maximized)
            return;
        if (!m_frameless)
        {
            // 窗口内浮动：铺满窗口 / 回到最大化前的位置
            if (maximized)
            {
                const Rect live = GetRect();
                m_normalRect = live.IsEmpty() ? m_rect : live;      // 刚打开、还没布局时用请求的位置
                const UIContext* ctx = GetContext();
                ApplyRect(Rect{ { 0.0f, 0.0f }, ctx ? ctx->GetDisplaySize() : m_rect.Size() });
            }
            else
            {
                ApplyRect(m_normalRect);
            }
        }
        m_maximized = maximized;
        m_titleBar->Invalidate();
    }

    void FloatingPanel::SetOnMaximize(std::function<void()> cb)
    {
        m_onMaximize = std::move(cb);
        m_titleBar->Invalidate();
    }

    Rect FloatingPanel::GetRect() const
    {
        return GetParent() ? GetScreenBounds() : m_rect;
    }

    void FloatingPanel::SetFrameless(bool frameless)
    {
        m_frameless = frameless;
        LayoutStyle& s = EditLayoutStyle();
        if (frameless)
        {
            s.width  = kAuto;       // 由父节点（窗口的根）拉伸
            s.height = kAuto;
        }
        else
        {
            s.width  = std::max(kMinWidth, m_rect.Width());
            s.height = std::max(kMinHeight, m_rect.Height());
        }
        Invalidate();
    }

    void FloatingPanel::Moved()
    {
        if (!m_frameless)
            m_rect = GetRect();
        if (auto cb = m_onMoved)
            cb();
    }

    void FloatingPanel::OnPaint(DrawList& dl, const Rect& r)
    {
        if (m_frameless)
            dl.AddRectFilled(r, Theme::Panel);
        else
            Popup::OnPaint(dl, r);
    }

    void FloatingPanel::OnPaintOverlay(DrawList& dl, const Rect& r)
    {
        if (!m_frameless)
            dl.AddRect(r, Theme::BorderSubtle, 0.0f, 1.0f);     // 边框画在内容之上，内容贴边时也看得见
    }

    int FloatingPanel::EdgeAt(Vec2 local) const
    {
        if (m_frameless || m_maximized)
            return 0;
        const Vec2 size = GetSize();
        int edges = 0;
        if (local.x < kResizeBorder)           edges |= Left;
        if (local.x >= size.x - kResizeBorder) edges |= Right;
        if (local.y < kResizeBorder)           edges |= Top;
        if (local.y >= size.y - kResizeBorder) edges |= Bottom;
        return edges;
    }

    bool FloatingPanel::InterceptsHit(Vec2 local) const
    {
        return EdgeAt(local) != 0;
    }

    CursorShape FloatingPanel::GetCursor(Vec2 local) const
    {
        const int e = m_resizeEdges ? m_resizeEdges : EdgeAt(local);
        const bool h = (e & (Left | Right)) != 0;
        const bool v = (e & (Top | Bottom)) != 0;
        if (h && v)
            return ((e & Left) != 0) == ((e & Top) != 0) ? CursorShape::SizeNWSE : CursorShape::SizeNESW;
        if (h)
            return CursorShape::SizeWE;
        if (v)
            return CursorShape::SizeNS;
        return CursorShape::Arrow;
    }

    void FloatingPanel::OnPointerEvent(PointerEvent& e)
    {
        // 在面板任何地方按下：请宿主把它放到最前面
        if (e.phase == EventPhase::Capture)
        {
            if (e.type == PointerEventType::Down)
            {
                if (auto cb = m_onActivate)
                    cb();
            }
            return;
        }
        if (e.phase != EventPhase::Target)
            return;

        UIContext* ctx = GetContext();
        switch (e.type)
        {
        case PointerEventType::Down:
            if (e.button != MouseButton::Left || m_frameless)
                return;
            m_resizeEdges = EdgeAt(e.localPosition);
            if (!m_resizeEdges)
                return;
            m_resizeStart = e.position;
            m_resizeRect  = GetRect();
            if (ctx)
                ctx->SetCapture(this);
            e.handled = true;
            break;

        case PointerEventType::Move:
        {
            if (!m_resizeEdges)
                return;
            const Vec2 d = e.position - m_resizeStart;
            Rect r = m_resizeRect;
            if (m_resizeEdges & Left)   r.min.x = std::min(r.min.x + d.x, r.max.x - kMinWidth);
            if (m_resizeEdges & Right)  r.max.x = std::max(r.max.x + d.x, r.min.x + kMinWidth);
            if (m_resizeEdges & Top)    r.min.y = std::min(r.min.y + d.y, r.max.y - kMinHeight);
            if (m_resizeEdges & Bottom) r.max.y = std::max(r.max.y + d.y, r.min.y + kMinHeight);
            SetRect(r);
            e.handled = true;
            break;
        }

        case PointerEventType::Up:
        case PointerEventType::Cancel:
            if (!m_resizeEdges)
                return;
            m_resizeEdges = 0;
            if (ctx && e.type == PointerEventType::Up)
                ctx->ReleaseCapture();
            e.handled = true;
            Moved();
            break;

        default:
            break;
        }
    }
}
