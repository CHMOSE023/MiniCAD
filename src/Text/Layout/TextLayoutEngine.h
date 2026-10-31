#pragma once
#include <string>
#include <vector>

#include "../Font/IFont.h"
#include "../Glyph/Glyph.h"
#include "Core/Math/Point3.hpp"

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
        };

        // 下划线 / 上划线 / 删除线：一条水平线段，与 GlyphInstance::m_position 同一排版坐标（未旋转）
        struct DecorationLine
        {
            double m_x0 = 0.0, m_x1 = 0.0, m_y = 0.0;
        };

        struct LayoutResult
        {
            std::vector<GlyphInstance>  m_glyphs;
            std::vector<DecorationLine> m_decorations;
        };

    public:
        LayoutResult Layout(const std::string& text,
            IFont* font,
            double height,
            double widthFactor,
            double rotation,
            double boxWidth,
            HAlign align = HAlign::Left);

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
