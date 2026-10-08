#pragma once
#include "Core/Node.h"
#include "Core/Event.hpp"
#include "Render/DrawData.hpp"
#include <functional>
#include <string_view>

namespace MiniGUI
{
    // 转发给宿主的指针事件：在 PointerEvent 的基础上补充视口内的物理像素坐标
    struct ViewportPointerEvent
    {
        PointerEventType type       = PointerEventType::Move;
        Vec2             local;                 // 视口内坐标（逻辑像素）
        Vec2             pixel;                 // 视口内坐标（物理像素，与宿主渲染目标的像素一一对应）
        Vec2             wheelDelta;            // 滚轮格数，向上/向右为正
        MouseButton      button     = MouseButton::None;
        uint8_t          buttons    = 0;        // MouseButtonMask
        uint8_t          modifiers  = 0;        // ModifierKey
        int              clickCount = 1;
    };

    // 嵌入宿主渲染的画面（例如 MiniCAD 的 CAD 视口）。
    // - 布局决定它的矩形；宿主按 GetPixelSize() 的物理像素大小渲染，把结果登记为纹理（D3D11Backend::RegisterExternalTexture）
    //   后用 SetTexture 交给它显示，纹理与视口像素一一对应，不缩放
    // - 指针和键盘事件原样转发给宿主，坐标换算成视口内坐标；按下后自动捕获，拖出视口仍继续收到事件
    // - 点击获得键盘焦点；有焦点时按键和文字输入转发给宿主（宿主不处理的按键继续冒泡，例如 Tab 切换焦点）
    //
    // 帧顺序（避免晚一帧）：输入事件到达时宿主立即应用（例如平移相机），并调用 RequestRender；
    // 宿主每帧先 UIContext::Update() 完成布局，再调用 RenderContent() 让宿主按最新尺寸和状态渲染视口，
    // 最后 UIContext::Render() 把界面连同视口纹理一起画出。输入、视口画面、界面在同一帧内呈现
    class ViewportHost : public Node
    {
    public:
        using RenderFn  = std::function<void(int pixelWidth, int pixelHeight)>;
        using PointerFn = std::function<void(const ViewportPointerEvent& e)>;
        using KeyFn     = std::function<bool(const KeyEvent& e)>;         // 返回 true 表示已处理
        using TextFn    = std::function<bool(std::string_view utf8)>;

        ViewportHost();

        void SetOnRender (RenderFn fn)  { m_onRender  = std::move(fn); }
        void SetOnPointer(PointerFn fn) { m_onPointer = std::move(fn); }
        void SetOnKey    (KeyFn fn)     { m_onKey     = std::move(fn); }
        void SetOnText   (TextFn fn)    { m_onText    = std::move(fn); }

        // 要显示的纹理（宿主的渲染结果）；尺寸变化后宿主重新创建渲染目标时一并更新
        void      SetTexture(TextureId texture);
        TextureId GetTexture() const { return m_texture; }

        // 视口内显示的光标；CAD 视口自己绘制十字光标时用 Hidden
        void SetCursor(CursorShape cursor) { m_cursor = cursor; }

        // 画面需要更新（宿主的场景或相机变化）：标记并请求重绘
        void RequestRender();
        bool NeedsRender() const { return m_renderRequested; }

        // 物理像素尺寸（布局完成后有效）
        void GetPixelSize(int& width, int& height) const;

        // 布局之后、UIContext::Render 之前调用：可见且尺寸有效时，若请求了渲染或尺寸变化，回调宿主渲染。
        // 返回是否调用了回调
        bool RenderContent();

    protected:
        void        OnPaint(DrawList& dl, const Rect& screenRect) override;
        void        OnPointerEvent(PointerEvent& e) override;
        void        OnKeyEvent(KeyEvent& e) override;
        void        OnTextInput(TextInputEvent& e) override;
        CursorShape GetCursor(Vec2 local) const override { (void)local; return m_cursor; }

    private:
        bool IsEffectivelyVisible() const;

        RenderFn  m_onRender;
        PointerFn m_onPointer;
        KeyFn     m_onKey;
        TextFn    m_onText;

        TextureId   m_texture         = InvalidTextureId;
        CursorShape m_cursor          = CursorShape::Arrow;
        bool        m_renderRequested = true;
        int         m_renderedWidth   = 0;      // 上一次回调宿主渲染时的像素尺寸
        int         m_renderedHeight  = 0;
    };
}
