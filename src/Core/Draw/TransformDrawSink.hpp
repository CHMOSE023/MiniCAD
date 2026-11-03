#pragma once
#include "IDrawSink.hpp"
#include "Core/Math/Mat4.hpp"
#include <algorithm>
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
            // 预算缩放与镜像,供文字近似变换使用(取 X 轴向量;行向量约定,第 0、1 行是 X、Y 轴的像)。
            const double* m = m_xform.Data();
            m_scale    = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            m_maxScale = std::max({ m_scale, std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
                                    std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]) });
            m_mirrored = m[0] * m[5] - m[1] * m[4] < 0.0;
        }

        // 属性查询/实体上下文直接转发,保证块内子实体的 ByLayer 颜色与线型解析正确
        Math::Color4 GetLayerColor(LayerID layerId) const override { return m_target.GetLayerColor(layerId); }
        bool IsLayerVisible(LayerID layerId) const override        { return m_target.IsLayerVisible(layerId); }
        void BeginEntity(const EntityAttr* attr) override          { m_target.BeginEntity(attr); }
        // 块内成员自己的线型上下文（见 IDrawSink::SetEntityContext）
        void SetEntityContext(const EntityAttr* attr) override     { m_target.SetEntityContext(attr); }
        // 嵌套块参照的子实体也一起分段（见 IDrawSink::BeginBlockChild）
        bool BeginBlockChild(bool nested) override                 { return m_target.BeginBlockChild(nested); }
        void EndBlockChild(bool nested) override                   { m_target.EndBlockChild(nested); }
        // 嵌套块参照按块定义缓存（见 IDrawSink::BeginBlockInstance）：变换接上本 sink 的（行向量约定，先作用的在左）
        bool BeginBlockInstance(const void* block, const EntityAttr& host, const Math::Mat4& xform, const BlockClipChain* clips) override
        {
            return m_target.BeginBlockInstance(block, host, xform * m_xform, clips);
        }
        void EndBlockInstance() override        { m_target.EndBlockInstance(); }
        void NoteClipped(int depth) override    { m_target.NoteClipped(depth); }

        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) override
        {
            m_target.DrawLine(m_xform * a, m_xform * b, color, isOverlay);
        }

        // 块内粗多段线：线段两端经插入变换，宽度按插入缩放放大
        void DrawStyledLine(const Math::Point3& a, const Math::Point3& b,
                            const Math::Color4& color, double width) override
        {
            m_target.DrawStyledLine(m_xform * a, m_xform * b, color, width * m_scale);
        }

        bool IsCurrentLinetypeDashed() const override { return m_target.IsCurrentLinetypeDashed(); }

        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) override
        {
            m_target.FillTriangle(m_xform * a, m_xform * b, m_xform * c, color);
        }

        // 块内的渐变填充：位置经插入变换，**颜色不随变换改变**
        //（不转发的话块里的渐变会被压成单色实心）
        void FillTriangleColored(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c,
                                 const Math::Color4& ca, const Math::Color4& cb, const Math::Color4& cc) override
        {
            m_target.FillTriangleColored(m_xform * a, m_xform * b, m_xform * c, ca, cb, cc);
        }

        // 块内的区域覆盖：边界同样经插入变换
        void Wipe(const std::vector<Math::Point3>& polygon) override
        {
            std::vector<Math::Point3> pts;
            pts.reserve(polygon.size());
            for (const auto& p : polygon)
                pts.push_back(m_xform * p);
            m_target.Wipe(pts);
        }

        void EmitText(const Math::Point3& pos, const std::string& utf8Text, float height, float rotation, const Math::Color4& color) override
        {
            m_target.EmitText(m_xform * pos, utf8Text, height * static_cast<float>(m_scale), static_cast<float>(TextRotation(rotation)), color);
        }

        // 镜像的插入：基线方向照样经变换，字形沿基线上下翻转（见 IDrawSink::ToggleTextMirror）
        void EmitMText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation, double boxWidth, const Math::Color4& color) override
        {
            if (m_mirrored) m_target.ToggleTextMirror();
            m_target.EmitMText(m_xform * pos, utf8Text, styleId, height * m_scale, TextRotation(rotation), boxWidth * m_scale, color);
            if (m_mirrored) m_target.ToggleTextMirror();
        }

        void EmitAlignedText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                             int hAlign, int vAlign, double widthFactor, const Math::Color4& color) override
        {
            if (m_mirrored) m_target.ToggleTextMirror();
            m_target.EmitAlignedText(m_xform * pos, utf8Text, styleId, height * m_scale, TextRotation(rotation), hAlign, vAlign, widthFactor, color);
            if (m_mirrored) m_target.ToggleTextMirror();
        }

        // 块内文字：按 EmitMText 同样放大后的字高去量（世界单位），再换回块的局部单位——
        // 调用方（如多重引线的基线）在局部坐标里用这个宽度，画出的线再经本变换
        double MeasureMTextWidth(const std::string& utf8Text, uint32_t styleId,
                                 double height, double boxWidth) override
        {
            if (m_scale <= 0.0) return 0.0;
            return m_target.MeasureMTextWidth(utf8Text, styleId, height * m_scale, boxWidth * m_scale)
                   / m_scale;
        }

        bool MeasureMTextInkBounds(const std::string& utf8Text, uint32_t styleId,
                                   double height, double boxWidth,
                                   double& outMinX, double& outMaxX) override
        {
            if (m_scale <= 0.0 ||
                !m_target.MeasureMTextInkBounds(utf8Text, styleId, height * m_scale,
                                                boxWidth * m_scale, outMinX, outMaxX))
                return false;
            outMinX /= m_scale;
            outMaxX /= m_scale;
            return true;
        }

        // ── 点 ────────────────────────────────────────────────────────
        //
        // ⚠ 这几个**必须转发**，否则块内 / 视口内的点会静默消失：
        //   基类默认实现是空函数，不转发就等于什么都没画。
        //   实测「B地块地下室」图纸：模型空间有 3 万个 POINT，布局页里一个都看不到。
        void DrawPoint(const Math::Point3& p, const Math::Color4& color, bool isOverlay) override
        {
            m_target.DrawPoint(m_xform * p, color, isOverlay);
        }

        // 点样式（$PDMODE/$PDSIZE）是全局设置，直接透传查询
        bool GetPointStyle(int& outPdMode, double& outPdSize) const override
        {
            return m_target.GetPointStyle(outPdMode, outPdSize);
        }

        // ── 图像 ──────────────────────────────────────────────────────
        bool EmitImage(const std::string& path, const std::array<Math::Point3, 4>& corners) override
        {
            std::array<Math::Point3, 4> c{ m_xform * corners[0], m_xform * corners[1],
                                           m_xform * corners[2], m_xform * corners[3] };
            return m_target.EmitImage(path, c);
        }

        // ── 视图相关量 ────────────────────────────────────────────────
        //
        // 线宽 / 虚线是按屏幕像素烘焙进世界顶点的，必须有真实像素比例。
        // 不转发的话拿到 0，块内的粗线会退化成 1px、虚线判定也会出错。
        double GetWorldPerPixel() const override   { return m_target.GetWorldPerPixel(); }
        double GetViewportHeightPx() const override { return m_target.GetViewportHeightPx(); }
        void   MarkViewDependent() override        { m_target.MarkViewDependent(); }
        // 曲线加密：弦的两端经插入变换，弦高按最大的轴向缩放放大（不等比缩放时偏保守）
        bool   RefineCurveSegment(const Math::Point3& a, const Math::Point3& b, double sagitta, double reach) const override
        {
            return m_target.RefineCurveSegment(m_xform * a, m_xform * b, sagitta * m_maxScale, reach * m_maxScale);
        }
        void   SetTextWidthFactor(double f) override { m_target.SetTextWidthFactor(f); }
        void   ToggleTextMirror() override           { m_target.ToggleTextMirror(); }

    private:
        // 文字基线方向 (cos r, sin r) 经变换后的角度。镜像时不是"文字角度 + 块的转角"：
        // 沿 X 轴镜像的块里 30° 的文字画出来是 -30°
        double TextRotation(double rotation) const
        {
            const double* m = m_xform.Data();
            const double c = std::cos(rotation), s = std::sin(rotation);
            return std::atan2(c * m[1] + s * m[5], c * m[0] + s * m[4]);
        }

        IDrawSink&  m_target;
        Math::Mat4  m_xform;
        double      m_scale    = 1.0;     // 由矩阵 X 轴长度估算的统一缩放
        double      m_maxScale = 1.0;     // 三个轴里最大的缩放（曲线加密的弦高用）
        bool        m_mirrored = false;   // XY 平面内行列式为负（负比例、法向朝 -Z 的插入）
    };
}
