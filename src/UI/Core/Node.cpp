#include "Core/Node.h"
#include "Core/UIContext.h"
#include "Layout/FlexLayout.h"
#include <algorithm>
#include <cassert>

namespace MiniGUI
{
    Node::Node() = default;

    Node::~Node() = default;

    // =========================================================
    // 树结构
    // =========================================================
    Node* Node::AddChild(std::unique_ptr<Node> child)
    {
        assert(child && child->m_parent == nullptr);
        Node* raw = child.get();
        raw->m_parent = this;
        m_children.push_back(std::move(child));

        // 新子节点需要由自己在 OnLayout 中安排位置
        InvalidateLayout();
        return raw;
    }

    Node* Node::InsertChild(size_t index, std::unique_ptr<Node> child)
    {
        assert(child && child->m_parent == nullptr);
        Node* raw = child.get();
        raw->m_parent = this;
        index = std::min(index, m_children.size());
        m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
        InvalidateLayout();
        return raw;
    }

    std::unique_ptr<Node> Node::RemoveChild(Node* child)
    {
        auto it = std::find_if(m_children.begin(), m_children.end(),
                               [child](const std::unique_ptr<Node>& c) { return c.get() == child; });
        if (it == m_children.end())
            return nullptr;

        if (UIContext* ctx = GetContext())
            ctx->OnSubtreeDetached(child);

        std::unique_ptr<Node> removed = std::move(*it);
        m_children.erase(it);
        removed->m_parent = nullptr;

        InvalidateLayout();
        return removed;
    }

    void Node::RemoveAllChildren()
    {
        if (m_children.empty())
            return;

        if (UIContext* ctx = GetContext())
        {
            for (auto& c : m_children)
                ctx->OnSubtreeDetached(c.get());
        }
        m_children.clear();
        InvalidateLayout();
    }

    UIContext* Node::GetContext() const
    {
        const Node* n = this;
        while (n->m_parent)
            n = n->m_parent;
        return n->m_context;
    }

    bool Node::IsAncestorOf(const Node* node) const
    {
        for (const Node* n = node; n; n = n->m_parent)
        {
            if (n == this)
                return true;
        }
        return false;
    }

    // =========================================================
    // 几何
    // =========================================================
    void Node::SetBounds(const Rect& bounds)
    {
        if (bounds == m_bounds)
            return;

        const bool sizeChanged = bounds.Size() != m_bounds.Size();
        m_bounds = bounds;

        if (sizeChanged)
        {
            // 尺寸变了，自己的子节点需要重新安排。
            // 布局过程中由父节点调用时不用向上传播：遍历马上就会走到自己
            m_needsLayout = true;
            UIContext* ctx = GetContext();
            if (!ctx || !ctx->IsInLayout())
                MarkSubtreeDirtyUpward();
        }
        Invalidate();
    }

    Rect Node::GetScreenBounds() const
    {
        Vec2 origin;
        for (const Node* n = this; n; n = n->m_parent)
            origin += n->m_bounds.min;
        return Rect{ origin, origin + m_bounds.Size() };
    }

    Vec2 Node::ToLocal(Vec2 windowPos) const
    {
        return windowPos - GetScreenBounds().min;
    }

    // =========================================================
    // 标志
    // =========================================================
    void Node::SetVisible(bool visible)
    {
        if (m_visible == visible)
            return;
        m_visible = visible;

        // 隐藏时清掉子树上的悬停和捕获，Update 之后会按指针位置重新计算
        if (!visible)
        {
            if (UIContext* ctx = GetContext())
                ctx->OnSubtreeDetached(this);
        }

        if (m_parent)
            m_parent->InvalidateLayout();
        Invalidate();
    }

    void Node::SetEnabled(bool enabled)
    {
        if (m_enabled == enabled)
            return;
        m_enabled = enabled;

        if (!enabled)
        {
            UIContext* ctx = GetContext();
            if (ctx && ctx->GetCapture() && IsAncestorOf(ctx->GetCapture()))
                ctx->PointerCancel();
            if (ctx && ctx->GetFocus() && IsAncestorOf(ctx->GetFocus()))
                ctx->SetFocus(nullptr);
        }

        OnEnabledChanged();
        Invalidate();
    }

    bool Node::IsEnabled() const
    {
        for (const Node* n = this; n; n = n->m_parent)
        {
            if (!n->m_enabled)
                return false;
        }
        return true;
    }

