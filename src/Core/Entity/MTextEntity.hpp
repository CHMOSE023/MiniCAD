#pragma once

#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Object/Object.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

namespace MiniCAD
{
    using FontStyleId = uint32_t;

    // 附着点(DXF 组码 71):决定文字块相对插入点 m_position 的对齐方式。
    enum class MTextAttachment : uint8_t
    {
        TopLeft = 1, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight,
    };

    // 绘制方向(DXF 组码 72)。
    enum class MTextDrawingDirection : uint8_t
    {
        LeftToRight = 1,
        TopToBottom = 3,
        ByStyle     = 5,
    };

    // 行距样式(DXF 组码 73);因子见组码 44。
    enum class MTextLineSpacing : uint8_t
    {
        AtLeast = 1,   // 至少(按内容增大)
        Exact   = 2,   // 精确
    };

    // 分栏类型(DXF 组码 75)。
    enum class MTextColumnType : uint8_t
    {
        None    = 0,
        Static  = 1,
        Dynamic = 2,
    };

    // 多行矢量文字实体（纯数据层）。
    // 字形解析与排版在 Document 层的 DrawContext::EmitMText() 中完成。
    // m_text 保留含内联格式码的原始内容以保证 DXF 往返无损;度量时用去码后的纯文本。
    class MTextEntity : public Entity
    {
    public:
        explicit MTextEntity(ObjectID id) : Entity(id) {}

        MTextEntity(ObjectID objectId, FontStyleId styleId, std::string text, Math::Point3 postion,double h = 1,double r = 0,double w=1)
            : Entity(objectId)
            , m_styleId(styleId)
            , m_text(text)
            , m_position(postion)
            , m_height(h)
            , m_rotation(r)
            , m_boxWidth(w)
        {
        }
        // --- 赋值 ---
        void SetText(const std::string& text)      { m_text = text; }
        void SetStyleId(FontStyleId id)            { m_styleId = id; }
        void SetPosition(const Math::Point3& pos)  { m_position = pos; }
        void SetHeight(double h)                   { m_height = h; }
        void SetRotation(double r)                 { m_rotation = r; }
        void SetBoxWidth(double w)                 { m_boxWidth = w; }   // 参考矩形宽度(组码 41)

        // 附着点 / 方向 / 行距
        void SetAttachment(MTextAttachment a)              { m_attach = a; }
        void SetDrawingDirection(MTextDrawingDirection d)  { m_drawDir = d; }
        void SetLineSpacing(MTextLineSpacing s, double f)  { m_lineSpacingStyle = s; m_lineSpacingFactor = f; }
        void SetDefinedHeight(double h)                    { m_definedHeight = h; }   // 组码 46

        // 分栏(组码 75/76/78/48/49)
        void SetColumns(MTextColumnType type, int count, double width, double gutter)
        {
            m_columnType   = type;
            m_columnCount  = count > 0 ? count : 1;
            m_columnWidth  = width;
            m_columnGutter = gutter;
        }
        void SetColumnHeights(std::vector<double> heights) { m_columnHeights = std::move(heights); }

        // --- 读取 ---
        const std::string&  GetText()     const { return m_text; }   // 原始(含内联格式码)
        std::string         GetPlainText() const { return StripInlineCodes(m_text); }
        FontStyleId         GetStyleId()  const { return m_styleId; }
        const Math::Point3& GetPosition() const { return m_position; }
        double              GetHeight()   const { return m_height; }
        double              GetRotation() const { return m_rotation; }
        double              GetBoxWidth() const { return m_boxWidth; }

        MTextAttachment       GetAttachment()        const { return m_attach; }
        MTextDrawingDirection GetDrawingDirection()  const { return m_drawDir; }
        MTextLineSpacing      GetLineSpacingStyle()  const { return m_lineSpacingStyle; }
        double                GetLineSpacingFactor() const { return m_lineSpacingFactor; }
        double                GetDefinedHeight()     const { return m_definedHeight; }

        MTextColumnType            GetColumnType()    const { return m_columnType; }
        int                        GetColumnCount()   const { return m_columnCount; }
        double                     GetColumnWidth()   const { return m_columnWidth; }
        double                     GetColumnGutter()  const { return m_columnGutter; }
        const std::vector<double>& GetColumnHeights() const { return m_columnHeights; }

