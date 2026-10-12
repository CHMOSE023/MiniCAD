#pragma once
#include "Import/TchSymbol.h"
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    namespace Detail
    {
        // 天正注释文字的显示：西文与汉字分段，西文字高 0.85 × 名义字高、宽度因子 0.6 / 0.85，汉字字高 0.96454 × 名义字高
        //（天正 2014 分解出的 TEXT 与此相同）。各段按 MiniCAD 实际显示的字宽（simplex / TSSDCHN）首尾相接，
        // align：0 左对齐、1 居中、2 右对齐，(x, y) 为该对齐位置的基线点
        // asciiScale / asciiWidth / tzAscii：西文段的字高系数、宽度因子（0 为样式自身）、天正排布用的每字宽度（× 名义字高）；
        // 注释符号为 0.85、0.6 / 0.85、0.5714，天正单行文字为 1、0（样式）、0.604
        inline void AddRuns(TchGraphics& g, const std::string& text, double nominal, double x, double y, double rotation, int align,
                            int color = 7, int layer = 0, int style = 0, double asciiScale = 0.85, double asciiWidth = 0.6 / 0.85,
                            double tzAscii = 0.5714)
        {
            struct Run { std::string Value; bool Wide; double Width; };
            std::vector<Run> runs;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const std::size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                const bool wide = c >= 0x80;
                if (runs.empty() || runs.back().Wide != wide) runs.push_back({{}, wide, 0});
                runs.back().Value.append(text, i, len);
                i += len;
            }
            double total = 0;
            for (auto& r : runs)
            {
                r.Width = r.Wide ? DisplayExtent("", r.Value).Width * 0.96454 * nominal
                                 : DisplayExtent("simplex", r.Value).Width * asciiScale * (asciiWidth > 0 ? asciiWidth : 1.0) * nominal;
                total += r.Width;
            }
            // 对齐按天正自己的字宽定起点（分解结果核对：西文每字 0.5714、句点 0.114、汉字约 0.69 × 名义字高），
            // 这样文字起点与天正一致，不会因 MiniCAD 字体宽窄不同越过引线转折点等
            double tz = 0;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                tz += c >= 0x80 ? 0.69 : (c == '.' || c == ',') ? 0.2 * tzAscii : tzAscii;
                i += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            }
            tz *= nominal;
            (void)total;
            double cursor = align == 1 ? -tz / 2 : align == 2 ? -tz : 0;
            const double c = std::cos(rotation), s = std::sin(rotation);
            for (const auto& r : runs)
            {
                if (r.Value.find_first_not_of(' ') != std::string::npos)
                    g.Texts.push_back({{x + cursor * c, y + cursor * s}, r.Wide ? 0.96454 * nominal : asciiScale * nominal, rotation, r.Value, 7, style, color, layer,
                                       r.Wide ? 0.0 : asciiWidth});
                cursor += r.Width;
            }
        }
    }

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
    };

    // 天正基类、RC 版本（1～2）、句柄流：文字样式；文字流：内容；3BD 插入点、BD 字高、BD 转角、RC 对齐、版本 ≥ 2 再一个 RC
    inline bool DecodeText(const MiniDWG::UnknownEntity& entity, TchText& text, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_TEXT") return false;
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
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native text fields do not consume the payload exactly");
        if (!Detail::Finite(t.X) || !Detail::Finite(t.Y) || !Detail::Finite(t.Rotation) || !(t.Height > 0) || !(t.Scale > 0))
            return fail("invalid native text settings");
        if (t.Attachment < 1 || t.Attachment > 9) return fail("unsupported text alignment");
        Detail::TextStream stream(data);
        for (int i = 0; i < base.TextCount; ++i) stream.Next();
        t.Value = stream.Next();
        if (stream.Reader.Failed()) return fail("native text stream is incomplete");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        t.Style = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed()) return fail("native text handle stream is incomplete");
        text = std::move(t);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // SHX 样式：天正 2014 分解为西文、汉字交替的 TEXT：西文字高 = 名义字高 × 比例（样式自身宽度因子），汉字 × 0.96454，颜色随层。
    // 左下对齐时各段从插入点起首尾相接；其他对齐方式只有纯西文或纯汉字时按附着点显示，混排时按水平对齐近似。
    // TrueType 样式（trueType，如“宋体”）：字高 = 名义字高 × 0.75527，用样式宽度因子；天正排布的步进汉字 = 字高、西文 = 0.5 × 字高
    //（宋体宽度因子 0.7 时与字形步进一致；8#楼封面目录的天正显示与分解结果核对）。
    // 混排时各段按附着点的水平、竖直位置排布（竖直：上 / 中按字高下移）
    inline TchGraphics BuildText(const TchText& t, bool trueType = false)
    {
        TchGraphics g;
        const double nominal = t.Height * t.Scale;
        bool ascii = false, wide = false;
        for (unsigned char c : t.Value) (c >= 0x80 ? wide : ascii) = true;
        const double h = trueType ? nominal * 0.75527 : nominal;
        if (!(ascii && wide))
        {
            g.Texts.push_back({{t.X, t.Y}, trueType ? h : nominal * (wide ? 0.96454 : 1.0), t.Rotation, t.Value, t.Attachment, 0, 0});
            return g;
        }
        const int column = (t.Attachment - 1) % 3;      // 0 左、1 中、2 右
        const int row = (t.Attachment - 1) / 3;         // 0 上、1 中、2 下（基线）
        const double c = std::cos(t.Rotation), s = std::sin(t.Rotation);
        const double drop = row == 0 ? h : row == 1 ? h / 2 : 0.0;
        const double x = t.X + drop * s, y = t.Y - drop * c;
        if (!trueType)
        {
            Detail::AddRuns(g, t.Value, nominal, x, y, t.Rotation, column, 0, 0, 0, 1.0, 0.0, 0.604);
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
}