    void Node::SetClipChildren(bool clip)
    {
        if (m_clipChildren == clip)
            return;
        m_clipChildren = clip;
        Invalidate();
    }

    bool Node::HasCapture() const
    {
        UIContext* ctx = GetContext();
        return ctx && ctx->GetCapture() == this;
    }

    // =========================================================
    // 焦点
    // =========================================================
    void Node::SetFocusable(bool focusable)
    {
        m_focusable = focusable;
        if (!focusable && HasFocus())
            GetContext()->SetFocus(nullptr);
    }

    bool Node::CanTakeFocus() const
    {
        if (!m_focusable || !IsEnabled())
            return false;
        for (const Node* n = this; n; n = n->m_parent)
        {
            if (!n->m_visible)
                return false;
        }
        return true;
    }

    bool Node::HasFocus() const
    {
        UIContext* ctx = GetContext();
        return ctx && ctx->GetFocus() == this;
    }

    bool Node::Focus(FocusReason reason)
    {
        UIContext* ctx = GetContext();
        if (!ctx || !CanTakeFocus())
            return false;
        ctx->SetFocus(this, reason);
        return true;
    }

    // =========================================================
    // 脏标记
    // =========================================================
    void Node::Invalidate()
    {
        if (UIContext* ctx = GetContext())
            ctx->RequestRedraw();
    }

    void Node::InvalidateLayout()
    {
        // 自己的期望尺寸可能变了，会影响每一级祖先的测量结果和排列，所以一路标记到根。
        // 不在中途提前结束：层级通常很浅，而提前结束需要维护复杂的不变式
        m_needsLayout  = true;
        m_measureValid = false;
        for (Node* p = m_parent; p; p = p->m_parent)
        {
            p->m_needsLayout  = true;
            p->m_measureValid = false;
        }
        Invalidate();
    }

    // =========================================================
    // 布局
    // =========================================================
    void Node::SetLayoutStyle(const LayoutStyle& style)
    {
        m_layoutStyle = style;
        InvalidateLayout();
    }

    LayoutStyle& Node::EditLayoutStyle()
    {
        InvalidateLayout();
        return m_layoutStyle;
    }

    Vec2 Node::Measure(Vec2 available)
    {
        if (m_measureValid && m_measureAvailable == available)
            return m_measureResult;

        const LayoutStyle& s = m_layoutStyle;

        // 固定尺寸的方向，内容只能在这个尺寸内排布
        Vec2 contentAvail = available;
        if (!IsAuto(s.width))  contentAvail.x = s.width;
        if (!IsAuto(s.height)) contentAvail.y = s.height;

        // 两个方向都固定时不需要测量内容
        Vec2 result{ s.width, s.height };
        if (IsAuto(s.width) || IsAuto(s.height))
        {
            const Vec2 content = MeasureContent(contentAvail);
            if (IsAuto(s.width))  result.x = content.x;
            if (IsAuto(s.height)) result.y = content.y;
        }
        result.x = std::max(s.minWidth,  std::min(result.x, s.maxWidth));
        result.y = std::max(s.minHeight, std::min(result.y, s.maxHeight));

        m_measureAvailable = available;
        m_measureResult    = result;
        m_measureValid     = true;
        return result;
    }

    Vec2 Node::MeasureContent(Vec2 available)
    {
        return GetLayoutEngine().MeasureChildren(*this, available);
    }

    void Node::OnLayout()
    {
        GetLayoutEngine().ArrangeChildren(*this, GetPixelScale());
    }

    ILayoutEngine& Node::GetLayoutEngine() const
    {
        UIContext* ctx = GetContext();
        return ctx ? ctx->GetLayoutEngine() : FlexLayout::Default();
    }

    float Node::GetPixelScale() const
    {
        UIContext* ctx = GetContext();
        return ctx ? ctx->GetPixelScale() : 1.0f;
    }

    void Node::MarkSubtreeDirtyUpward()
    {
        // 已经标记过的祖先，其上方也一定已标记，可以提前结束
        for (Node* p = m_parent; p && !p->m_subtreeNeedsLayout; p = p->m_parent)
            p->m_subtreeNeedsLayout = true;
    }

    void Node::ClearHoverRecursive()
    {
        if (!m_hovered)
            return;
        m_hovered = false;
        for (auto& c : m_children)
            c->ClearHoverRecursive();
    }
}
