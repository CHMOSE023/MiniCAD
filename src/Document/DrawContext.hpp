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
#include "Scene/TextStyleTable.h"
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

    // 形字体解析回调：SHX 形文件路径（如 "ltypeshp.shx"）→ 字体（找不到返回空）。
    // 复杂线型的形段（组码 75）不引用文字样式，直接按 DWG 里组码 340 的 STYLE
    // 的组码 3 定位形文件；文字样式表里没有这一项，所以单独走一条解析路径。
    using ShapeFontResolver = std::function<IFont*(const std::string& fontFile)>;

    // 形段（组码 75）只用折线描边，不补实心三角形。
    //
    // 曾经按「AutoCAD 把闭合形画成实心」的假设给闭合轮廓补过三角面，实测推翻：
    // ltypeshp.shx 里只有 BOX(132)、CIRC1(133) 首尾闭合，而 BATTING 用的 BAT(134)
    // 是开放轮廓 —— 起点 (0,0)、终点 (2,0)，差一条底边；两个 BAT 以 rot=0 / rot=180
    // 交替出现（acad.lin 的 BATTING），各自的缺口由相邻符号拼上，才连续成 SSSSS。
    // 也就是说 AutoCAD 对形段从来不填充，FENCELINE1 的圆点、FENCELINE2 的方框
    // 都是空心轮廓；补实心反而与 CAD 不一致。SHXParser::BuildGlyph 只产出折线
    // （Glyph::Triangles 恒为空）正好是这个语义，不需要额外处理。
    //
    // 复现证据：out/probe/ProbeShapeClose.cpp 逐个打印 ltypeshp.shx 的形端点与闭合性。

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

        // 文字样式表：解析复杂线型里文字段的字高用（DXF 组码 340 → 样式 ID）。
        // 不随构造函数传，是因为线型表本身就在构造函数里，样式表在 Scene 里更晚可取；
        // 不设置也能画（退回默认字高）。
        void SetTextStyleTable(const TextStyleTable* table) { m_textStyles = table; }

        // 形字体解析回调（复杂线型的形段用）。不设置则形段不画——
        // 总比拿文字样式的字体乱找形号要好。
        void SetShapeFontResolver(ShapeFontResolver r) { m_shapeFontResolver = std::move(r); }

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

        // 量多行文字排版后的宽度（最宽一行，世界单位）。
        // 与 EmitMText 用**同一套**字体解析 + TextLayoutEngine ⇒ 量与画一致。
        double MeasureMTextWidth(const std::string& utf8Text,
                                 uint32_t           styleId,
                                 double             height,
                                 double             boxWidth) override
        {
            if (utf8Text.empty() || !m_fontResolver)
                return 0.0;

            const ResolvedTextStyle rs = m_fontResolver(styleId);
            if (!rs.Font)
                return 0.0;

            TextLayoutEngine layout;
            return layout.MeasureWidth(utf8Text, rs.Font, height, rs.WidthFactor, boxWidth);
        }

        // 量文字的**墨迹**左右边界（相对排版原点）。与 EmitMText 同一套字体与规则。
        bool MeasureMTextInkBounds(const std::string& utf8Text,
                                   uint32_t           styleId,
                                   double             height,
                                   double             boxWidth,
                                   double&            outMinX,
                                   double&            outMaxX) override
        {
            if (utf8Text.empty() || !m_fontResolver)
                return false;

            const ResolvedTextStyle rs = m_fontResolver(styleId);
            if (!rs.Font)
                return false;

            TextLayoutEngine layout;
            return layout.MeasureInkBounds(utf8Text, rs.Font, height, rs.WidthFactor, boxWidth,
                                           outMinX, outMaxX);
        }

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

            // 方向单位向量与沿线取点：护栏（缩得过小、dash 过多）判定和
            // 符号铺设都要用，提到判定之前
            double ux = dx / len, uy = dy / len, uz = dz / len;
            auto P = [&](double t) -> Math::Point3 {
                return { a.x + ux * t, a.y + uy * t, a.z + uz * t };
            };

            // 复杂线型:文字/符号段铺在 Pattern 的某个下标上。按 (cycle 序号, 下标)
            // 归组,一遍扫完就把同一个周期里的所有符号按 AutoCAD 的次序画出去 ——
            // 次序很重要:ZIGZAG 靠「正偏移符号 → 负偏移符号」的交替才形成锯齿。
            std::vector<std::pair<size_t, size_t>> order;   // (patternIndex, symbolIndex)
            if (rec->HasSymbols())
            {
                order.reserve(rec->Symbols.size());
                for (size_t k = 0; k < rec->Symbols.size(); ++k)
                {
                    const size_t pi = rec->Symbols[k].PatternIndex;
                    if (pi < rec->Pattern.size())
                        order.emplace_back(pi, k);
                }
                std::sort(order.begin(), order.end());
            }

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

                // 护栏把虚线压成实线后，**符号不能跟着一起消失** ——
                // 否则缩得太小时 GAS_LINE / TRACKS 看起来就是一条普通实线，
                // 用户根本认不出这条线是什么线型。
                //
                // 做法：按符号在周期内的相对位置（该段起点累计长度）摆放，
                // 之后整组按 pitch 重复。pitch 有下限（约 40px）和总量上限，
                // 缩得再小也只是一行疏符号，不会糊成一团，顶点数也不会爆掉。
                if (!order.empty())
                {
                    // 每个符号在周期内的偏移：段起点累计 |Pattern| 长度
                    const size_t nPat = rec->Pattern.size();
                    std::vector<double> starts(nPat, 0.0);
                    double acc = 0.0;
                    for (size_t i = 0; i < nPat; ++i)
                    {
                        starts[i] = acc;
                        acc += std::abs(rec->Pattern[i]);
                    }
                    const double cycleLen = std::max(rec->PatternLength * scale, 1e-12);
                    const double pitch = (m_worldPerPixel > 0.0)
                                           ? std::max(cycleLen, m_worldPerPixel * 40.0)
                                           : cycleLen;
                    size_t repeats = static_cast<size_t>(len / pitch) + 1;
                    repeats = std::min<size_t>(repeats, std::max<size_t>(1, 512 / order.size()));
                    for (size_t r = 0; r < repeats; ++r)
                        for (const auto& o : order)
                        {
                            const double inCycle = starts[o.first] * scale;
                            const double at = std::min(r * pitch + inCycle, len);
                            EmitLineTypeSymbol(P(at), ux, uy, uz,
                                               rec->Symbols[o.second], scale, color);
                        }
                }
                return;
            }

            // “点”(0 长度段)的可见长度:至少 ~1.5px
            double dotLen = std::max(width, m_worldPerPixel * 1.5);
            if (dotLen <= 0.0) dotLen = len * 0.002;

            double t = 0.0;
            size_t idx = 0;
            while (t < len)
            {
                const size_t pat = idx % rec->Pattern.size();
                double seg = rec->Pattern[pat] * scale;
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

                // 该段画完,铺设落在它之后的符号(符号沿线段中线对齐,见 EmitLineTypeSymbol)。
                // 每个周期都重新判定 —— 光标游走只会让第一个周期之后再也画不出符号,
                // GAS_LINE / FENCELINE1 就只剩一个文字/一个叉号,后面全是光杆虚线。
                for (const auto& o : order)
                {
                    if (o.first == pat)
                        EmitLineTypeSymbol(P(t), ux, uy, uz,
                                           rec->Symbols[o.second], scale, color);
                }
            }
        }

        // 在沿线位置 p 铺一个复杂线型的文字/符号。
        //   ux,uy,uz = 线段单位方向;p = 符号锚点(该 Pattern 段刚画完的位置)。
        //   sy.OffsetX/Y 是组码 44/45 的偏移,在线的局部坐标系里:局部 x 沿线、局部 y 向上。
        //   sy.Rotation = 0 时符号随线转向(AutoCAD 默认);AbsoluteRotation 时相对世界 X 轴。
        void EmitLineTypeSymbol(const Math::Point3& p, double ux, double uy, double uz,
                                const LineTypeSymbol& sy, double scale, const Math::Color4& color)
        {
            // 符号高度 = 组码 46 × 线型比例;再乘所引文字样式的固定字高(组码 40)。
            // 样式字高为 0(不固定)时用 1.0 —— 与 AutoCAD 一样:绝对旋转的文字
            // 靠这个值拿到可读的尺寸,不会退化成 0。
            double styleHeight = 1.0;
            if (m_textStyles != nullptr)
                if (const TextStyleRecord* st = m_textStyles->Find(sy.StyleId); st != nullptr && st->Height > 0.0)
                    styleHeight = st->Height;
            const double height = sy.Scale * scale * styleHeight;
            if (!(height > 1e-9) || !(height < 1e12)) return;

            // 锚点:局部 (OffsetX, OffsetY) → 世界
            const double ox = ux * sy.OffsetX * scale - uy * sy.OffsetY * scale;
            const double oy = uy * sy.OffsetX * scale + ux * sy.OffsetY * scale;
            Math::Point3 at { p.x + ox, p.y + oy, p.z };

            // 旋转:文字基线沿 x,所以「随线转向」= 线段方向角;绝对旋转直接用 sy.Rotation。
            const double rotation = sy.AbsoluteRotation
                                      ? sy.Rotation
                                      : sy.Rotation + std::atan2(uy, ux);
            (void)uz;

            // 形段(组码 75):字形来自 SHX 形文件,不是文字,不走排版。
            if (sy.Shape != 0)
            {
                EmitLineTypeShape(at, sy, height, rotation, color);
                return;
            }

            if (sy.Text.empty() || !m_fontResolver) return;

            EmitLaidOutText(at, sy.Text, sy.StyleId, height, rotation, 0.0,
                            TextLayoutEngine::HAlign::Left, 0.0, 0.0, color);
        }

        // 在 at 处画一个线型形段(组码 75 的 SHX 形号,字形取自 sy.ShapeFont)。
        //   height = sy.Scale × scale × 样式字高(与文字段同一套尺寸约定);
        //   rotation = 已算好的朝向(随线或绝对)。
        // 字形直接用字体的原始坐标(不排版、不居中),与 AutoCAD 的形段一致。
        void EmitLineTypeShape(const Math::Point3& at, const LineTypeSymbol& sy,
                               double height, double rotation, const Math::Color4& color)
        {
            if (sy.Shape == 0 || !m_shapeFontResolver)
                return;

            IFont* font = m_shapeFontResolver(sy.ShapeFont);
            if (font == nullptr)
                return;

            // SHXFont::ResolveShxKey 对 < 0x80 的码点直接当形号查表,
            // 与文字 %%nnn 控制码走同一条路。加 kRawShapeFlag 明确语义
            // (低 32 位就是形号),也防止以后换成 Unicode 映射的字体时被转错。
            const uint32_t key = kRawShapeFlag | sy.Shape;
            if (!font->HasGlyph(key))
                return;

            const Glyph& glyph = font->GetGlyph(key);

            // 形文件的单位:字形坐标以字体高度为 1。按目标高度整体缩放,
            // 再按 rotation 旋转 —— 与 EmitLaidOutText 的 xform 等价(无倾斜、无宽度因子)。
            const double s = height / (font->GetHeight() > 0.0 ? font->GetHeight() : 1.0);
            if (!(s > 0.0) || !(s < 1e12)) return;

            const double cosR = std::cos(rotation);
            const double sinR = std::sin(rotation);
            const Math::Float4 c4
            {
                static_cast<float>(color.r), static_cast<float>(color.g),
                static_cast<float>(color.b), static_cast<float>(color.a)
            };

            auto xform = [&](const Math::Point3& lp) -> Math::Float3
            {
                const double x = lp.x * s;
                const double y = lp.y * s;
                return {
                    static_cast<float>(at.x + x * cosR - y * sinR),
                    static_cast<float>(at.y + x * sinR + y * cosR),
                    static_cast<float>(at.z)
                };
            };

            if (m_glyphs)
            {
                uint32_t mesh;
                if (!m_glyphLib->Register(glyph, mesh)) return;
                GlyphInstance g;
                g.m00   = static_cast<float>(s * cosR);
                g.m01   = static_cast<float>(s * sinR);
                g.m10   = static_cast<float>(-s * sinR);
                g.m11   = static_cast<float>(s * cosR);
                g.tx    = static_cast<float>(at.x);
                g.ty    = static_cast<float>(at.y);
                g.z     = static_cast<float>(at.z);
                g.color = c4;
                g.mesh  = mesh;
                m_glyphs->push_back(g);
                return;
            }
            // 选中流等没有实例化输出的路径：只描边（形段本来就不填充）。
            for (const auto& seg : glyph.Lines)
            {
                m_verts.push_back({ xform(seg.Start), c4 });
                m_verts.push_back({ xform(seg.End),   c4 });
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
        const TextStyleTable*          m_textStyles = nullptr;   // 复杂线型文字段的字高解析
        // 复杂线型形段的形文件解析（组码 75 的形号 → SHX 形文件 → 字形）
        ShapeFontResolver              m_shapeFontResolver;
    };
}
