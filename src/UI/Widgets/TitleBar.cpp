#include "Widgets/TitleBar.h"
#include "Core/UIContext.h"
#include "Data/CommandRegistry.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Widgets/Controls.h"
#include "Widgets/ImageView.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"
#include <algorithm>
#include <typeinfo>

namespace MiniGUI
{
    namespace
    {
        constexpr float kIconSize   = 16.0f;
        constexpr float kIconMargin = 10.0f;    // 图标左右的留白
        constexpr float kTitleGap   = 16.0f;    // 标题与两侧内容的最小间距
        constexpr float kGlyph      = 10.0f;    // 按钮图标尺寸

        const Color32 kCloseHover   = ColorFromHex(0xE81123);
        const Color32 kClosePressed = ColorFromHex(0xF1707A);

        // 不处理输入的节点：点在它们上面等于点在标题栏的空白处
        bool IsStaticNode(const Node* n)
        {
            return dynamic_cast<const Label*>(n) || dynamic_cast<const Panel*>(n) || dynamic_cast<const Separator*>(n)
                || dynamic_cast<const ImageView*>(n) || typeid(*n) == typeid(Node);
        }
    }

    // =========================================================
    // 最小化 / 最大化（还原）/ 关闭按钮
    // =========================================================
    enum class CaptionKind { Minimize, Maximize, Close };

    class CaptionButton : public Node
    {
    public:
        CaptionButton(CommandRegistry& commands, CaptionKind kind, const TitleBar& bar)
            : m_commands(commands)
            , m_kind(kind)
            , m_bar(bar)
        {
            LayoutStyle s;
            s.width  = TitleBar::kButtonWidth;
            s.shrink = 0.0f;
            SetLayoutStyle(s);
        }

        const char* CommandId() const
        {
            switch (m_kind)
            {
            case CaptionKind::Minimize: return TitleBar::kMinimizeCommand;
            case CaptionKind::Maximize: return TitleBar::kMaximizeCommand;
            default:                    return TitleBar::kCloseCommand;
            }
        }

        void Refresh()
        {
            const bool exists = m_commands.Find(CommandId()) != nullptr;
            SetVisible(exists);
            const bool maximized = m_kind == CaptionKind::Maximize && m_commands.IsChecked(CommandId());
            if (maximized != m_maximized)
            {
                m_maximized = maximized;
                Invalidate();
            }
            switch (m_kind)
            {
            case CaptionKind::Minimize: SetTooltip("最小化"); break;
            case CaptionKind::Maximize: SetTooltip(m_maximized ? "向下还原" : "最大化"); break;
            case CaptionKind::Close:    SetTooltip("关闭"); break;
            }
        }

