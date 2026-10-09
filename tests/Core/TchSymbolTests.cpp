// 天正坐标标注（TCH_COORD）与箭头（TCH_ARROW）：私有布局解码与显示
// 合成对象的数值取自真实样例与用 MiniCADTchPatch 构造的变体，期望的图元取自天正 2014 的分解结果（docs/Tch-compatibility.md）
#include "Import/TchSymbol.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace MiniDWG;
using MiniCAD::Tch::TchGraphics;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    // vl-princ 只输出 6 位有效数字：大坐标的期望值误差可达 0.5
    bool Near(double a, double b, double tol = 0.11) { return std::abs(a - b) < std::max(tol, std::abs(b) * 5e-6); }
    bool Near(const TchGraphics::Point& p, double x, double y, double tol = 0.11) { return Near(p.X, x, tol) && Near(p.Y, y, tol); }

    // 符号基类（版本 3）：样例的字高 3.5
    void SymbolBase(DwgBitWriter& r, double height)
    {
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(3); r.WriteBitDouble(height); r.WriteBitDouble(0); r.WriteBitShort(7);
    }

    void SymbolHandles(DwgBitWriter& r, RawDwgData& raw, Handle style)
    {
        r.WriteHandle(DwgRef::HardPointer, style);
        r.WriteHandle(DwgRef::HardPointer, 0x366);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
    }

    struct CoordSpec
    {
        double ValueX = 58.89201696736875, ValueY = 20.74228917231248;
        double LeaderX = 59471.78098871338, LeaderY = 21698.13942179414;
        int Format = 48, Side = 0, Version = 5;
        double Height = 3.5, Gap = -1000, Length = 100000;
    };

    // 版本 5；样例 370（720 位），被标注点 (58892.02, 20742.29)
    std::unique_ptr<UnknownEntity> CoordObject(const CoordSpec& s, Handle style = 0x294)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_COORD";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        SymbolBase(r, s.Height);
        r.WriteByte(static_cast<std::uint8_t>(s.Version));
        r.WriteByte(static_cast<std::uint8_t>(s.Format)); r.WriteByte(static_cast<std::uint8_t>(s.Side));
        r.WriteRawDouble(58892.01696736875); r.WriteRawDouble(20742.289172312478);
        r.WriteRawDouble(s.ValueX); r.WriteRawDouble(s.ValueY);
        r.WriteRawDouble(s.LeaderX); r.WriteRawDouble(s.LeaderY);
        r.WriteByte(0); r.WriteBitDouble(s.Gap);
        if (s.Version >= 5) r.WriteBitDouble(s.Length);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        SymbolHandles(r, raw, style);
        return e;
    }

    struct ArrowSpec
    {
        int Mode = 1;
        double Length = 3, Height = 3.5;
        double TailX = 63021.56436710451;
        std::string Text1 = "上", Text2 = "下";
    };

    // 版本 5；样例 371（749 位）：三个顶点，末端为箭头
    std::unique_ptr<UnknownEntity> ArrowObject(const ArrowSpec& s, Handle style = 0x294)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_ARROW";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        SymbolBase(r, s.Height);
        r.WriteByte(5); r.WriteByte(static_cast<std::uint8_t>(s.Mode));
        r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteBitDouble(0);
        r.WriteByte(0); r.WriteBitShort(3);
        const double v[3][2] = {{s.TailX, 18688.228029981386}, {61322.95742863529, 18688.228029981386}, {60560.109991518184, 19735.59585386717}};
        for (const auto& p : v) { r.WriteRawDouble(p[0]); r.WriteRawDouble(p[1]); r.WriteBitDouble(0); }
        r.WriteBitDouble(s.Length); r.WriteByte(0);
        r.WriteBitDouble(-1000); r.WriteBit(true);
        r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteBitDouble(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        r.WriteVariableText(s.Text1); r.WriteVariableText(s.Text2);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        SymbolHandles(r, raw, style);
        return e;
    }

    // CADReader 0x6f2fa0（版本 2）；样例 376（1166 位），字高 5
    std::unique_ptr<UnknownEntity> SectionObject(std::vector<std::pair<double, double>> pts, int kind = 1, double height = 5,
                                                 double view = 4.671538513534203, const std::string& label = "1")
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_SYMB_SECTION";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        SymbolBase(r, height);
        r.WriteByte(2); r.WriteByte(static_cast<std::uint8_t>(kind)); r.WriteBitDouble(view);
        r.WriteByte(static_cast<std::uint8_t>(pts.size()));
        for (const auto& [x, y] : pts) { r.WriteRawDouble(x); r.WriteRawDouble(y); }
        for (int i = 0; i < 8; ++i) r.WriteRawDouble(0);
        r.WriteBitDouble(3.5); r.WriteBitShort(0); r.WriteBitDouble(-1000); r.WriteBitDouble(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        r.WriteVariableText(label); r.WriteVariableText(label);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, 0x372);
        r.WriteHandle(DwgRef::HardPointer, 0x366);
        r.WriteHandle(DwgRef::HardPointer, 0x294);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    struct IndexSpec
    {
        int Kind = 1;
        double PointX = 61892.55016858876, PointY = 17742.546379929714;
        double CornerX = 63652.18486555127, CornerY = 17742.546379929714;
        double Radius = 5, Run = 11.900420271553449, Mark = -5;
        std::string Number = "1", Sheet = "-", Above, Below;
    };

    // CADReader 0x6f07a0（版本 3）；样例 375（782 位）。基类属性包含一个 RC 与一个句柄（旁注文字样式 0x294）
    std::unique_ptr<UnknownEntity> IndexObject(const IndexSpec& s)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_INDEXPOINTER";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0);
        r.WriteBitLong(2); r.WriteBitLong(2); r.WriteByte(0); r.WriteBitLong(208);
        r.WriteByte(3); r.WriteBitDouble(3.5); r.WriteBitDouble(0); r.WriteBitShort(7);
        r.WriteByte(3); r.WriteByte(static_cast<std::uint8_t>(s.Kind));
        r.WriteRawDouble(s.PointX); r.WriteRawDouble(s.PointY); r.WriteRawDouble(s.CornerX); r.WriteRawDouble(s.CornerY);
        r.WriteBitDouble(s.Radius); r.WriteBitDouble(s.Run); r.WriteBitDouble(s.Mark);
        r.WriteByte(0); r.WriteByte(0);
        r.WriteBitDouble(-1000); r.WriteBitDouble(0); r.WriteByte(3); r.WriteByte(0); r.WriteBitShort(0); r.WriteBitDouble(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        for (const std::string& t : {std::string("标注位置点"), std::string("标注文字样式ID"), s.Number, s.Sheet, s.Above, s.Below, std::string()})
            r.WriteVariableText(t);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, 0x294);
        r.WriteHandle(DwgRef::HardPointer, 0x372);
        r.WriteHandle(DwgRef::HardPointer, 0x366);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    struct NameSpec
    {
        double NameHeight = 7, ScaleHeight = 5, LineWidth = 0.7, Gap = 0.6;
        bool Show = true;
        int LineMode = 1, Color = 7;
        std::string Name = "天正图名标注输入", ScaleText = "1:100";
    };

    // CADReader 0x6ce8c0（版本 3）；样例 378（523 位），插入点 (56674.67, 16918.89)
    std::unique_ptr<UnknownEntity> NameObject(const NameSpec& s)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_DRAWINGNAME";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteBitShort(3); r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteBitDouble(0);
        r.WriteBitDouble(0); r.WriteBitDouble(s.NameHeight); r.WriteBitDouble(s.ScaleHeight); r.WriteBitDouble(s.LineWidth); r.WriteBitDouble(s.Gap);
        r.WriteBit(s.Show); r.WriteBitShort(static_cast<std::int16_t>(s.LineMode)); r.WriteBitShort(static_cast<std::int16_t>(s.Color));
        r.WriteBitDouble(56674.67383619447); r.WriteBitDouble(16918.88821029265); r.WriteBitDouble(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        r.WriteVariableText(s.Name); r.WriteVariableText(s.ScaleText);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, 0x11);
        r.WriteHandle(DwgRef::HardPointer, 0x11);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    // 样例的文字样式 Standard 为 GBENOR + GBCBIG（注意：插入到 AutoCAD 默认样板时会被样板的 Standard/txt 覆盖，核对时需恢复）
    MiniCAD::Tch::Detail::TextExtent GbenorText(const std::string& text, double height, bool)
    {
        const auto e = MiniCAD::Tch::Detail::EstimateExtent("GBENOR", text);
        return {e.Width * height, e.Bottom * height};
    }

    using Pts = std::vector<std::pair<double, double>>;
    bool SamePath(const TchGraphics::Path& path, const Pts& expected)
    {
        if (path.Points.size() != expected.size() || std::abs(path.Width - 50) > 1e-9) return false;
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (!Near(path.Points[i].P, expected[i].first, expected[i].second)) return false;
        return true;
    }

    // 天正分解出的 TEXT（左下对齐：插入点、textbox 宽度、字高）换算到附着点 4（左中）/ 6（右中）
    TchGraphics::Point Anchor(double x, double y, double width, double height, int attachment)
    {
        return {attachment == 6 ? x + width : x, y + height / 2};
    }

    DxfClass Class(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
    }

    template <class Decode, class T>
    int AcceptedTruncations(const UnknownEntity& full, Decode decode, T& out)
    {
        int accepted = 0;
        for (std::uint64_t bits = 0; bits < full.Raw.Dwg.MainBits; bits += 7)
        {
            UnknownEntity cut;
            cut.Raw = full.Raw;
            cut.Raw.Dwg.MainBits = bits;
            accepted += decode(cut, out, nullptr);
        }
        return accepted;
    }
}

