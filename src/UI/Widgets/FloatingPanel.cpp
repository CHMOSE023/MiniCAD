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
    // 标题栏：标题、×；按住拖动（上报给面板），双击停靠回去
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
        Rect CloseRect() const
        {
            const Vec2  size = GetSize();
            const float cy   = size.y * 0.5f;
            return Rect{ size.x - kCloseW - 8.0f, cy - 8.0f, size.x - 8.0f, cy + 8.0f };
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            dl.AddRectFilled(r, Theme::PanelAlt);
            dl.AddRectFilled(Rect{ r.min.x, r.max.y - 1.0f, r.max.x, r.max.y }, Theme::BorderSubtle);
            if (UIContext* ctx = GetContext())
            {
                TextParams p;
                p.size     = 13.0f;
                p.color    = Theme::Text;
                p.vAlign   = TextAlign::Center;
                p.ellipsis = true;
                ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + kTitlePad, r.min.y, r.max.x - kCloseW - 12.0f, r.max.y - 1.0f },
                                          m_panel->m_title, p);
            }
            const Rect cr{ CloseRect().min + r.min, CloseRect().max + r.min };
            if (m_closeHover)
                dl.AddRectFilled(cr, m_pressOnClose ? Theme::ControlPressed : Theme::ControlHover, Theme::RadiusSm);
            dl.AddCross(cr.Center(), 3.5f, Theme::TextDim, 1.3f);
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase != EventPhase::Target)
                return;
            UIContext* ctx     = GetContext();
            const bool onClose = CloseRect().Contains(e.localPosition);
            switch (e.type)
            {
            case PointerEventType::Move:
                if (m_pressed)
                {
                    if (!m_dragging)
                    {
                        if (!m_pressOnClose && (e.position - m_pressPos).Length() >= FloatingPanel::kDragStart)
                        {
                            m_dragging = true;
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
                    e.handled = true;
                }
                else if (onClose != m_closeHover)
                {
                    m_closeHover = onClose;
                    Invalidate();
                }
                break;

            case PointerEventType::Leave:
                m_closeHover = false;
                Invalidate();
                break;

            case PointerEventType::Down:
                if (e.button != MouseButton::Left)
                    return;
                e.handled = true;
                if (e.clickCount == 2 && !onClose)
                {
                    // 双击标题栏：停靠回去（回调可能销毁本节点，之后不再访问成员）
                    m_pressed = false;
                    if (auto cb = m_panel->m_onRedock)       // 复制再调用：回调里可能清掉这些回调
                        cb();
                    return;
                }
                m_pressed      = true;
                m_pressOnClose = onClose;
                m_dragging     = false;
                m_pressPos     = e.position;
                m_grab         = e.position - m_panel->GetScreenBounds().min;
                if (ctx)
                    ctx->SetCapture(this);
                Invalidate();
                break;

            case PointerEventType::Up:
            {
                if (e.button != MouseButton::Left || !m_pressed)
                    return;
                const bool dragging = m_dragging;
                const bool close    = m_pressOnClose && onClose;
                m_pressed = m_dragging = m_pressOnClose = false;
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
                if (close)
                {
                    if (auto cb = panel->m_onClose)
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
                m_pressed = m_dragging = m_pressOnClose = false;
                Invalidate();
                return;

            default:
                break;
            }
        }

    private:
        FloatingPanel* m_panel;
        bool m_pressed      = false;
        bool m_pressOnClose = false;
        bool m_dragging     = false;
        bool m_closeHover   = false;
        Vec2 m_pressPos;
        Vec2 m_grab;        // 按下点相对面板左上角的偏移，拖动时保持不变
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
        m_rect = windowRect;
        LayoutStyle& s = EditLayoutStyle();
        s.width  = std::max(kMinWidth, windowRect.Width());
        s.height = std::max(kMinHeight, windowRect.Height());
        MoveTo(windowRect.min);
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
        if (m_frameless)
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
