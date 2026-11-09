#pragma once

#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Object/Object.hpp"
#include "Core/TextCodes.h"
#include "Core/MTextFormat.h"
#include <algorithm>
#include <array>
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
    // 格式码在 ParseMTextFormat（Core/MTextFormat.h）切成段，字形解析与排版在 DrawContext::EmitRichMText 中完成。
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
        // 导出时写成开头的内联格式码 \W0.7;
        void SetWidthFactor(double f)              { m_widthFactor = f > 0.0 ? f : 0.0; }

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
        double              GetWidthFactor() const { return m_widthFactor; }

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
        //                          \Q(倾斜) \T(字间距) \A(对齐) \p(段落);\S(堆叠)留下内容;
        //   开关码(无参数):\L \l \O \o \K \k \N;\U+XXXX 还原为对应字符。
        // keepDecorations:下划线 \L、上划线 \O、删除线 \K 换成排版流里的开关字符(Core/TextCodes.h),
        //   按 { } 分组恢复,只在状态变化处写;绘制时用,度量/纯文本不用。
        // spans：每个可见片段在结果与原文里的范围（原位编辑按它把改动对回原文）
        struct PlainSpan { size_t plainBegin, plainEnd, rawBegin, rawEnd; };

        static std::string StripInlineCodes(const std::string& s, bool keepDecorations = false, std::vector<PlainSpan>* spans = nullptr)
        {
            std::string out;
            out.reserve(s.size());
            size_t i = 0, i0 = 0, o0 = 0;
            auto flush = [&]
            {
                if (spans && out.size() > o0)
                    spans->push_back({ o0, out.size(), i0, i });
                i0 = i;
                o0 = out.size();
            };

            struct Decor { bool under = false, over = false, strike = false; };
            Decor cur, shown;
            std::vector<Decor> groups;
            auto put = [&](char ch)
            {
                if (keepDecorations)
                {
                    if (cur.under  != shown.under)  out.push_back(kUnderlineToggle);
                    if (cur.over   != shown.over)   out.push_back(kOverlineToggle);
                    if (cur.strike != shown.strike) out.push_back(kStrikeToggle);
                    shown = cur;
                }
                out.push_back(ch);
            };

            for (; i < s.size(); )
            {
                flush();
                const char ch = s[i];
                if (ch == '{') { groups.push_back(cur); ++i; continue; }   // 分组:格式只在组内有效
                if (ch == '}')
                {
                    if (!groups.empty()) { cur = groups.back(); groups.pop_back(); }
                    ++i; continue;
                }

                if (ch == '\\' && i + 1 < s.size())
                {
                    const char c = s[i + 1];
                    switch (c)
                    {
                    case 'P': case 'X': put('\n'); i += 2; continue;  // 段落 / 换行
                    case '~':           put(' ');  i += 2; continue;  // 不换行空格
                    case '\\':          put('\\'); i += 2; continue;
                    case '{':           put('{');  i += 2; continue;
                    case '}':           put('}');  i += 2; continue;

                    // 堆叠（分数、上下标）：留下内容，写成 上/下（上下对齐的写成 上 下）
                    case 'S':
                    {
                        i += 2;
                        std::string top, bottom, *part = &top;
                        char type = 0;
                        while (i < s.size() && s[i] != ';')
                        {
                            if (s[i] == '\\' && i + 1 < s.size()) { *part += s[i + 1]; i += 2; continue; }
                            if (!type && (s[i] == '/' || s[i] == '#' || s[i] == '^')) { type = s[i++]; part = &bottom; continue; }
                            *part += s[i++];
                        }
                        if (i < s.size()) ++i;
                        const std::string shown = top.empty() || bottom.empty() ? top + bottom : top + (type == '^' ? " " : "/") + bottom;
                        for (char b : shown) put(b);
                        continue;
                    }

                    // 带参数:跳到分号(含)为止
                    case 'f': case 'F': case 'H': case 'C': case 'c':
                    case 'W': case 'Q': case 'T': case 'A': case 'p':
                    {
                        i += 2;
                        while (i < s.size() && s[i] != ';') ++i;
                        if (i < s.size()) ++i;   // 跳过 ';'
                        continue;
                    }

                    // 开关码(无参数):下划线 / 上划线 / 删除线的开与关
                    case 'L': cur.under  = true;  i += 2; continue;
                    case 'l': cur.under  = false; i += 2; continue;
                    case 'O': cur.over   = true;  i += 2; continue;
                    case 'o': cur.over   = false; i += 2; continue;
                    case 'K': cur.strike = true;  i += 2; continue;
                    case 'k': cur.strike = false; i += 2; continue;
                    case 'N': i += 2; continue;

                    // \U+XXXX:代码页外的字符(见 Core/TextCodes.h),还原为 UTF-8
                    case 'U': case 'u':
                        if (char32_t cp = 0; ParseUnicodeEscape(s, i, cp))
                        {
                            std::string utf8;
                            AppendUtf8(utf8, cp);
                            for (char b : utf8) put(b);
                            i += 7; continue;
                        }
                        i += 2; continue;

                    default:
                        i += 2; continue;   // 未知转义,丢弃
                    }
                }

                put(ch);
                ++i;
            }
            flush();
            return out;
        }

        // 原位编辑：编辑框里显示去码的文字（StripInlineCodes），改完按与原文字的差异只替换改动的那段，
        // 改动之外的格式码、分组照旧；新写入的反斜杠、花括号转义，换行写成 \P
        static std::string ApplyPlainEdit(const std::string& raw, const std::string& edited)
        {
            std::vector<PlainSpan> spans;
            const std::string old = StripInlineCodes(raw, false, &spans);
            if (old == edited)
                return raw;

            size_t p = 0;
            while (p < old.size() && p < edited.size() && old[p] == edited[p]) ++p;
            size_t q = 0;
            while (q < old.size() - p && q < edited.size() - p && old[old.size() - 1 - q] == edited[edited.size() - 1 - q]) ++q;

            auto cont = [](const std::string& t, size_t k) { return k < t.size() && (static_cast<unsigned char>(t[k]) & 0xC0) == 0x80; };
            size_t a = p, b = old.size() - q;
            while (a > 0 && cont(old, a)) --a;
            while (cont(old, b)) ++b;
            for (const auto& sp : spans)
            {
                if (sp.plainBegin < a && a < sp.plainEnd) a = sp.plainBegin;
                if (sp.plainBegin < b && b < sp.plainEnd) b = sp.plainEnd;
            }
            const size_t nb = edited.size() - (old.size() - b);

            std::string ins;
            for (size_t k = a; k < nb; ++k)
            {
                const char c = edited[k];
                if      (c == '\\') ins += "\\\\";
                else if (c == '{')  ins += "\\{";
                else if (c == '}')  ins += "\\}";
                else if (c == '\n') ins += "\\P";
                else if (c != '\r') ins += c;
            }

            std::string out;
            size_t rawPos = 0;
            bool   placed = false;
            for (const auto& sp : spans)
            {
                if (sp.plainEnd <= a || sp.plainBegin >= b)
                    continue;
                out.append(raw, rawPos, sp.rawBegin - rawPos);
                if (!placed) { out += ins; placed = true; }
                rawPos = sp.rawEnd;
            }
            if (!placed)
            {
                size_t at = raw.size();
                for (const auto& sp : spans)
                {
                    if (a > 0 && sp.plainEnd == a) { at = sp.rawEnd; break; }
                    if (a == 0) { at = sp.rawBegin; break; }
                }
                return raw.substr(0, at) + ins + raw.substr(at);
            }
            out.append(raw, rawPos, std::string::npos);
            return out;
        }

        // --- Entity 接口 ---

        // 文字实际占的矩形（不是参考框）：左上、右上、右下、左下，已按附着点放置并绕插入点旋转。
        // 宽度按估算的最长行；有行折行或分栏时取参考框宽；在参考框内按对齐方式摆放
        std::array<Math::Point3, 4> GetTextFrame() const
        {
            double w = 0.0, h = 0.0, tw = 0.0;
            MeasureBlock(ParseMTextFormat(m_text, m_height), w, h, &tw);
            if (w <= 0.0) w = m_height;
            if (h <= 0.0) h = m_height;
            if (tw <= 0.0 || tw > w) tw = w;

            const Math::Point3 tl = AnchorTopLeft(w, h);
            const double off = IsLeftAttachment() ? 0.0 : IsRightAttachment() ? w - tw : (w - tw) * 0.5;
            const double x0 = tl.x + off, x1 = x0 + tw, y0 = tl.y - h, y1 = tl.y;
            const double c = std::cos(m_rotation), s = std::sin(m_rotation);
            auto rot = [&](double x, double y) -> Math::Point3
            {
                const double dx = x - m_position.x, dy = y - m_position.y;
                return { m_position.x + dx * c - dy * s, m_position.y + dx * s + dy * c, m_position.z };
            };
            return { rot(x0, y1), rot(x1, y1), rot(x1, y0), rot(x0, y0) };
        }

        // 近似包围盒：GetTextFrame 四角的轴对齐盒（宽度估算：ASCII 0.6 字高、多字节 1.0 字高）
        AABB GetBoundingBox() const override
        {
            const auto f = GetTextFrame();
            Math::Point3 lo = f[0], hi = f[0];
            for (const auto& p : f)
            {
                lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
                hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
            }
            return AABB(lo, hi);
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_text.empty()) return;

            const auto& color = isSelected ? IDrawSink::kSelectionColor
                              : isHovered  ? IDrawSink::kHoverColor
                              : ResolveDrawColor(sink);

            const MTextContent content = ParseMTextFormat(m_text, m_height);
            double w = 0.0, h = 0.0;
            MeasureBlock(content, w, h);

            MTextBlock block;
            block.content      = &content;
            block.plain        = StripInlineCodes(m_text, true);
            block.styleId      = m_styleId;
            block.height       = m_height;
            block.boxWidth     = m_boxWidth;
            block.widthFactor  = m_widthFactor;
            block.lineSpacing  = LineSpacing();
            block.exactSpacing = m_lineSpacingStyle == MTextLineSpacing::Exact;
            block.justify      = IsLeftAttachment() ? 0 : IsRightAttachment() ? 2 : 1;
            block.runColors    = !isSelected && !isHovered;
            block.layerColor   = sink.GetLayerColor(GetAttr().LayerId);

            // 不限宽的居中 / 右对齐按实际排版的宽度、中 / 下附着按实际的高度定起点：估算不含字体的字宽，也不知道折成几行
            const bool needWidth  = m_boxWidth <= 0.0 && !IsLeftAttachment();
            const bool needHeight = !IsTopAttachment();
            if (m_columnType == MTextColumnType::None && (needWidth || needHeight))
                if (double mw = 0.0, mh = 0.0; sink.MeasureRichMText(block, mw, mh))
                {
                    if (needWidth && mw > 0.0) w = mw;
                    if (mh > 0.0) h = mh;
                }
            if (w <= 0.0) w = m_height;
            if (h <= 0.0) h = m_height;
            sink.EmitRichMText(DrawOrigin(w, h), block, m_rotation, color);
        }

        double LineSpacing() const { return m_lineSpacingFactor > 0.0 ? m_lineSpacingFactor : 1.0; }

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
            e->m_widthFactor       = m_widthFactor;
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
        // 估算一段文字的宽度（字高的倍数）：按 UTF-8 码点计数，ASCII 计 0.6，多字节字符（CJK 等）计 1.0
        static double EstimateWidth(const std::string& text)
        {
            double w = 0.0;
            for (size_t i = 0; i < text.size(); )
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const int len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
                w += len == 1 ? 0.6 : 1.0;
                i += static_cast<size_t>(len);
            }
            return w;
        }

        // 估算文字块宽 w 与总高 h（首行顶到末行基线，WCS 单位，未含旋转）。各段按自己的字高、宽度因子，
        // 行距按 MTextLinePitch（与 TextLayoutEngine::LayoutRich 同一规则，堆叠按底对齐）；放不下时按宽度比估算折成几行
        void MeasureBlock(const MTextContent& content, double& w, double& h, double* textW = nullptr) const
        {
            const bool   exact    = m_lineSpacingStyle == MTextLineSpacing::Exact;
            const double entityWf = m_widthFactor > 0.0 ? m_widthFactor : 1.0;

            int    totalLines = 0;
            bool   wrapped    = false;
            double maxW = 0.0, singleH = 0.0, prevH = 0.0, prevDescent = 0.0;
            auto addLine = [&](double lineH, double ascent, double descent)
            {
                singleH += totalLines == 0 ? (exact ? m_height : ascent)
                                           : MTextLinePitch(m_height, LineSpacing(), exact, prevH, prevDescent, ascent);
                prevH = lineH;
                prevDescent = descent;
                ++totalLines;
            };

            const auto& runs = content.runs;
            size_t r = 0;
            while (r < runs.size())
            {
                const MTextRun& head = runs[r];
                const double indent = (head.indentLeft + std::max(0.0, head.indentFirst)) * m_height;
                double segW = 0.0, paraH = 0.0, stackTop = 0.0;
                bool   hasStack = false;
                for (; r < runs.size() && runs[r].kind != MTextRun::Kind::Break; ++r)
                {
                    const MTextRun& run = runs[r];
                    const double wf = (run.widthFactor > 0.0 ? run.widthFactor : entityWf * run.widthScale) * run.tracking;
                    double est = EstimateWidth(run.text);
                    if (run.kind == MTextRun::Kind::Stack)
                    {
                        est = run.stackType == '#' ? est + EstimateWidth(run.bottom) : std::max(est, EstimateWidth(run.bottom));
                        stackTop = std::max(stackTop, (run.bottom.empty() ? 0.0 : run.height) + run.height / 3.0 + (run.text.empty() ? 0.0 : run.height));
                        hasStack = true;
                    }
                    segW  += est * run.height * m_height * wf;
                    paraH  = std::max(paraH, run.height);
                }
                if (paraH <= 0.0) paraH = head.height;
                const double lineH = paraH * m_height;
                const double avail = m_boxWidth - indent;
                const int    count = m_boxWidth > 0.0 && avail > 0.0 && segW > avail ? static_cast<int>(std::ceil(segW / avail)) : 1;
                for (int k = 0; k < count; ++k)
                    addLine(lineH, std::max(lineH, stackTop * m_height), hasStack ? 0.0 : lineH / 3.0);
                if (count == 1) maxW = std::max(maxW, indent + segW);
                else            wrapped = true;
                if (r < runs.size()) ++r;   // 跳过换段
            }

            // 单栏宽:有参考宽度用之,否则取最长行。
            double singleW = (m_boxWidth > 0.0) ? m_boxWidth : maxW;

            // 分栏:宽度为各栏并排 + 栏间距;高度取栏高(若给定)或单栏估算高。
            if (m_columnType != MTextColumnType::None && m_columnCount > 1 && m_columnWidth > 0.0)
            {
                w = m_columnCount * m_columnWidth + (m_columnCount - 1) * m_columnGutter;
                double colH = 0.0;
                for (double ch : m_columnHeights) colH = std::max(colH, ch);
                h = colH > 0.0 ? colH : singleH;
                if (textW) *textW = w;
            }
            else
            {
                w = singleW;
                h = singleH;
                if (textW) *textW = wrapped ? singleW : maxW;
            }
        }

        bool IsLeftAttachment() const
        {
            return m_attach == MTextAttachment::TopLeft || m_attach == MTextAttachment::MiddleLeft || m_attach == MTextAttachment::BottomLeft;
        }
        bool IsRightAttachment() const
        {
            return m_attach == MTextAttachment::TopRight || m_attach == MTextAttachment::MiddleRight || m_attach == MTextAttachment::BottomRight;
        }
        bool IsTopAttachment() const
        {
            return m_attach == MTextAttachment::TopLeft || m_attach == MTextAttachment::TopCenter || m_attach == MTextAttachment::TopRight;
        }

        // 由附着点求文字块左上角(WCS,旋转前)。
        // EmitMText 的原点(首行基线起点):按附着点求文字块左上角、下移一个字高,
        // 这段偏移在文字自身的坐标系里,随文字绕插入点旋转(以前没转,旋转 180° 的文字差出两个字高)
        Math::Point3 DrawOrigin(double w, double h) const
        {
            const Math::Point3 tl = AnchorTopLeft(w, h);
            const double dx = tl.x - m_position.x, dy = tl.y - m_height - m_position.y;
            const double c = std::cos(m_rotation), s = std::sin(m_rotation);
            return { m_position.x + dx * c - dy * s, m_position.y + dx * s + dy * c, m_position.z };
        }

        // 未旋转时文字块的左上角
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
        double       m_widthFactor = 0.0;   // 宽度因子，0 = 按样式

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
