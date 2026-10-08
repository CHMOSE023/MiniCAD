#pragma once
#include "IDrawSink.hpp"
#include "Core/Math/Mat4.hpp"
#include <cmath>

namespace MiniCAD
{
    // 变换转发型绘制汇:把所有几何点先经一个 4×4 矩阵变换,再转发给目标 sink。
    // 用于块引用(INSERT)/块定义(BLOCK)展开绘制:子实体在自身坐标系中正常
    // 绘制,由本 sink 统一施加插入变换(平移/旋转/缩放/OCS)。
    class TransformDrawSink : public IDrawSink
    {
    public:
        TransformDrawSink(IDrawSink& target, const Math::Mat4& xform)
            : m_target(target)
            , m_xform(xform)
        {
            // 预算缩放与旋转,供文字近似变换使用(取 X 轴向量)。
            const double* m = m_xform.Data();
            m_scale    = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            m_rotation = std::atan2(m[1], m[0]);
        }

        // 属性查询/实体上下文直接转发,保证块内子实体的 ByLayer 颜色与线型解析正确
        Math::Color4 GetLayerColor(LayerID layerId) const override { return m_target.GetLayerColor(layerId); }
        void BeginEntity(const EntityAttr* attr) override          { m_target.BeginEntity(attr); }

        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) override
        {
            m_target.DrawLine(m_xform * a, m_xform * b, color, isOverlay);
        }

        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) override
        {
            m_target.FillTriangle(m_xform * a, m_xform * b, m_xform * c, color);
        }

        void EmitText(const Math::Point3& pos, const std::string& utf8Text, float height, float rotation, const Math::Color4& color) override
        {
            m_target.EmitText(m_xform * pos, utf8Text, height * static_cast<float>(m_scale), rotation + static_cast<float>(m_rotation), color);
        }

        void EmitMText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation, double boxWidth, const Math::Color4& color) override
        {
            m_target.EmitMText(m_xform * pos, utf8Text, styleId, height * m_scale, rotation + m_rotation, boxWidth * m_scale, color);
        }

    private:
        IDrawSink&  m_target;
        Math::Mat4  m_xform;
        double      m_scale    = 1.0;   // 由矩阵 X 轴长度估算的统一缩放
        double      m_rotation = 0.0;   // 由矩阵 X 轴方向估算的旋转(弧度)
    };
}
