#include "TchCipherWriter.h"
#include "Import/TchAnnotation.h"
#include "Import/TchColumn.h"
#include "Import/TchShape.h"
#include "Import/TchSheet.h"
#include "Import/TchStair.h"
#include "Import/TchWall.h"
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgWriter.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <string>
#include <vector>

using namespace MiniDWG;
namespace Tch = MiniCAD::Tch;
using TchTest::CipherWriter;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    bool Near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

    // 对称轴 版本 2：两端（二维点）、横线长、距端点、间距。guidKey2 ≥ 0 时伪 GUID 写成与加密所用不同的第二个密钥
    std::unique_ptr<UnknownEntity> SymmetryObject(int baseKey, int classKey, int key2, bool extraBit = false, int guidKey2 = -1)
    {
        CipherWriter w;
        w.Base(baseKey, key2, 100, guidKey2 >= 0 ? guidKey2 : key2);
        w.Main.WriteByte(2);
        w.Main.WriteByte(static_cast<std::uint8_t>(classKey));
        w.Key = classKey;
        w.Point2(1000, 2000);
        w.Point2(11000, 2000);
        w.Double(8); w.Double(2); w.Double(2.5);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_SYMMETRY");
    }

    // 内视符号（符号基类版本 4 之后为明文字段）：类型 0，四个编号时 BL 写 1 并带版本 1 与下、左、右三个编号
    std::unique_ptr<UnknownEntity> InSightObject(double angle, const char* number, const char* bottom, const char* left, const char* right)
    {
        CipherWriter w;
        w.Base(0x12, 94, 100, 94);
        w.Main.WriteByte(4);
        w.Main.WriteByte(0x12);
        w.Key = 0x12;
        w.Handle(0x82a); w.Double(3.5); w.Double(0); w.Handle(0x289); w.Short(7);
        w.Main.WriteBitShort(0);
        w.Text.WriteVariableText("");
        w.Text.WriteVariableText(number);
        w.Main.WriteBitDouble(36589); w.Main.WriteBitDouble(32758); w.Main.WriteBitDouble(0);
        w.Main.WriteBitDouble(5); w.Main.WriteBitDouble(angle); w.Main.WriteBitDouble(0);
        w.Main.WriteBitLong(1);
        w.Main.WriteByte(2);
        w.Main.WriteBitDouble(0);
        w.Main.WriteByte(1);
        w.Text.WriteVariableText(bottom);
        w.Text.WriteVariableText(left);
        w.Text.WriteVariableText(right);
        return w.Take("TCH_TDBINSIGHT");
    }

    // 剖切符号（剖切符号 版本 3）：从 (0, 0) 竖直向下到 (0, −10000)，投射方向朝左；style 写进扩展数据表的 T20d37
    std::unique_ptr<UnknownEntity> SectionObject(int style, int kind, const char* label2 = "", int flags = 0, double endView = 0)
    {
        CipherWriter w;
        w.Base(0x12, 94, 100, 94, {{"T20d35", 0}, {"T20d37", style}});
        w.Main.WriteByte(4);
        w.Main.WriteByte(0x12);
        w.Key = 0x12;
        w.Handle(0x82a); w.Double(3.5); w.Double(0); w.Handle(0x289); w.Short(7);
        w.Main.WriteByte(3);
        w.Main.WriteByte(0x12);
        w.Byte(kind);
        w.Double(std::numbers::pi);
        w.Byte(2);
        w.Point2(0, 0); w.Point2(0, -10000);
        w.Text.WriteVariableText("1");
        w.Point2(0, 0); w.Point2(0, 0);
        w.Text.WriteVariableText(label2);
        w.Point2(0, 0); w.Point2(0, 0);
        w.Double(3.5);
        w.Short(flags);
        w.Handle(0x82a);
        w.Double(-1000);
        w.Double(endView);
        w.Byte(0);
        w.Short(108); w.Double(3); w.Double(10); w.Byte(2); w.Double(0);
        return w.Take("TCH_SYMB_SECTION");
    }

    std::unique_ptr<UnknownEntity> ColumnObject(int version, int key, int flags = 0, bool extraBit = false, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Main.WriteByte(static_cast<std::uint8_t>(version));
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Point3(1300, 2300 + oy, 0);
        w.Main.WriteByte(1);
        w.Main.WriteBitShort(4);
        const double pts[4][2] = {{1000, 2000 + oy}, {1600, 2000 + oy}, {1600, 2600 + oy}, {1000, 2600 + oy}};
        for (const auto& p : pts) { w.Main.WriteRawDouble(p[0]); w.Main.WriteRawDouble(p[1]); w.Main.WriteBitDouble(0); }
        w.Double(3000);
        w.Byte(1);
        w.Byte(1); w.Handle(0);
        w.Byte(60);
        w.Short(flags);
        w.Handle(0);
        if (flags & 8) { w.Text.WriteVariableText("Z1"); w.Point2(1300, 2300 + oy); w.Handle(0); w.Handle(0); w.Double(3.5); }
        w.Byte(0);
        w.Double(80);
        w.Handle(0);
        if (version >= 8) { w.Double(0); w.Text.WriteVariableText(""); w.Text.WriteVariableText(""); }
        if (version >= 9) w.Double(50);
        if (version >= 10) for (int i = 0; i < 7; ++i) w.Double(i);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_COLUMN");
    }

    std::unique_ptr<UnknownEntity> RectObject(int key, int flag, bool extraBit = false, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Main.WriteByte(3);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Short(flag);
        w.Point3(500, 700 + oy, 0);
        w.Double(0.5); w.Double(400); w.Double(200); w.Double(0);
        w.Point3(0, 0, 1);
        w.Byte(0);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_RECT");
    }

    std::unique_ptr<UnknownEntity> WallObject(bool curtain, int flags, int key = 0x12, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Main.WriteByte(12);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Point3(0, oy, 0); w.Point3(6000, oy, 0);
        w.Double(0);
        w.Number(std::to_string(flags), [](MiniDWG::DwgBitWriter& o, const std::string& t) { o.WriteBitLong(std::stoi(t)); });
        w.Double(100); w.Double(100); w.Double(3000);
        w.Handle(0); w.Handle(0);
        for (int i = 0; i < 4; ++i) w.Handle(0);
        w.Byte(0);
        w.Short(0);
        w.Byte(curtain ? 20 : 40);
        w.Byte(0);
        w.Double(0);
        for (int i = 0; i < 4; ++i) w.Double(0);
        for (int i = 0; i < 6; ++i) w.Handle(0);
        w.Byte(0);
        w.Number("0", [](MiniDWG::DwgBitWriter& o, const std::string& t) { o.WriteBit(t != "0"); });
        w.Text.WriteVariableText("");
        w.Double(0);
        w.Handle(0); w.Handle(0);
        w.Byte(0);
        if (curtain)
        {
            w.Main.WriteByte(3);
            w.Main.WriteByte(static_cast<std::uint8_t>(key));
            w.Byte(1);
            for (int i = 0; i < 6; ++i) w.Double(10 * i);
            w.Byte(2); w.Byte(3);
            if (!(flags & 0x100)) { w.Double(1000); w.Double(2000); }
            if (!(flags & 0x10000)) { w.Double(500); w.Double(1500); w.Double(2500); }
            w.Handle(0); w.Handle(0); w.Handle(0);
        }
        return w.Take(curtain ? "TCH_CURTAIN_WALL" : "TCH_WALL");
    }

    bool NearPoint(const Tch::TchGraphics::Point& p, double x, double y, double tol = 1e-6) { return Near(p.X, x, tol) && Near(p.Y, y, tol); }

    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        const std::u8string u8(path.begin(), path.end());      // 路径可能含中文，按 UTF-8 解释
        std::ifstream f(std::filesystem::path(u8), std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }

    // 按类型调用对应的解码函数；没有解码函数的类型返回 -1
    int DecodeByType(const UnknownEntity& e, std::string& why)
    {
        const std::string& n = e.Raw.DxfName;
        auto run = [&](auto decode, auto out) { return decode(e, out, &why) ? 1 : 0; };
        if (n == "TCH_WALL") return run(Tch::DecodeWall, Tch::TchWall{});
        if (n == "TCH_CURTAIN_WALL") return run(Tch::DecodeCurtainWall, Tch::TchWall{});
        if (n == "TCH_RECT") return run(Tch::DecodeRect, Tch::TchRect{});
        if (n == "TCH_OPENING") return run(Tch::DecodeOpening, Tch::TchOpening{});
        if (n == "TCH_COLUMN") return run(Tch::DecodeColumn, Tch::TchColumn{});
        if (n == "TCH_TEXT" || n == "TCH_MTEXT") return run(Tch::DecodeText, Tch::TchText{});
        if (n == "TCH_AXIS_LABEL") return run(Tch::DecodeAxisLabel, Tch::TchAxisLabel{});
        if (n == "TCH_DIMENSION2") return run(Tch::DecodeDimension, Tch::TchDimension{});
        if (n == "TCH_RECTSTAIR") return run(Tch::DecodeRectStair, Tch::TchRectStair{});
        if (n == "TCH_COORD") return run(Tch::DecodeCoord, Tch::TchCoord{});
        if (n == "TCH_ARROW") return run(Tch::DecodeArrow, Tch::TchArrow{});
        if (n == "TCH_SYMB_SECTION") return run(Tch::DecodeSection, Tch::TchSection{});
        if (n == "TCH_INDEXPOINTER") return run(Tch::DecodeIndexPointer, Tch::TchIndexPointer{});
        if (n == "TCH_DRAWINGNAME") return run(Tch::DecodeDrawingName, Tch::TchDrawingName{});
        if (n == "TCH_ELEVATION") return run(Tch::DecodeElevation, Tch::TchElevation{});
        if (n == "TCH_MULTILEADER") return run(Tch::DecodeMultiLeader, Tch::TchMultiLeader{});
        if (n == "TCH_COMPOSING") return run(Tch::DecodeComposing, Tch::TchComposing{});
        if (n == "TCH_DRAWINGINDEX") return run(Tch::DecodeDrawingIndex, Tch::TchDrawingIndex{});
        if (n == "TCH_NORTHTHUMB") return run(Tch::DecodeNorthThumb, Tch::TchNorthThumb{});
        if (n == "TCH_SYMMETRY") return run(Tch::DecodeSymmetry, Tch::TchSymmetry{});
        if (n == "TCH_SHEET") return run(Tch::DecodeSheet, Tch::TchSheet{});
        if (n == "TCH_TDBINSIGHT") return run(Tch::DecodeInSight, Tch::TchInSight{});
        return -1;
    }
}

