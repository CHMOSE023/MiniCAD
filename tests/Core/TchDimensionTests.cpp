// 天正尺寸标注（TCH_DIMENSION2）与轴号（TCH_AXIS_LABEL）：私有布局解码与显示
// 合成对象的数值取自真实样例，期望的图元取自天正 2014 对同一对象的分解结果（docs/Tch-compatibility.md）
#include "Import/TchDimension.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <cmath>
#include <cstdio>

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

    // 分解结果保留一位小数
    bool Near(double a, double b, double tol = 0.11) { return std::abs(a - b) < tol; }
    bool Near(const TchGraphics::Point& p, double x, double y) { return Near(p.X, x) && Near(p.Y, y); }
    bool SameSegment(const TchGraphics::Segment& s, double x0, double y0, double x1, double y1)
    {
        return (Near(s.A, x0, y0) && Near(s.B, x1, y1)) || (Near(s.A, x1, y1) && Near(s.B, x0, y0));
    }
    template <class F> int Count(const std::vector<TchGraphics::Segment>& v, F f) { int n = 0; for (const auto& s : v) n += f(s); return n; }

    struct DimSpec
    {
        XYZ Position;
        double Angle = 0, Offset = 0;
        std::vector<double> Spans;
        int Flags = 0x80;
        double ArcRadius = 0, MeasuredRadius = 0;
        int Version = 1;
        std::vector<std::pair<int, std::string>> Overrides;
    };

    // 按基类与版本 1 布局写出
    std::unique_ptr<UnknownEntity> DimObject(const DimSpec& s, Handle style = 0x295)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_DIMENSION2";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(static_cast<std::uint8_t>(s.Version));
        r.WriteBitDouble(s.Position.X); r.WriteBitDouble(s.Position.Y); r.WriteBitDouble(s.Position.Z);
        r.WriteBitDouble(s.Angle); r.WriteBitDouble(s.Offset);
        r.WriteBitShort(static_cast<std::int16_t>(s.Spans.size()));
        for (double v : s.Spans) r.WriteBitDouble(v);
        r.WriteBitShort(static_cast<std::int16_t>(s.Flags));
        if (s.Flags & 4)
        {
            r.WriteBitShort(static_cast<std::int16_t>(s.Overrides.size()));
            for (const auto& o : s.Overrides) r.WriteBitShort(static_cast<std::int16_t>(o.first));
        }
        if (s.Flags & 0x10) { r.WriteBitDouble(s.ArcRadius); r.WriteBitDouble(s.MeasuredRadius); }
        if (s.Flags & 0x80) r.WriteBitShort(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        for (const auto& o : s.Overrides) r.WriteVariableText(o.second);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, style);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    // 样例 665：水平、向下偏移 33（×100），一段 6000（384 位）
    DimSpec Horizontal() { return {{76000.23060409744, 20146.285235553034, 0}, 0.0, -33.0, {6000.0}}; }
    // 样例 893：斜向两段（514 位）
    DimSpec Slanted() { return {{48405.40775533178, 27077.339220497648, 0}, 6.55009337793222, -4.600801015033991, {1233.7270651686754, 892.7457050480026}}; }
    // 样例 892：弧形（角度）标注（580 位）
    DimSpec Angular()
    {
        DimSpec s{{53911.69800612089, 24997.712722353437, 0}, 5.63648267591233, 2.0, {1.7550110794940232}, 0x90};
        s.ArcRadius = 1356.3001525396746; s.MeasuredRadius = 1556.3001525396746;
        return s;
    }

    struct RadiusSpec
    {
        double Leader = -26;
        double TipX = 58897.61646244189, TipY = 26077.139280330914;
        int Flags = 0;
        std::string Text;
        double TextX = 0, TextY = 0;
    };

    // 按版本 1 布局写出；样例 37B（424 位），圆心 (58403.79, 26884.14)
    std::unique_ptr<UnknownEntity> RadiusObject(const RadiusSpec& s, Handle style = 0x37a)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_RADIUSDIM";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(1); r.WriteBitShort(static_cast<std::int16_t>(s.Flags)); r.WriteBitDouble(s.Leader);
        r.WriteBitDouble(58403.7946188372); r.WriteBitDouble(26884.135436352582); r.WriteBitDouble(0);
        r.WriteRawDouble(s.TipX); r.WriteRawDouble(s.TipY);
        if (s.Flags & 2) { r.WriteRawDouble(s.TextX); r.WriteRawDouble(s.TextY); }
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        if (s.Flags & 4) r.WriteVariableText(s.Text);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, style);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    // 天正分解出的 TEXT（左下对齐，插入点 x, y、旋转、textbox 宽度、字高 297.5）换算到给定附着点
    TchGraphics::Point TextAnchor(double x, double y, double rotation, double width, int attachment)
    {
        const double dx = std::cos(rotation), dy = std::sin(rotation), h = 297.5 / 2;
        const double along = attachment == 6 ? width : attachment == 5 ? width / 2 : 0;
        return {x + dx * along - dy * h, y + dy * along + dx * h};
    }

    // 按版本 5 布局写出；样例 664（741 位）
    std::unique_ptr<UnknownEntity> AxisLabel(int mode = 0)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_AXIS_LABEL";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(0);
        r.WriteBitLong(1); r.WriteBitLong(0); r.WriteBit(false);          // 属性包：一项布尔
        r.WriteByte(5); r.WriteBitShort(static_cast<std::int16_t>(mode));
        r.WriteRawDouble(76000.23060409744); r.WriteRawDouble(20146.285235553034);
        r.WriteRawDouble(76000.23060409744); r.WriteRawDouble(23878.43364205037);
        r.WriteBitDouble(0);
        r.WriteByte(3);
        for (double spacing : {2400.0, 3600.0, 0.0}) { r.WriteBitShort(0); r.WriteBitDouble(spacing); }
        r.WriteBitDouble(0); r.WriteBitDouble(40); r.WriteBitDouble(40); r.WriteBitDouble(4);
        r.WriteBitShort(7); r.WriteBitShort(0x3361);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        for (const char* t : {"当前轴号列表", "1", "2", "3"}) r.WriteVariableText(t);
        raw.TextBits = r.PositionInBits(); raw.Text = r.Take();
        r.WriteHandle(DwgRef::HardPointer, 0x296);
        r.WriteHandle(DwgRef::HardPointer, 0x297);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
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
    const TchDimStyle style;    // _TCH_ARCH：DIMEXO 3、DIMEXE 2.5、DIMASZ 1、DIMTXT 3.5、DIMGAP 1、精度 0 / 角度 2

    // ── 水平标注 ──
    auto horizontal = DimObject(Horizontal());
    Check(horizontal->Raw.Dwg.MainBits == 384, "synthetic horizontal dimension reproduces the 384-bit real layout");
    TchDimension d;
    std::string why;
    Check(DecodeDimension(*horizontal, d, &why) && d.Spans.size() == 1 && Near(d.Offset, -33) && Near(d.Scale, 100) &&
          d.DimStyle == 0x295, "horizontal dimension decodes position, offset, span, scale and dimension style");
    TchGraphics g = BuildDimension(d, style);
    Check(Count(g.Lines, [](auto& s) { return SameSegment(s, 76000.2, 19846.3, 76000.2, 16596.3); }) == 1 &&
          Count(g.Lines, [](auto& s) { return SameSegment(s, 82000.2, 19846.3, 82000.2, 16596.3); }) == 1,
          "extension lines start DIMEXO from the points and pass the dimension line by DIMEXE");
    Check(Count(g.Lines, [](auto& s) { return SameSegment(s, 76000.2, 16846.3, 82000.2, 16846.3); }) == 1,
          "dimension line sits at offset x scale");
    Check(g.Ticks.size() == 2 && Near(g.Ticks[0].A, 76050.2, 16896.3) && Near(g.Ticks[0].B, 75950.2, 16796.3) && Near(g.Ticks[0].Width, 40),
          "45-degree ticks are DIMASZ wide with width 40");
    Check(g.Texts.size() == 1 && g.Texts[0].Value == "6000" && Near(g.Texts[0].Center, 79000.2, 17121.3) &&
          Near(g.Texts[0].Height, 297.5) && Near(g.Texts[0].Rotation, 0),
          "text is centred DIMTXT/2 + DIMGAP above the dimension line");

    // ── 斜向两段 ──
    auto slanted = DimObject(Slanted());
    Check(slanted->Raw.Dwg.MainBits == 514, "synthetic slanted dimension reproduces the 514-bit real layout");
    Check(DecodeDimension(*slanted, d), "slanted dimension decodes");
    g = BuildDimension(d, style);
    Check(Count(g.Lines, [](auto& s) { return SameSegment(s, 48484.5, 26788.0, 48592.7, 26392.4); }) == 1,
          "slanted extension line matches the Tianzheng explode");
    Check(g.Texts.size() == 2 && g.Texts[0].Value == "1234" && Near(g.Texts[0].Center, 49049.2, 27061.5) &&
          g.Texts[1].Value == "893" && Near(g.Texts[1].Center, 50074.8, 27341.9) && Near(g.Texts[0].Rotation, 0.266908, 1e-5),
          "each span gets rounded text along the normalised direction");

    // ── 弧形（角度） ──
    auto angular = DimObject(Angular(), 0x37a);
    Check(angular->Raw.Dwg.MainBits == 580, "synthetic angular dimension reproduces the 580-bit real layout");
    Check(DecodeDimension(*angular, d) && d.IsArc(), "angular dimension decodes the arc radii");
    g = BuildDimension(d, style);
    Check(g.Arcs.size() == 1 && Near(g.Arcs[0].Center, 54849.5, 26239.8) && Near(g.Arcs[0].Radius, 1356.3) &&
          Near(g.Arcs[0].Start, 4.13943, 1e-4) && Near(g.Arcs[0].End, 5.74695, 1e-4),
          "arc centre is on the left normal; the arc stops one arrow chord from each end");
    Check(g.Arrows.size() == 2 && Near(g.Arrows[0].C, 54032.2, 25157.3), "arrow tips sit on the extension lines");
    Check(g.Arrows.size() == 2 && (Near(g.Arrows[0].A, 54104.6, 25086.4) || Near(g.Arrows[0].A, 54123.7, 25113.7)),
          "arrow base matches the Tianzheng solid");
    Check(Count(g.Lines, [](auto& s) { return SameSegment(s, 54092.5, 25237.1, 54182.8, 25356.8); }) == 1,
          "radial extension line runs from DIMEXO inside the measured radius to DIMEXE past the arc");
    Check(g.Texts.size() == 1 && g.Texts[0].Value == "100.55\xC2\xB0" && Near(g.Texts[0].Center, 55102.8, 25161.6) &&
          Near(g.Texts[0].Rotation, 0.230803, 1e-5), "angle text uses two decimals and a degree sign");

    // ── 替代文字 ──
    DimSpec withText = Horizontal();
    withText.Flags = 0x84; withText.Overrides = {{0, "轴距"}};
    Check(DecodeDimension(*DimObject(withText), d) && d.TextOverrides.size() == 1 &&
          BuildDimension(d, style).Texts[0].Value == "轴距", "text overrides come from the text stream");

    // ── 拒绝 ──
    Check(AcceptedTruncations(*slanted, [](auto& e, auto& o, std::string* w) { return DecodeDimension(e, o, w); }, d) == 0,
          "every truncated dimension payload is rejected");
    DimSpec v2 = Horizontal(); v2.Version = 2;
    Check(!DecodeDimension(*DimObject(v2), d), "encoded version-2 dimensions are not guessed");

    // ── 轴号 ──
    auto axis = AxisLabel();
    Check(axis->Raw.Dwg.MainBits == 741, "synthetic axis label reproduces the 741-bit real layout");
    TchAxisLabel a;
    Check(DecodeAxisLabel(*axis, a, &why) && a.Axes.size() == 3 && a.Axes[0].Label == "1" && a.Axes[2].Label == "3" &&
          a.TextStyle == 0x296, "axis labels decode spacings, labels (after the base property bag name) and text style");
    g = BuildAxisLabel(a);
    Check(g.Circles.size() == 6 && Near(g.Circles[0].Center, 76000.2, 15746.3) && Near(g.Circles[0].Radius, 400) &&
          Near(g.Circles[3].Center, 78400.2, 28278.4) && Near(g.Circles[4].Center, 82000.2, 15746.3),
          "label circles sit beyond the extension at both ends of every axis");
    Check(Count(g.Lines, [](auto& s) { return SameSegment(s, 76000.2, 20146.3, 76000.2, 16146.3); }) == 1 &&
          Count(g.Lines, [](auto& s) { return SameSegment(s, 82000.2, 23878.4, 82000.2, 27878.4); }) == 1,
          "extension lines run 40 x scale outward from both axis ends");
    Check(g.Texts.size() == 6 && g.Texts[2].Value == "2" && Near(g.Texts[2].Center, 78400.2, 15746.3), "labels are centred in the circles");
    Check(AcceptedTruncations(*axis, [](auto& e, auto& o, std::string* w) { return DecodeAxisLabel(e, o, w); }, a) == 0,
          "every truncated axis label payload is rejected");
    Check(!DecodeAxisLabel(*AxisLabel(1), a), "unverified axis label modes are not drawn");

    // ── 半径标注：样例与 6 个变体，期望值取自天正 2014 的分解结果 ──
    {
        const TchDimStyle arrowStyle;   // _TCH_ARROW 与 _TCH_ARCH 数值相同
        struct Case
        {
            const char* Name;
            RadiusSpec Spec;
            double EndX, EndY, BaseX, BaseY;              // 引线末端、箭头底部
            double TextX, TextY, Rotation, Width;          // 分解出的 TEXT
            const char* Value;
            int Attachment;
        };
        const double c = 58403.7946188372, cy = 26884.135436352582, rr = 946.0987311348023;
        const Case cases[] = {
            {"sample: leader 26 outward", {}, 60254.7, 23859.4, 59002.0, 25906.5, 59923.9, 24641.8, 5.26153, 740, "R946", 6},
            {"shorter leader", {-15}, 59680.6, 24797.7, 59002.0, 25906.5, 59349.8, 25580.1, 5.26153, 740, "R946", 6},
            {"positive leader points to the centre (text flipped)", {20}, 57853.7, 27783.1, 58793.2, 26247.7, 58013.6, 27763.7, 5.26153, 740, "R946", 4},
            {"left-pointing leader flips the text",
             {-26, c + rr * std::cos(2.6), cy + rr * std::sin(2.6)}, 55365.2, 28712.2, 57421.7, 27475.0, 55516.0, 28768.8, 5.74159, 740, "R946", 4},
            {"radius 500", {-26, c + 500 * std::cos(-1.0215), cy + 500 * std::sin(-1.0215)}, 60022.3, 24240.2, 58769.3, 26287.1,
             59686.2, 25031.0, 5.26169, 750, "R500", 6},
            {"text override (flag 0x04)", {-26, 58897.61646244189, 26077.139280330914, 4, "ABC"}, 60254.7, 23859.4, 59002.0, 25906.5,
             60007.5, 24505.3, 5.26153, 580, "ABC", 6},
            {"text position (flag 0x02) is the text centre", {-26, 58897.61646244189, 26077.139280330914, 2, "", 60500, 24000},
             60254.7, 23859.4, 59002.0, 25906.5, 60180.0, 24238.0, 5.26153, 740, "R946", 5},
        };
        TchRadiusDim rd;
        auto sample = RadiusObject({});
        Check(sample->Raw.Dwg.MainBits == 424, "synthetic radius dimension reproduces the 424-bit real layout");
        for (const auto& k : cases)
        {
            std::string label = std::string("radius dimension ") + k.Name;
            if (!DecodeRadiusDim(*RadiusObject(k.Spec), rd, &why)) { Check(false, (label + ": decode " + why).c_str()); continue; }
            g = BuildRadiusDim(rd, arrowStyle);
            const TchGraphics::Point anchor = TextAnchor(k.TextX, k.TextY, k.Rotation, k.Width, k.Attachment);
            const bool ok = g.Lines.size() == 1 && SameSegment(g.Lines[0], k.BaseX, k.BaseY, k.EndX, k.EndY) &&
                g.Arrows.size() == 1 && Near(g.Arrows[0].C, rd.TipX, rd.TipY) &&
                Near(std::hypot(g.Arrows[0].A.X - g.Arrows[0].B.X, g.Arrows[0].A.Y - g.Arrows[0].B.Y), 66, 0.1) &&
                g.Texts.size() == 1 && g.Texts[0].Value == k.Value && g.Texts[0].Attachment == k.Attachment &&
                Near(g.Texts[0].Center, anchor.X, anchor.Y) && Near(std::remainder(g.Texts[0].Rotation - k.Rotation, 2 * 3.14159265358979), 0, 1e-4);
            Check(ok, label.c_str());
        }
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeRadiusDim(e, o, w); }, rd) == 0,
              "every truncated radius dimension payload is rejected");
    }

    // ── 端到端：DWG → Scene → 普通几何 DWG/DXF ──
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027);
    db.CreateDefaults();
    db.Classes.push_back(Class("TCH_DIMENSION2", "TDbDimension2", 500));
    db.Classes.push_back(Class("TCH_AXIS_LABEL", "TDbAxisLabelSet", 501));
    auto* dimStyle = db.FindTableEntry<DimensionStyle>(db.DimensionStyles(), "Standard");
    dimStyle->ExtensionLineOffset = 3; dimStyle->ExtensionLineExtension = 2.5; dimStyle->ArrowSize = 1;
    dimStyle->TextHeight = 3.5; dimStyle->DimensionLineGap = 1; dimStyle->DecimalPlaces = 0; dimStyle->AngularDecimalPlaces = 2;
    db.AddEntity(db.ModelSpace(), DimObject(Horizontal(), dimStyle->ObjectHandle));
    db.AddEntity(db.ModelSpace(), DimObject(Angular(), dimStyle->ObjectHandle));
    db.Classes.push_back(Class("TCH_RADIUSDIM", "TDbRadiusDim", 502));
    db.AddEntity(db.ModelSpace(), RadiusObject({}, dimStyle->ObjectHandle));
    auto label = AxisLabel();
    label->Raw.Dwg.Handles.clear();
    {
        DwgBitWriter w(CadVersion::AC1027, Codec::CodePage::Gbk);
        w.WriteHandle(DwgRef::HardPointer, db.FindTableEntry(db.TextStyles(), "Standard")->ObjectHandle);
        label->Raw.Dwg.HandleBits = w.PositionInBits(); label->Raw.Dwg.Handles = w.Take();
    }
    db.AddEntity(db.ModelSpace(), std::move(label));
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    Check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 4, "both dimensions, the radius dimension and the axis label appear in the scene");

    int lines = 0, ticks = 0, arcs = 0, solids = 0, texts = 0, circles = 0, narrowTexts = 0;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
        if (!insert || !insert->GetBlock()) return;
        for (const auto& e : insert->GetBlock()->GetEntities())
        {
            lines += dynamic_cast<const MiniCAD::LineEntity*>(e.get()) != nullptr;
            ticks += dynamic_cast<const MiniCAD::PolylineEntity*>(e.get()) != nullptr;
            arcs += dynamic_cast<const MiniCAD::ArcEntity*>(e.get()) != nullptr;
            circles += dynamic_cast<const MiniCAD::CircleEntity*>(e.get()) != nullptr && dynamic_cast<const MiniCAD::ArcEntity*>(e.get()) == nullptr;
            solids += dynamic_cast<const MiniCAD::SolidEntity*>(e.get()) != nullptr;
            if (const auto* t = dynamic_cast<const MiniCAD::MTextEntity*>(e.get()))
            {
                ++texts;
                const auto* rec = scene.GetTextStyleTable().Find(t->GetStyleId());
                narrowTexts += rec != nullptr && Near(rec->WidthFactor, 0.7, 1e-9);
            }
        }
    });
    // 水平：2 条尺寸界线 + 1 条尺寸线、2 个斜线、1 个文字；弧形：2 条界线、1 段弧、2 个箭头、1 个文字；
    // 半径：1 条引线、1 个箭头、1 个文字；轴号：6 条线、6 个圆、6 个文字
    Check(lines == 12 && ticks == 2 && arcs == 1 && solids == 3 && circles == 6 && texts == 9,
          "display blocks contain the expected lines, ticks, arc, arrows, circles and texts");
    Check(narrowTexts == 3, "dimension texts use a 0.7 width-factor variant of the dimension text style");

    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf})
    {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        Check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 4,
              "displayed dimensions and axis label survive plain export and reimport");
    }
    return g_failed;
}
