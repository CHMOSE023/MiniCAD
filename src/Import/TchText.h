#pragma once
#include "Import/TchSymbol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{ 
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

    struct TchText
    {
        double X = 0, Y = 0;
        double Height = 3.5;        // 名义字高（× 比例为图面字高；含汉字时再乘 0.96454，与图名、箭头文字相同）
        double Rotation = 0;
        double Scale = 100;
        int    Attachment = 7;      // 对齐方式，同 MTEXT 附着点（1 左上 … 7 左下、8 中下、9 右下）
        std::string Value;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
        bool   Multiline = false;
        double BoxWidth = 0;        // 换行宽度（× 比例为图面宽度）
        double LineSpacing = 0;
    };

    namespace Detail
    { 
        inline int TextAttachment(int align) { return align >= 10 && align <= 13 ? (align == 13 ? 5 : align - 3) : align; } 
        inline bool DecodeTextCipher(const MiniDWG::UnknownEntity& entity, TchText& text, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            if (entity.Raw.DxfName == "TCH_MTEXTNEW") return fail("unsupported native multiline text layout");
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 4) return fail("unsupported native text version");
            f.ReadKey();
            TchText t;
            t.Scale = f.Scale;
            t.Style = f.Handle();
            t.Value = f.Text();
            const auto p = f.Point3();
            t.X = p.X; t.Y = p.Y;
            t.Height = f.Double();
            t.Rotation = f.Double();
            t.Attachment = TextAttachment(f.Byte());
            f.Byte();
            f.Handle(); f.Handle();
            if (entity.Raw.DxfName == "TCH_MTEXT")
            {
                if (f.Main.ReadByte() != 2) return fail("unsupported native multiline text version");
                f.ReadKey();
                t.Multiline = true;
                t.BoxWidth = f.Double();
                t.LineSpacing = f.Double();
            }
            if (!f.Exact()) return fail("native text fields do not consume the payload exactly");
            if (!Finite(t.X) || !Finite(t.Y) || !Finite(t.Rotation) || !(t.Height > 0) || !(t.Scale > 0))
                return fail("invalid native text settings");
            if (t.Attachment < 1 || t.Attachment > 9) return fail("unsupported text alignment");
            text = std::move(t);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeText(const MiniDWG::UnknownEntity& entity, TchText& text, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        const bool multilineNew = entity.Raw.DxfName == "TCH_MTEXTNEW";
        const bool multiline = entity.Raw.DxfName == "TCH_MTEXT" || multilineNew;
        if (entity.Raw.DxfName != "TCH_TEXT" && !multiline) return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeTextCipher(entity, text, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native text");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        Detail::TextStream stream(data, r);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native text version");
        TchText t;
        for (int i = 0; i < base.TextCount; ++i) stream.Next();
        t.Value = stream.Next();                        
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
        t.Attachment = Detail::TextAttachment(t.Attachment);
        if (t.Attachment < 1 || t.Attachment > 9) return fail("unsupported text alignment");
        if (stream.Reader.Failed()) return fail("native text stream is incomplete");
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
 
    namespace Detail
    {
        inline double TextAsciiStep(const TchTextStyleRule& rule) { return rule.Symbol() ? 0.5714 * rule.StyleWidth * rule.AsciiStep() : 0.604; }
        inline double TextWideStep(const TchTextStyleRule& rule) { return rule.Symbol() ? 0.69 * rule.StyleWidth : 0.69; }
         
        inline void AddTextRuns(TchGraphics& g, const std::string& text, double nominal, double x, double y, double rotation, int column,
                                const TchTextStyleRule& rule)
        {
            AddRuns(g, text, nominal, x, y, rotation, column, 0, 0, 0, rule.HeightFactor(), rule.AsciiWidthFactor(), TextAsciiStep(rule),
                    TextWideStep(rule), true, rule.WideHeight);
        }
    }

    inline TchGraphics BuildText(const TchText& t, const TchTextStyleRule& rule = {})
    {
        TchGraphics g;
        const double nominal = t.Height * t.Scale;
        bool ascii = false, wide = false;
        for (unsigned char c : t.Value) (c >= 0x80 ? wide : ascii) = true;
        const bool trueType = rule.TrueType;
        const double h = trueType ? nominal * 0.75527 : nominal;
        const double asciiScale = rule.HeightFactor(), asciiWidth = rule.AsciiWidthFactor();
        const bool superscript = !trueType && t.Value.find("^U") != std::string::npos;
        if (!(ascii && wide) && !superscript)
        {
            const double height = trueType ? h : nominal * (wide ? rule.WideHeight : asciiScale);
            double width = wide ? 0.0 : asciiWidth;
            if (!trueType)
            {
                double tz = 0;
                for (std::size_t i = 0; i < t.Value.size();)
                {
                    const unsigned char ch = static_cast<unsigned char>(t.Value[i]);
                    tz += ch >= 0x80 ? Detail::TextWideStep(rule) : (ch == '.' || ch == ',') ? 0.2 * Detail::TextAsciiStep(rule) : Detail::TextAsciiStep(rule);
                    i += ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
                }
                if (const double natural = Detail::DisplayExtent(wide ? "" : "simplex", t.Value).Width * height; natural > 0)
                    width = tz * nominal / natural;
            }
            g.Texts.push_back({{t.X, t.Y}, height, t.Rotation, t.Value, t.Attachment, 0, 0, 0, width});
            return g;
        }
        const int column = (t.Attachment - 1) % 3;      // 0 左、1 中、2 右
        const int row = (t.Attachment - 1) / 3;         // 0 上、1 中、2 下（基线）
        const double c = std::cos(t.Rotation), s = std::sin(t.Rotation);
        const double drop = row == 0 ? h : row == 1 ? h / 2 : 0.0;
        const double x = t.X + drop * s, y = t.Y - drop * c;
        if (!trueType)
        {
            Detail::AddTextRuns(g, t.Value, nominal, x, y, t.Rotation, column, rule);
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
        // i18n: 数据（避头的标点）
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
                    {
                        // 画出的宽度取这段在天正排布里占的位置（见 Detail::AddRuns；平面小说明的长句以前冲出表格）
                        const double height = runWide ? rule.WideHeight * nominal : rule.HeightFactor() * nominal;
                        double runWidth = runWide ? 0.0 : rule.AsciiWidthFactor();
                        if (const double natural = Detail::DisplayExtent(runWide ? "" : "simplex", run).Width * height; natural > 0)
                            runWidth = (pos - runStart) / natural;
                        g.Texts.push_back({at(runStart, 0), height, t.Rotation, run, 7, 0, 0, 0, runWidth});
                    }
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
