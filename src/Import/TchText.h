#pragma once
#include "Import/TchSymbol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    // 天正文字的样式规则（由 CadExchange 按原图样式判断后传入）。
    // 天正在文字样式上挂 _TCH 扩展数据（两个 1040：a、b）：SHX 样式的西文字高 = a × 名义字高，宽度因子 = 样式宽度 × b / a，
    // 天正排布的西文步进随 b 等比（以 b = 0.6 时的标定值为准），汉字字高与步进不变；没有扩展数据时按 (1, 1)。
    // 核对：_TCH_DIM (0.85, 0.6) 即原先写死的 0.85、0.6 / 0.85；139 户型图 _TCH_DIM (0.8, 0.6)、宽 0.7 → 西文字高 0.8、宽 0.525、步进 × 0.7；
    // 同图 Standard（simplex、宽 0.7、无扩展数据）→ 字高 1.0、宽 0.7、西文步进 0.6667 × 名义字高；
    // 总图 _TWT_SERIAL (1, 0.8)、宽 0.6 → 字高 1.0、宽 0.48；14#~16#楼 (0.7, 0.7)、宽 0.7 → 字高 0.7、宽 0.7。
    // 汉字字高：大字体 gbcbig 为 0.96454 × 名义字高；hztxt 为 1.0（139 户型图 _TWT_PIPEDN、_TWT_DIM 的分解结果核对）。
    // TrueType 样式不受影响：字高 × 0.75527、宽度用样式自身的
    struct TchTextStyleRule
    {
        bool   TrueType = false;
        bool   SymbolFont = false;                      // SHX 主字体不是 gbenor / gbeitc（如 simplex）：天正按注释符号的方式排布
        double StyleWidth = 1.0;
        double AsciiHeight = 1.0, AsciiWidth = 1.0;     // _TCH 扩展数据的 a、b
        double WideHeight = 0.96454;                    // SHX 样式的汉字字高（× 名义字高），随大字体

        bool Symbol() const { return SymbolFont && !TrueType; }
        double HeightFactor() const { return TrueType ? 0.75527 : AsciiHeight; }
        double WidthFactor() const { return TrueType ? 0.0 : StyleWidth * AsciiWidth / AsciiHeight; }   // 0：用样式自身的
        // 西文段的宽度因子：(1, 1) 时为 0（用样式自身的）
        double AsciiWidthFactor() const { return TrueType || (AsciiHeight == 1.0 && AsciiWidth == 1.0) ? 0.0 : WidthFactor(); }
        double AsciiStep() const { return AsciiWidth / 0.6; }  // 西文步进相对 b = 0.6 标定值的倍数
    };

    // TCH_TEXT 天正单行文字（CADReader TDbText 读取函数 0x6f4070，版本 < 3 为明文分支）
    struct TchText
    {
        double X = 0, Y = 0;
        double Height = 3.5;        // 名义字高（× 比例为图面字高；含汉字时再乘 0.96454，与图名、箭头文字相同）
        double Rotation = 0;
        double Scale = 100;
        int    Attachment = 7;      // 对齐方式，同 MTEXT 附着点（1 左上 … 7 左下、8 中下、9 右下）
        std::string Value;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
        // TCH_MTEXT 天正多行文字（内核 TDbMText::dwgInFields：先读 TDbText，再 RC 版本 ≤ 2，版本 2 时 RC 密钥，2 个 BD）
        bool   Multiline = false;
        double BoxWidth = 0;        // 换行宽度（× 比例为图面宽度）
        double LineSpacing = 0;
    };

    // 天正基类、RC 版本（1～2）、句柄流：文字样式；文字流：内容；3BD 插入点、BD 字高、BD 转角、RC 对齐、版本 ≥ 2 再一个 RC
    inline bool DecodeText(const MiniDWG::UnknownEntity& entity, TchText& text, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        // TCH_MTEXTNEW（天正给排水，TDbMTextNew::dwgInFields）：TDbMText 之后再 B、2 × BD，显示同 TCH_MTEXT
        const bool multilineNew = entity.Raw.DxfName == "TCH_MTEXTNEW";
        const bool multiline = entity.Raw.DxfName == "TCH_MTEXT" || multilineNew;
        if (entity.Raw.DxfName != "TCH_TEXT" && !multiline) return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native text");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native text version");
        TchText t;
        t.Scale = base.Scale;
        t.X = r.ReadBitDouble(); t.Y = r.ReadBitDouble(); r.ReadBitDouble();
        t.Height = r.ReadBitDouble();
        t.Rotation = r.ReadBitDouble();
        t.Attachment = r.ReadByte();
        if (version >= 2) r.ReadByte();
        if (multiline)
        {
            const int mversion = r.ReadByte();
            if (mversion > 2) return fail("unsupported native multiline text version");
            if (mversion > 1 && r.ReadByte() >= 16) return fail("encrypted native multiline text");
            t.Multiline = true;
            t.BoxWidth = r.ReadBitDouble();
            t.LineSpacing = r.ReadBitDouble();
            if (multilineNew)
            {
                r.ReadBit();
                r.ReadBitDouble(); r.ReadBitDouble();
            }
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native text fields do not consume the payload exactly");
        if (!Detail::Finite(t.X) || !Detail::Finite(t.Y) || !Detail::Finite(t.Rotation) || !(t.Height > 0) || !(t.Scale > 0))
            return fail("invalid native text settings");
        // 天正对齐枚举（内核 TGGiExplodeDrawBase::Align2Base）：1～9 同 MTEXT 附着点（7～9 以文字底部为准），
        // 10～12 为基线左 / 中 / 右，13 为文字全高的正中。基线与底部只差下伸部分，按 7～9 显示，13 按正中（5）显示
        if (t.Attachment >= 10 && t.Attachment <= 13) t.Attachment = t.Attachment == 13 ? 5 : t.Attachment - 3;
        if (t.Attachment < 1 || t.Attachment > 9) return fail("unsupported text alignment");
        Detail::TextStream stream(data);
        for (int i = 0; i < base.TextCount; ++i) stream.Next();
        t.Value = stream.Next();
        if (stream.Reader.Failed()) return fail("native text stream is incomplete");
        // TCH_MTEXTNEW 的正文夹有格式码 $|字高|0$（139 户型水施图的说明为 $|3.6|0$），不显示；字高变化不处理
        if (multilineNew)
            for (std::size_t at = t.Value.find("$|"); at != std::string::npos; at = t.Value.find("$|", at))
            {
                const std::size_t end = t.Value.find('$', at + 2);
                if (end == std::string::npos) break;
                if (t.Value.find_first_not_of("0123456789.|-", at + 2) == end) t.Value.erase(at, end + 1 - at);
                else at += 2;
            }
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        t.Style = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed()) return fail("native text handle stream is incomplete");
        text = std::move(t);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // SHX 样式：天正 2014 分解为西文、汉字交替的 TEXT：西文字高 = 名义字高 × 比例（样式自身宽度因子），汉字 × 0.96454（随大字体），颜色随层。
    // 左下对齐时各段从插入点起首尾相接；其他对齐方式只有纯西文或纯汉字时按附着点显示，混排时按水平对齐近似。
    // TrueType 样式（trueType，如“宋体”）：字高 = 名义字高 × 0.75527，用样式宽度因子；天正排布的步进汉字 = 字高、西文 = 0.5 × 字高
    //（宋体宽度因子 0.7 时与字形步进一致；8#楼封面目录的天正显示与分解结果核对）。
    // 混排时各段按附着点的水平、竖直位置排布（竖直：上 / 中按字高下移）
    // SHX 样式的西文字高、宽度因子按样式的 _TCH 扩展数据（TchTextStyleRule）；符号字体（如 _TCH_DIM、_TCH_WINDOW 的 simplex）
    // 按注释符号的方式排布（8#楼立剖面、详图，139 户型图的分解结果核对）
    inline TchGraphics BuildText(const TchText& t, const TchTextStyleRule& rule = {})
    {
        TchGraphics g;
        const double nominal = t.Height * t.Scale;
        bool ascii = false, wide = false;
        for (unsigned char c : t.Value) (c >= 0x80 ? wide : ascii) = true;
        const bool trueType = rule.TrueType;
        const double styleWidth = rule.StyleWidth;
        const double h = trueType ? nominal * 0.75527 : nominal;
        const bool symbol = rule.Symbol();
        const double asciiScale = rule.HeightFactor(), asciiWidth = rule.AsciiWidthFactor();
        if (!(ascii && wide))
        {
            g.Texts.push_back({{t.X, t.Y}, trueType ? h : nominal * (wide ? rule.WideHeight : asciiScale), t.Rotation, t.Value, t.Attachment, 0, 0,
                               0, wide ? 0.0 : asciiWidth});
            return g;
        }
        const int column = (t.Attachment - 1) % 3;      // 0 左、1 中、2 右
        const int row = (t.Attachment - 1) / 3;         // 0 上、1 中、2 下（基线）
        const double c = std::cos(t.Rotation), s = std::sin(t.Rotation);
        const double drop = row == 0 ? h : row == 1 ? h / 2 : 0.0;
        const double x = t.X + drop * s, y = t.Y - drop * c;
        if (!trueType)
        {
            // 天正排布步进：符号字体为西文 0.5714、汉字 0.69 × 名义字高，均乘样式宽度因子（8#楼详图的窗号、标高说明核对）
            if (symbol)
                Detail::AddRuns(g, t.Value, nominal, x, y, t.Rotation, column, 0, 0, 0, asciiScale, asciiWidth, 0.5714 * styleWidth * rule.AsciiStep(),
                                0.69 * styleWidth, true, rule.WideHeight);
            else Detail::AddRuns(g, t.Value, nominal, x, y, t.Rotation, column, 0, 0, 0, asciiScale, asciiWidth, 0.604, 0.69, false, rule.WideHeight);
            return g;
        }
        struct Run { std::string Value; bool Wide; double Width; };
        std::vector<Run> runs;
        double total = 0;
        for (std::size_t i = 0; i < t.Value.size();)
        {
            const unsigned char ch = static_cast<unsigned char>(t.Value[i]);
            const std::size_t len = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
            const bool w = ch >= 0x80;
            if (runs.empty() || runs.back().Wide != w) runs.push_back({{}, w, 0});
            runs.back().Value.append(t.Value, i, len);
            runs.back().Width += w ? h : 0.5 * h;
            total += w ? h : 0.5 * h;
            i += len;
        }
        double cursor = column == 1 ? -total / 2 : column == 2 ? -total : 0;
        for (const auto& r : runs)
        {
            if (r.Value.find_first_not_of(' ') != std::string::npos)
                g.Texts.push_back({{x + cursor * c, y + cursor * s}, h, t.Rotation, r.Value, 7, 0, 0});
            cursor += r.Width;
        }
        return g;
    }

    // TCH_MTEXT：插入点为左上角，按换行宽度（BoxWidth × 比例）与原文换行折行，左对齐。
    // 第一行基线在插入点下一个名义字高，行距 = 名义字高 × (LineSpacing − 0.2)。
    // 符号字体（SHX 主字体不是 gbenor / gbeitc）按天正多行文字的实际排布：西文每字步进 = simplex 字宽 × 名义字高 × 样式宽度因子 × 0.8
    //（× b / 0.6，见 TchTextStyleRule），汉字 = 名义字高 × 样式宽度因子 × 0.70213，字高与宽度因子同单行文字；其他字体用单行文字的天正步进，每行交给 BuildText。
    // 折行（与 8#楼平面小说明 43 行的分解结果逐行一致）：连续的西文字符为一个词，整词放得下才放；汉字只要字的中点不超出行宽就放；
    // 避头标点（句号、逗号、右括号等）放不下时连同前一个字移到下一行；原文换行后紧跟避头标点时，该换行不生效。
    // 天正符号 ^1（钢筋符号，形如 Φ）：宽 = 名义字高 × 宽度因子，圆半径 0.21 × 名义字高、圆心高 0.5 × 名义字高，竖线自基线到 0.95 × 名义字高
    inline TchGraphics BuildMultilineText(const TchText& t, const TchTextStyleRule& rule = {})
    {
        const bool trueType = rule.TrueType;
        const double styleWidth = rule.StyleWidth;
        TchGraphics g;
        const double nominal = t.Height * t.Scale;
        const double width = t.BoxWidth * t.Scale;
        const double pitch = nominal * std::max(1.0, t.LineSpacing - 0.2);
        const bool symbol = rule.Symbol();
        auto isRebar = [](const std::string& u) { return u == "^1"; };
        auto isNewline = [](const std::string& u) { return u == "\n"; };
        // 一个字符（或控制码、符号）的步进
        auto advance = [&](const std::string& unit) {
            const unsigned char ch = static_cast<unsigned char>(unit[0]);
            if (isRebar(unit)) return styleWidth * nominal;
            if (symbol)
            {
                if (ch >= 0x80) return 0.70213 * styleWidth * nominal;
                const std::string shown = unit.size() > 1 && unit[0] == '%' ? std::string("%") : unit;    // 控制码按一个字符
                return Detail::DisplayExtent("simplex", shown).Width * 0.8 * styleWidth * rule.AsciiStep() * nominal;
            }
            const double wide = (trueType ? 0.75527 : 0.69) * nominal, ascii = (trueType ? 0.5 * 0.75527 : 0.604) * nominal;
            return ch >= 0x80 ? wide : (ch == '.' || ch == ',') ? 0.2 * ascii : ascii;
        };
        static const std::string kNoLineStart[] = {"。", "，", "、", "；", "：", "？", "！", "）", "》", "」", "』", "】", "”", "’", "…",
                                                   ",", ".", ";", ":", "?", "!", ")"};
        auto noLineStart = [&](const std::string& u) { return std::find(std::begin(kNoLineStart), std::end(kNoLineStart), u) != std::end(kNoLineStart); };

        // 拆成字符单位：段落（换行符、\P）、控制码 %%x / %%nnn、天正符号 ^1、UTF-8 字符
        std::vector<std::string> units;
        for (std::size_t i = 0; i < t.Value.size();)
        {
            const unsigned char ch = static_cast<unsigned char>(t.Value[i]);
            if (ch == '\r' || ch == '\n' || (ch == '\\' && i + 1 < t.Value.size() && (t.Value[i + 1] == 'P' || t.Value[i + 1] == 'p')))
            {
                const bool pair = ch == '\\' || (ch == '\r' && i + 1 < t.Value.size() && t.Value[i + 1] == '\n');
                units.emplace_back("\n");
                i += pair ? 2 : 1;
                continue;
            }
            std::size_t len = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
            if (ch == '%' && i + 2 < t.Value.size() && t.Value[i + 1] == '%')
            {
                len = 3;
                while (len < 5 && i + len < t.Value.size() && std::isdigit(static_cast<unsigned char>(t.Value[i + len])) &&
                       std::isdigit(static_cast<unsigned char>(t.Value[i + 2]))) ++len;
            }
            else if (ch == '^' && i + 1 < t.Value.size() && t.Value[i + 1] == '1')
                len = 2;
            units.push_back(t.Value.substr(i, len));
            i += len;
        }
        for (std::size_t k = 0; k + 1 < units.size();)
            if (isNewline(units[k]) && noLineStart(units[k + 1])) units.erase(units.begin() + static_cast<std::ptrdiff_t>(k));
            else ++k;

        // 折行：词为连续的西文字符（不含空格），其余每个单位自成一词
        auto isAsciiWord = [&](const std::string& u) { return static_cast<unsigned char>(u[0]) < 0x80 && u != " " && !isNewline(u) && !isRebar(u); };
        std::vector<std::vector<std::string>> lines(1);
        double cursor = 0;
        for (std::size_t i = 0; i < units.size();)
        {
            if (isNewline(units[i]))
            {
                lines.emplace_back();
                cursor = 0;
                ++i;
                continue;
            }
            std::size_t end = i + 1;
            if (isAsciiWord(units[i]))
                while (end < units.size() && isAsciiWord(units[end])) ++end;
            double step = 0;
            for (std::size_t k = i; k < end; ++k) step += advance(units[k]);
            // 西文词整词放得下才放；其余单位按中点判断
            const double reach = isAsciiWord(units[i]) ? cursor + step : cursor + step - advance(units[end - 1]) / 2;
            if (width > 0 && !lines.back().empty() && reach > width + 1e-9)
            {
                if (end == i + 1 && noLineStart(units[i]))
                {
                    std::string prev = std::move(lines.back().back());
                    lines.back().pop_back();
                    cursor = advance(prev);
                    lines.push_back({std::move(prev)});
                }
                else
                {
                    lines.emplace_back();
                    cursor = 0;
                }
            }
            for (std::size_t k = i; k < end; ++k) lines.back().push_back(units[k]);
            cursor += step;
            i = end;
        }

        const double c = std::cos(t.Rotation), s = std::sin(t.Rotation);
        for (std::size_t k = 0; k < lines.size(); ++k)
        {
            const double down = nominal + pitch * static_cast<double>(k);
            const double x = t.X + down * s, y = t.Y - down * c;
            // 行内局部坐标（沿行方向 u、垂直方向 v）→ 图面坐标
            auto at = [&](double u, double v) { return TchGraphics::Point{x + u * c - v * s, y + u * s + v * c}; };
            double pos = 0, runStart = 0;
            std::string run;
            bool runWide = false;
            auto flush = [&] {
                if (run.find_first_not_of(' ') != std::string::npos)
                {
                    if (symbol)
                        g.Texts.push_back({at(runStart, 0), runWide ? rule.WideHeight * nominal : rule.HeightFactor() * nominal, t.Rotation, run, 7, 0, 0, 0,
                                           runWide ? 0.0 : rule.AsciiWidthFactor()});
                    else
                    {
                        TchText one = t;
                        one.Multiline = false;
                        one.Value = run;
                        one.Attachment = 7;
                        const auto p = at(runStart, 0);
                        one.X = p.X;
                        one.Y = p.Y;
                        TchGraphics part = BuildText(one, rule);
                        g.Texts.insert(g.Texts.end(), part.Texts.begin(), part.Texts.end());
                    }
                }
                run.clear();
            };
            for (const auto& u : lines[k])
            {
                if (isRebar(u))
                {
                    flush();
                    const double w = advance(u), cx = pos + w / 2;
                    g.Circles.push_back({at(cx, 0.5 * nominal), 0.21 * nominal});
                    g.Lines.push_back({at(cx, 0), at(cx, 0.95 * nominal)});
                    pos += w;
                    continue;
                }
                // 符号字体：西文、汉字交替成段；其他字体整行一段交给 BuildText
                const bool wide = symbol && static_cast<unsigned char>(u[0]) >= 0x80;
                if (!run.empty() && wide != runWide) flush();
                if (run.empty()) { runStart = pos; runWide = wide; }
                run += u;
                pos += advance(u);
                if (symbol && u == " ") flush();    // 天正的西文段在空格后断开（“GB ”“50028-2006”）
            }
            flush();
        }
        return g;
    }
}