int main()
{
    using namespace MiniCAD::Tch;
    std::string why;

    // ── 坐标标注：样例与 12 个变体 ──
    {
        struct Case
        {
            const char* Name;
            CoordSpec Spec;
            double EndX, EndY;
            const char* Top; double TopX, TopY, TopW;
            const char* Bottom; double BottomX, BottomY, BottomW;
            double Height;
            int Attachment;
        };
        CoordSpec left; left.Side = 1;
        CoordSpec upLeft; upLeft.LeaderX = 58292.01696736875; upLeft.LeaderY = 21642.289172312478;
        CoordSpec longer; longer.ValueX = 123456.789; longer.ValueY = -5.5;
        CoordSpec tall; tall.Height = 5;
        CoordSpec integer; integer.Format = 0;
        CoordSpec tallInteger; tallInteger.Height = 5; tallInteger.Format = 0;
        CoordSpec one; one.Format = 16;
        CoordSpec two; two.Format = 32;
        CoordSpec letters; letters.Format = 49;
        CoordSpec gap; gap.Format = 0; gap.Gap = -500;
        CoordSpec fixed; fixed.Format = 0; fixed.Length = 500;
        CoordSpec small; small.Format = 0; small.ValueX = 1; small.ValueY = 2;
        const Case cases[] = {
            {"sample", {}, 61121.8, 21698.1, "X=20.742", 59621.8, 21803.1, 1500, "Y=58.892", 59621.8, 21295.6, 1500, 297.5, 6},
            {"side 1 draws the line to the left", left, 57821.8, 21698.1, "X=20.742", 57821.8, 21803.1, 1500, "Y=58.892", 57821.8, 21295.6, 1500, 297.5, 4},
            {"leader to the upper left", upLeft, 59942.0, 21642.3, "X=20.742", 58442.0, 21747.3, 1500, "Y=58.892", 58442.0, 21239.8, 1500, 297.5, 6},
            {"longer values widen the line", longer, 61946.8, 21698.1, "X=-5.500", 60386.8, 21803.1, 1560, "Y=123456.789", 59696.8, 21295.6, 2250, 297.5, 6},
            {"text height 5", tall, 61828.9, 21698.1, "X=20.742", 59686.1, 21848.1, 2142.86, "Y=58.892", 59686.1, 21123.1, 2142.86, 425, 6},
            {"no decimals uses the 11 x scale minimum", integer, 60571.8, 21698.1, "X=21", 59861.8, 21803.1, 710, "Y=59", 59781.8, 21295.6, 790, 297.5, 6},
            {"tall integers: 1.1 x text width", tallInteger, 60713.2, 21698.1, "X=21", 59698.9, 21848.1, 1014.29, "Y=59", 59584.6, 21123.1, 1128.57, 425, 6},
            {"one decimal", one, 60681.8, 21698.1, "X=20.7", 59581.8, 21803.1, 1100, "Y=58.9", 59591.8, 21295.6, 1090, 297.5, 6},
            {"two decimals", two, 60912.8, 21698.1, "X=20.74", 59602.8, 21803.1, 1310, "Y=58.89", 59622.8, 21295.6, 1290, 297.5, 6},
            {"format bit 0 switches to A= / B=", letters, 61143.8, 21698.1, "A=20.742", 59623.8, 21803.1, 1520, "B=58.892", 59633.8, 21295.6, 1510, 297.5, 6},
            {"stored text gap", gap, 60571.8, 21698.1, "X=21", 59861.8, -153302.0, 710, "Y=59", 59781.8, 196401.0, 790, 297.5, 6},
            {"stored line length", fixed, 109471.8, 21698.1, "X=21", 108761.8, 21803.1, 710, "Y=59", 108681.8, 21295.6, 790, 297.5, 6},
            {"one-digit values", small, 60571.8, 21698.1, "X=2", 59971.8, 21803.1, 600, "Y=1", 60061.8, 21295.6, 510, 297.5, 6},
        };
        auto sample = CoordObject({});
        Check(sample->Raw.Dwg.MainBits == 720, "synthetic coordinate reproduces the 720-bit real layout");
        TchCoord c;
        for (const auto& k : cases)
        {
            std::string label = std::string("coordinate ") + k.Name;
            if (!DecodeCoord(*CoordObject(k.Spec), c, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
            const TchGraphics g = BuildCoord(c);
            const auto top = Anchor(k.TopX, k.TopY, k.TopW, k.Height, k.Attachment);
            const auto bottom = Anchor(k.BottomX, k.BottomY, k.BottomW, k.Height, k.Attachment);
            const bool ok = g.Lines.size() == 2 && Near(g.Lines[0].A, 58892.0, 20742.3) && Near(g.Lines[1].B, k.EndX, k.EndY, 0.25) &&
                g.Texts.size() == 2 && g.Texts[0].Value == k.Top && g.Texts[1].Value == k.Bottom &&
                g.Texts[0].Attachment == k.Attachment && Near(g.Texts[0].Center, top.X, top.Y, 0.25) &&
                Near(g.Texts[1].Center, bottom.X, bottom.Y, 0.25) && Near(g.Texts[0].Height, k.Height);
            Check(ok, label.c_str());
        }
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeCoord(e, o, w); }, c) == 0,
              "every truncated coordinate payload is rejected");
        CoordSpec v6; v6.Version = 6;
        Check(!DecodeCoord(*CoordObject(v6), c), "coordinate version 6 (extra text and point) is not guessed");
    }

    // ── 箭头：样例与变体 ──
    {
        TchArrow a;
        auto sample = ArrowObject({});
        Check(sample->Raw.Dwg.MainBits == 749, "synthetic arrow reproduces the 749-bit real layout");
        Check(DecodeArrow(*sample, a, &why) && a.Vertices.size() == 3 && a.Text1 == "上" && a.Text2 == "下" && a.TextStyle == 0x294,
              "arrow decodes vertices, both texts (text stream) and text style");
        TchGraphics g = BuildArrow(a);
        Check(g.Paths.size() == 1 && g.Paths[0].Points.size() == 3 && Near(g.Paths[0].Points[0].P, 63021.6, 18688.2) &&
              Near(g.Paths[0].Points[2].P, 60736.7, 19493.1), "the polyline stops at the arrow base 300 (3 x scale) before the tip");
        Check(g.Arrows.size() == 1 && Near(g.Arrows[0].C, 60560.1, 19735.6) &&
              Near(std::hypot(g.Arrows[0].A.X - g.Arrows[0].B.X, g.Arrows[0].A.Y - g.Arrows[0].B.Y), 80, 0.1),
              "arrow head is a solid triangle with base width length / 3.75");
        Check(g.Texts.size() == 1 && g.Texts[0].Value == "上" && g.Texts[0].Attachment == 4 && Near(g.Texts[0].Center, 63126.6, 18688.2) &&
              Near(g.Texts[0].Height, 337.589, 0.01), "mode 1: the first text starts 0.3 x height beyond the tail");

        ArrowSpec longHead; longHead.Length = 5;
        Check(DecodeArrow(*ArrowObject(longHead), a) && Near(BuildArrow(a).Paths[0].Points[2].P, 60854.5, 19331.4) &&
              Near(std::hypot(BuildArrow(a).Arrows[0].A.X - BuildArrow(a).Arrows[0].B.X, BuildArrow(a).Arrows[0].A.Y - BuildArrow(a).Arrows[0].B.Y), 133.333, 0.01),
              "arrow length 5 moves the base and widens the head");
        ArrowSpec tall; tall.Height = 5;
        Check(DecodeArrow(*ArrowObject(tall), a) && Near(BuildArrow(a).Texts[0].Center.X, 63171.6) && Near(BuildArrow(a).Texts[0].Height, 482.27, 0.01),
              "text height 5 scales the gap and the text");
        ArrowSpec left; left.TailX = 59000;
        // 天正把文字墨迹右缘放在尾端外 105 处，MiniCAD 以右中对齐近似（误差小于字宽的 1/10）
        Check(DecodeArrow(*ArrowObject(left), a) && BuildArrow(a).Texts[0].Attachment == 6 && Near(BuildArrow(a).Texts[0].Center.X, 58686.5 + 227.128, 25),
              "a tail pointing left puts the text on the left");
        ArrowSpec both; both.Mode = 2;
        Check(DecodeArrow(*ArrowObject(both), a) && BuildArrow(a).Texts.size() == 2 &&
              Near(BuildArrow(a).Texts[0].Center, 62068.0 + (18.617 + 227.128) / 2, 18782.1 + (11.1702 + 305.319) / 2, 25) &&
              Near(BuildArrow(a).Texts[1].Center, 62077.3 + (22.3404 + 212.234) / 2, 18285.4 + (3.7234 + 297.872) / 2, 25),
              "mode 2: both texts centred above and below the first segment");
        ArrowSpec mode0; mode0.Mode = 0;
        Check(!DecodeArrow(*ArrowObject(mode0), a), "unverified arrow text mode 0 is not drawn");
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeArrow(e, o, w); }, a) == 0,
              "every truncated arrow payload is rejected");
    }

    // ── 剖切符号：样例与 5 个变体 ──
    {
        const Pts two{{57976.600098945244, 19877.956497234933}, {67181.62561101379, 19501.717600188458}};
        TchSection sec;
        auto sample = SectionObject(two);
        Check(sample->Raw.Dwg.MainBits == 1166, "synthetic section symbol reproduces the 1166-bit real layout");
        Check(DecodeSection(*sample, sec, &why) && sec.Points.size() == 2 && sec.Label == "1" && sec.TextStyle == 0x372,
              "section symbol decodes the cut points, label and text style");
        struct Case { const char* Name; std::unique_ptr<UnknownEntity> Object; std::vector<Pts> Paths; double TextX, TextY, TextH; };
        Case cases[] = {
            {"sample", SectionObject(two),
             {{{57952.1, 19278.5}, {57976.6, 19878.0}, {58975.8, 19837.1}}, {{66182.5, 19542.6}, {67181.6, 19501.7}, {67157.1, 18902.2}}},
             57867.4 + 128.571 / 2, 18566.4 + 212.5, 425},
            {"text height 3.5 keeps the strokes and moves the label", SectionObject(two, 1, 3.5),
             {{{57952.1, 19278.5}, {57976.6, 19878.0}, {58975.8, 19837.1}}, {{66182.5, 19542.6}, {67181.6, 19501.7}, {67157.1, 18902.2}}},
             57892.8 + 45, 18780.0 + 148.75, 297.5},
            {"kind 0 has no view legs", SectionObject(two, 0),
             {{{57976.6, 19878.0}, {58975.8, 19837.1}}, {{66182.5, 19542.6}, {67181.6, 19501.7}}}, 57891.9 + 128.571 / 2, 19165.9 + 212.5, 425},
            {"opposite view direction", SectionObject(two, 1, 5, 4.671538513534203 - 3.14159265358979323846),
             {{{58001.1, 20477.5}, {57976.6, 19878.0}, {58975.8, 19837.1}}, {{66182.5, 19542.6}, {67181.6, 19501.7}, {67206.1, 20101.2}}},
             57957.2 + 128.571 / 2, 20764.5 + 212.5, 425},
            {"stepped cut marks every bend toward both neighbours",
             SectionObject({two[0], {62000, 19700}, {62100, 21700}, two[1]}),
             {{{57952.1, 19278.5}, {57976.6, 19878.0}, {58975.6, 19833.8}}, {{61001.0, 19744.2}, {62000, 19700}, {62049.9, 20698.8}},
              {{62050.1, 20701.2}, {62100, 21700}, {63017.8, 21303.0}}, {{66263.8, 19898.8}, {67181.6, 19501.7}, {67157.1, 18902.2}}},
             57867.4 + 128.571 / 2, 18566.4 + 212.5, 425},
            {"both ends use the first string", SectionObject(two, 1, 5, 4.671538513534203, "A"),
             {{{57952.1, 19278.5}, {57976.6, 19878.0}, {58975.8, 19837.1}}, {{66182.5, 19542.6}, {67181.6, 19501.7}, {67157.1, 18902.2}}},
             57803.1 + 257.143 / 2, 18566.4 + 212.5, 425},
        };
        for (auto& k : cases)
        {
            std::string label = std::string("section symbol ") + k.Name;
            if (!DecodeSection(*k.Object, sec, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
            const TchGraphics g = BuildSection(sec);
            bool ok = g.Paths.size() == k.Paths.size() && g.Texts.size() == 2 && Near(g.Texts[0].Center, k.TextX, k.TextY, 0.25) &&
                      Near(g.Texts[0].Height, k.TextH);
            for (std::size_t i = 0; ok && i < k.Paths.size(); ++i) ok = SamePath(g.Paths[i], k.Paths[i]);
            Check(ok, label.c_str());
        }
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeSection(e, o, w); }, sec) == 0,
              "every truncated section symbol payload is rejected");
    }

    // ── 索引符号：样例与 9 个变体 ──
    {
        TchIndexPointer ip;
        auto sample = IndexObject({});
        Check(sample->Raw.Dwg.MainBits == 782, "synthetic index symbol reproduces the 782-bit real layout");
        Check(DecodeIndexPointer(*sample, ip, &why) && ip.Number == "1" && ip.Sheet == "-" && ip.TextStyle == 0x372 && ip.NoteStyle == 0x294,
              "index symbol skips the property-bag strings and handle, then reads labels and both text styles");
        struct Case
        {
            const char* Name; IndexSpec Spec;
            double CX, CY, R;
            Pts Mark;                       // 类型 1 的粗线；类型 0 时为空
            double DotR;                    // 类型 0 的小圆半径
        };
        IndexSpec below; below.Mark = 5;
        IndexSpec longer; longer.Mark = -8;
        IndexSpec left; left.CornerX = 60000;
        IndexSpec small; small.Radius = 4;
        IndexSpec dot; dot.Kind = 0; dot.Mark = 9;
        IndexSpec slanted; slanted.CornerY = 19000;
        IndexSpec real; real.Kind = 0; real.PointX = 61872.20753566013; real.PointY = 20294.86993562871; real.CornerX = 63764.069171293;
        real.CornerY = 20294.86993562871; real.Run = 4.068519571096113; real.Mark = 935.7595714984054;
        const Case cases[] = {
            {"sample (kind 1, mark above)", {}, 65342.2, 17742.5, 500, {{61892.6, 17892.5}, {62392.6, 17892.5}}, 0},
            {"positive mark goes below", below, 65342.2, 17742.5, 500, {{61892.6, 17592.5}, {62392.6, 17592.5}}, 0},
            {"mark length 8", longer, 65342.2, 17742.5, 500, {{61892.6, 17892.5}, {62692.6, 17892.5}}, 0},
            {"leader to the left", left, 61690.0, 17742.5, 500, {{61892.6, 17592.5}, {61392.6, 17592.5}}, 0},
            {"radius 4", small, 65242.2, 17742.5, 400, {{61892.6, 17892.5}, {62392.6, 17892.5}}, 0},
            {"kind 0 draws a dot of diameter mark", dot, 65342.2, 17742.5, 500, {}, 4.5},
            {"slanted leader", slanted, 65342.2, 19000.0, 500, {{61805.3, 17864.6}, {62212.1, 18155.3}}, 0},
            {"real kind-0 sample", real, 64670.9, 20294.9, 500, {}, 467.88},
        };
        for (const auto& k : cases)
        {
            std::string label = std::string("index symbol ") + k.Name;
            if (!DecodeIndexPointer(*IndexObject(k.Spec), ip, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
            const TchGraphics g = BuildIndexPointer(ip);
            bool ok = !g.Circles.empty() && Near(g.Circles.back().Center, k.CX, k.CY) && Near(g.Circles.back().Radius, k.R) &&
                      g.Texts.size() == 2 && Near(g.Texts[0].Center.X, k.CX) && Near(g.Texts[0].Center.Y, k.CY + 225.4 * k.R / 500, 3);
            if (k.Mark.empty())
                ok = ok && g.Paths.empty() && g.Circles.size() == 2 && Near(g.Circles[0].Radius, k.DotR, 0.01);
            else
                ok = ok && g.Paths.size() == 1 && SamePath(g.Paths[0], k.Mark) && g.Circles.size() == 1;
            Check(ok, label.c_str());
        }
        IndexSpec notes; notes.Above = "UP"; notes.Below = "DN";
        Check(DecodeIndexPointer(*IndexObject(notes), ip) && BuildIndexPointer(ip).Texts.size() == 4 &&
              BuildIndexPointer(ip).Texts[2].Style == 1 && BuildIndexPointer(ip).Texts[2].Attachment == 6 &&
              Near(BuildIndexPointer(ip).Texts[2].Center, 64377.2 + 360, 17847.5 + 148.75) &&
              Near(BuildIndexPointer(ip).Texts[3].Center, 64387.2 + 350, 17340.0 + 148.75),
              "notes above and below end 0.3 x height before the circle and use the property-bag text style");
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeIndexPointer(e, o, w); }, ip) == 0,
              "every truncated index symbol payload is rejected");
    }

    // ── 图名：样例与 9 个变体 ──
    {
        TchDrawingName dn;
        auto sample = NameObject({});
        Check(sample->Raw.Dwg.MainBits == 523, "synthetic drawing name reproduces the 523-bit real layout");
        Check(DecodeDrawingName(*sample, dn, &why) && dn.Name == "天正图名标注输入" && dn.ScaleText == "1:100" && dn.NameStyle == 0x11 &&
              Near(dn.X, 56674.7) && Near(dn.Y, 16918.9), "drawing name decodes texts, heights, styles and the later insertion point");
        Check(Near(MiniCAD::Tch::Detail::TxtWidth("天正图名标注输入") * 700, 4433.33, 0.01) && Near(MiniCAD::Tch::Detail::TxtWidth("1:100") * 500, 1500) &&
              Near(MiniCAD::Tch::Detail::TxtWidth("ABC") * 700, 1866.67, 0.01) && Near(MiniCAD::Tch::Detail::TxtWidth("1:50") * 500, 1250),
              "txt.shx widths match AutoCAD textbox (Chinese renders as ?)");
        Check(Near(GbenorText("1:100", 500, true).Width, 1000.33, 0.05) && Near(GbenorText("1:50", 500, true).Width, 791.995, 0.05) &&
              Near(GbenorText("ABC", 700, false).Width, 1225.0, 0.05) && Near(GbenorText("天正图名标注输入", 675.177, false).Width, 3864.9, 10) &&
              Near(GbenorText("天正图名标注输入", 675.177, false).Bottom, -44.68, 0.05),
              "GBENOR / GBCBIG textbox widths and the Chinese descent match AutoCAD");
        struct Case
        {
            const char* Name; NameSpec Spec;
            double NameX, ScaleX;           // 分解出的 TEXT 插入点（左下）；ScaleX < 0 表示无比例文字。Standard 已恢复为 GBENOR + GBCBIG
            double LineX0, LineX1, LineY, LineW;
            double NameH, ScaleH;
        };
        NameSpec hidden; hidden.Show = false;
        NameSpec tall; tall.NameHeight = 10;
        NameSpec small; small.ScaleHeight = 3;
        NameSpec thick; thick.LineWidth = 1;
        NameSpec wide; wide.Gap = 1;
        NameSpec mode2; mode2.LineMode = 2;
        NameSpec latin; latin.Name = "ABC"; latin.ScaleText = "1:50";
        const Case cases[] = {
            {"sample", {}, 54032.1, 58317.0, 54032.1, 58037.0, 16734.2, 70, 675.177, 500},
            {"hidden scale centres name + 0.2 x height", hidden, 54672.2, -1, 54672.2, 58677.1, 16734.2, 70, 675.177, 0},
            {"name height 10", tall, 53113.9, 59235.1, 53113.9, 58835.1, 16715.1, 70, 964.539, 500},
            {"scale height 3", small, 54232.1, 58517.0, 54232.1, 58237.0, 16734.2, 70, 675.177, 300},
            {"underline width 1 moves it to 2 x width below the lowest ink", thick, 54032.1, 58317.0, 54032.1, 58037.0, 16674.2, 100, 675.177, 500},
            {"gap factor 1", wide, 53892.1, 58457.0, 53892.1, 57897.0, 16734.2, 70, 675.177, 500},
            {"underline mode 2 equals mode 1", mode2, 54032.1, 58317.0, 54032.1, 58037.0, 16734.2, 70, 675.177, 500},
            {"latin texts keep the nominal height", latin, 55456.2, 57101.2, 55456.2, 56821.2, 16780.9, 70, 700, 500},
        };
        for (const auto& k : cases)
        {
            std::string label = std::string("drawing name ") + k.Name;
            if (!DecodeDrawingName(*NameObject(k.Spec), dn, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
            const TchGraphics g = BuildDrawingName(dn, GbenorText);
            const std::size_t texts = k.ScaleX < 0 ? 1 : 2;
            constexpr double tol = 10;      // 汉字按平均字形估宽，误差约 0.015 × 字高
            bool ok = g.Texts.size() == texts && g.Texts[0].Attachment == 4 && Near(g.Texts[0].Center, k.NameX, 16918.9 + k.NameH / 2, tol) &&
                      Near(g.Texts[0].Height, k.NameH, 0.01) && g.Paths.size() == 1 && Near(g.Paths[0].Width, k.LineW) &&
                      Near(g.Paths[0].Points[0].P, k.LineX0, k.LineY, tol) && Near(g.Paths[0].Points[1].P, k.LineX1, k.LineY, tol);
            if (texts == 2) ok = ok && Near(g.Texts[1].Center, k.ScaleX, 16918.9 + k.ScaleH / 2, tol) && g.Texts[1].Style == 1;
            Check(ok, label.c_str());
        }
        NameSpec byBlock; byBlock.Color = 0;
        Check(DecodeDrawingName(*NameObject(byBlock), dn) && BuildDrawingName(dn, GbenorText).Texts[0].Color == 0,
              "the last short is the text colour (0 = ByBlock)");
        NameSpec doubleLine; doubleLine.LineMode = 0;
        Check(!DecodeDrawingName(*NameObject(doubleLine), dn), "unverified double underline mode 0 is not drawn");
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeDrawingName(e, o, w); }, dn) == 0,
              "every truncated drawing name payload is rejected");
    }

    // ── 端到端：DWG → Scene → 普通几何 DWG/DXF ──
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027);
    db.CreateDefaults();
    db.Classes.push_back(Class("TCH_COORD", "TDbSymbCoord", 500));
    db.Classes.push_back(Class("TCH_ARROW", "TDbSymbArrow", 501));
    const Handle standard = db.FindTableEntry(db.TextStyles(), "Standard")->ObjectHandle;
    db.AddEntity(db.ModelSpace(), CoordObject({}, standard));
    db.AddEntity(db.ModelSpace(), ArrowObject({}, standard));
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    Check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 2, "coordinate and arrow appear in the scene");
    int lines = 0, paths = 0, solids = 0, texts = 0;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
        if (!insert || !insert->GetBlock()) return;
        for (const auto& e : insert->GetBlock()->GetEntities())
        {
            lines += dynamic_cast<const MiniCAD::LineEntity*>(e.get()) != nullptr;
            paths += dynamic_cast<const MiniCAD::PolylineEntity*>(e.get()) != nullptr;
            solids += dynamic_cast<const MiniCAD::SolidEntity*>(e.get()) != nullptr;
            texts += dynamic_cast<const MiniCAD::MTextEntity*>(e.get()) != nullptr;
        }
    });
    Check(lines == 2 && paths == 1 && solids == 1 && texts == 3, "display blocks hold the leader, line, arrow polyline, head and texts");
    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf})
    {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        Check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 2,
              "displayed coordinate and arrow survive plain export and reimport");
    }
    return g_failed;
}
