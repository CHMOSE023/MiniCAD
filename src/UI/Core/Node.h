#pragma once
#include "Core/Event.hpp"
#include "Core/Types/Color.hpp"
#include "Core/Types/Rect.hpp"
#include "Layout/LayoutStyle.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class DrawList;
    class UIContext;
    class ILayoutEngine;

    // 界面节点：保留模式的基本单元。
    // - 父节点通过 unique_ptr 持有子节点，向上的父指针是裸指针
    // - bounds 是相对父节点左上角的矩形（逻辑像素），由父节点在 OnLayout 中设置
    // - 默认 OnLayout 按 LayoutStyle 用布局引擎（Flexbox）排列子节点；需要特殊排布时可以重写
    // - 子节点按顺序绘制，后面的盖在前面的上面；命中测试顺序相反
    class Node
    {
    public:
        Node();
        virtual ~Node();

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

        // ── 树结构 ──────────────────────────────────────────────
        Node* AddChild(std::unique_ptr<Node> child);

        template<typename T, typename... Args>
        T* AddChild(Args&&... args)
        {
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = child.get();
            AddChild(std::move(child));
            return raw;
        }

        Node* InsertChild(size_t index, std::unique_ptr<Node> child);   // 插入到第 index 个位置（超出则追加）
        std::unique_ptr<Node> RemoveChild(Node* child);   // 返回被移除的子节点，调用方决定是否销毁
        void                  RemoveAllChildren();

        Node*       GetParent()  const { return m_parent; }
        UIContext*  GetContext() const;                     // 未挂到 UIContext 上时返回 nullptr
        const std::vector<std::unique_ptr<Node>>& GetChildren() const { return m_children; }
        bool        IsAncestorOf(const Node* node) const;   // node 是否是自己或自己的后代

        // ── 几何 ────────────────────────────────────────────────
        void        SetBounds(const Rect& bounds);
        const Rect& GetBounds() const { return m_bounds; }
        Vec2        GetSize()   const { return m_bounds.Size(); }
        Rect        GetScreenBounds() const;                // 窗口坐标
        Vec2        ToLocal(Vec2 windowPos) const;

        // ── 布局 ────────────────────────────────────────────────
        const LayoutStyle& GetLayoutStyle() const { return m_layoutStyle; }
        void               SetLayoutStyle(const LayoutStyle& style);
        LayoutStyle&       EditLayoutStyle();                // 返回可修改的引用，调用时即标记需要重新布局

        // 期望尺寸：固定尺寸优先，否则由内容决定，再受 min/max 限制。结果按 available 缓存
        Vec2 Measure(Vec2 available);

        // ── 标志 ────────────────────────────────────────────────
        void SetVisible(bool visible);
        bool IsVisible() const { return m_visible; }

        void SetEnabled(bool enabled);
        bool IsEnabled() const;                             // 考虑祖先：任一祖先禁用则自己也禁用

        void SetHitTestVisible(bool v) { m_hitTestVisible = v; }  // false：命中测试穿透自己（子节点仍可命中）
        bool IsHitTestVisible() const  { return m_hitTestVisible; }

        void SetClipChildren(bool clip);
        bool GetClipChildren() const { return m_clipChildren; }

        bool IsHovered() const { return m_hovered; }        // 指针在自己或某个子节点上
        bool HasCapture() const;

        // ── 焦点 ────────────────────────────────────────────────
        void SetFocusable(bool focusable);                  // 可以通过点击或 Tab 获得键盘焦点
        bool IsFocusable() const { return m_focusable; }
        bool CanTakeFocus() const;                          // 可聚焦、可见（含祖先）且启用
        bool HasFocus() const;
        bool Focus(FocusReason reason = FocusReason::Program);

        // 文本输入控件返回 true：此时全局快捷键让位，平台层开启输入法
        virtual bool AcceptsTextInput() const { return false; }

        // 文字光标矩形（窗口坐标），用于定位输入法候选窗；返回 false 表示没有光标
        virtual bool GetTextCaretRect(Rect& out) const { (void)out; return false; }

        // ── 悬浮提示与光标 ──────────────────────────────────────
        // 鼠标停留一段时间后显示提示；子节点没有提示时沿用父节点的
        void               SetTooltip(std::string text) { m_tooltip = std::move(text); }
        const std::string& GetTooltip() const { return m_tooltip; }

        // 指针在 local 位置时的光标形状；Default 表示沿用父节点
        virtual CursorShape GetCursor(Vec2 local) const { (void)local; return CursorShape::Default; }

        // ── 右键菜单 ────────────────────────────────────────────
        // 右键抬起、菜单键、Shift+F10 时，从目标节点往上依次询问，第一个返回 true 的节点负责弹出菜单。
        // windowPos：鼠标位置，或用键盘打开时焦点控件的位置
        void SetContextMenuHandler(std::function<bool(Vec2 windowPos)> handler) { m_contextMenuHandler = std::move(handler); }
        virtual bool OnContextMenu(Vec2 windowPos) { return m_contextMenuHandler && m_contextMenuHandler(windowPos); }

        // ── 脏标记 ──────────────────────────────────────────────
        void Invalidate();          // 外观变化：请求重绘
        void InvalidateLayout();    // 内容或尺寸需求变化：请求重新布局（向上传播）并重绘

    protected:
        // ── 由子类重写 ──────────────────────────────────────────
        virtual void OnLayout();                                            // 设置子节点的 bounds，默认用布局引擎
        virtual Vec2 MeasureContent(Vec2 available);                        // 内容所需尺寸（含内边距），默认由子节点决定
        virtual void OnPaint(DrawList& dl, const Rect& screenRect) { (void)dl; (void)screenRect; }
        // 子节点绘制完之后调用（例如滚动条要盖在内容上面）
        virtual void OnPaintOverlay(DrawList& dl, const Rect& screenRect) { (void)dl; (void)screenRect; }
        // 返回 true 时，这个位置由自己接收指针事件，不再测试子节点（例如滚动条区域）
        virtual bool InterceptsHit(Vec2 local) const { (void)local; return false; }
        virtual void OnPointerEvent(PointerEvent& e) { (void)e; }
        virtual void OnKeyEvent(KeyEvent& e) { (void)e; }
        virtual void OnTextInput(TextInputEvent& e) { (void)e; }
        virtual void OnComposition(const CompositionEvent& e) { (void)e; }
        virtual void OnFocusChanged(bool focused) { (void)focused; }
        virtual void OnEnabledChanged() {}
        virtual void OnThemeChanged() {}            // UIContext::SetTheme 之后对整棵树调用（颜色引用无需处理）

    private:
        friend class UIContext;

        void MarkSubtreeDirtyUpward();
        void ClearHoverRecursive();
        ILayoutEngine& GetLayoutEngine() const;
        float          GetPixelScale() const;

    private:
        Node*                              m_parent  = nullptr;
        UIContext*                         m_context = nullptr;   // 只有根节点设置
        std::vector<std::unique_ptr<Node>> m_children;

        Rect        m_bounds;
        LayoutStyle m_layoutStyle;
        std::string m_tooltip;
        std::function<bool(Vec2)> m_contextMenuHandler;

        // Measure 缓存（单条）：InvalidateLayout 时连同所有祖先一起失效
        Vec2 m_measureAvailable;
        Vec2 m_measureResult;
        bool m_measureValid = false;

        bool m_visible       = true;
        bool m_enabled        = true;
        bool m_hitTestVisible = true;
        bool m_clipChildren   = false;
        bool m_hovered        = false;
        bool m_focusable      = false;

        bool m_needsLayout        = true;   // 自己需要重新执行 OnLayout
        bool m_subtreeNeedsLayout = false;  // 某个后代需要重新布局
    };
}
