#pragma once
#include "Core/TextCodes.h"
#include "Core/Entity/EntityColor.hpp"
#include "Core/Math/Color4.hpp"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 多行文字的内联字体：\f 是 TrueType 族名（\fSimSun|b0|i0|c134|p2;），\F 是字体文件（\Fsimplex8,hzdx|c134; 逗号后是大字体）
    struct MTextFont
    {
        std::string family;
        std::string file;
        std::string bigFont;
        bool operator==(const MTextFont& o) const { return family == o.family && file == o.file && bigFont == o.bigFont; }
    };

    // 多行文字按格式码切成的一段。字高、缩进都是基准字高的倍数，块参照缩放时只改基准字高
    struct MTextRun
    {
        enum class Kind : uint8_t { Text, Break, Stack };
        Kind        kind = Kind::Text;
        std::string text;               // Text：内容（%% 控制码留给排版解析）；Stack：上半
        std::string bottom;             // Stack：下半
        char        stackType = '/';    // '/' 横线分数、'#' 斜线分数、'^' 上下对齐（公差、上下标）
        int         font = -1;          // MTextContent::fonts 下标，-1 = 文字样式的字体
        double      height = 1.0;
        double      widthFactor = 0.0;  // > 0 时代替实体 / 样式的宽度因子
        double      widthScale = 1.0;   // 相对的 \W0.8x; 乘在实体 / 样式的宽度因子上
        double      obliqueDeg = 0.0;
        bool        hasOblique = false;
        double      tracking = 1.0;
        enum class Color : uint8_t { Entity, ByLayer, Rgb };
        Color        colorKind = Color::Entity;
        Math::Color4 color{};
        bool        underline = false, overline = false, strike = false;
        double      indentFirst = 0.0;  // 段落首行相对左缩进（\pi）
        double      indentLeft = 0.0;   // 段落左缩进（\pl）
        int8_t      align = -1;         // 段落对齐（\pq）：-1 按附着点，0 左、1 中、2 右
        int8_t      valign = 0;         // 与同行较高的字竖向对齐（\A）：0 底（基线）、1 中、2 顶

        bool SameStyle(const MTextRun& o) const
        {
            return font == o.font && height == o.height && widthFactor == o.widthFactor && widthScale == o.widthScale &&
                   obliqueDeg == o.obliqueDeg && hasOblique == o.hasOblique && tracking == o.tracking && colorKind == o.colorKind &&
                   color.r == o.color.r && color.g == o.color.g && color.b == o.color.b && underline == o.underline &&
                   overline == o.overline && strike == o.strike && valign == o.valign && indentFirst == o.indentFirst && indentLeft == o.indentLeft && align == o.align;
        }
    };

    struct MTextContent
    {
        std::vector<MTextRun>  runs;
        std::vector<MTextFont> fonts;

        // 去掉格式的文字：换段为 '\n'，分数写成 上/下
        std::string PlainText() const
        {
            std::string out;
            for (const MTextRun& r : runs)
            {
                if (r.kind == MTextRun::Kind::Break) out += '\n';
                else if (r.kind == MTextRun::Kind::Text) out += r.text;
                else if (r.text.empty() || r.bottom.empty()) out += r.text + r.bottom;
                else out += r.text + (r.stackType == '^' ? " " : "/") + r.bottom;
            }
            return out;
        }
    };

    // 相邻两行基线的距离（AutoCAD 2020 炸开多行文字实测）。base 实体字高，factor 行距比例（组码 44）；
    // "至少"：本行下沉 + 下一行上升 + base/3，比例不为 1 时再加 (factor-1)·(4/3·本行字高 + base/3)；
    // 下沉在没有堆叠的行是字高的 1/3，有堆叠的行是堆叠下半基线在本行基线下的深度。"精确"：5/3·base·factor
    inline double MTextLinePitch(double base, double factor, bool exact, double lineHeight, double descent, double nextAscent)
    {
        if (exact) return 5.0 / 3.0 * base * factor;
        return descent + nextAscent + base / 3.0 + (factor - 1.0) * (4.0 / 3.0 * lineHeight + base / 3.0);
    }

    // 解析多行文字的内联格式码（AutoCAD MTEXT 格式）。baseHeight 是实体字高，用来把绝对字高 \H2.5; 与缩进换成倍数。
    //   { } 分组（组内的格式出组恢复）；\P \X \N 换段；\~ 不换行空格；\\ \{ \} 转义；\U+XXXX
    //   \f \F 字体、\H 字高（x 结尾为相对）、\W 宽度因子、\Q 倾斜角、\T 字间距、\C \c 颜色、\L \O \K 装饰线
    //   \p 段落（i 首行缩进、l 左缩进、q 对齐；\px 的缩进是字高的倍数，不带 x 的是绝对长度）、\S 堆叠（上/下、上#下、上^下）、\A 竖向对齐
    inline MTextContent ParseMTextFormat(const std::string& s, double baseHeight)
    {
        MTextContent out;
        const double base = baseHeight > 0.0 ? baseHeight : 1.0;
        MTextRun cur;
        std::vector<MTextRun> groups;
        size_t paraStart = 0;

        auto number = [](const std::string& v, bool& relative) -> double
        {
            relative = !v.empty() && (v.back() == 'x' || v.back() == 'X');
            return std::strtod(v.c_str(), nullptr);
        };
        auto readArg = [&s](size_t& i) -> std::string
        {
            const size_t start = i;
            while (i < s.size() && s[i] != ';') ++i;
            std::string arg = s.substr(start, i - start);
            if (i < s.size()) ++i;
            return arg;
        };
        auto appendText = [&](const std::string& t)
        {
            MTextRun* last = out.runs.empty() ? nullptr : &out.runs.back();
            if (last && last->kind == MTextRun::Kind::Text && last->SameStyle(cur))
                last->text += t;
            else
            {
                MTextRun r = cur;
                r.kind = MTextRun::Kind::Text;
                r.text = t;
                out.runs.push_back(std::move(r));
            }
        };
        auto pushBreak = [&]()
        {
            MTextRun r = cur;
            r.kind = MTextRun::Kind::Break;
            r.text.clear();
            out.runs.push_back(std::move(r));
            paraStart = out.runs.size();
        };
        auto fontIndex = [&](MTextFont f)
        {
            for (size_t k = 0; k < out.fonts.size(); ++k)
                if (out.fonts[k] == f) return static_cast<int>(k);
            out.fonts.push_back(std::move(f));
            return static_cast<int>(out.fonts.size() - 1);
        };
        auto paragraph = [&](const std::string& arg)
        {
            size_t k = 0;
            const bool extended = k < arg.size() && arg[k] == 'x';
            if (extended) ++k;
            const double unit = extended ? 1.0 : 1.0 / base;
            char key = 0;
            while (k < arg.size())
            {
                const char c = arg[k];
                if (c == ',') { ++k; continue; }
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')
                {
                    char* end = nullptr;
                    const double v = std::strtod(arg.c_str() + k, &end);
                    k = end > arg.c_str() + k ? static_cast<size_t>(end - arg.c_str()) : k + 1;
                    if (key == 'i') cur.indentFirst = v * unit;
                    else if (key == 'l') cur.indentLeft = v * unit;
                    continue;
                }
                key = c;
                ++k;
                if (key == 'q' && k < arg.size())
                {
                    const char q = arg[k++];
                    cur.align = q == 'c' ? 1 : q == 'r' ? 2 : q == '*' ? -1 : 0;
                }
            }
            for (size_t r = paraStart; r < out.runs.size(); ++r)
            {
                out.runs[r].indentFirst = cur.indentFirst;
                out.runs[r].indentLeft  = cur.indentLeft;
                out.runs[r].align       = cur.align;
            }
        };
        auto stack = [&](size_t& i)
        {
            std::string top, bottom, *part = &top;
            char type = 0;
            while (i < s.size() && s[i] != ';')
            {
                if (s[i] == '\\' && i + 1 < s.size()) { *part += s[i + 1]; i += 2; continue; }
                if (!type && (s[i] == '/' || s[i] == '#' || s[i] == '^')) { type = s[i]; part = &bottom; ++i; continue; }
                *part += s[i++];
            }
            if (i < s.size()) ++i;
            if (!type) { appendText(top); return; }
            MTextRun r = cur;
            r.kind = MTextRun::Kind::Stack;
            r.text = std::move(top);
            r.bottom = std::move(bottom);
            r.stackType = type;
            out.runs.push_back(std::move(r));
        };

        for (size_t i = 0; i < s.size(); )
        {
            const char ch = s[i];
            if (ch == '{') { groups.push_back(cur); ++i; continue; }
            if (ch == '}')
            {
                if (!groups.empty())
                {
                    const MTextRun para = cur;
                    cur = groups.back();
                    groups.pop_back();
                    cur.indentFirst = para.indentFirst;
                    cur.indentLeft  = para.indentLeft;
                    cur.align       = para.align;
                }
                ++i; continue;
            }
            if (ch == '\n') { pushBreak(); ++i; continue; }
            if (ch != '\\' || i + 1 >= s.size()) { appendText(std::string(1, ch)); ++i; continue; }

            const char c = s[i + 1];
            switch (c)
            {
            case 'P': case 'X': case 'N': pushBreak(); i += 2; continue;
            case '~': appendText("\xC2\xA0"); i += 2; continue;
            case '\\': case '{': case '}': appendText(std::string(1, c)); i += 2; continue;
            case 'L': cur.underline = true;  i += 2; continue;
            case 'l': cur.underline = false; i += 2; continue;
            case 'O': cur.overline  = true;  i += 2; continue;
            case 'o': cur.overline  = false; i += 2; continue;
            case 'K': cur.strike    = true;  i += 2; continue;
            case 'k': cur.strike    = false; i += 2; continue;
            case 'U': case 'u':
                if (char32_t cp = 0; ParseUnicodeEscape(s, i, cp))
                {
                    std::string utf8;
                    AppendUtf8(utf8, cp);
                    appendText(utf8);
                    i += 7; continue;
                }
                i += 2; continue;
            case 'S': i += 2; stack(i); continue;
            default: break;
            }

            const bool hasArg = c == 'f' || c == 'F' || c == 'H' || c == 'W' || c == 'Q' || c == 'T' || c == 'C' || c == 'c' || c == 'A' || c == 'p';
            if (!hasArg) { i += 2; continue; }   // 未知转义：丢弃
            i += 2;
            const std::string arg = readArg(i);
            bool rel = false;
            switch (c)
            {
            case 'f': case 'F':
            {
                const std::string name = arg.substr(0, arg.find('|'));
                MTextFont f;
                if (c == 'f') f.family = name;
                else
                {
                    const size_t comma = name.find(',');
                    f.file = name.substr(0, comma);
                    if (comma != std::string::npos) f.bigFont = name.substr(comma + 1);
                }
                cur.font = name.empty() ? -1 : fontIndex(std::move(f));
                break;
            }
            case 'H':
            {
                const double v = number(arg, rel);
                if (v > 0.0) cur.height = rel ? cur.height * v : v / base;
                break;
            }
            case 'W':
            {
                const double v = number(arg, rel);
                if (v <= 0.0) break;
                if (!rel) { cur.widthFactor = v; cur.widthScale = 1.0; }
                else if (cur.widthFactor > 0.0) cur.widthFactor *= v;
                else cur.widthScale *= v;
                break;
            }
            case 'Q': cur.obliqueDeg = std::strtod(arg.c_str(), nullptr); cur.hasOblique = true; break;
            case 'T':
            {
                const double v = number(arg, rel);
                if (v > 0.0) cur.tracking = rel ? cur.tracking * v : v;
                break;
            }
            case 'C':
            {
                const long aci = std::strtol(arg.c_str(), nullptr, 10);
                if (aci == 256) cur.colorKind = MTextRun::Color::ByLayer;
                else if (aci <= 0 || aci > 256) cur.colorKind = MTextRun::Color::Entity;
                else { cur.colorKind = MTextRun::Color::Rgb; cur.color = EntityColor::FromAci(static_cast<uint16_t>(aci)).DrawRgba(); }
                break;
            }
            case 'c':
            {
                // 24 位真彩，低字节是红（与 DXF 420 的顺序相反）
                const unsigned long v = std::strtoul(arg.c_str(), nullptr, 10);
                cur.colorKind = MTextRun::Color::Rgb;
                cur.color = EntityColor::FromRgb((v & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, ((v >> 16) & 0xFF) / 255.0).DrawRgba();
                break;
            }
            case 'p': paragraph(arg); break;
            case 'A':
            {
                const long a = std::strtol(arg.c_str(), nullptr, 10);
                cur.valign = static_cast<int8_t>(a == 1 || a == 2 ? a : 0);
                break;
            }
            default: break;
            }
        }
        return out;
    }
}
