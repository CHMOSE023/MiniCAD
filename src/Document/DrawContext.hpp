#pragma once
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Render/ImageDraw.hpp"
#include "Document/WipeClip.hpp"
#include "Document/GlyphMeshLibrary.hpp"
#include "Editor/Overlay/Overlay.h"
#include "Text/Font/IFont.h"
#include "Text/Layout/TextLayoutEngine.h"
#include "Document/GlyphTypes.h"
#include "Scene/LayerManager.h"
#include "Scene/LineTypeTable.h"
#include "Core/Entity/EntityAttr.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    // 文字样式解析结果：字体 + 宽度因子 + 倾斜角（弧度，向右倾为正）
    struct ResolvedTextStyle
    {
        IFont* Font        = nullptr;
        double WidthFactor = 1.0;
        double Oblique     = 0.0;
    };

    // 矢量字体解析回调：styleId（文字样式表 ID）→ 字体与字形变换（由 Editor 注入，Font 为空则不绘制）
    using FontResolver = std::function<ResolvedTextStyle(uint32_t styleId)>;

    // 图像解析回调：路径 → 解码后的位图（找不到返回空）
    using ImageProvider = std::function<std::shared_ptr<const ImageData>(const std::string& path)>;

    class DrawContext : public IDrawSink
    {
    public:
        DrawContext(std::vector<Vertex_P3_C4>&     sceneVertices,
                    std::vector<Vertex_P3_C4>&     sceneFillVertices,
                    std::vector<Vertex_P3_C4_UV>&  textVertices,
                    Overlay&                       overlay,
                    const LayerManager&            layerManager,
                    const LineTypeTable&           lineTypeTable,
                    double                         worldPerPixel = 0.0,
                    GlyphProvider                  glyphProvider = nullptr,
                    FontResolver                   fontResolver  = nullptr)
            : m_verts(sceneVertices)
            , m_fillVerts(sceneFillVertices)
            , m_textVerts(textVertices)
            , m_overlay(overlay)
            , m_layerManager(layerManager)
            , m_lineTypes(lineTypeTable)
            , m_worldPerPixel(worldPerPixel)
            , m_glyphProvider(std::move(glyphProvider))
            , m_fontResolver(std::move(fontResolver))
        {
        }

        Math::Color4 GetLayerColor(LayerID layerId) const override
        {
            const Layer* layer = m_layerManager.GetLayer(layerId);
            return layer ? layer->GetColor() : Math::Color4::White();
        }

        bool IsLayerVisible(LayerID layerId) const override
        {
            const Layer* layer = m_layerManager.GetLayer(layerId);
            return !layer || layer->IsVisible();
        }

        std::string GetLayerName(LayerID layerId) const override
        {
            const Layer* layer = m_layerManager.GetLayer(layerId);
            return layer ? layer->GetName() : std::string();
        }

        // 点样式（HEADER 的 $PDMODE/$PDSIZE）。默认 0/0 = AutoCAD 默认"实心小点"。
        bool GetPointStyle(int& mode, double& size) const override
        {
            mode = m_pointMode;
            size = m_pointSize;
            return true;
        }
        void SetPointStyle(int mode, double size)
        {
            m_pointMode = mode;
            m_pointSize = size;
        }

        double GetWorldPerPixel() const override    { return m_worldPerPixel; }
        double GetViewportHeightPx() const override { return m_viewportHeightPx; }
        void MarkViewDependent() override { if (m_worldPerPixel > 0.0) m_viewDependent = true; }
        // 视口像素高度：$PDSIZE<=0（视口高度百分比）换算世界尺寸时必需。
        // Editor 逐实体细分前设置；为 0 表示未知（点退化成 1 像素小点）。
        void SetViewportHeightPx(double px) { m_viewportHeightPx = px; }

        // 全局线型比例（Scene 的 $LTSCALE）。与实体级组码 48 相乘得到虚线实际周期。
        void SetLineTypeScale(double s) { m_lineTypeScale = (s > 0.0 ? s : 1.0); }

        // 点图元：一个顶点一个点，渲染端按 POINTLIST / GL_POINTS 画成
        // 恒定 1 屏幕像素。用途：图案填充的 dash=0 点笔划、POINT 的 $PDMODE=0。
        //
        // ⚠ 走**独立的点流** m_points，不与线段流混：
        //   · 图元类型不同（POINTLIST vs LINELIST），混在一起没法提交；
        //   · 点流也参与区域覆盖裁剪（WipeClip 的 ClipPoints），与线段同规则。
        // 不能进 overlay：那是每帧清空的动态层，承载不了场景几何。
        void DrawPoint(const Math::Point3& p, const Math::Color4& color, bool isOverlay) override
        {
            if (isOverlay)
            {
                m_overlay.AddPoint(p, color);
                return;
            }
            m_points.push_back(ToVertex(p, color));
        }

        // 本次细分产出的点图元（调用方取走后清空）
        std::vector<Vertex_P3_C4> TakePoints() { return std::move(m_points); }
        void ClearPoints() { m_points.clear(); }

        // 各缓冲当前长度。用于「只裁剪本次追加的一段」——
        // 典型场景是布局视口合成：先把长度记下来，合成完再裁 [from, end) 区间。
        size_t LineCount()  const { return m_verts.size(); }
        size_t FillCount()  const { return m_fillVerts.size(); }
        size_t TextCount()  const { return m_textVerts.size(); }
        size_t PointCount() const { return m_points.size(); }
        // 字形实例流同理；未设置实例接收器（选中流等）时为 0
        size_t GlyphCount() const { return m_glyphs ? m_glyphs->size() : 0; }
        // 光栅图像流同理（布局页也要按视口矩形裁，否则贴图溢出到纸面上）
        size_t ImageCount() const { return m_images.size(); }

        // 可写缓冲（供布局视口做「只裁本次追加区间」的裁剪）
        std::vector<Vertex_P3_C4>&    Lines()  { return m_verts; }
        std::vector<Vertex_P3_C4>&    Fills()  { return m_fillVerts; }
        std::vector<Vertex_P3_C4_UV>& Texts()  { return m_textVerts; }
        std::vector<Vertex_P3_C4>&    Points() { return m_points; }
        // 字形实例流（未设置实例接收器时为空指针）
        std::vector<GlyphInstance>*   Glyphs() { return m_glyphs; }
        std::vector<ImageDraw>&       Images() { return m_images; }

        // 实体级上下文:解析该实体生效的线型/线宽(ByLayer → 图层值)
        void BeginEntity(const EntityAttr* attr) override
        {
            FlushWipes();
            m_curAttr = attr;
            if (attr)
            {
                m_wipes.clear();
                m_images.clear();
                m_entityLines = m_verts.size();
                m_entityFills = m_fillVerts.size();
                m_entityTexts = m_textVerts.size();
                m_entityGlyphs = m_glyphs ? m_glyphs->size() : 0;
                m_entityPoints = m_points.size();
            }
        }

        // 块内子实体：只换线型/线宽解析所用的属性，不做实体边界处理。
        // 见 IDrawSink::SetEntityContext 的说明（块内成员自己的线型此前被 INSERT 覆盖）。
        void SetEntityContext(const EntityAttr* attr) override { m_curAttr = attr; }

        // 矢量文字按字形实例输出（见 GlyphMeshLibrary）：每个字一条实例，字形网格进网格库。
        // 不设置时（选中流等）字形照旧展开成世界坐标的线段与三角形
        void SetGlyphInstanceSink(std::vector<GlyphInstance>* instances, GlyphMeshLibrary* library)
        {
            m_glyphs   = library ? instances : nullptr;
            m_glyphLib = instances ? library : nullptr;
        }

        // 光栅图像：没有图像解析回调（选中流、悬停等）时返回 false，实体改画占位框 / 高亮框
        void SetImageProvider(ImageProvider p) { m_imageProvider = std::move(p); }
        bool EmitImage(const std::string& path, const std::array<Math::Point3, 4>& c) override
        {
            if (!m_imageProvider) return false;
            auto img = m_imageProvider(path);
            if (!img) return false;

            auto V = [](const Math::Point3& p, float u, float v) -> Vertex_P3_C4_UV
            {
                return { { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) },
                         { 1.f, 1.f, 1.f, 1.f }, { u, v } };
            };
            ImageDraw d;
            d.Image    = std::move(img);
            d.Verts[0] = V(c[0], 0, 1);  d.Verts[1] = V(c[1], 1, 1);  d.Verts[2] = V(c[2], 1, 0);
            d.Verts[3] = V(c[0], 0, 1);  d.Verts[4] = V(c[2], 1, 0);  d.Verts[5] = V(c[3], 0, 0);
            m_images.push_back(std::move(d));
            return true;
        }
        std::vector<ImageDraw> TakeImages() { return std::move(m_images); }

        // 区域覆盖：同一实体（块参照）里先画的部分在实体结束时（BeginEntity）一次裁剪；之前实体的内容记录下来，
        // 由 Editor 在拼接顶点流时按绘制序裁剪
        void Wipe(const std::vector<Math::Point3>& polygon) override
        {
            m_pendingWipes.Add(polygon, { m_verts.size(), m_fillVerts.size(), m_textVerts.size(), m_glyphs ? m_glyphs->size() : 0, m_points.size() });
            m_wipes.push_back(polygon);
        }
        std::vector<std::vector<Math::Point3>> TakeWipes()
        {
            FlushWipes();
            return std::move(m_wipes);
        }
        ~DrawContext() override { FlushWipes(); }

        // 本次细分是否产出了依赖像素比例的几何(线宽四边形、按像素取舍的虚线),
        // 供 Editor 决定缩放时哪些实体需要重新细分
        bool HasViewDependentGeometry() const { return m_viewDependent; }
        // 复位该标志：逐实体细分时在每个实体前调用，按实体统计
        void ResetViewDependentFlag()         { m_viewDependent = false; }

        // 全局细线：忽略线宽，全部按 1px 细线显示（同 AutoCAD 关闭"显示线宽"）
        void SetThinLines(bool thin) { m_thinLines = thin; }

        // 当前实体的线型记录（ByLayer → 图层线型、ByBlock → Continuous）。
        // DrawLine 与 DrawStyledLine 共用，保证「铺线型」与「单独判定虚线」取到同一个线型。
        const LineTypeRecord* CurrentLineType() const
        {
            if (!m_curAttr) return m_lineTypes.Find(LineTypeTable::ContinuousID);
            const Layer* layer = m_layerManager.GetLayer(m_curAttr->LayerId);
            LineTypeID lt = m_curAttr->LineType;
            if (lt == LineTypeTable::ByLayerID)
                lt = layer ? layer->GetLineType() : LineTypeTable::ContinuousID;
            if (lt == LineTypeTable::ByBlockID)
                lt = LineTypeTable::ContinuousID;
            return m_lineTypes.Find(lt);
        }

        // --- 几何线段 ---
        void DrawLine(const Math::Point3& a, const Math::Point3& b,  const Math::Color4& color, bool isOverlay) override
        {
            if (isOverlay)
            {
                m_overlay.AddLine(a, b, color);
                return;
            }

            if (!m_curAttr)   // 无实体上下文(字形线段等):按 1px 实线输出
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
                return;
            }

            const Layer* layer = m_layerManager.GetLayer(m_curAttr->LayerId);
            EmitStyled(a, b, color, CurrentLineType(), ResolveWidthWorld(layer));
        }

        // 有宽度的线段按线型铺设（粗多段线的虚线走这里）。宽度是实体自己的几何宽度
        // （多段线逐段宽度），不是图层线宽，所以直接用传入值。
        void DrawStyledLine(const Math::Point3& a, const Math::Point3& b,
                            const Math::Color4& color, double width) override
        {
            if (!m_curAttr)   // 无实体上下文：按 1px 细线输出
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
                return;
            }
            EmitStyled(a, b, color, CurrentLineType(), m_thinLines ? 0.0 : std::max(0.0, width));
        }

        // 当前实体是否虚线：粗多段线据此在「整体填充（保留斜接尖角）」与「逐段铺线型」之间选
        bool IsCurrentLinetypeDashed() const override
        {
            const LineTypeRecord* rec = CurrentLineType();
            return rec != nullptr && !rec->IsContinuous();
        }

        // --- 填充三角形（线宽 > 1 的实体）---

        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) override
        {
            m_fillVerts.push_back(ToVertex(a, color));
            m_fillVerts.push_back(ToVertex(b, color));
            m_fillVerts.push_back(ToVertex(c, color));
        }

        // --- 纹理文字（WebFontAtlas 路径）---

        void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                      float height, float rotation,
                      const Math::Color4& color) override
        {
            if (utf8Text.empty() || !m_glyphProvider) return;

            const float cosR = cosf(rotation);
            const float sinR = sinf(rotation);

            Math::Float4 c4 = {
                static_cast<float>(color.r), static_cast<float>(color.g),
                static_cast<float>(color.b), static_cast<float>(color.a)
            };

            float curX = 0.f;
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();

            while (p < end)
            {
                unsigned int cp;
                p = DecodeUtf8(p, end, cp);

                GlyphInfo g;
                float fallback = 0.f;
                if (!m_glyphProvider(cp, g, fallback)) { curX += fallback; continue; }

                float lx0 = (curX + g.X0) * height, ly0 = g.Y0 * height;
                float lx1 = (curX + g.X1) * height, ly1 = g.Y1 * height;

                auto xform = [&](float lx, float ly) -> Math::Float3 {
                    float wx =  lx, wy = -ly;
                    return {
                        static_cast<float>(pos.x) + wx * cosR - wy * sinR,
                        static_cast<float>(pos.y) + wx * sinR + wy * cosR,
                        static_cast<float>(pos.z)
                    };
                };

                Math::Float3 tl = xform(lx0, ly0), tr = xform(lx1, ly0);
                Math::Float3 br = xform(lx1, ly1), bl = xform(lx0, ly1);

                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ tr, c4, { g.U1, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ bl, c4, { g.U0, g.V1 } });

                curX += g.AdvanceX;
            }
        }

        // --- 矢量多行文字（SHX / TTF 路径）---
        // 字形线段直接输出到 m_verts（场景几何），不走纹理。

        void EmitMText(const Math::Point3& pos,
                       const std::string&  utf8Text,
                       uint32_t            styleId,
                       double              height,
                       double              rotation,
                       double              boxWidth,
                       const Math::Color4& color) override
        {
            EmitLaidOutText(pos, utf8Text, styleId, height, rotation, boxWidth, TextLayoutEngine::HAlign::Left, 0.0, 0.0, color);
        }

        // 单行文字按对齐点：水平按排版宽度居中 / 右对齐；竖直以字高（大写字母高）为准，
        // “底”取基线下 0.2 × 字高（下伸部）
        void EmitAlignedText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                             int hAlign, int vAlign, double widthFactor, const Math::Color4& color) override
        {
            const auto align = hAlign == 1 ? TextLayoutEngine::HAlign::Center : hAlign == 2 ? TextLayoutEngine::HAlign::Right
                                                                                             : TextLayoutEngine::HAlign::Left;
            const double baseline = vAlign == 1 ? 0.2 * height : vAlign == 2 ? -0.5 * height : vAlign == 3 ? -height : 0.0;
            EmitLaidOutText(pos, utf8Text, styleId, height, rotation, 0.0, align, baseline, widthFactor, color);
        }

    private:
        // 排版并输出字形；baseline 为基线相对 pos 的局部 y 偏移（随文字旋转），widthFactor > 0 时代替样式的宽度因子
        void EmitLaidOutText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                             double boxWidth, TextLayoutEngine::HAlign align, double baseline, double widthFactor, const Math::Color4& color)
        {
            if (utf8Text.empty() || !m_fontResolver) return;

            ResolvedTextStyle rs = m_fontResolver(styleId);
            if (!rs.Font) return;
            if (widthFactor > 0.0)
                rs.WidthFactor = widthFactor;

            TextLayoutEngine layout;
            auto result = layout.Layout(utf8Text, rs.Font, height, rs.WidthFactor, rotation, boxWidth, align);
            const double shear = std::tan(rs.Oblique);     // 倾斜：字形 x 随高度错切

            const double cosR = std::cos(rotation);
            const double sinR = std::sin(rotation);

            Math::Float4 c4 = 
            {
                static_cast<float>(color.r), static_cast<float>(color.g),
                static_cast<float>(color.b), static_cast<float>(color.a)
            };

            for (const auto& inst : result.m_glyphs)
            {
                auto xform = [&](const Math::Point3& lp) -> Math::Float3
                {
                    double x = (lp.x * rs.WidthFactor + lp.y * shear) * inst.m_scale + inst.m_position.x;
                    double y = lp.y * inst.m_scale + inst.m_position.y + baseline;
                    return {
                        static_cast<float>(pos.x + x * cosR - y * sinR),
                        static_cast<float>(pos.y + x * sinR + y * cosR),
                        static_cast<float>(pos.z)
                    };
                };

                const Glyph& glyph = *inst.m_glyph;
                if (m_glyphs)
                {
                    // 实例：局部 → 世界的仿射变换（与上面 xform 相同，只是不逐点计算）
                    uint32_t mesh;
                    if (!m_glyphLib->Register(glyph, mesh)) continue;
                    const double s  = inst.m_scale;
                    const double ox = inst.m_position.x, oy = inst.m_position.y + baseline;
                    GlyphInstance g;
                    g.m00   = static_cast<float>(rs.WidthFactor * s * cosR);
                    g.m01   = static_cast<float>(rs.WidthFactor * s * sinR);
                    g.m10   = static_cast<float>(shear * s * cosR - s * sinR);
                    g.m11   = static_cast<float>(shear * s * sinR + s * cosR);
                    g.tx    = static_cast<float>(pos.x + ox * cosR - oy * sinR);
                    g.ty    = static_cast<float>(pos.y + ox * sinR + oy * cosR);
                    g.z     = static_cast<float>(pos.z);
                    g.color = c4;
                    g.mesh  = mesh;
                    m_glyphs->push_back(g);
                    continue;
                }
                if (glyph.Filled && !glyph.Triangles.empty())
                {
                    // 填充字形：输出三角形到场景填充几何
                    for (const auto& tri : glyph.Triangles)
                    {
                        m_fillVerts.push_back({ xform(tri.a), c4 });
                        m_fillVerts.push_back({ xform(tri.b), c4 });
                        m_fillVerts.push_back({ xform(tri.c), c4 });
                    }
                }
                // 轮廓线段：SHX 等无填充数据时就是字形本身；填充字形（TTF）也叠加轮廓——
                // 缩小后笔画不足 1 像素，三角形盖不住像素中心会断续发花，硬件线至少画 1 像素，笔画保持连续
                for (const auto& seg : glyph.Lines)
                {
                    m_verts.push_back({ xform(seg.Start), c4 });
                    m_verts.push_back({ xform(seg.End),   c4 });
                }
            }
        }

    private:
        void FlushWipes()
        {
            if (m_pendingWipes.Empty()) return;
            m_pendingWipes.ClipLines(m_verts, 0, m_entityLines);
            m_pendingWipes.ClipFills(m_fillVerts, 1, 5, m_entityFills);
            m_pendingWipes.ClipTexts(m_textVerts, 2, m_entityTexts);
            m_pendingWipes.ClipPoints(m_points, 4, m_entityPoints);
            if (m_glyphs)
                m_pendingWipes.ClipGlyphs(*m_glyphs, 3, m_entityGlyphs, *m_glyphLib, m_verts, m_fillVerts);
            m_pendingWipes.Clear();
        }

    private:
        // 1mm 线宽对应的屏幕像素(约 96 DPI 显示),与 AutoCAD 屏显观感一致
        static constexpr double kPixelsPerMm = 96.0 / 25.4;

        // 解析生效线宽 → 世界单位宽度(0 = 1px 硬件细线)
        double ResolveWidthWorld(const Layer* layer) const
        {
            if (m_thinLines)
                return 0.0;

            Lineweight lw = m_curAttr->Lineweight;
            if (lw == Lineweight::ByLayer)
                lw = layer ? layer->GetLineweight() : Lineweight::Default;
            if (lw == Lineweight::ByBlock || lw == Lineweight::Default)
                return 0.0;

            double mm = LineweightToMillimeters(lw);
            if (mm <= 0.0 || m_worldPerPixel <= 0.0)
                return 0.0;

            double px = mm * kPixelsPerMm;
            if (px <= 1.5)              // 过细:退化为硬件 1px 线
                return 0.0;
            return px * m_worldPerPixel;
        }

        // 按线型 dash/gap 沿线段铺设;width > 0 时输出加宽四边形
        void EmitStyled(const Math::Point3& a, const Math::Point3& b,
                        const Math::Color4& color, const LineTypeRecord* rec, double width)
        {
            bool continuous = (!rec || rec->IsContinuous());

            // 实体级比例(组码48) × 全局 $LTSCALE。
            // 少了全局那一乘，大尺寸图纸的虚线周期会小上千倍 —— 再叠加下面
            // 「一个周期不足 4px 退化为实线」的护栏，虚线会整条消失。
            const double entityScale = (m_curAttr && m_curAttr->LinetypeScale > 0.0)
                                          ? m_curAttr->LinetypeScale : 1.0;
            const double scale = entityScale * m_lineTypeScale;

            double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-12) return;

            if (!continuous)
            {
                // 虚线的取舍与"点"的长度都按像素算,缩放后要重新细分
                if (m_worldPerPixel > 0.0)
                    m_viewDependent = true;

                double cycle = rec->PatternLength * scale;
                if (cycle <= 1e-12)
                    continuous = true;
                // 缩放过小(一个周期不足 4px)或 dash 数量过大:退化为实线显示
                else if (m_worldPerPixel > 0.0 && cycle / m_worldPerPixel < 4.0)
                    continuous = true;
                else if (len / cycle > 4000.0)
                    continuous = true;
            }

            if (continuous)
            {
                EmitSolid(a, b, color, width);
                return;
            }

            double ux = dx / len, uy = dy / len, uz = dz / len;
            auto P = [&](double t) -> Math::Point3 {
                return { a.x + ux * t, a.y + uy * t, a.z + uz * t };
            };

            // “点”(0 长度段)的可见长度:至少 ~1.5px
            double dotLen = std::max(width, m_worldPerPixel * 1.5);
            if (dotLen <= 0.0) dotLen = len * 0.002;

            double t = 0.0;
            size_t idx = 0;
            while (t < len)
            {
                double seg = rec->Pattern[idx % rec->Pattern.size()] * scale;
                ++idx;
                if (seg > 0.0)
                {
                    double t1 = std::min(t + seg, len);
                    EmitSolid(P(t), P(t1), color, width);
                    t = t1;
                }
                else if (seg < 0.0)
                {
                    t += -seg;
                }
                else
                {
                    double t1 = std::min(t + dotLen, len);
                    EmitSolid(P(t), P(t1), color, width);
                    t = t1;
                }
            }
        }

        void EmitSolid(const Math::Point3& a, const Math::Point3& b,
                       const Math::Color4& color, double width)
        {
            if (width <= 0.0)
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
                return;
            }

            // XY 平面内沿垂直方向挤出为四边形(两个三角形)
            double dx = b.x - a.x, dy = b.y - a.y;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-12)
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
                return;
            }
            double nx = -dy / len * width * 0.5;
            double ny =  dx / len * width * 0.5;

            Math::Point3 p0{ a.x + nx, a.y + ny, a.z };
            Math::Point3 p1{ b.x + nx, b.y + ny, b.z };
            Math::Point3 p2{ b.x - nx, b.y - ny, b.z };
            Math::Point3 p3{ a.x - nx, a.y - ny, a.z };

            m_fillVerts.push_back(ToVertex(p0, color));
            m_fillVerts.push_back(ToVertex(p1, color));
            m_fillVerts.push_back(ToVertex(p2, color));
            m_fillVerts.push_back(ToVertex(p0, color));
            m_fillVerts.push_back(ToVertex(p2, color));
            m_fillVerts.push_back(ToVertex(p3, color));

            m_viewDependent = true;
        }

        static const char* DecodeUtf8(const char* p, const char* end, unsigned int& cp)
        {
            auto u = [](const char c) { return static_cast<unsigned char>(c); };
            unsigned char c0 = u(*p);
            if (c0 < 0x80) { cp = c0; return p + 1; }
            if (c0 < 0xC0) { cp = 0xFFFD; return p + 1; }
            if (c0 < 0xE0 && p + 1 < end) { cp = ((c0 & 0x1F) << 6)  | (u(p[1]) & 0x3F); return p + 2; }
            if (c0 < 0xF0 && p + 2 < end) { cp = ((c0 & 0x0F) << 12) | ((u(p[1]) & 0x3F) << 6) | (u(p[2]) & 0x3F); return p + 3; }
            if (p + 3 < end) { cp = ((c0 & 0x07) << 18) | ((u(p[1]) & 0x3F) << 12) | ((u(p[2]) & 0x3F) << 6) | (u(p[3]) & 0x3F); return p + 4; }
            cp = 0xFFFD; return p + 1;
        }

        Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& col) const
        {
            return {
                { static_cast<float>(pt.x), static_cast<float>(pt.y), static_cast<float>(pt.z) },
                { static_cast<float>(col.r), static_cast<float>(col.g),
                  static_cast<float>(col.b), static_cast<float>(col.a) }
            };
        }

        std::vector<Vertex_P3_C4>&     m_verts;
        std::vector<Vertex_P3_C4>&     m_fillVerts;
        std::vector<Vertex_P3_C4>      m_points;     // 点图元（POINTLIST，与线段/填充分开提交）
        std::vector<Vertex_P3_C4_UV>&  m_textVerts;
        Overlay&                       m_overlay;
        const LayerManager&            m_layerManager;
        const LineTypeTable&           m_lineTypes;
        double                         m_worldPerPixel = 0.0;   // 1 屏幕像素对应的世界长度
        double                         m_viewportHeightPx = 0.0;   // 视口像素高度（$PDSIZE<=0 换算世界尺寸用）
        int                            m_pointMode = 0;    // $PDMODE：点样式
        double                         m_pointSize = 0.0;  // $PDSIZE：点图形尺寸
        double                         m_lineTypeScale = 1.0;  // $LTSCALE：全局线型比例
        const EntityAttr*              m_curAttr       = nullptr;
        bool                           m_viewDependent = false;
        bool                           m_thinLines     = false;
        std::vector<std::vector<Math::Point3>> m_wipes;
        WipeClip::Batch                m_pendingWipes{ 5 };   // 本实体内尚未裁剪的遮罩（线、填充、文字、字形实例、点五条流）
        size_t                         m_entityLines = 0;   // 当前实体在各顶点流中的起点（块内遮罩只裁剪本实体先画的部分）
        size_t                         m_entityFills = 0;
        size_t                         m_entityTexts = 0;
        size_t                         m_entityPoints = 0;
        size_t                         m_entityGlyphs = 0;
        std::vector<GlyphInstance>*    m_glyphs   = nullptr;   // 字形实例输出（为空时字形展开成顶点）
        GlyphMeshLibrary*              m_glyphLib = nullptr;
        std::vector<ImageDraw>         m_images;
        ImageProvider                  m_imageProvider;
        GlyphProvider                 m_glyphProvider;
        FontResolver                   m_fontResolver;
    };
}
