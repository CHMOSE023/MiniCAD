#pragma once
// ── SHAPE 实体（AutoCAD 的形）──────────────────────────────────────────────
// 形是 SHX 形文件里用形名（或形号）引用的符号。字段对应 DXF 组码：
//     10/20/30  插入点
//     40        尺寸（字高）
//     2         形名（DXF 里是字符串，**不是**样式句柄 —— 注意与 LTYPE 的组码 2 区分）
//     50        旋转角（度）
//     41        X 方向相对缩放
//     51        倾斜角（度）
//     39        厚度
//     210/220/230  拉伸方向
//
// 画出来的形状取决于形文件：形名必须能在某个 SHX 形文件里查到。图纸常常
// 不附带它的 .shx（实测 sample_AC1032.dwg 的 MY-SHAPE 在本地所有字体里
// 都找不到），这时退化成占位标记，而不是让实体凭空消失。
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Math/Constants.hpp"
#include <cmath>
#include <memory>
#include <string>

namespace MiniCAD
{
    class ShapeEntity : public Entity
    {
    public:
        ShapeEntity(ObjectID id, const Math::Point3& insertion, const std::string& name, double size,
                    double rotation = 0.0, double xScale = 1.0, double oblique = 0.0, double thickness = 0.0)
            : Entity(id), m_insertion(insertion), m_name(name), m_size(size)
            , m_rotation(rotation), m_xScale(xScale), m_oblique(oblique), m_thickness(thickness) {}

        const Math::Point3& GetInsertion() const { return m_insertion; }
        void SetInsertion(const Math::Point3& p) { m_insertion = p; }

        const std::string& GetName() const { return m_name; }
        void SetName(const std::string& n) { m_name = n; }

        double GetSize() const { return m_size; }
        void SetSize(double s) { m_size = s; }

        double GetRotation() const { return m_rotation; }          // 弧度（内部统一用弧度）
        void SetRotation(double rad) { m_rotation = rad; }

        double GetRelativeXScale() const { return m_xScale; }
        void SetRelativeXScale(double s) { m_xScale = s; }

        double GetObliqueAngle() const { return m_oblique; }       // 弧度
        void SetObliqueAngle(double rad) { m_oblique = rad; }

        double GetThickness() const { return m_thickness; }
        void SetThickness(double t) { m_thickness = t; }

        // 形文件的路径（由导入时按形名解析；为空表示找不到，画占位标记）
        const std::string& GetShapeFont() const { return m_shapeFont; }
        void SetShapeFont(const std::string& f) { m_shapeFont = f; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<ShapeEntity>(newId, m_insertion, m_name, m_size);
            e->m_rotation  = m_rotation;
            e->m_xScale    = m_xScale;
            e->m_oblique   = m_oblique;
            e->m_thickness = m_thickness;
            e->m_shapeFont = m_shapeFont;
            e->SetAttr(GetAttr());
            return e;
        }

        // 包围盒按尺寸估算：形本身的墨迹范围取决于形文件，没有形文件时也要
        // 给出一个合理的框，否则拾取和 ZoOm All 会漏掉它。
        AABB GetBoundingBox() const override
        {
            const double h = (m_size > 0.0) ? m_size : 1.0;
            const double w = h * ((m_xScale > 0.0) ? m_xScale : 1.0);
            // 旋转后的外接框：用 |cos|+|sin| 近似（保守，宁可略大）
            const double c = std::fabs(std::cos(m_rotation));
            const double s = std::fabs(std::sin(m_rotation));
            const double ex = (w * c + h * s) * 0.5;
            const double ey = (h * c + w * s) * 0.5;
            AABB box;
            box.Min = { m_insertion.x - ex, m_insertion.y - ey, m_insertion.z };
            box.Max = { m_insertion.x + ex, m_insertion.y + ey, m_insertion.z };
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            (void)isSelected;
            (void)isHovered;
            const Math::Color4 color = ResolveDrawColor(sink);

            // 有形文件时由 DrawContext 用 SHX 字形铺出来（实现在
            // DrawContext::DrawShape 里，因为它要拿到字体解析器）。
            // 走到这里说明渲染端没有接管，画一个占位标记。
            EmitPlaceholder(sink, color);
        }

        // 占位标记：一个方框 + 两条对角线（与 IMAGE 找不到文件时的画法一致），
        // 让用户知道"这里有个形，但形文件缺失"。
        void EmitPlaceholder(IDrawSink& sink, const Math::Color4& color) const
        {
            const double h = (m_size > 0.0) ? m_size : 1.0;
            const double w = h * ((m_xScale > 0.0) ? m_xScale : 1.0);
            const double c = std::cos(m_rotation), s = std::sin(m_rotation);
            auto P = [&](double lx, double ly) -> Math::Point3
            {
                return { m_insertion.x + lx * c - ly * s,
                         m_insertion.y + lx * s + ly * c,
                         m_insertion.z };
            };
            const Math::Point3 bl = P(-w * 0.5, -h * 0.5), br = P(w * 0.5, -h * 0.5);
            const Math::Point3 tr = P(w * 0.5,  h * 0.5), tl = P(-w * 0.5, h * 0.5);
            sink.DrawLine(bl, br, color, false);
            sink.DrawLine(br, tr, color, false);
            sink.DrawLine(tr, tl, color, false);
            sink.DrawLine(tl, bl, color, false);
            sink.DrawLine(bl, tr, color, false);
            sink.DrawLine(br, tl, color, false);
        }

        DECLARE_RUNTIME_TYPE(ShapeEntity, Entity)

    private:
        Math::Point3 m_insertion;
        std::string  m_name;
        double       m_size      = 1.0;
        double       m_rotation  = 0.0;    // 弧度
        double       m_xScale    = 1.0;
        double       m_oblique   = 0.0;    // 弧度
        double       m_thickness = 0.0;
        std::string  m_shapeFont;          // 解析到的形文件（空 = 缺失）
    };
}
