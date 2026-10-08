#pragma once
#include "Style/Theme.hpp"
#include "Core/Node.h"
#include <functional>

namespace MiniGUI
{
    enum class PopupPlacement
    {
        Below,      // 锚点矩形下方，左对齐；下方放不下时翻到上方（下拉框、菜单栏菜单）
        Right,      // 锚点矩形右侧，顶部对齐；右侧放不下时翻到左侧（子菜单）
        AtPoint,    // 以一个点为左上角；放不下时向左/向上翻转（右键菜单、悬浮提示）
        Center,     // 窗口居中（对话框）
        Manual,     // 指定左上角（对话框被拖动之后）
    };

    struct PopupStyle
    {
        ColorRef background = Theme::Panel;
        ColorRef border     = Theme::PopupBorder;
        float   rounding   = 6.0f;
        bool    shadow     = true;
    };

    // 弹层基类：菜单、下拉列表、对话框、输入建议都从它派生。
    // 弹层由 UIContext 的弹层（位于主界面之上）持有，通过 UIContext::OpenPopup 打开。
    // - 轻触关闭（light dismiss）：点击弹层和所有者以外的地方时自动关闭
    // - 模态：下方铺一层半透明遮罩，挡住主界面的输入；Tab 只在模态弹层内部循环
    // - Esc 关闭；关闭时把焦点还给打开前的节点
    // - 关闭时先从树上摘下，销毁推迟到下一次 Render，因此在自己的事件处理函数里调用 Close() 是安全的
    class Popup : public Node
    {
    public:
        Popup();

        // ── 位置 ────────────────────────────────────────────────
        void SetAnchor(const Rect& windowRect, PopupPlacement placement = PopupPlacement::Below);
        void SetPoint(Vec2 windowPos);
        void SetCentered();
        void MoveTo(Vec2 windowPos);                    // 改为手动位置（拖动标题栏时使用）
        void SetMatchAnchorWidth(bool match) { m_matchAnchorWidth = match; }
        void SetMaxHeight(float h) { m_maxHeight = h; }

        // ── 行为 ────────────────────────────────────────────────
        void SetModal(bool modal)             { m_modal = modal; if (modal) m_lightDismiss = false; }
        bool IsModal() const                  { return m_modal; }
        void SetLightDismiss(bool dismiss)    { m_lightDismiss = dismiss; }
        void SetCloseOnEscape(bool close)     { m_closeOnEscape = close; }
        void SetOwner(Node* owner)            { m_owner = owner; }   // 点击所有者不触发轻触关闭（交给所有者自己切换）
        Node* GetOwner() const                { return m_owner; }
        void SetOnClosed(std::function<void()> cb) { m_onClosed = std::move(cb); }

        void SetStyle(const PopupStyle& style) { m_style = style; Invalidate(); }

        void Close();
        bool IsOpen() const { return m_open; }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnKeyEvent(KeyEvent& e) override;

        // 打开后调用：默认聚焦弹层自身；对话框可以改为聚焦第一个输入框
        virtual void OnOpened() {}

    private:
        friend class UIContext;
        friend class PopupLayer;

        void RelayoutInLayer();

        PopupPlacement m_placement = PopupPlacement::Center;
        Rect           m_anchor;
        Vec2           m_point;
        bool           m_matchAnchorWidth = false;
        float          m_maxHeight        = 0.0f;     // 0 表示只受窗口限制

        bool  m_modal         = false;
        bool  m_lightDismiss  = true;
        bool  m_closeOnEscape = true;
        bool  m_open          = false;
        Node* m_owner         = nullptr;
        Node* m_restoreFocus  = nullptr;              // 关闭时恢复焦点
        Node* m_backdrop      = nullptr;              // 模态遮罩

        PopupStyle            m_style;
        std::function<void()> m_onClosed;
    };

    // 计算弹出矩形的位置：按 placement 放置，超出窗口时翻转或平移，保证完整可见
    Rect PlacePopup(Vec2 size, PopupPlacement placement, const Rect& anchor, Vec2 point, Vec2 display, float margin = 4.0f);

    // ── UIContext 内部使用的图层 ─────────────────────────────────

    // 弹层所在的图层：铺满窗口，自身不接收点击（空白处穿透到主界面）
    class PopupLayer : public Node
    {
    public:
        PopupLayer() { SetHitTestVisible(false); }

    protected:
        void OnLayout() override;
    };

    // 模态遮罩：铺满窗口，吞掉所有指针事件
    class ModalBackdrop : public Node
    {
    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
    };
}