        // ── 内联格式码处理 ───────────────────────────────────────────────────
        // 去除 MTEXT 内联格式码,返回用于度量/纯文本提取的内容。处理:
        //   \P 段落换行 → '\n';\~ 不换行空格 → ' ';\\ \{ \} 转义字符;{ } 分组;
        //   带参数(以 ';' 结束)的码:\f \F(字体) \H(字高) \C \c(颜色) \W(宽度因子)
        //                          \Q(倾斜) \T(字间距) \A(对齐) \p(段落) \S(堆叠);
        //   开关码(无参数):\L \l \O \o \K \k \N。
        static std::string StripInlineCodes(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); )
            {
                const char ch = s[i];
                if (ch == '{' || ch == '}') { ++i; continue; }   // 分组括号

                if (ch == '\\' && i + 1 < s.size())
                {
                    const char c = s[i + 1];
                    switch (c)
                    {
                    case 'P': case 'X': out.push_back('\n'); i += 2; continue;  // 段落 / 换行
                    case '~':           out.push_back(' ');  i += 2; continue;  // 不换行空格
                    case '\\':          out.push_back('\\'); i += 2; continue;
                    case '{':           out.push_back('{');  i += 2; continue;
                    case '}':           out.push_back('}');  i += 2; continue;

                    // 带参数:跳到分号(含)为止
                    case 'f': case 'F': case 'H': case 'C': case 'c':
                    case 'W': case 'Q': case 'T': case 'A': case 'p': case 'S':
                    {
                        i += 2;
                        while (i < s.size() && s[i] != ';') ++i;
                        if (i < s.size()) ++i;   // 跳过 ';'
                        continue;
                    }

                    // 开关码(无参数)
                    case 'L': case 'l': case 'O': case 'o':
                    case 'K': case 'k': case 'N':
                        i += 2; continue;

                    default:
                        i += 2; continue;   // 未知转义,丢弃
                    }
                }

                out.push_back(ch);
                ++i;
            }
            return out;
        }

        // --- Entity 接口 ---

        // 近似包围盒:按附着点放置文字块。宽高由去码后的纯文本估算。
        // 宽度估算:按 UTF-8 码点计数,ASCII 0.6 字高、多字节(CJK)1.0 字高。
        // 有旋转时绕绘制原点(首行底部,与 EmitMText 一致)旋转四角后取轴对齐盒。
        AABB GetBoundingBox() const override
        {
            double w = 0.0, h = 0.0;
            MeasureBlock(w, h);

            // 空文字块给一个最小尺寸,避免退化为零面积。
            if (w <= 0.0) w = m_height;
            if (h <= 0.0) h = m_height;

            const Math::Point3 tl = AnchorTopLeft(w, h);

            if (std::abs(m_rotation) < 1e-12)
                return AABB(
                    { tl.x,     tl.y - h, tl.z },
                    { tl.x + w, tl.y,     tl.z }
                );

            // 旋转基准 = Draw 传给 EmitMText 的原点(左上角下移一个字高)
            const Math::Point3 pivot{ tl.x, tl.y - m_height, tl.z };
            const double c = std::cos(m_rotation);
            const double s = std::sin(m_rotation);

            // 文字块矩形相对 pivot:x∈[0,w],y∈[m_height-h, m_height]
            const double xs[2] = { 0.0, w };
            const double ys[2] = { m_height - h, m_height };

            double minX =  1e300, minY =  1e300;
            double maxX = -1e300, maxY = -1e300;
            for (double x : xs)
                for (double y : ys)
                {
                    const double rx = x * c - y * s;
                    const double ry = x * s + y * c;
                    minX = std::min(minX, rx); maxX = std::max(maxX, rx);
                    minY = std::min(minY, ry); maxY = std::max(maxY, ry);
                }

            return AABB(
                { pivot.x + minX, pivot.y + minY, pivot.z },
                { pivot.x + maxX, pivot.y + maxY, pivot.z }
            );
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_text.empty()) return;

            const auto& color = isSelected ? IDrawSink::kSelectionColor
                              : isHovered  ? IDrawSink::kHoverColor
                              : ResolveDrawColor(sink);

            double w = 0.0, h = 0.0;
            MeasureBlock(w, h);
            if (w <= 0.0) w = m_height;
            if (h <= 0.0) h = m_height;

            // 按附着点求文字块左上角;EmitMText 原点在首行底部,故下移一个字高。
            // 排版层不解析内联格式码,故送入去码后的纯文本(\P → '\n' 等)。
            const Math::Point3 tl = AnchorTopLeft(w, h);
            const Math::Point3 drawPos{ tl.x, tl.y - m_height, tl.z };
            sink.EmitMText(drawPos, GetPlainText(), m_styleId, m_height, m_rotation, m_boxWidth, color);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MTextEntity>(newId);
            e->SetAttr(GetAttr());
            e->m_text              = m_text;
            e->m_styleId           = m_styleId;
            e->m_position          = m_position;
            e->m_height            = m_height;
            e->m_rotation          = m_rotation;
            e->m_boxWidth          = m_boxWidth;
            e->m_attach            = m_attach;
            e->m_drawDir           = m_drawDir;
            e->m_lineSpacingStyle  = m_lineSpacingStyle;
            e->m_lineSpacingFactor = m_lineSpacingFactor;
            e->m_definedHeight     = m_definedHeight;
            e->m_columnType        = m_columnType;
            e->m_columnCount       = m_columnCount;
            e->m_columnWidth       = m_columnWidth;
            e->m_columnGutter      = m_columnGutter;
            e->m_columnHeights     = m_columnHeights;
            return e;
        }

        DECLARE_RUNTIME_TYPE(MTextEntity, Entity)

    private:
        // 估算一段纯文本的显示宽度(WCS):按 UTF-8 码点计数,
        // ASCII 计 0.6 字高,多字节字符(CJK 等)计 1.0 字高。
        double EstimateSegWidth(const char* s, const char* e) const
        {
            double w = 0.0;
            while (s < e)
            {
                const unsigned char c = static_cast<unsigned char>(*s);
                int len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3
                        : ((c >> 3) == 0x1E) ? 4 : 1;
                w += (len == 1 ? 0.6 : 1.0) * m_height;
                s += len;
            }
            return w;
        }

        // 估算文字块宽 w 与总高 h(均为 WCS 单位,未含旋转)。
        void MeasureBlock(double& w, double& h) const
        {
            const double lineH = m_height * (m_lineSpacingFactor > 0.0 ? m_lineSpacingFactor : 1.0);
            const std::string plain = StripInlineCodes(m_text);

            int    totalLines = 0;
            double maxW       = 0.0;

            const char* p    = plain.c_str();
            const char* pEnd = p + plain.size();
            const char* seg  = p;

            auto processSeg = [&](const char* s, const char* e)
            {
                const double segW = EstimateSegWidth(s, e);
                if (m_boxWidth > 0.0 && segW > m_boxWidth)
                    totalLines += static_cast<int>(std::ceil(segW / m_boxWidth));   // 自动折行(保守上界)
                else
                {
                    totalLines += 1;
                    if (segW > maxW) maxW = segW;
                }
            };

            if (plain.empty())
            {
                totalLines = 0;
            }
            else
            {
                for (const char* c = p; c <= pEnd; ++c)
                {
                    if (c == pEnd || *c == '\n')
                    {
                        processSeg(seg, c);
                        seg = c + 1;
                    }
                }
            }

            // 单栏宽:有参考宽度用之,否则取最长行。
            double singleW = (m_boxWidth > 0.0) ? m_boxWidth : maxW;
            double singleH = totalLines * lineH;

            // 分栏:宽度为各栏并排 + 栏间距;高度取栏高(若给定)或单栏估算高。
            if (m_columnType != MTextColumnType::None && m_columnCount > 1 && m_columnWidth > 0.0)
            {
                w = m_columnCount * m_columnWidth + (m_columnCount - 1) * m_columnGutter;
                double colH = 0.0;
                for (double ch : m_columnHeights) colH = std::max(colH, ch);
                h = colH > 0.0 ? colH : singleH;
            }
            else
            {
                w = singleW;
                h = singleH;
            }
        }

        // 由附着点求文字块左上角(WCS,旋转前)。
        Math::Point3 AnchorTopLeft(double w, double h) const
        {
            double tlx = m_position.x;
            double tly = m_position.y;

            switch (m_attach)
            {
            case MTextAttachment::TopLeft:    case MTextAttachment::MiddleLeft:  case MTextAttachment::BottomLeft:
                tlx = m_position.x;          break;
            case MTextAttachment::TopCenter:  case MTextAttachment::MiddleCenter: case MTextAttachment::BottomCenter:
                tlx = m_position.x - w * 0.5; break;
            case MTextAttachment::TopRight:   case MTextAttachment::MiddleRight:  case MTextAttachment::BottomRight:
                tlx = m_position.x - w;       break;
            }

            switch (m_attach)
            {
            case MTextAttachment::TopLeft:    case MTextAttachment::TopCenter:    case MTextAttachment::TopRight:
                tly = m_position.y;           break;
            case MTextAttachment::MiddleLeft: case MTextAttachment::MiddleCenter: case MTextAttachment::MiddleRight:
                tly = m_position.y + h * 0.5; break;
            case MTextAttachment::BottomLeft: case MTextAttachment::BottomCenter: case MTextAttachment::BottomRight:
                tly = m_position.y + h;       break;
            }

            return { tlx, tly, m_position.z };
        }

        std::string  m_text;
        FontStyleId  m_styleId  = 0;       // 文字样式表 ID（Scene::GetTextStyleTable）
        Math::Point3 m_position = {};
        double       m_height   = 1.0;
        double       m_rotation = 0.0;
        double       m_boxWidth = 0.0;    // 参考矩形宽度(组码 41);0 = 不限宽

        // ── 附着 / 方向 / 行距 ─────────────────────────────────────────────────
        MTextAttachment       m_attach            = MTextAttachment::TopLeft;          // 组码 71
        MTextDrawingDirection m_drawDir           = MTextDrawingDirection::ByStyle;    // 组码 72
        MTextLineSpacing      m_lineSpacingStyle  = MTextLineSpacing::AtLeast;         // 组码 73
        double                m_lineSpacingFactor = 1.0;                               // 组码 44
        double                m_definedHeight     = 0.0;                               // 组码 46

        // ── 分栏 ───────────────────────────────────────────────────────────────
        MTextColumnType     m_columnType   = MTextColumnType::None;   // 组码 75
        int                 m_columnCount  = 1;                        // 组码 76
        double              m_columnWidth  = 0.0;                      // 组码 48
        double              m_columnGutter = 0.0;                      // 组码 49
        std::vector<double> m_columnHeights;                           // 组码 50(静态分栏各栏高)
    };
}