    protected:
        void OnPaint(DrawList& dl, const Rect& r) override
        {
            const bool close = m_kind == CaptionKind::Close;
            const bool hot   = IsHovered() || m_pressed;
            if (hot)
            {
                const ColorRef bg = close ? ColorRef(m_pressed ? kClosePressed : kCloseHover)
                                          : (m_pressed ? Theme::ControlPressed : Theme::ControlHover);
                dl.AddRectFilled(r, bg);
            }

            const ColorRef ink = (close && hot) ? ColorRef(Colors::White) : (m_bar.IsWindowActive() ? Theme::Text : Theme::TextDim);
            const Vec2  c = r.Center();
            const float h = kGlyph * 0.5f;
            switch (m_kind)
            {
            case CaptionKind::Minimize:
                dl.AddLine({ c.x - h, c.y }, { c.x + h, c.y }, ink, 1.0f);
                break;
            case CaptionKind::Maximize:
                if (!m_maximized)
                {
                    dl.AddRect(Rect{ c.x - h, c.y - h, c.x + h, c.y + h }, ink, 0.0f, 1.0f);
                }
                else
                {
                    // 还原：前面一个方框，右上方露出后面方框的两条边
                    const float s = kGlyph * 0.8f;
                    const float o = kGlyph * 0.2f;
                    const Rect front{ c.x - h, c.y - h + o, c.x - h + s, c.y - h + o + s };
                    dl.AddRect(front, ink, 0.0f, 1.0f);
                    dl.AddLine({ front.min.x + o, front.min.y - o + 0.5f }, { front.max.x + o - 0.5f, front.min.y - o + 0.5f }, ink, 1.0f);
                    dl.AddLine({ front.max.x + o - 0.5f, front.min.y - o + 0.5f }, { front.max.x + o - 0.5f, front.max.y - o }, ink, 1.0f);
                }
                break;
            case CaptionKind::Close:
                dl.AddCross(c, h, ink, 1.1f);
                break;
            }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase == EventPhase::Capture)
                return;
            switch (e.type)
            {
            case PointerEventType::Enter:
            case PointerEventType::Leave:
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.button == MouseButton::Left)
                {
                    m_pressed = true;
                    GetContext()->SetCapture(this);
                    Invalidate();
                    e.handled = true;
                }
                break;
            case PointerEventType::Up:
                if (e.button == MouseButton::Left && m_pressed)
                {
                    const bool inside = IsHovered();
                    m_pressed = false;
                    GetContext()->ReleaseCapture();
                    Invalidate();
                    e.handled = true;
                    if (inside)
                        m_commands.Execute(CommandId());    // 放在最后：关闭窗口时宿主可能销毁整个界面
                }
                break;
            case PointerEventType::Cancel:
                m_pressed = false;
                Invalidate();
                break;
            default:
                break;
            }
        }

    private:
        CommandRegistry& m_commands;
        CaptionKind      m_kind;
        const TitleBar&  m_bar;
        bool             m_pressed   = false;
        bool             m_maximized = false;
    };

    // =========================================================
    // 标题栏
    // =========================================================
    TitleBar::TitleBar(CommandRegistry& commands)
        : m_commands(commands)
    {
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Stretch;
        s.height     = kHeight;
        s.shrink     = 0.0f;
        s.padding    = Edges::Make(kIconMargin, 0.0f, 0.0f, 0.0f);
        SetLayoutStyle(s);

        m_content = AddChild<Node>();
        LayoutStyle cs;
        cs.direction  = FlexDirection::Row;
        cs.alignItems = Align::Center;
        cs.shrink     = 1.0f;
        cs.minWidth   = 0.0f;
        m_content->SetLayoutStyle(cs);
        m_content->SetHitTestVisible(false);        // 内容之间的空白算作标题区域
        m_content->SetClipChildren(true);

        Node* spacer = AddChild<Node>();
        spacer->EditLayoutStyle().grow = 1.0f;
        spacer->SetHitTestVisible(false);

        m_buttons[0] = AddChild<CaptionButton>(m_commands, CaptionKind::Minimize, *this);
        m_buttons[1] = AddChild<CaptionButton>(m_commands, CaptionKind::Maximize, *this);
        m_buttons[2] = AddChild<CaptionButton>(m_commands, CaptionKind::Close,    *this);

        m_listener      = m_commands.AddListener([this] { RefreshState(); });
        m_registryAlive = m_commands.GetLifetimeToken();
        RefreshState();
    }

    TitleBar::~TitleBar()
    {
        if (!m_registryAlive.expired())
            m_commands.RemoveListener(m_listener);
    }

    void TitleBar::SetTitle(std::string title)
    {
        if (title == m_title)
            return;
        m_title = std::move(title);
        Invalidate();
    }

    void TitleBar::SetIcon(TextureId icon)
    {
        m_icon = icon;
        EditLayoutStyle().padding.left = icon != InvalidTextureId ? kIconMargin * 2.0f + kIconSize : kIconMargin;
        Invalidate();
    }

    void TitleBar::SetWindowActive(bool active)
    {
        if (active == m_active)
            return;
        m_active = active;
        Invalidate();
        for (CaptionButton* b : m_buttons)
            b->Invalidate();
    }

    void TitleBar::RefreshState()
    {
        for (CaptionButton* b : m_buttons)
            b->Refresh();
    }

    bool TitleBar::IsCaptionAt(Vec2 windowPos) const
    {
        UIContext* ctx = GetContext();
        if (!ctx || !IsVisible() || !GetScreenBounds().Contains(windowPos))
            return false;
        // 有弹层打开时（例如菜单）点击要交给界面做轻触关闭，不能变成拖动窗口
        if (ctx->GetTopPopup() || ctx->GetCapture())
            return false;

        for (const Node* n = ctx->HitTest(windowPos); n; n = n->GetParent())
        {
            if (n == this)
                return true;
            if (!IsStaticNode(n))
                return false;
        }
        return false;       // 命中的是别的层（不应发生）
    }

    Rect TitleBar::TitleBox() const
    {
        UIContext* ctx = GetContext();
        const Vec2 size = GetSize();
        if (!ctx || m_title.empty())
            return {};

        TextParams p;
        p.size = 13.0f;
        const float textW = ctx->GetTextSystem().Measure(m_title, p).x;

        float left  = m_content->GetBounds().max.x + kTitleGap;
        float right = size.x - kTitleGap;
        for (const CaptionButton* b : m_buttons)
        {
            if (b->IsVisible())
            {
                right = b->GetBounds().min.x - kTitleGap;
                break;
            }
        }

        // 优先在整个标题栏居中；与两侧重叠时在中间的空白处居中，再放不下就截断
        float x0 = (size.x - textW) * 0.5f;
        if (x0 < left || x0 + textW > right)
            x0 = left + std::max(0.0f, (right - left - textW) * 0.5f);
        const float x1 = std::min(x0 + textW, right);
        if (x1 <= x0)
            return {};
        return Rect{ x0, 0.0f, x1, size.y };
    }

    Rect TitleBar::GetTitleRect() const
    {
        const Rect box = TitleBox();
        const Rect sb  = GetScreenBounds();
        return box.IsEmpty() ? Rect{} : Rect{ box.min + sb.min, box.max + sb.min };
    }

    void TitleBar::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, Theme::Panel);

        if (m_icon != InvalidTextureId)
        {
            const float y = r.min.y + (r.Height() - kIconSize) * 0.5f;
            const float x = r.min.x + kIconMargin;
            dl.AddImage(m_icon, Rect{ x, y, x + kIconSize, y + kIconSize }, { 0, 0 }, { 1, 1 },
                        m_active ? ColorRef(Colors::White) : ColorRef(ColorFromHex(0xFFFFFF, 150)));
        }

        const Rect box = TitleBox();
        if (UIContext* ctx = GetContext(); ctx && !box.IsEmpty())
        {
            TextParams p;
            p.size     = 13.0f;
            p.color    = m_active ? Theme::Text : Theme::TextDim;
            p.vAlign   = TextAlign::Center;
            p.ellipsis = true;
            ctx->GetTextSystem().Draw(dl, Rect{ box.min + r.min, box.max + r.min }, m_title, p);
        }
    }
}
