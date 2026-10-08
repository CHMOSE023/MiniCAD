#pragma once
#include "RectangleEntity.hpp"
#include <string>

namespace MiniCAD
{
    // 光栅图像（IMAGE）：一张贴在平行四边形（通常是矩形）上的位图。
    //
    // 几何与 RectangleEntity 共用四个顶点，顺序为左下 P1、右下 P2、右上 P3、左上 P4，
    // 因此移动 / 旋转 / 镜像 / 捕捉 / 点选边框 / 框选都沿用矩形的实现；夹点另有
    // ImageGripHandler（按比例缩放，不允许把图像拉歪）。
    // 图像本身只存路径，像素由 ImageLibrary 读取；找不到文件时只画占位框。
    class ImageEntity : public RectangleEntity
    {
    public:
        ImageEntity(ObjectID id, std::string path,
                    const Math::Point3& p1, const Math::Point3& p2,
                    const Math::Point3& p3, const Math::Point3& p4)
            : RectangleEntity(id, p1, p2, p3, p4), m_path(std::move(path))
        {
        }

        const std::string& GetPath() const { return m_path; }
        void SetPath(std::string p)        { m_path = std::move(p); }

        void SetShowFrame(bool on) { m_showFrame = on; }
        bool GetShowFrame() const  { return m_showFrame; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            const auto& r = GetRectangle();
            auto e = std::make_unique<ImageEntity>(newId, m_path, r.P1, r.P2, r.P3, r.P4);
            e->SetAttr(GetAttr());
            e->m_showFrame = m_showFrame;
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& r = GetRectangle();

            // 选中 / 悬停：图像已画在场景流里，这里只叠加高亮边框
            const bool highlight = isSelected || isHovered;
            bool shown = highlight || sink.EmitImage(m_path, { r.P1, r.P2, r.P3, r.P4 });

            if (!m_showFrame && shown && !highlight)
                return;

            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            sink.DrawLine(r.P1, r.P2, color, false);
            sink.DrawLine(r.P2, r.P3, color, false);
            sink.DrawLine(r.P3, r.P4, color, false);
            sink.DrawLine(r.P4, r.P1, color, false);

            if (!shown)         // 图像不可用：画对角线作占位
            {
                sink.DrawLine(r.P1, r.P3, color, false);
                sink.DrawLine(r.P2, r.P4, color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(ImageEntity, RectangleEntity)

    private:
        std::string m_path;
        bool        m_showFrame = true;
    };
}
