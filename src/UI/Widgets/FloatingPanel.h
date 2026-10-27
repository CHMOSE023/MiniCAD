#pragma once
#include "Core/Popup.h"
#include <functional>
#include <memory>
#include <string>

namespace MiniGUI
{
    // 浮动面板：标题栏（标题、×；按住拖动，双击停靠回去）+ 内容区，四边与四角可拖动改大小。
    // 停靠区的面板浮出后放在这里（见 DockSpace / IDockFloatHost）：
    // - 窗口内浮动：作为弹层打开（非模态、不轻触关闭、不随 Esc 关闭），位置由 SetRect 给出，弹层层把它限制在窗口可见范围内
    // - 独立的系统窗口：SetFrameless(true) 后直接挂在那个窗口 UIContext 的根节点下铺满窗口，不画阴影与边框；
    //   移动与改大小交给宿主（移动系统窗口、系统的缩放边框），本节点只经 SetOnDrag 上报标题栏的拖动
    // 标题栏拖动按窗口坐标上报，宿主据此在停靠区里找落点（DockSpace::BeginExternalDrag …）；
    // 拖动中按 Esc 由停靠区取消落点，面板本身照常移动
    class FloatingPanel : public Popup
    {
    public:
        enum class DragPhase { Begin, Update, End, Cancel };

        explicit FloatingPanel(std::string title);

        void               SetTitle(std::string title);
        const std::string& GetTitle() const { return m_title; }

        void                  SetContent(std::unique_ptr<Node> content);
        std::unique_ptr<Node> TakeContent();
        Node*                 GetContent() const { return m_content; }

        // 位置与尺寸（窗口坐标，逻辑像素）。挂在树上之后 GetRect 为布局后的实际位置（弹层可能被平移进窗口）
        void SetRect(const Rect& windowRect);
        Rect GetRect() const;
        void SetFrameless(bool frameless);
        bool IsFrameless() const { return m_frameless; }

        void SetOnClose   (std::function<void()> cb) { m_onClose    = std::move(cb); }   // 点了 ×
        void SetOnRedock  (std::function<void()> cb) { m_onRedock   = std::move(cb); }   // 双击标题栏
        void SetOnMoved   (std::function<void()> cb) { m_onMoved    = std::move(cb); }   // 用户拖动或改大小结束（宿主保存布局）
        void SetOnActivate(std::function<void()> cb) { m_onActivate = std::move(cb); }   // 在面板上按下：宿主置前
        void SetOnDrag(std::function<void(DragPhase, Vec2 windowPos)> cb) { m_onDrag = std::move(cb); }

        static constexpr float kTitleHeight  = 28.0f;
        static constexpr float kResizeBorder = 6.0f;
        static constexpr float kDragStart    = 6.0f;
        static constexpr float kMinWidth     = 160.0f;
        static constexpr float kMinHeight    = 100.0f;

    protected:
        void        OnPaint(DrawList& dl, const Rect& screenRect) override;
        void        OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;
        bool        InterceptsHit(Vec2 local) const override;
        CursorShape GetCursor(Vec2 local) const override;
        void        OnPointerEvent(PointerEvent& e) override;

    private:
        friend class FloatingTitleBar;

        enum Edge { Left = 1, Right = 2, Top = 4, Bottom = 8 };
        int  EdgeAt(Vec2 local) const;
        void Moved();

        std::string m_title;
        Node*       m_titleBar  = nullptr;
        Node*       m_body      = nullptr;    // 内容的容器（铺满标题栏以下）
        Node*       m_content   = nullptr;
        Rect        m_rect;                   // 请求的位置
        bool        m_frameless = false;

        int  m_resizeEdges = 0;               // 正在拖动的边（Edge 掩码）
        Vec2 m_resizeStart;
        Rect m_resizeRect;

        std::function<void()>                m_onClose, m_onRedock, m_onMoved, m_onActivate;
        std::function<void(DragPhase, Vec2)> m_onDrag;
    };
}
