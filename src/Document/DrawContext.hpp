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
#include "Document/DeferredDash.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <type_traits>

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
        void SetTextWidthFactor(double widthFactor) override { m_textWidthFactor = widthFactor > 0.0 ? widthFactor : 0.0; }
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

        // ── 分段细分大块参照（Editor 分帧细分用）──────────────────
        // 之后画的块参照从 resume 处（空 = 从头）接着画块内子实体，到 deadline 就停（至少画一个）。位置是一条路径：
        // 每层块参照里第几个子实体（BeginBlockChild 的调用序），路径之前的子实体整个跳过、不进嵌套块参照，
        // 所以接着画的开销与已画了多少无关。画着的子实体里面（天正对象、标注里再套的块）不分段、不计序号
        void BeginChunk(std::vector<size_t> resume, std::chrono::steady_clock::time_point deadline)
        {
            m_chunk          = {};
            m_chunk.active   = true;
            m_chunk.resuming = !resume.empty();
            m_chunk.resume   = std::move(resume);
            m_chunk.next     = { 0 };
            m_chunk.deadline = deadline;
        }
        // 返回本段画了几个子实体；没画完时 resume 是下一段的起点，画完了为空
        size_t EndChunk(std::vector<size_t>& resume)
        {
            resume = m_chunk.stopped ? std::move(m_chunk.resume) : std::vector<size_t>{};
            const size_t drawn = m_chunk.drawn;
            m_chunk = {};
            return drawn;
        }
        bool ChunkStopped() const { return m_chunk.active && m_chunk.stopped; }

        bool BeginBlockChild(bool nested) override
        {
            Chunk& c = m_chunk;
            if (!c.active) return true;
            if (c.leafDepth > 0)
            {
                ++c.leafDepth;
                return true;
            }
            if (c.stopped) return false;
            const size_t level = c.next.size() - 1;
            const size_t i     = c.next[level]++;
            c.path.resize(level + 1);
            c.path[level] = i;
            if (c.resuming)
            {
                const size_t r = level < c.resume.size() ? c.resume[level] : SIZE_MAX;
                if (i < r) return false;                        // 前几段已画过
                if (i == r && nested && level + 1 < c.resume.size())
                {
                    c.next.push_back(0);                        // 路径上的嵌套块参照：进去接着找
                    return true;
                }
                c.resuming = false;                             // 走到上次停下的子实体（或块的内容变了）：从这里接着画
            }
            else if (!nested && c.drawn > 0 && std::chrono::steady_clock::now() >= c.deadline)
            {
                c.stopped = true;
                c.resume  = c.path;
                return false;
            }
            if (nested)
            {
                c.next.push_back(0);
                return true;
            }
            ++c.drawn;
            c.leafDepth = 1;
            return true;
        }
        void EndBlockChild(bool nested) override
        {
            Chunk& c = m_chunk;
            if (!c.active) return;
            if (c.leafDepth > 0)
                --c.leafDepth;
            else if (nested && c.next.size() > 1)
                c.next.pop_back();
        }

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

        // 本次细分是否产出了依赖像素比例的几何(线宽四边形、按像素画的点样式),
        // 供 Editor 决定缩放时哪些实体需要重新细分。虚线不算：它的几何在世界坐标里，与缩放无关
        bool HasViewDependentGeometry() const { return m_viewDependent; }
        // 复位该标志：逐实体细分时在每个实体前调用，按实体统计
        void ResetViewDependentFlag()         { m_viewDependent = false; }

        // 线型里的“点”笔划画成 1.5px 的小段而不是点图元（选中流用：它没有点流一路）
        void SetLineTypeDotsAsSegments(bool on) { m_dotsAsSegments = on; }

        // 全局细线：忽略线宽，全部按 1px 细线显示（同 AutoCAD 关闭"显示线宽"）
        void SetThinLines(bool thin) { m_thinLines = thin; }

        // 当前实体的线型记录（ByLayer → 图层线型、ByBlock → Continuous）。
        // DrawLine 与 DrawStyledLine 共用，保证「铺线型」与「单独判定虚线」取到同一个线型。
        const LineTypeRecord* CurrentLineType() const { return m_lineTypes.Find(CurrentLineTypeId()); }
        LineTypeID CurrentLineTypeId() const
        {
            if (!m_curAttr) return LineTypeTable::ContinuousID;
            const Layer* layer = m_layerManager.GetLayer(m_curAttr->LayerId);
            LineTypeID lt = m_curAttr->LineType;
            if (lt == LineTypeTable::ByLayerID)
                lt = layer ? layer->GetLineType() : LineTypeTable::ContinuousID;
            if (lt == LineTypeTable::ByBlockID)
                lt = LineTypeTable::ContinuousID;
            return lt;
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

            ResolvedTextStyle rs = m_fontResolver(styleId);
            if (!rs.Font)
                return 0.0;
            if (m_textWidthFactor > 0.0)
                rs.WidthFactor = m_textWidthFactor;

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

            ResolvedTextStyle rs = m_fontResolver(styleId);
            if (!rs.Font)
                return false;
            if (m_textWidthFactor > 0.0)
                rs.WidthFactor = m_textWidthFactor;

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
            else if (m_textWidthFactor > 0.0)
                rs.WidthFactor = m_textWidthFactor;      // 多行文字实体自己的宽度因子（SetTextWidthFactor）

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
            // 少了全局那一乘，大尺寸图纸的虚线周期会小上千倍，虚线会整条消失。
            const double entityScale = (m_curAttr && m_curAttr->LinetypeScale > 0.0)
                                          ? m_curAttr->LinetypeScale : 1.0;
            const double scale = entityScale * m_lineTypeScale;

            double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-12) return;

            // 方向单位向量与沿线取点：护栏（dash 过多、长虚线延迟）判定和符号铺设都要用，
            // 提到判定之前
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

            // 虚线几何完全在世界坐标里（周期 = 图案周期 × 比例），与缩放无关：**不**标记随视图变化，进静态流，
            // 滚轮缩放时不重新细分。以前所有虚线按「一个周期不足 4px 就退化为实线」取舍，每格滚轮都要把
            // 整张图的虚线实体重新细分（13 万实体的管综图每格 56 ms）。
            // 一段几百上千个周期的长线（线型比例很小的管线、轴线）仍按像素取舍——全部铺出来顶点会多出几倍
            //（管综图 490 万 → 2080 万），而缩小时它们本来就该是实线。但不能因此把所在实体整个标成随视图变化
            //（含一段长虚线的大块每格滚轮要重铺几十毫秒）：无线宽时只把这一段「延迟」出来（DeferredDash），
            // 由 Editor 放进动态流，缩放时从这个小列表直接重铺，不再调用实体的 Draw。
            // 有线宽时实体本来就随视图变化（线宽按像素烘焙），按像素取舍即可。
            //
            // 例外：带符号的复杂线型（文字/形）不按像素取舍，也不延迟 —— 符号是字形实例，在世界坐标里、
            // 不随缩放变化，延迟的长虚线又带不了符号；它们照常进静态流，数量由 kMaxDashes 限住：
            // 超过时与普通虚线一样退化为实线、不画符号（每个周期一组字形，几万个周期会卡住）。
            if (!continuous)
            {
                const double cycle = rec->PatternLength * scale;
                if (cycle <= 1e-12 || len / cycle > kMaxDashes)          // dash 数量过大：退化为实线显示
                    continuous = true;
                else if (order.empty() && len / cycle > kStaticMaxDashes)
                {
                    if (width <= 0.0 && m_deferLongDashes)
                    {
                        m_deferred.push_back({ a, b, color, CurrentLineTypeId(), scale });
                        return;
                    }
                    MarkViewDependent();
                    if (m_worldPerPixel > 0.0 && cycle / m_worldPerPixel < 4.0)   // 一个周期不足 4px：实线
                        continuous = true;
                }
            }

            if (continuous)
            {
                EmitSolid(a, b, color, width);
                return;
            }

            // “点”（0 长度段）：无线宽时走点流（恒定 1 屏幕像素，同图案填充的 dash = 0），
            // 不依赖缩放；有线宽时画成一个线宽长的加宽段（线宽本身就按像素烘焙、随视图变化）。
            // 选中流没有点图元一路，按 1.5px 的小段画（随缩放重建，由 UpdateSelectionVertices 设置）
            const bool dotAsSegment = width > 0.0 || m_dotsAsSegments;
            double dotLen = width;
            if (dotAsSegment && width <= 0.0)
            {
                dotLen = m_worldPerPixel * 1.5;
                if (dotLen <= 0.0) dotLen = len * 0.002;
                MarkViewDependent();
            }

            LayPattern(a, b, *rec, scale, dotAsSegment ? dotLen : 0.0,
                       [&](const Math::Point3& p, const Math::Point3& q) { EmitSolid(p, q, color, width); },
                       [&](const Math::Point3& p) { m_points.push_back(ToVertex(p, color)); },
                       0.0,
                       [&](size_t pat, double at)
                       {
                           // 符号锚点落在该 Pattern 段的**末尾**（见 LayPattern 的说明）
                           for (const auto& o : order)
                               if (o.first == pat)
                                   EmitLineTypeSymbol(P(at), ux, uy, uz,
                                                      rec->Symbols[o.second], scale, color);
                       });
        }

    public:
        // 沿 a→b 按线型图案铺设：实段交给 emitSeg(p, q)；点笔划（0 长度段）在 dotLen > 0 时作为
        // dotLen 长的实段，否则交给 emitDot(p)。图案全为点（铺不动）时整段当实线。
        // emitSym(patternIndex, 段末位置 t)：复杂线型一个 Pattern 段画完之后的落符号点（可选，默认不铺符号）
        // startOffset：a 点落在图案里的位置（世界单位，已含比例；对周期取模）——一段虚线被裁掉前半段后，
        // 后半段从这个相位接着铺，裁剪处的 dash / gap 才不会错位
        template <class EmitSeg, class EmitDot, class EmitSym = std::nullptr_t>
        static void LayPattern(const Math::Point3& a, const Math::Point3& b, const LineTypeRecord& rec, double scale,
                               double dotLen, EmitSeg&& emitSeg, EmitDot&& emitDot, double startOffset = 0.0,
                               EmitSym&& emitSym = nullptr)
        {
            const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-12) return;
            double advance = 0.0;
            for (double v : rec.Pattern)
                advance += std::abs(v);
            if (rec.Pattern.empty() || advance * scale <= 1e-12)
            {
                emitSeg(a, b);
                return;
            }

            const double ux = dx / len, uy = dy / len, uz = dz / len;
            auto P = [&](double t) -> Math::Point3 { return { a.x + ux * t, a.y + uy * t, a.z + uz * t }; };

            // 先把起点相位走掉：跳过整段落在相位之前的元素，停在相位所在的元素中间（partial = 该元素剩余长度）
            size_t idx = 0;
            double partial = -1.0;
            if (startOffset > 0.0)
            {
                const double period = advance * scale;
                double off = std::fmod(startOffset, period);
                if (off < 0.0) off += period;
                // 元素总长 = 周期 > off，所以最多走一圈就停下
                for (size_t n = 0; off > 1e-12 && n < rec.Pattern.size(); ++n, ++idx)
                {
                    const double seg = std::abs(rec.Pattern[idx % rec.Pattern.size()]) * scale;
                    if (seg > off + 1e-12) { partial = seg - off; break; }
                    off -= seg;
                }
            }

            double t = 0.0;
            while (t < len)
            {
                const size_t pat = idx % rec.Pattern.size();
                double seg = rec.Pattern[pat] * scale;
                if (partial >= 0.0) { seg = seg < 0.0 ? -partial : partial; partial = -1.0; }
                ++idx;

                // 符号锚点取该 Pattern 段的**末尾**（画完这一段之后的位置）。
                // 笔先走完元素长度、再落字形，所以文字/形落在元素**末尾**
                // （在随后的 gap 里）——与 .lin 的记号顺序
                // `dash, -space, ["TEXT"]` 和 AutoCAD 的摆放一致。
                // 取段首会让符号压在前面的 dash 上，dash 从字形中间穿过。

                if (seg > 0.0)
                {
                    const double t1 = std::min(t + seg, len);
                    emitSeg(P(t), P(t1));
                    t = t1;
                }
                else if (seg < 0.0)
                {
                    t += -seg;
                }
                else if (dotLen > 0.0)
                {
                    const double t1 = std::min(t + dotLen, len);
                    emitSeg(P(t), P(t1));
                    t = t1;
                }
                else
                {
                    emitDot(P(t));
                }

                // 铺设落在这一段上的符号(符号沿线段中线对齐,见 EmitLineTypeSymbol)。
                // 每个周期都重新判定 —— 光标游走只会让第一个周期之后再也画不出符号,
                // GAS_LINE / FENCELINE1 就只剩一个文字/一个叉号,后面全是光杆虚线。
                //
                // 锚点用**段末** t（不是 segStart）：见上面关于 .lin 记号顺序的说明。
                if constexpr (!std::is_same_v<std::decay_t<EmitSym>, std::nullptr_t>)
                    emitSym(pat, t);
            }
        }

        // 在沿线位置 p 铺一个复杂线型的文字/符号。
        //   ux,uy,uz = 线段单位方向;p = 符号锚点 = 该 Pattern 段的**末尾**
        //   （X/Y 偏移相对这一点，见 LayPattern 里的说明）。
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

        // 延迟的长虚线段按当前缩放铺成线段：一个周期不足 4px（或缩放未知）时画成一段实线；
        // 点笔划画成 1.5px 的小段（动态流随缩放重建，不需要点流）
        static void EmitDeferredDash(const DeferredDash& d, const LineTypeTable& lineTypes, double worldPerPixel,
                                     std::vector<Vertex_P3_C4>& out)
        {
            const LineTypeRecord* rec = lineTypes.Find(d.lineType);
            const double cycle = (rec && !rec->IsContinuous()) ? rec->PatternLength * d.scale : 0.0;
            if (cycle <= 1e-12 || worldPerPixel <= 0.0 || cycle / worldPerPixel < 4.0)
            {
                out.push_back(ToVertex(d.a, d.color));
                out.push_back(ToVertex(d.b, d.color));
                return;
            }
            LayPattern(d.a, d.b, *rec, d.scale, worldPerPixel * 1.5,
                       [&](const Math::Point3& p, const Math::Point3& q)
                       {
                           out.push_back(ToVertex(p, d.color));
                           out.push_back(ToVertex(q, d.color));
                       },
                       [](const Math::Point3&) {}, d.phase);
        }

        // 延迟长虚线：开启后无线宽的长虚线段只记录不铺设（场景流用；选中流、对话框预览不开）
        void SetDeferLongDashes(bool on) { m_deferLongDashes = on; }
        bool DeferLongDashes() const     { return m_deferLongDashes; }
        // 本次细分延迟的长虚线段（调用方取走后清空）
        std::vector<DeferredDash> TakeDeferredDashes() { return std::move(m_deferred); }
        // 尚未取走的延迟段（布局页的视口合成后按视口矩形裁剪其中新增的那些）
        std::vector<DeferredDash>& DeferredDashes() { return m_deferred; }
        double GetLineTypeScale() const { return m_lineTypeScale; }

    private:

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

        static Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& col)
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
        double                         m_textWidthFactor = 0.0;   // 当前实体的多行文字宽度因子（0 = 按样式）
        bool                           m_dotsAsSegments = false;   // 线型的点笔划画成小段（选中流）
        bool                           m_deferLongDashes = false;  // 长虚线段延迟铺设（见 EmitStyled）
        std::vector<DeferredDash>      m_deferred;                 // 本次细分延迟的长虚线段
        // 一段虚线最多多少个周期还进静态流（不随缩放重建）；超过的延迟铺设 / 按像素取舍
        static constexpr double        kStaticMaxDashes = 200.0;
        static constexpr double        kMaxDashes       = 4000.0;  // 再多就当实线
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
        // 分段细分块参照（BeginChunk）
        struct Chunk
        {
            bool                                  active    = false;
            bool                                  resuming  = false;   // 还没走到上次停下的子实体
            bool                                  stopped   = false;
            int                                   leafDepth = 0;       // 正在画的子实体里面又套了几层
            size_t                                drawn     = 0;       // 本段画了几个子实体
            std::vector<size_t>                   resume;              // 从哪接着画 / 停在哪：每层第几个子实体
            std::vector<size_t>                   next;                // 每层下一个子实体的序号（一层一项）
            std::vector<size_t>                   path;                // 当前子实体的路径
            std::chrono::steady_clock::time_point deadline;
        };
        Chunk                          m_chunk;
    };
}
