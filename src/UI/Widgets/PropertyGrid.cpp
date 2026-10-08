#include "Widgets/PropertyGrid.h"
#include "Widgets/Label.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    // =========================================================
    // Expander
    // =========================================================
    Expander::Expander(std::string title, bool expanded)
        : m_title(std::move(title))
        , m_expanded(expanded)
    {
        SetFocusable(true);
        LayoutStyle s;
        s.padding = Edges::Make(0.0f, kHeaderHeight, 0.0f, 0.0f);   // 标题行画在上内边距里
        SetLayoutStyle(s);

        m_content = AddChild<Node>();
        m_content->SetVisible(expanded);
    }

    void Expander::SetExpanded(bool expanded)
    {
        if (m_expanded == expanded)
            return;
        m_expanded = expanded;
        m_content->SetVisible(expanded);
        Invalidate();
        if (m_onToggled)
            m_onToggled(expanded);
    }

    void Expander::OnPaint(DrawList& dl, const Rect& r)
    {
        const Rect header{ r.min.x, r.min.y, r.max.x, r.min.y + kHeaderHeight };
        dl.AddRectFilled(header, m_hover ? Theme::RowHover : Theme::Panel);
        dl.AddRectFilled(Rect{ header.min.x, header.max.y - 1.0f, header.max.x, header.max.y }, Theme::BorderSubtle);

        const Vec2 c{ header.min.x + 14.0f, header.Center().y };
        if (m_expanded)
            dl.AddTriangleFilled({ c.x - 4.0f, c.y - 2.0f }, { c.x + 4.0f, c.y - 2.0f }, { c.x, c.y + 3.0f }, Theme::TextDim);
        else
            dl.AddTriangleFilled({ c.x - 2.0f, c.y - 4.0f }, { c.x + 3.0f, c.y }, { c.x - 2.0f, c.y + 4.0f }, Theme::TextDim);

        if (UIContext* ctx = GetContext())
        {
            TextParams p;
            p.size   = 13.0f;
            p.color  = Theme::Text;
            p.vAlign = TextAlign::Center;
            ctx->GetTextSystem().Draw(dl, Rect{ header.min.x + 26.0f, header.min.y, header.max.x - 8.0f, header.max.y }, m_title, p);
            if (HasFocus() && ctx->IsFocusVisible())
                dl.AddRect(header.Deflated(1.0f), Theme::Accent, 2.0f, 1.0f);
        }
    }

    void Expander::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase != EventPhase::Target)
            return;
        const bool inHeader = e.localPosition.y < kHeaderHeight;
        switch (e.type)
        {
        case PointerEventType::Move:
            if (inHeader != m_hover) { m_hover = inHeader; Invalidate(); }
            break;
        case PointerEventType::Leave:
            m_hover = false;
            Invalidate();
            break;
        case PointerEventType::Down:
            if (inHeader && e.button == MouseButton::Left)
            {
                SetExpanded(!m_expanded);
                e.handled = true;
            }
            break;
        default:
            break;
        }
    }

    void Expander::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.modifiers != 0)
            return;
        switch (e.key)
        {
        case Key::Space:
        case Key::Enter: SetExpanded(!m_expanded); break;
        case Key::Left:  SetExpanded(false);       break;
        case Key::Right: SetExpanded(true);        break;
        default: return;
        }
        e.handled = true;
    }

    // =========================================================
    // PropertyGrid
    // =========================================================
    namespace
    {
        // 一行特性：底部一条横线 + 名称列与编辑控件之间的竖线。
        // 竖线由每一行自己画，所以不会穿过分组标题行，也不会画到最后一行下面的空白处
        class PropertyRow : public Node
        {
        public:
            explicit PropertyRow(const PropertyGrid* grid) : m_grid(grid) {}

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                dl.AddRectFilled(Rect{ r.min.x, r.max.y - 1.0f, r.max.x, r.max.y }, Theme::GridLine);
                const float x = std::round(r.min.x + PropertyGrid::kIndent + m_grid->GetNameWidth() + PropertyGrid::kNameGap * 0.5f);
                dl.AddRectFilled(Rect{ x, r.min.y, x + 1.0f, r.max.y }, m_grid->IsBoundaryHot() ? Theme::Accent : Theme::GridLine);
            }

        private:
            const PropertyGrid* m_grid;
        };
    }

    PropertyGrid::PropertyGrid()
    {
        m_body = SetContent<Node>();
    }

    Expander* PropertyGrid::AddGroup(std::string title, bool expanded)
    {
        return m_body->AddChild<Expander>(std::move(title), expanded);
    }

    Node* PropertyGrid::AddProperty(Expander* group, std::string name, std::unique_ptr<Node> editor)
    {
        Node* parent = group ? group->GetContent() : m_body;
        Node* row = parent->AddChild<PropertyRow>(this);
        {
            LayoutStyle s;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = kNameGap;
            s.height     = kRowHeight;
            s.padding    = Edges::Make(kIndent, 0.0f, 6.0f, 1.0f);
            row->SetLayoutStyle(s);
        }

        Label* label = row->AddChild<Label>(std::move(name), 13.0f, Theme::TextDim);
        label->SetEllipsis(true);
        {
            LayoutStyle ls;
            ls.width  = m_nameWidth;
            ls.shrink = 0.0f;
            label->SetLayoutStyle(ls);
        }
        m_names.push_back(label);

        Node* e = row->AddChild(std::move(editor));
        {
            // 编辑控件占满剩余宽度；没指定高度时用紧凑的 24
            LayoutStyle& es = e->EditLayoutStyle();
            es.grow     = 1.0f;
            es.shrink   = 1.0f;
            es.minWidth = 40.0f;
            if (IsAuto(es.height))
                es.height = 24.0f;
        }
        return e;
    }

    void PropertyGrid::SetNameWidth(float width)
    {
        width = std::round(width);
        if (width == m_nameWidth)
            return;
        m_nameWidth = width;
        for (Label* l : m_names)
            l->EditLayoutStyle().width = width;
    }

    bool PropertyGrid::NearBoundary(Vec2 local) const
    {
        // 分隔线位置：行左内边距 + 名称列宽
        return std::abs(local.x - (kIndent + m_nameWidth + kNameGap * 0.5f)) <= 3.0f && local.x < GetViewportSize().x;
    }

    bool PropertyGrid::InterceptsHit(Vec2 local) const
    {
        return m_dragging || NearBoundary(local) || ScrollView::InterceptsHit(local);
    }

    CursorShape PropertyGrid::GetCursor(Vec2 local) const
    {
        return (m_dragging || NearBoundary(local)) ? CursorShape::SizeWE : CursorShape::Default;
    }

    void PropertyGrid::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase != EventPhase::Capture)
        {
            switch (e.type)
            {
            case PointerEventType::Move:
                if (m_dragging)
                {
                    SetNameWidth(std::clamp(e.localPosition.x - kIndent - kNameGap * 0.5f, 50.0f, std::max(50.0f, GetViewportSize().x - 80.0f)));
                    e.handled = true;
                    return;
                }
                if (const bool hover = e.phase == EventPhase::Target && NearBoundary(e.localPosition); hover != m_hoverBoundary)
                {
                    m_hoverBoundary = hover;
                    Invalidate();
                }
                break;
            case PointerEventType::Leave:
                m_hoverBoundary = false;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.phase == EventPhase::Target && e.button == MouseButton::Left && NearBoundary(e.localPosition))
                {
                    m_dragging = true;
                    GetContext()->SetCapture(this);
                    e.handled = true;
                    return;
                }
                break;
            case PointerEventType::Up:
                if (m_dragging)
                {
                    m_dragging = false;
                    GetContext()->ReleaseCapture();
                    e.handled = true;
                    Invalidate();
                    return;
                }
                break;
            default:
                break;
            }
        }
        ScrollView::OnPointerEvent(e);
    }
}
