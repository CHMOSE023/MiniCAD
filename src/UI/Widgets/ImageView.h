#pragma once
#include "Core/Node.h"
#include "Render/DrawData.hpp"
#include "Style/ColorRef.hpp"
#include <string>

namespace MiniGUI
{
    // 图片 / 图标：显示一张纹理。size 为 0 时使用纹理原始尺寸（需由 UIContext 创建）；
    // 禁用时自动变淡。点击穿透到父节点（通常放在按钮里）
    class ImageView : public Node
    {
    public:
        explicit ImageView(TextureId texture = InvalidTextureId, Vec2 size = {}, ColorRef tint = Colors::White);

        void SetTexture(TextureId texture, Vec2 size = {});
        // 按路径显示：绘制时按 显示尺寸 × 缩放系数 取对应像素大小的纹理（图标在任何缩放下都清晰）
        void SetImagePath(std::string utf8Path, Vec2 size);
        void SetTint(ColorRef tint) { m_tint = tint; Invalidate(); }
        void SetUV(Vec2 uv0, Vec2 uv1) { m_uv0 = uv0; m_uv1 = uv1; Invalidate(); }
        TextureId GetTexture() const { return m_texture; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        TextureId   m_texture;
        std::string m_path;
        Vec2        m_size;
        ColorRef  m_tint;
        Vec2      m_uv0{ 0, 0 };
        Vec2      m_uv1{ 1, 1 };
    };
}