int main()
{
    // 解密参数（密钥 18、第二个密钥 94～97 时基数 0x320 + 3 × (密钥2 − 94)、异或 7）
    {
        const auto p94 = Tch::Detail::CipherParams(0x12, 94), p97 = Tch::Detail::CipherParams(0x12, 97);
        Check(p94.second == 0x320 && ((p94.first >> 8) & 0xff) == 7 && p97.second == 0x329 && ((p97.first >> 8) & 0xff) == 7,
              "cipher parameters for key 18 match the sample drawing");
        Check(Tch::Detail::CipherParams(0x12, 0) == std::pair{0x72, 0x76} && Tch::Detail::CipherParams(0x10, 50) == std::pair{0x528, 0x41} &&
              Tch::Detail::CipherParams(0x11, 3) == std::pair{0x9d5, 0xdf} && Tch::Detail::CipherParams(0x11, -1) == std::pair{0x9d2, 0xc6},
              "cipher parameter table branches");
        int k2 = -1;
        Check(Tch::Detail::Layout2Wcs(11, "{2DC09ED6-F2B9-4975-BA2E-3D4B5A8C9E10}", k2) && k2 == 90 &&
              Tch::Detail::Layout2Wcs(9, "{2DC09ED6-F2B9-4975-BA2E-3D4B5A8C9E10}", k2) && k2 == 0 &&
              !Tch::Detail::Layout2Wcs(19, "{2DC09ED6-F2B9-4975-BA2E-3D4B5A8C9E10}", k2),
              "second key from the pseudo GUID (digits at characters 4, 16, 30; a letter counts as 0)");
    }

    // 合成对称轴：各种密钥组合都恰好读完并还原数值（明文 15、不打乱点的 16、去两个数字的 17、去三个的 18 / 19、反转的 20 起）
    {
        const int keys[][3] = {{0x12, 0x12, 94}, {0x12, 0x14, 94}, {0x13, 0x11, 5}, {0x12, 0x0f, 94}, {0x15, 0x10, 60}, {0x14, 0x13, 0}};
        for (const auto& k : keys)
        {
            Tch::TchSymmetry m;
            std::string why;
            const std::string label = "synthetic encrypted symmetry decodes (base key " + std::to_string(k[0]) + ", class key " +
                                      std::to_string(k[1]) + ", second key " + std::to_string(k[2]) + ")";
            const bool ok = Tch::DecodeSymmetry(*SymmetryObject(k[0], k[1], k[2]), m, &why);
            Check(ok && Near(m.X1, 1000) && Near(m.Y1, 2000) && Near(m.X2, 11000) && Near(m.Y2, 2000) && Near(m.Scale, 100) &&
                  Near(m.TickLength, 8) && Near(m.Gap, 2) && Near(m.Spacing, 2.5), (label + (ok ? "" : ": " + why)).c_str());
        }
        Tch::TchSymmetry m;
        Check(!Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94, true), m), "an encrypted object with a leftover bit is rejected");
        Check(!Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94, false, 93), m) || !Near(m.TickLength, 8), "a wrong second key does not restore the values");

        // 天正的画法：两端各两条横线，距端点 2 × 比例与 4.5 × 比例，长 8 × 比例
        Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94), m);
        const auto g = Tch::BuildSymmetry(m);
        std::vector<double> ticks;
        for (std::size_t i = 1; i < g.Lines.size(); ++i)
            if (Near(g.Lines[i].A.X, g.Lines[i].B.X) && Near(std::abs(g.Lines[i].A.Y - g.Lines[i].B.Y), 800)) ticks.push_back(g.Lines[i].A.X);
        std::sort(ticks.begin(), ticks.end());
        Check(g.Lines.size() == 5 && ticks.size() == 4 && Near(ticks[0], 1200) && Near(ticks[1], 1450) && Near(ticks[2], 10550) && Near(ticks[3], 10800),
              "symmetry ticks sit at both ends (gap and gap + spacing from each end)");
    }

    // 内视符号：合成对象的往返解码与画法
    {
        Tch::TchInSight s;
        std::string why;
        const bool ok = Tch::DecodeInSight(*InSightObject(-std::numbers::pi / 2, "A", "C", "D", "B"), s, &why);
        Check(ok && s.Type == 0 && s.Number == "A" && s.Bottom == "C" && s.Left == "D" && s.Right == "B" && Near(s.Scale, 100) &&
              Near(s.Radius, 5) && Near(s.X, 36589) && Near(s.Y, 32758) && s.TextMode == 2 && s.TextStyle == 0x82a,
              ("synthetic insight symbol decodes" + (ok ? std::string() : ": " + why)).c_str());

        // 四个编号：菱形（两块实心区域，顶点在 √2R）、两条 45° 分隔线；编号宽 2·0.6R / (2√2 + 1)、高 = 宽 / 宽高比，中心距圆心 R / 2
        const auto width = [](const std::string& t) { return 0.75 * static_cast<double>(t.size()); };
        auto g = Tch::BuildInSight(s, 0.6, 0, width);
        const double r = 500, d = r * std::sqrt(2.0), cx = 36589, cy = 32758;
        bool diamond = g.Fills.size() == 2;
        for (const auto& f : g.Fills)
            diamond = diamond && f.Points.size() == 6 && NearPoint(f.Points[0].P, cx, cy + r) && NearPoint(f.Points[1].P, cx, cy - r) &&
                      NearPoint(f.Points[2].P, cx, cy - d) && NearPoint(f.Points[4].P, cx, cy + d) && Near(std::abs(f.Points[0].Bulge), 1);
        diamond = diamond && NearPoint(g.Fills[0].Points[3].P, cx + d, cy) && NearPoint(g.Fills[1].Points[3].P, cx - d, cy) &&
                  Near(g.Fills[0].Points[0].Bulge, -1) && Near(g.Fills[1].Points[0].Bulge, 1);
        Check(diamond && g.Circles.size() == 1 && Near(g.Circles[0].Radius, r), "four-way insight symbol: diamond of two solid halves around the circle");
        bool diagonals = g.Lines.size() == 2;
        for (const auto& l : g.Lines)
            diagonals = diagonals && Near(std::abs(l.A.X - cx), r / std::sqrt(2.0), 1e-6) && Near(std::abs(l.A.Y - cy), r / std::sqrt(2.0), 1e-6);
        Check(diagonals, "four-way insight symbol: dividers at 45 degrees");
        const double w = 2 * 0.6 * r / (2 * std::sqrt(2.0) + 1), h = w / 0.75;
        bool texts = g.Texts.size() == 4;
        const double where[4][2] = {{cx, cy + r / 2}, {cx, cy - r / 2}, {cx - r / 2, cy}, {cx + r / 2, cy}};
        const char* order[4] = {"A", "C", "D", "B"};
        for (std::size_t i = 0; texts && i < 4; ++i)
            texts = g.Texts[i].Value == order[i] && NearPoint(g.Texts[i].Center, where[i][0], where[i][1]) && Near(g.Texts[i].Height, h, 1e-6) &&
                    Near(g.Texts[i].WidthFactor, 1, 1e-9) && g.Texts[i].Attachment == 5 && Near(g.Texts[i].Rotation, 0);
        Check(texts, "four-way insight symbol: numbers fit in the four quadrants (top A, bottom C, left D, right B)");

        // 转角 0：编号的位置随 Angle + π/2 转（上方的 A 到左边），文字仍水平；对象自己的比例优先于图纸设置
        Tch::DecodeInSight(*InSightObject(0, "A", "C", "D", "B"), s);
        g = Tch::BuildInSight(s, 0.6, 0, width);
        Check(g.Texts.size() == 4 && NearPoint(g.Texts[0].Center, cx - r / 2, cy) && NearPoint(g.Texts[3].Center, cx, cy + r / 2) &&
              Near(g.Texts[0].Rotation, 0), "rotated insight symbol: number positions turn with the symbol, text stays level");
        s.TextRatio = 0.3;
        Check(Near(Tch::BuildInSight(s, 0.6, 0, width).Texts[0].Height, h / 2, 1e-6), "the object's own text ratio overrides the drawing setting");

        // 只有上、下两个编号：只有沿 y 轴（三角朝向）的一条分隔线，编号距分隔线 0.4 × (圆在文字宽度处余下的高度 − 字高)
        Tch::DecodeInSight(*InSightObject(-std::numbers::pi / 2, "1", "2", "", ""), s);
        g = Tch::BuildInSight(s, 0.6, 0, width);
        const double a = 0.75, h2 = 0.6 * r / std::sqrt(a * a * 0.25 + 1.21), w2 = h2 * a;
        const double up = (std::sqrt(r * r - w2 * w2 / 4) - h2) * 0.4;
        Check(g.Lines.size() == 1 && Near(g.Lines[0].A.Y, cy) && Near(g.Lines[0].B.Y, cy) && g.Fills.size() == 2 && g.Texts.size() == 2 &&
              NearPoint(g.Texts[0].Center, cx, cy + up + h2 / 2, 1e-6) && NearPoint(g.Texts[1].Center, cx, cy - up - h2 / 2, 1e-6),
              "two-way insight symbol: one divider along the arrow axis, numbers above and below it");
        // 只有一个编号：一块实心区域、没有分隔线，编号在圆心
        Tch::DecodeInSight(*InSightObject(-std::numbers::pi / 2, "", "", "", "3"), s);
        g = Tch::BuildInSight(s, 0.6, 0, width);
        Check(g.Fills.size() == 1 && g.Lines.empty() && g.Texts.size() == 1 && g.Texts[0].Value == "3" && NearPoint(g.Texts[0].Center, cx, cy),
              "one-way insight symbol: a single solid pointer and the number at the center");
    }

    {
        Tch::TchColumn c;
        std::string why;
        const bool ok = Tch::DecodeColumn(*ColumnObject(10, 0x12, 8), c, &why);
        Check(ok && c.Outline.size() == 4 && Near(c.Outline[2].X, 1600) && Near(c.Outline[2].Y, 2600) && Near(c.Height, 3000) &&
              c.Material == 60 && Near(c.Scale, 50) && Near(c.Position.X, 1300),
              ("encrypted column version 10 with a label decodes" + (ok ? std::string() : ": " + why)).c_str());
        why.clear();
        Check(Tch::DecodeColumn(*ColumnObject(7, 0x0c), c, &why) && Near(c.Scale, 100) && Near(c.Height, 3000) && c.Outline.size() == 4,
              ("column version 7 with plain numbers takes the base scale" + (why.empty() ? std::string() : ": " + why)).c_str());
        Check(Tch::DecodeColumn(*ColumnObject(8, 0x13), c) && Tch::DecodeColumn(*ColumnObject(9, 0x14), c) && Near(c.Scale, 50),
              "column versions 8 and 9 decode");
        Check(!Tch::DecodeColumn(*ColumnObject(10, 0x12, 0, true), c), "an encrypted column with a leftover bit is rejected");
    }

    {
        Tch::TchRect t;
        std::string why;
        const bool ok = Tch::DecodeRect(*RectObject(0x12, 3), t, &why);
        Check(ok && t.Flag == 3 && Near(t.X, 500) && Near(t.Y, 700) && Near(t.Rotation, 0.5) && Near(t.Length, 400) && Near(t.Width, 200),
              ("encrypted rectangle decodes" + (ok ? std::string() : ": " + why)).c_str());
        Check(Tch::DecodeRect(*RectObject(0x0c, 0), t) && Tch::BuildRect(t).Paths.size() == 1, "rectangle with plain numbers decodes");
        Check(!Tch::DecodeRect(*RectObject(0x12, 0, true), t), "an encrypted rectangle with a leftover bit is rejected");
    }

    {
        Tch::TchWall w;
        std::string why;
        bool ok = Tch::DecodeWall(*WallObject(false, 0), w, &why);
        Check(ok && Near(w.End.X, 6000) && Near(w.LeftWidth, 100) && w.Material == 40, ("encrypted wall decodes" + (ok ? std::string() : ": " + why)).c_str());
        ok = Tch::DecodeCurtainWall(*WallObject(true, 0x4), w, &why);
        Check(ok && (w.Flags & 0x4) && w.Material == 20 && Near(w.RightWidth, 100), ("encrypted glass curtain wall decodes" + (ok ? std::string() : ": " + why)).c_str());
        Check(Tch::DecodeCurtainWall(*WallObject(true, 0x4 | 0x100 | 0x10000), w) && Tch::DecodeCurtainWall(*WallObject(true, 0, 0x0c), w),
              "curtain wall division lists follow the wall flags");
        Check(!Tch::DecodeWall(*WallObject(true, 0x4), w) && !Tch::DecodeCurtainWall(*WallObject(false, 0x4), w),
              "wall and curtain wall layouts are not mixed up");
    }

    {
        CadDatabase db;
        db.SetVersion(CadVersion::AC1024);
        db.CreateDefaults();
        db.Header.ModelSpaceExtMin = {-500, -500, 0};
        db.Header.ModelSpaceExtMax = {6500, 7200, 0};
        if (auto* vport = db.FindTableEntry<VPort>(db.VPorts(), "*Active"))
        {
            vport->Center = {3000, 3350};
            vport->ViewHeight = 8000;
            vport->AspectRatio = 1.8;
        }
        std::int16_t number = 500;
        for (const auto& [dxf, cpp] : {std::pair{"TCH_WALL", "TDbWall"}, {"TCH_CURTAIN_WALL", "TDbCurtainWall"}, {"TCH_COLUMN", "TDbColumn"},
                                       {"TCH_RECT", "TDbRect"}})
        {
            DxfClass c;
            c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
            c.ClassNumber = number++; c.IsAnEntity = true; c.ProxyFlags = 1; c.ItemClassId = 0x1f2;
            db.Classes.push_back(c);
        }
        std::vector<std::unique_ptr<UnknownEntity>> samples;
        samples.push_back(WallObject(false, 0, 0x12, 0));
        samples.push_back(WallObject(true, 0x4, 0x12, 1500));
        samples.push_back(WallObject(true, 0x100 | 0x10000, 0x13, 3000));
        samples.push_back(ColumnObject(10, 0x12, 8, false, 4000));
        samples.push_back(ColumnObject(7, 0x0c, 0, false, 4000));
        samples.push_back(RectObject(0x12, 3, false, 6000));
        const std::size_t count = samples.size();
        std::vector<Handle> handles;
        for (auto& e : samples)
        {
            e->Raw.Dwg.Version = CadVersion::AC1024;
            handles.push_back(db.AddEntity(db.ModelSpace(), std::move(e))->ObjectHandle);
        }
        DwgWriteOptions options;
        options.Version = CadVersion::AC1024;
        const auto bytes = WriteDwg(db, options);
        MiniCAD::Scene scene;
        std::size_t wallLines = 0;
        if (MiniCAD::ImportCad(bytes, scene))
            scene.ForEachObject([&](const MiniCAD::Object& object) {
                const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
                if (!insert || !insert->GetBlock() || insert->GetBlock()->GetName() != "*U_TCH_" + std::to_string(handles[0])) return;
                for (const auto& entity : insert->GetBlock()->GetEntities())
                    if (dynamic_cast<const MiniCAD::LineEntity*>(entity.get())) ++wallLines;
            });
        Check(wallLines == 4, ("a wall with no connected walls gets end lines at both free ends: " + std::to_string(wallLines) + " lines").c_str());
        if (const char* path = std::getenv("MINICAD_TCH_SAMPLE"))
        {
            const std::string p(path);
            std::ofstream(std::filesystem::path(std::u8string(p.begin(), p.end())), std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        std::size_t decoded = 0, found = 0;
        if (const auto back = ReadDwg(bytes))
            for (const auto& [handle, object] : back->Objects())
                if (const auto* e = dynamic_cast<const UnknownEntity*>(object.get()); e && e->Raw.DxfName.starts_with("TCH_"))
                {
                    std::string why;
                    ++found;
                    if (DecodeByType(*e, why) == 1) ++decoded;
                    else std::printf("  %s: %s\n", e->Raw.DxfName.c_str(), why.c_str());
                }
        Check(found == count && decoded == count, ("synthetic encrypted objects survive a DWG write and read: " + std::to_string(decoded) + " of " +
              std::to_string(found) + " decode, " + std::to_string(count) + " written").c_str());
    }

    // 剖切符号的新国标样式与旧样式的兼容
    {
        Tch::TchSection s;
        std::string why;
        const auto width = [](const std::string& t) { return 0.75 * static_cast<double>(t.size()); };
        const bool ok = Tch::DecodeSection(*SectionObject(1, 1), s, &why);
        Check(ok && s.NewStyle && s.Label == "1" && Near(s.Diameter, 10) && s.TextMode == 2 && Near(s.TextRatio, 0) &&
              Near(s.EndViewAngle, std::numbers::pi) && s.Points.size() == 2,
              ("synthetic section symbol with T20d37 = 1 decodes as the new style" + (ok ? std::string() : ": " + why)).c_str());
        // 剖面：剖切线两端在圆边（半径 0.5 × 直径 × 比例）截断，两端各一个朝左的三角（顶点在圆心左侧 √2R）与圆心处的编号
        auto g = Tch::BuildSectionNewStyle(s, 0.6, 0, width);
        const double r = 500, d = r * std::sqrt(2.0);
        Check(!g.Paths.empty() && g.Paths[0].Points.size() == 2 && Near(g.Paths[0].Width, 0) && NearPoint(g.Paths[0].Points[0].P, 0, -r) &&
              NearPoint(g.Paths[0].Points[1].P, 0, -10000 + r), "new-style section line stops at both circles");
        Check(g.Circles.size() == 2 && NearPoint(g.Circles[0].Center, 0, 0) && NearPoint(g.Circles[1].Center, 0, -10000) && Near(g.Circles[0].Radius, r) &&
              g.Fills.size() == 2 && NearPoint(g.Fills[0].Points[3].P, -d, 0) && NearPoint(g.Fills[1].Points[3].P, -d, -10000) &&
              g.Texts.size() == 2 && g.Texts[0].Value == "1" && NearPoint(g.Texts[0].Center, 0, 0) && NearPoint(g.Texts[1].Center, 0, -10000) &&
              g.Lines.empty(), "new-style section ends: circle, solid pointer toward the view and the number inside");

        // T20d37 = 0（或没有这一项）：旧样式，粗线与投射方向线
        Check(Tch::DecodeSection(*SectionObject(0, 1), s) && !s.NewStyle, "T20d37 = 0 keeps the old section style");
        g = Tch::BuildSection(s);
        Check(g.Circles.empty() && g.Fills.empty() && g.Paths.size() == 2 && Near(g.Paths[0].Width, 50) && NearPoint(g.Paths[0].Points[0].P, -600, 0),
              "old-style section: thick strokes with the view leg");

        // 断面：起点不画圆，改画沿投射方向长 7 × 比例、线宽 2 × 比例的粗线，剖切线起点不截断
        Tch::DecodeSection(*SectionObject(1, 0), s);
        g = Tch::BuildSectionNewStyle(s, 0.6, 0, width);
        Check(g.Circles.size() == 1 && NearPoint(g.Circles[0].Center, 0, -10000) && NearPoint(g.Paths[0].Points[0].P, 0, 0) &&
              g.Paths.size() >= 2 && Near(g.Paths[1].Width, 200) && NearPoint(g.Paths[1].Points[1].P, -700, 0),
              "new-style cross section: a thick bar at the start instead of a circle");

        // 有索引图号：圆内水平线上编号、线下图号；两端不垂直（标志 8）时终点用另存的投射方向
        Tch::DecodeSection(*SectionObject(1, 1, "A01", 2 | 8, 0), s);
        g = Tch::BuildSectionNewStyle(s, 0.6, 0, width);
        Check(s.Label2 == "A01" && Near(s.EndViewAngle, 0) && g.Lines.size() == 2 && g.Texts.size() == 4 && g.Texts[1].Value == "A01" &&
              NearPoint(g.Fills[1].Points[3].P, d, -10000), "new-style section with a sheet number and a separate end view direction");
    }

    // 本机样图（T30 写的 2007 格式，不在版本库里）：47 个天正对象（20 类）全部解出；图纸设置为版本 11
    {
        const auto data = ReadFile(std::string(MINICAD_TEST_DATA_DIR) + "/tianzheng2.dwg");
        if (data.empty()) std::printf("[SKIP] tianzheng2.dwg（没有样图）\n");
        else if (const auto db = ReadDwg(data); !db) Check(false, "tianzheng2.dwg reads");
        else
        {
            std::map<std::string, std::pair<int, int>> counts;     // 类型 → (对象数, 解出数)
            for (const auto& [handle, object] : db->Objects())
            {
                const auto* e = dynamic_cast<const UnknownEntity*>(object.get());
                if (!e || e->Raw.DxfName.rfind("TCH_", 0) != 0) continue;
                std::string why;
                const int r = DecodeByType(*e, why);
                if (r < 0) continue;
                ++counts[e->Raw.DxfName].first;
                counts[e->Raw.DxfName].second += r;
                if (r == 0) std::printf("  %llx %s: %s\n", static_cast<unsigned long long>(handle), e->Raw.DxfName.c_str(), why.c_str());
            }
            int total = 0, decoded = 0;
            for (const auto& [name, c] : counts) { total += c.first; decoded += c.second; }
            Check(total == 47 && decoded == total && counts.size() == 20, ("tianzheng2.dwg: " + std::to_string(decoded) + " of " +
                  std::to_string(total) + " Tianzheng objects in " + std::to_string(counts.size()) + " types decode").c_str());

            Tch::TchInSight s;
            const auto* inSight = db->FindAs<UnknownEntity>(0x831);
            Check(inSight && Tch::DecodeInSight(*inSight, s) && s.Number == "A" && s.Bottom == "C" && s.Left == "D" && s.Right == "B" &&
                  Near(s.Radius, 5) && Near(s.Angle, -std::numbers::pi / 2) && s.TextMode == 2 && Near(s.TextRatio, 0),
                  "tianzheng2.dwg: the four-way insight symbol 831 reads A (top), C (bottom), D (left), B (right)");

            Tch::TchSection oldStyle, newStyle;
            const auto* s82f = db->FindAs<UnknownEntity>(0x82f);
            const auto* s830 = db->FindAs<UnknownEntity>(0x830);
            Check(s82f && s830 && Tch::DecodeSection(*s82f, oldStyle) && Tch::DecodeSection(*s830, newStyle) && !oldStyle.NewStyle &&
                  newStyle.NewStyle && newStyle.Label == "1" && Near(newStyle.Diameter, 10) && newStyle.Kind == 1,
                  "tianzheng2.dwg: section 82f is the old style, 830 the new national-standard style");

            Tch::TchConfig config;
            bool configured = false;
            for (const auto& [handle, object] : db->Objects())
                if (const auto* o = dynamic_cast<const UnknownObject*>(object.get())) configured = configured || Tch::DecodeConfig(*o, config);
            Check(configured && !config.Fill && Near(config.DetailScale, 100) && Near(config.IndexTextRatio, 0.6) && config.IndexTextMode == 0 &&
                  config.Standard[6] == "SOLID" && config.Detail[4] == "普通砖",   // i18n: 数据
                  "tianzheng2.dwg: drawing settings version 11 read through the insight number settings");
        }
    }

    std::printf("%s\n", g_failed ? "FAILED" : "ALL PASSED");
    return g_failed ? 1 : 0;
}
