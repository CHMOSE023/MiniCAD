#pragma once
#include "RectangleEntity.hpp"

namespace MiniCAD
{
    // 二维填充（SOLID）：三角形或四边形的实心填充。
    //
    // 几何与 RectangleEntity 共用四个顶点 P1..P4，按轮廓顺序（顺时针或逆时针）排列，
    // 因此移动 / 旋转 / 镜像 / 夹点 / 捕捉 / 序列化都沿用矩形的实现。
    // 与 AutoCAD 的区别：AutoCAD 按「1-2-4-3」的点序保存，这里由绘制工具负责换序。
    // 三角形用 P4 == P3 表示。
    class SolidEntity : public RectangleEntity
    {
    public:
        SolidEntity(ObjectID id, const Math::Point3& p1, const Math::Point3& p2,
                    const Math::Point3& p3, const Math::Point3& p4)
            : RectangleEntity(id, p1, p2, p3, p4)
        {
        }

        // 三角形：第四点与第三点重合
        SolidEntity(ObjectID id, const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3)
            : RectangleEntity(id, p1, p2, p3, p3)
        {
        }

        bool IsTriangle() const
        {
            const auto& r = GetRectangle();
            return r.P3.x == r.P4.x && r.P3.y == r.P4.y && r.P3.z == r.P4.z;
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            const auto& r = GetRectangle();
            auto e = std::make_unique<SolidEntity>(newId, r.P1, r.P2, r.P3, r.P4);
            e->SetAttr(GetAttr());
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            const auto& r = GetRectangle();
            sink.FillTriangle(r.P1, r.P2, r.P3, color);
            if (!IsTriangle())
                sink.FillTriangle(r.P1, r.P3, r.P4, color);
        }

        DECLARE_RUNTIME_TYPE(SolidEntity, RectangleEntity)
    };
}
