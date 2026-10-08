#include "Widgets/ViewportHost.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include <cmath>

namespace MiniGUI
{
    ViewportHost::ViewportHost()
    {
        SetFocusable(true);
        SetClipChildren(true);
    }

    void ViewportHost::SetTexture(TextureId texture)
    {
        if (m_texture == texture)
            return;
        m_texture = texture;
        Invalidate();
    }

    void ViewportHost::RequestRender()
    {
        m_renderRequested = true;
        Invalidate();
    }

    void ViewportHost::GetPixelSize(int& width, int& height) const
    {
        const UIContext* ctx   = GetContext();
        const float      scale = ctx ? ctx->GetPixelScale() : 1.0f;
        const Vec2       size  = GetSize();
        width  = static_cast<int>(std::lround(size.x * scale));
        height = static_cast<int>(std::lround(size.y * scale));
    }

    bool ViewportHost::IsEffectivelyVisible() const
    {
        for (const Node* n = this; n; n = n->GetParent())
        {
            if (!n->IsVisible())
                return false;
        }
        return GetContext() != nullptr;
    }

    // =========================================================
    // 宿主渲染
    // =========================================================
    bool ViewportHost::RenderContent()
    {
        if (!m_onRender || !IsEffectivelyVisible())
            return false;

        int w = 0, h = 0;
        GetPixelSize(w, h);
        if (w <= 0 || h <= 0)
            return false;

        if (!m_renderRequested && w == m_renderedWidth && h == m_renderedHeight)
            return false;

        m_renderRequested = false;
        m_renderedWidth   = w;
        m_renderedHeight  = h;
        m_onRender(w, h);
        Invalidate();
        return true;
    }

    void ViewportHost::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        if (m_texture == InvalidTextureId)
        {
            dl.AddRectFilled(screenRect, Theme::Background);
            return;
        }
        dl.AddImage(m_texture, screenRect);
    }

    // =========================================================
    // 输入转发
    // =========================================================
    void ViewportHost::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase != EventPhase::Target)
            return;

        UIContext* ctx = GetContext();
        if (e.type == PointerEventType::Down && ctx && !HasCapture())
            ctx->SetCapture(this);      // 拖出视口时继续收到移动和抬起

        if (!m_onPointer)
            return;

        const float scale = ctx ? ctx->GetPixelScale() : 1.0f;

        ViewportPointerEvent ve;
        ve.type       = e.type;
        ve.local      = e.localPosition;
        // 逻辑坐标 × 缩放会带来浮点误差（例如 475.99997），吸附到 1/256 像素，整数像素的鼠标位置换算后仍是整数
        auto snap     = [](float v) { return std::round(v * 256.0f) / 256.0f; };
        ve.pixel      = { snap(e.localPosition.x * scale), snap(e.localPosition.y * scale) };
        ve.wheelDelta = e.wheelDelta;
        ve.button     = e.button;
        ve.buttons    = e.buttons;
        ve.modifiers  = e.modifiers;
        ve.clickCount = e.clickCount;
        m_onPointer(ve);

        // 视口消费所有指针事件：滚轮不交给外层滚动，右键抬起不弹出 MiniGUI 的右键菜单
        e.handled = true;
    }

    void ViewportHost::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || !m_onKey)
            return;
        if (m_onKey(e))
            e.handled = true;
    }

    void ViewportHost::OnTextInput(TextInputEvent& e)
    {
        if (e.phase != EventPhase::Target || !m_onText)
            return;
        if (m_onText(e.text))
            e.handled = true;
    }
}
