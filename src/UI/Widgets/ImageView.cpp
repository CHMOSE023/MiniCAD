#include "Widgets/ImageView.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <cmath>

namespace MiniGUI
{
    ImageView::ImageView(TextureId texture, Vec2 size, ColorRef tint)
        : m_texture(texture)
        , m_size(size)
        , m_tint(tint)
    {
        SetHitTestVisible(false);
    }

    void ImageView::SetTexture(TextureId texture, Vec2 size)
    {
        m_texture = texture;
        m_path.clear();
        m_size    = size;
        InvalidateLayout();
    }

    void ImageView::SetImagePath(std::string utf8Path, Vec2 size)
    {
        m_path    = std::move(utf8Path);
        m_texture = InvalidTextureId;
        m_size    = size;
        InvalidateLayout();
    }

    Vec2 ImageView::MeasureContent(Vec2 available)
    {
        (void)available;
        if (m_size.x > 0.0f && m_size.y > 0.0f)
            return m_size;
        UIContext* ctx = GetContext();
        return ctx ? ctx->GetTextureSize(m_texture) : Vec2{};
    }

    void ImageView::OnPaint(DrawList& dl, const Rect& r)
    {
        // 按路径显示时，每次绘制按当前缩放系数取对应尺寸的纹理（有缓存；缩放变化后自动换成新尺寸）
        if (!m_path.empty())
            if (UIContext* ctx = GetContext())
                m_texture = ctx->LoadTexture(m_path, r.Size());
        if (m_texture == InvalidTextureId)
            return;
        // 位置对齐到物理像素：图标按原始尺寸显示时不被插值模糊
        Rect snapped = r;
        if (UIContext* ctx = GetContext())
        {
            const float s = ctx->GetPixelScale();
            snapped = Rect::FromXYWH(std::round(r.min.x * s) / s, std::round(r.min.y * s) / s, r.Width(), r.Height());
        }
        dl.AddImage(m_texture, snapped, m_uv0, m_uv1, IsEnabled() ? m_tint : ColorScaleAlpha(m_tint, 0.4f));
    }
}
