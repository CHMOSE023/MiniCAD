#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace MiniCAD
{
    using LayerID = uint32_t;
    struct EntityAttr;
    namespace Math { struct Mat4; }
    class ClipDrawSink;

    // 块参照所在的各层裁剪框（IDrawSink::BeginBlockInstance 经过 ClipDrawSink 时逐层加上）：块定义级缓存命中时据此判断
    // 记下的内容整个在框内（原样接上）、整个在框外（什么都不画）还是跨在边上（逐个画）
    struct BlockClipChain
    {
        const ClipDrawSink*   clip  = nullptr;
        const Math::Mat4*     xform = nullptr;   // 块坐标 → 这层裁剪框的坐标
        const BlockClipChain* inner = nullptr;   // 离块参照更近的一层
        int                   count = 0;         // 从块参照往下数，到这层为止有几层裁剪框
    };

    class IDrawSink
    {
    public:
        static constexpr Math::Color4 kHoverColor = { 0, 0.5, 0.8, 0.9 };
        static constexpr Math::Color4 kSelectionColor = { 0, 0.3, 0.8, 0.9 };

        virtual ~IDrawSink() = default;

        // 实体级绘制上下文:Editor 在调用 entity.Draw 前传入该实体的属性,
        // 供 sink 解析 ByLayer 线型/线宽;传 nullptr 清除。默认实现忽略。
        virtual void BeginEntity(const EntityAttr* attr) {}

        // 块内子实体的上下文切换：**只**更换线型 / 线宽 / 线型比例解析所用的属性，
        // 不做 BeginEntity 的实体边界处理（不 flush 区域覆盖、不清 wipe 记录、不重置实体起点）。
        //
        // 为什么需要它：块参照展开时，一个 INSERT 内含多个子实体，而 DrawContext 解析
        // 线型靠的是 m_curAttr。若整块只在外层设一次上下文，块内成员**自己的**线型会被
        // 外层 INSERT 的覆盖 —— 块内 DASHED 的线一律画成实线（实测：产出 1 段直线）。
        // 又不能改用 BeginEntity：它会把 m_wipes 清空，块内的区域覆盖记录随之丢失。
        virtual void SetEntityContext(const EntityAttr* attr) { (void)attr; }

        // 块参照展开时，块内每个子实体与属性画之前先调用 BeginBlockChild（nested：它本身是块参照，它的子实体再逐个来问）：
        // 返回 false 就跳过它（嵌套的块参照整个跳过）；画了的画完调用 EndBlockChild。Editor 据此把一个很大的块参照分几帧细分
        //（DrawContext::BeginChunk：从上次停下的子实体接着画，到时限就停），默认都画
        virtual bool BeginBlockChild(bool nested) { (void)nested; return true; }
        virtual void EndBlockChild(bool nested) { (void)nested; }

        // 块参照的一个阵列单元画之前先调用 BeginBlockInstance（block：块定义；host：块参照的属性，块内 ByBlock 与 0 层随它；
        // xform：块坐标 → 本 sink 坐标；clips：它所在的裁剪框，调用方传 nullptr）。sink 能直接给出这个单元的内容（同一块定义、
        // 同样的随块属性与缩放此前细分过）时画好并返回 true，调用方不再画块内子实体；返回 false 时照常逐个画，画完调用
        // EndBlockInstance。DrawContext 据此按块定义缓存细分结果（BlockTessCache），默认不缓存
        virtual bool BeginBlockInstance(const void* block, const EntityAttr& host, const Math::Mat4& xform, const BlockClipChain* clips)
        {
            (void)block; (void)host; (void)xform; (void)clips;
            return false;
        }
        virtual void EndBlockInstance() {}
        // ClipDrawSink 改动了经过它的内容（截断、丢掉，或吞掉了它不转发的查询）：depth 是它往下（往 DrawContext）数第几层
        // 裁剪框。DrawContext 据此不记在它里面的块——记下的要与不裁剪时相同
        virtual void NoteClipped(int depth) { (void)depth; }

        virtual Math::Color4 GetLayerColor(LayerID layerId) const { return Math::Color4::White(); }
        // 图层是否显示（关闭或冻结为 false）：块参照展开时据此跳过块内隐藏图层上的子实体。默认都显示
        virtual bool IsLayerVisible(LayerID layerId) const { return true; }

        // 图层名（大小写不敏感比较由调用方做）。用于 DEFPOINTS 特例等。
        // 块内图层形如 "块名|图层名"。默认返回空串。
        virtual std::string GetLayerName(LayerID layerId) const { (void)layerId; return {}; }

        virtual void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) = 0;

        // 点图元：**一个顶点 = 一个点**，由渲染器保证恒定 1 屏幕像素，与缩放无关。
        //
        // 用途：
        //   · 图案填充的"点"笔划（.pat 的 dash = 0，如 DOTS 点阵）；
        //   · POINT 实体的 $PDMODE=0（AutoCAD 默认实心小点）与拿不到点样式时的退路。
        //
        // ⚠ 不能改用"极短线段"表达：线段长度是世界尺寸，缩小视图时会小于 1px
        //   而整片消失（图案填充的点阵就是这么看不见的）。几何仍是世界坐标，
        //   因此可缓存、缩放不必重建顶点。
        virtual void DrawPoint(const Math::Point3& p, const Math::Color4& color, bool isOverlay) { (void)p; (void)color; (void)isOverlay; }

        // ── 点样式查询（DXF HEADER 的 $PDMODE / $PDSIZE）────────────────────
        //
        // 返回 false = sink 不知道该图纸设置（选中流、探针、文字预览等），调用方按
        // $PDMODE=0 的"实心小点"处理。
        //
        // $PDSIZE 语义：>0 = 世界单位；=0 = 视口高度的 5%；<0 = 视口高度的 |值|%。
        // 后两种是**屏幕相对**的，需要 worldPerPixel + 视口像素高，一并提供。
        virtual bool GetPointStyle(int& outPdMode, double& outPdSize) const
        {
            (void)outPdMode; (void)outPdSize;
            return false;
        }
        // 1 屏幕像素对应的世界长度；<=0 表示未知（相对尺寸无法换算）。
        virtual double GetWorldPerPixel() const { return 0.0; }
        // 视口高度（像素）。=0 表示未知。
        virtual double GetViewportHeightPx() const { return 0.0; }
        // 几何按当前缩放烘焙（如 $PDSIZE<=0 的点图形按视口百分比换算成世界尺寸）：缩放后要重新细分
        virtual void MarkViewDependent() {}
        // 之后的 EmitMText / MeasureMText* 用这个宽度因子代替样式的（> 0 时；0 = 按样式）。
        // 多行文字实体自己的宽度因子（天正文字按 0.7 宽显示等）经这里传下去，不必为每种宽度派生一个文字样式
        virtual void SetTextWidthFactor(double widthFactor) { (void)widthFactor; }

        // 按线型铺设有宽度的线段（width 为模型单位）。粗多段线无法整体填充成一条带宽度、
        // 又带线型的路径：填充几何不经过线型解析，虚线会被画成实线。所以虚线粗线逐段走这里，
        // 由 sink 解析线型、挤出加宽几何。默认实现忽略线宽，等价于 DrawLine（保持旧行为）。
        virtual void DrawStyledLine(const Math::Point3& a, const Math::Point3& b,
                                    const Math::Color4& color, double width)
        {
            (void)width;
            DrawLine(a, b, color, false);
        }

        // 当前实体的生效线型是否为虚线。有宽度的实体据此选择：
        // 连续线走整体填充（保留斜接尖角），虚线则逐段调用 DrawStyledLine。
        // 默认 false —— 未知线型按连续实线处理。
        virtual bool IsCurrentLinetypeDashed() const { return false; }

        // 填充三角形（线宽 > 1 的实体由三角形拼接而成，走 TRIANGLELIST 场景缓冲）
        virtual void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) {}

        // 区域覆盖（WIPEOUT）：声明一个闭合多边形，之前绘制的内容在其内部不可见。
        // 只有生成场景顶点流的 sink 需要处理，默认忽略。
        virtual void Wipe(const std::vector<Math::Point3>& polygon) {}

        // 光栅图像：corners 依次为左下、右下、右上、左上（世界坐标）。
        // 返回 false 表示 sink 不显示图像或图像不可用，调用方改画占位框。
        virtual bool EmitImage(const std::string& path, const std::array<Math::Point3, 4>& corners) { return false; }

        // 纹理字形（Web 字体图集路径）
        virtual void EmitText(const Math::Point3& pos, const std::string& utf8Text, float height, float rotation, const Math::Color4& color) {}

        // 矢量多行文字（SHX / TTF 路径）：原始数据透传，渲染逻辑由 DrawContext 实现
        // styleId: FontStyle ID（由 DocumentManager 管理）
        // boxWidth: 自动换行宽度，0 = 不限宽
        virtual void EmitMText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation, double boxWidth, const Math::Color4& color) {}

        // 单行文字（TEXT）按对齐点输出：hAlign 0 左 / 1 中 / 2 右（按实际字宽），vAlign 0 基线 / 1 底 / 2 中 / 3 上。
        // widthFactor > 0 时代替样式的宽度因子（TEXT 自身的组码 41），0 = 用样式的。
        // 默认当作左对齐基线交给 EmitMText（只关心样式等的 sink 不必实现）
        virtual void EmitAlignedText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                                     int hAlign, int vAlign, double widthFactor, const Math::Color4& color)
        {
            (void)hAlign; (void)vAlign; (void)widthFactor;
            EmitMText(pos, utf8Text, styleId, height, rotation, 0.0, color);
        }

        // ── 文字测量（多重引线的基线要画到文字边缘）─────────────────────────
        //
        // 存在意义：**我们的字体度量与 AutoCAD 不同**（实测 TSSD 图纸里我们窄约 1.5 倍），
        // 所以 AutoCAD 记录的文字块宽不能直接拿来当绘制宽度用。多重引线的基线
        // 要画到文字边缘，只能量我们自己真实渲染的宽度。
        //
        // 必须与 EmitMText 用同一套字体解析 + TextLayoutEngine ⇒ "量的"与"画的"一致。
        // 返回 0 表示无法测量（调用方应退回按记录值绘制）。
        virtual double MeasureMTextWidth(const std::string& /*utf8Text*/, uint32_t /*styleId*/,
                                         double /*height*/, double /*boxWidth*/) { return 0.0; }

        // 量多行文字的**墨迹**左右边界（相对排版原点，世界单位）。
        // 与 MeasureMTextWidth 的差别：后者是步进宽度（advance 之和，含右侧留白），
        // 墨迹边界才是肉眼看到的字外沿。要把线"画到文字外侧"必须用这个。
        // 返回 false 表示取不到（调用方退回 MeasureMTextWidth）。
        virtual bool MeasureMTextInkBounds(const std::string& /*utf8Text*/, uint32_t /*styleId*/,
                                           double /*height*/, double /*boxWidth*/,
                                           double& /*outMinX*/, double& /*outMaxX*/) { return false; }
    };
} 