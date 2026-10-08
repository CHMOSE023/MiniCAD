#pragma once
#include "PolylineEntity.hpp"

namespace MiniCAD
{
    // 区域覆盖（WIPEOUT）：一个闭合多边形，遮挡绘制序在它之前的图形。
    //
    // 顶点沿用多段线的存储（首尾点相同表示闭合，不用圆弧段），因此移动 / 旋转 / 镜像 /
    // 夹点 / 捕捉 / 点选边框都沿用多段线的实现。遮挡本身不产生顶点，而是通过
    // IDrawSink::Wipe 通知场景顶点流去裁剪先前的内容。
    class WipeoutEntity : public PolylineEntity
    {
    public:
        // pts 为多边形顶点；首尾不重合时自动补上闭合点
        WipeoutEntity(ObjectID id, std::vector<Math::Point3> pts)
            : PolylineEntity(id, Close(std::move(pts)))
        {
        }

        // 是否显示边框（AutoCAD 的 WIPEOUTFRAME）
        void SetShowFrame(bool on) { m_showFrame = on; }
        bool GetShowFrame() const  { return m_showFrame; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<WipeoutEntity>(newId, GetPolyline().Points);
            e->SetAttr(GetAttr());
            e->m_showFrame = m_showFrame;
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& pts = GetPolyline().Points;
            if (pts.size() < 4) return;

            sink.Wipe(pts);

            if (!m_showFrame && !isSelected && !isHovered) return;
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                sink.DrawLine(pts[i], pts[i + 1], color, false);
        }

        DECLARE_RUNTIME_TYPE(WipeoutEntity, PolylineEntity)

    private:
        static std::vector<Math::Point3> Close(std::vector<Math::Point3> pts)
        {
            if (pts.size() >= 2)
            {
                const auto& a = pts.front();
                const auto& b = pts.back();
                if (a.x != b.x || a.y != b.y || a.z != b.z)
                    pts.push_back(a);
            }
            return pts;
        }

        bool m_showFrame = true;
    };
}
