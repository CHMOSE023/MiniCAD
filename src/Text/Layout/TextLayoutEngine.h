#pragma once
#include <string>
#include <vector>

#include "../Font/IFont.h"
#include "../Glyph/Glyph.h"
#include "Core/Math/Point3.hpp"
#include "Core/MTextFormat.h"

namespace MiniCAD
{
    class TextLayoutEngine
    {
    public:
        enum class HAlign
        {
            Left,
            Center,
            Right
        };

        struct GlyphInstance
        {
            const Glyph* m_glyph = nullptr;   // 指向字体缓存（见 IFont::GetGlyph）

            Math::Point3 m_position;
            double       m_rotation = 0.0;
            double       m_scale    = 1.0;
            double       m_widthFactor = 1.0;
            double       m_shear       = 0.0;   // 倾斜：字形 x 随高度错切的斜率
            int          m_run         = -1;    // LayoutRich 的段下标（取颜色）；-1 = 整段同色
        };

        // 下划线 / 上划线 / 删除线、分数横线：(m_x0, m_y) 到 (m_x1, m_y + m_rise) 的线段，与 GlyphInstance::m_position 同一排版坐标（未旋转）；
        // 只有斜线分数（\S上#下;）的斜线 m_rise 不为 0
        struct DecorationLine
        {
            double m_x0 = 0.0, m_x1 = 0.0, m_y = 0.0;
            int    m_run = -1;
            double m_rise = 0.0;
        };

        struct LayoutResult
        {
            std::vector<GlyphInstance>  m_glyphs;
            std::vector<DecorationLine> m_decorations;
            double m_width  = 0.0;   // LayoutRich：最宽一行的右端（含缩进）
            double m_height = 0.0;   // LayoutRich：首行顶到末行基线
        };

        // LayoutRich 每段的字体与字形变换（与 MTextContent::runs 一一对应，调用方已合并样式、实体与格式码）
        struct RunStyle
        {
            IFont* font        = nullptr;
            double widthFactor = 1.0;
            double shear       = 0.0;
        };

        struct RichOptions
        {
            double height    = 1.0;         // 基准字高（世界单位）
            double boxWidth  = 0.0;         // 0 = 不限宽
            double lineSpacing = 1.0;       // 行距比例（组码 44），行距规则见 MTextLinePitch
            bool   exact     = false;       // 精确行距：按基准字高；否则"至少"，按每行实际的上升、下沉
            HAlign justify   = HAlign::Left; // 段落没有 \pq 时的对齐
        };

    public:
        LayoutResult Layout(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double rotation,
            double boxWidth,
            HAlign align = HAlign::Left);

        // 带格式的多行文字：各段字体、字高、宽度、倾斜、字间距不同，按段落折行、缩进、对齐，分数与上下标堆叠。
        // 原点是首行"基准字高的顶"下一个基准字高处（与 Layout 的单一字高时相同）；首行有更高的字时整体下移
        LayoutResult LayoutRich(const MTextContent& content, const std::vector<RunStyle>& styles, const RichOptions& options);

        // 多行文字排版后的宽度（取最宽一行，世界单位）。
        // 与 Layout() 共用 BreakLines + GetAdvance × scale × widthFactor ⇒ "量的"与"画的"一致。
        //
        // 用途：多重引线的基线要画到文字边缘，而这个宽度与 AutoCAD 记录的不同
        // （TSSD 等字体的度量差异，实测我们窄约 1.5 倍），所以只能量自己真实渲染的宽度。
        //
        // ⚠ 这是**步进宽度**（advance 之和），含字形右侧留白。要把线画到"看上去贴着字"，
        //    应该用 MeasureInkBounds()。
        double MeasureWidth(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double boxWidth);

        // 文字**墨迹**在排版坐标下的左右边界（相对排版原点，世界单位）。
        // 与 MeasureWidth 的差别：步进宽度含右侧留白（实测 45~133 单位，
        // 相当于 0.11~0.44 个字高），墨迹边界才是肉眼看到的字外沿。
        // 返回 false 表示取不到字形（调用方应退回 MeasureWidth）。
        bool MeasureInkBounds(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double boxWidth,
            double& outMinX,
            double& outMaxX);

        std::vector<uint32_t> DecodeLine(const std::string& line);
    private:
        void BreakLines(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double boxWidth,
            std::vector<std::string>& outLines);

        double ComputeLineWidth(const std::string& line,
            IFont* font,
            double widthFactor);
    };
}
