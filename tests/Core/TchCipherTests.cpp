#include "TchCipherWriter.h"
#include "Import/TchAnnotation.h"
#include "Import/TchColumn.h"
#include "Import/TchCurveMesh.h"
#include "Import/TchDormer.h"
#include "Import/TchSpace.h"
#include "Import/TchText.h"
#include "Import/TchElec.h"
#include "Import/TchHvac.h"
#include "Import/TchMultiStair.h"
#include "Import/TchPathArray.h"
#include "Import/TchPipe.h"
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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
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
    std::unique_ptr<UnknownEntity> SymmetryObject(int baseKey, int classKey, int key2, bool extraBit = false, int guidKey2 = -1, bool r2004 = false)
    {
        CipherWriter w(r2004);
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

    std::string Label(const char* text, bool ok, const std::string& why) { return ok ? std::string(text) : std::string(text) + ": " + why; }

    // 屋顶基类版本 2：自带密钥、第二个密钥为 0
    void RoofBase(CipherWriter& w, int key)
    {
        const int outer = w.Key, outer2 = w.Key2;
        w.Layer(2, key);
        w.Key2 = 0;
        for (int i = 0; i < 4; ++i) w.Handle(0);
        w.Double(0); w.Double(300); w.Double(0);
        w.Byte(1);
        w.Long(1);
        w.PlainPoly({{0, 0}, {6000, 0}, {6000, 4000}});
        w.Long(1);
        w.Handle(0);
        w.Key = outer; w.Key2 = outer2;
    }

    std::unique_ptr<UnknownEntity> MountRoofObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Main.WriteByte(2);
        RoofBase(w, 0x13);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Point3(ox, oy, 0);
        w.Double(0); w.Double(6000); w.Double(4000); w.Double(std::numbers::pi / 4); w.Double(600); w.Double(0); w.Double(3000); w.Double(1100);
        w.Bool(true);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_MOUNTROOF");
    }

    std::unique_ptr<UnknownEntity> CuspRoofObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Main.WriteByte(3);
        RoofBase(w, 0x13);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Point3(ox, oy, 0);
        w.Double(0.25); w.Double(3000); w.Double(2000); w.Byte(6); w.Double(600);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_CUSPROOF");
    }

    std::unique_ptr<UnknownEntity> SlopeRoofObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Layer(3, key);
        w.Double(0);
        w.Short(4);
        for (const auto& [x, y] : {std::pair{0.0, 0.0}, {6000.0, 0.0}, {6000.0, 4000.0}, {0.0, 4000.0}})
        {
            w.Point3(ox + x, oy + y, 0);
            w.Double(0.5);
        }
        RoofBase(w, 0x13);
        w.Double(0);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_SLOPEROOF");
    }

    std::unique_ptr<UnknownEntity> DualRoofObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Main.WriteByte(3);
        RoofBase(w, 0x13);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Double(0);
        w.PlainPoly({{ox, oy}, {ox + 6000, oy}, {ox + 6000, oy + 4000}, {ox, oy + 4000}});
        w.Point2(ox, oy + 2000); w.Point2(ox + 6000, oy + 2000);
        w.Double(0.5); w.Double(0.5); w.Bool(false); w.Bool(true); w.Double(0);
        if (extraBit) w.Main.WriteBit(true);
        return w.Take("TCH_DUALROOF");
    }

    std::unique_ptr<UnknownEntity> Finish(CipherWriter& w, bool extraBit, const char* dxf)
    {
        if (extraBit) w.Main.WriteBit(true);
        return w.Take(dxf);
    }

    // 多段线基类版本 1
    void PolyCurve(CipherWriter& w, int key, std::initializer_list<std::pair<double, double>> points, bool closed)
    {
        w.Layer(1, key);
        w.Handle(0);
        w.Short(static_cast<int>(points.size()));
        for (const auto& [x, y] : points) { w.Point3(x, y, 0); w.Double(0); }
        w.Bool(closed);
        w.Point3(0, 0, 1);
    }

    // 梯段基类版本 4
    void FlightBase(CipherWriter& w, int key, int steps)
    {
        w.Base(key, 94, 100, 94);
        w.Layer(4, key);
        w.Short(steps); w.Double(150); w.Byte(1); w.Double(0); w.Double(0);
        w.Long(0); w.Double(0); w.Double(0); w.Double(0);
        w.Handle(0); w.Handle(0);
    }

    std::unique_ptr<UnknownEntity> SlabObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(3, key);
        w.Double(120); w.Long(0); w.Double(0);
        w.PlainPoly({{ox, oy}, {ox + 3000, oy}, {ox + 3000, oy + 2000}, {ox, oy + 2000}});
        w.Short(1);
        w.PlainPoly({{ox + 1000, oy + 500}, {ox + 2000, oy + 500}, {ox + 2000, oy + 1500}, {ox + 1000, oy + 1500}});
        w.Point3(0, 0, 1);
        return Finish(w, extraBit, "TCH_SLAB");
    }

    std::unique_ptr<UnknownEntity> ApronObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        PolyCurve(w, key, {{ox, oy}, {ox + 3000, oy}, {ox + 3000, oy + 2000}, {ox, oy + 2000}}, true);
        w.Layer(2, key);
        w.Double(0); w.Double(0);
        w.Short(4);
        for (int i = 0; i < 4; ++i) w.Double(600);
        w.Long(0);
        return Finish(w, extraBit, "TCH_APRON");
    }

    // 标志 5：位 0 的 4 + 1 个 BD 与位 2 的一段折线
    std::unique_ptr<UnknownEntity> CornerWindowObject(int key, bool extraBit = false, double ox = 0, double oy = 0, double thick2 = -240)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(5, key);
        w.Long(5);
        w.Point3(ox, oy, 0);
        w.Handle(0); w.Handle(0);
        w.Double(1800); w.Double(1500); w.Double(1500); w.Double(0); w.Double(std::numbers::pi / 2);
        w.Double(240); w.Double(thick2); w.Double(0); w.Double(0);
        w.Text.WriteVariableText("ZJC1");
        w.Point2(ox + 900, oy - 600);
        w.Handle(0x82a); w.Handle(0);
        w.Double(0); w.Double(3.5);
        for (int i = 0; i < 4; ++i) w.Double(0);
        w.Short(0);
        w.Handle(0); w.Handle(0); w.Handle(0);
        w.Double(0); w.Double(0); w.Double(0);
        w.Short(1); w.Point2(0, 0); w.Point2(100, 0); w.Double(0);
        w.Short(0); w.Bool(false); w.Text.WriteVariableText("");
        for (int i = 0; i < 4; ++i) w.Double(0);
        w.Double(0);
        w.Double(0);
        return Finish(w, extraBit, "TCH_CORNER_WINDOW");
    }

    std::unique_ptr<UnknownEntity> AscentObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Point3(ox, oy, 0); w.Double(0); w.Byte(1);
        w.Double(2100); w.Double(3000); w.Double(450); w.Double(300);
        return Finish(w, extraBit, "TCH_ASCENT");
    }

    std::unique_ptr<UnknownEntity> BalconyObject(int key, bool extraBit = false, double ox = 0, double oy = 0, int noRail = 0, bool closed = false)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Layer(5, key);
        w.Long(noRail); w.Double(0);
        w.PlainPoly({{ox, oy}, {ox, oy - 1500}, {ox + 3000, oy - 1500}, {ox + 3000, oy}}, closed);
        w.Double(100); w.Double(1000); w.Double(100); w.Double(300);
        w.Handle(0); w.Long(0); w.Handle(0); w.Handle(0);
        w.Double(0); w.Double(0); w.Double(0); w.Bool(false);
        w.Long(1); w.Handle(0);
        w.Handle(0);
        w.Long(0);
        return Finish(w, extraBit, "TCH_BALCONY");
    }

    std::unique_ptr<UnknownEntity> StepObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Long(8); w.Double(0);
        w.PlainPoly({{ox, oy}, {ox + 2400, oy}, {ox + 2400, oy + 1200}, {ox, oy + 1200}});
        w.Short(3); w.Double(300); w.Double(150);
        return Finish(w, extraBit, "TCH_STEP");
    }

    // 墙体造型不加密：基类之后全是明文
    std::unique_ptr<UnknownEntity> WallPatchObject(int key, bool extraBit = false)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Main.WriteByte(4);
        w.Main.WriteBitShort(1);
        for (int i = 0; i < 7; ++i) w.Main.WriteBitDouble(0);
        w.Main.WriteBitDouble(0);
        w.PlainPoly({{0, 0}, {600, 0}, {600, 300}, {0, 300}});
        w.Main.WriteBitDouble(3000);
        w.Main.WriteByte(1); w.Main.WriteByte(0); w.Main.WriteByte(0); w.Main.WriteByte(0);
        w.Main.WriteBitLong(0); w.Main.WriteBitDouble(0);
        w.Handle(0x2b0); w.Handle(0); w.Handle(0);
        return Finish(w, extraBit, "TCH_WALL_PATCH");
    }

    std::unique_ptr<UnknownEntity> OpeningSlotObject(int key, bool extraBit = false)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(3, key);
        w.Short(0); w.Handle(0x2a0); w.Bool(false); w.Short(1); w.Short(0); w.Short(0); w.Short(0);
        w.Short(4);
        for (const auto& [x, y] : {std::pair{0.0, 0.0}, {60.0, 0.0}, {60.0, 15.0}, {0.0, 15.0}}) { w.Point3(x, y, 0); w.Double(0); }
        for (int i = 0; i < 11; ++i) w.Double(0);
        w.Short(0);
        w.Handle(0); w.Handle(0);
        return Finish(w, extraBit, "TCH_OPENINGSLOT");
    }

    std::unique_ptr<UnknownEntity> LineStairObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        FlightBase(w, key, 12);
        w.Layer(3, key);
        w.Double(270); w.Double(1200); w.Point3(ox, oy, 0); w.Double(0); w.Double(0);
        return Finish(w, extraBit, "TCH_LINESTAIR");
    }

    std::unique_ptr<UnknownEntity> ArcStairObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        FlightBase(w, key, 10);
        w.Layer(3, key);
        w.Point3(ox, oy, 0); w.Double(1000); w.Double(2200); w.Double(0); w.Double(std::numbers::pi / 2);
        w.PlainPoly({{0, 0}, {1, 0}}, false);
        w.PlainPoly({{0, 0}, {1, 0}}, false);
        return Finish(w, extraBit, "TCH_ARCSTAIR");
    }

    std::unique_ptr<UnknownEntity> CurveStairObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        FlightBase(w, key, 10);
        w.Layer(2, key);
        w.Double(0);
        w.Point2(ox, oy); w.Point2(ox + 3000, oy); w.Double(0);
        w.Point2(ox, oy + 1200); w.Point2(ox + 3000, oy + 1200); w.Double(0);
        return Finish(w, extraBit, "TCH_CURVESTAIR");
    }

    std::unique_ptr<UnknownEntity> AutoStairObject(int key, bool extraBit = false, double ox = 0, double oy = 0, int flags = 0x400)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Layer(1, key);
        w.Point3(ox, oy, 0); w.Double(0); w.Double(0); w.Double(400); w.Double(1000); w.Double(2000); w.Double(0); w.Double(0);
        w.Byte(1); w.Double(0); w.Double(0); w.Double(0); w.Double(100); w.Double(3.5); w.Double(4500); w.Double(30);
        w.Double(0); w.Double(0); w.Byte(0); w.Long(flags); w.Double(0);
        w.Handle(0); w.Handle(0); w.Handle(0x82a); w.Handle(0x289);
        w.Byte(0);
        return Finish(w, extraBit, "TCH_AUTOSTAIR");
    }

    std::unique_ptr<UnknownEntity> HandRailObject(int key, bool extraBit = false, double ox = 0, double oy = 0, int align = 0, int flags = 1)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Layer(3, key);
        w.Byte(0); w.Byte(0);
        w.Short(2);
        w.Point3(ox, oy, 0); w.Double(0);
        w.Point3(ox + 3000, oy, 0); w.Double(0);
        w.Double(60); w.Double(0); w.Double(900); w.Byte(align); w.Byte(flags);
        return Finish(w, extraBit, "TCH_HANDRAIL");
    }

    std::unique_ptr<UnknownEntity> ParallelStairObject(int key, bool extraBit = false, double ox = 0, double oy = 0, int flags = 0x400)
    {
        CipherWriter w;
        w.PathHead(key);
        w.Layer(1, key);
        w.Point3(ox, oy, 0);
        w.Double(0); w.Double(150); w.Double(270); w.Double(1200); w.Double(1200); w.Double(1300); w.Double(0); w.Double(0);
        w.Byte(10); w.Byte(10);
        w.Double(100); w.Double(0);
        w.Byte(1); w.Byte(0); w.Byte(0);
        w.Double(60); w.Double(0); w.Double(0); w.Double(0);
        w.Short(5); w.Double(std::numbers::pi / 4); w.Double(3.5);
        w.Long(flags);
        w.Double(0); w.Double(0); w.Double(0);
        for (int i = 0; i < 5; ++i) w.Handle(0);
        w.Handle(0x82a); w.Handle(0x289);
        w.Byte(0);
        return Finish(w, extraBit, "TCH_PARALLELSTAIR");
    }

    std::unique_ptr<UnknownEntity> PathArrayObject(int key, bool extraBit = false)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        PolyCurve(w, key, {{0, 0}, {3000, 0}}, false);
        w.Layer(3, key);
        w.Handle(0x2c0); w.Double(100); w.Byte(0); w.Double(1);
        return Finish(w, extraBit, "TCH_PATH_ARRAY");
    }

    std::unique_ptr<UnknownEntity> RuptureObject(int key, bool extraBit = false, double ox = 0, double oy = 0, int count = 1)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Point2(ox, oy); w.Point2(ox + 3000, oy); w.Byte(count);
        return Finish(w, extraBit, "TCH_RUPTURE");
    }

    std::unique_ptr<UnknownEntity> CutObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Main.WriteByte(0);
        w.Main.WriteBitShort(3);
        for (const auto& [x, y, b] : {std::tuple{0.0, 0.0, 1.0}, {1500.0, 0.0, 1.0}, {3000.0, 0.0, 0.0}})
        {
            w.Main.WriteRawDouble(ox + x); w.Main.WriteRawDouble(oy + y); w.Main.WriteBitDouble(b);
        }
        w.Byte(0);
        for (int i = 0; i < 3; ++i) w.Bool(false);
        return Finish(w, extraBit, "TCH_CUT");
    }

    // 修订标记版本 5、种类 1：带 8 个字符串的附加段、一条附加多段线
    std::unique_ptr<UnknownEntity> ModiObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(4, key);
        w.Handle(0x82a); w.Double(3.5); w.Double(0); w.Handle(0x289); w.Short(7);
        w.Layer(5, key);
        w.PlainPoly({{ox, oy}, {ox + 2000, oy}, {ox + 2000, oy + 1000}, {ox, oy + 1000}});
        w.Double(0); w.Double(0.5); w.Byte(0);
        w.Text.WriteVariableText("A");
        w.Point3(ox, oy, 0);
        w.Byte(1);
        w.Short(0); w.Byte(0); w.Double(0); w.Double(0);
        for (int i = 0; i < 8; ++i) w.Text.WriteVariableText("");
        w.Short(1);
        w.PlainPoly({{0, 0}, {1, 1}}, false);
        for (int i = 0; i < 5; ++i) w.Text.WriteVariableText("");
        w.Byte(0);
        w.Byte(0);
        w.Text.WriteVariableText("");
        return Finish(w, extraBit, "TCH_MODI");
    }

    std::unique_ptr<UnknownEntity> RadiusDimObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Short(4 | 2);
        w.Handle(0x2d0);
        w.Double(-5);
        w.Point3(ox, oy, 0);
        w.Point2(ox + 1000, oy);
        w.Text.WriteVariableText("R1000");
        w.Point2(ox + 1500, oy + 200);
        return Finish(w, extraBit, "TCH_RADIUSDIM");
    }

    // 文字类版本 3 句柄流只有文字样式，4 起末尾另有两个句柄
    std::unique_ptr<UnknownEntity> TextObject(int version, int handles, bool extraBit = false)
    {
        CipherWriter w;
        w.Base(0x12, 94, 100, 94);
        w.Layer(version, 0x12);
        w.Handle(0x2d0);
        w.Text.WriteVariableText("ABC");
        w.Point3(100, 200, 0);
        w.Double(3.5); w.Double(0); w.Byte(7); w.Byte(0);
        for (int i = 1; i < handles; ++i) w.Handle(0);
        return Finish(w, extraBit, "TCH_TEXT");
    }

    std::unique_ptr<UnknownEntity> DormerObject(int key, double nx = -0.7656, double ny = 0, bool extraBit = false)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(3, key);
        w.Double(1);
        for (double p : {200.0, 200.0, 0.5, 450.0, 200.0, 1500.0, 180.0, 1800.0, 1300.0, 1400.0}) w.Double(p);
        w.Point3(5000, 3000, 750);
        w.Point3(nx, ny, std::sqrt(std::max(0.0, 1 - nx * nx - ny * ny)));
        return Finish(w, extraBit, "TCH_DORMER");
    }

    std::unique_ptr<UnknownEntity> PlainDormerObject()
    {
        CipherWriter w;
        w.PlainBase(100);
        w.Main.WriteByte(2); w.Main.WriteBitShort(1);
        for (double p : {200.0, 200.0, 0.5, 450.0, 200.0, 1500.0, 180.0, 1800.0, 1300.0, 1400.0}) w.Main.WriteBitDouble(p);
        for (double v : {5000.0, 3000.0, 750.0, 0.0, 1.0, 0.0}) w.Main.WriteBitDouble(v);
        return w.Take("TCH_DORMER");
    }

    // 房间：密钥后若干字段，再 BS 顶点数与顶点数组（每点两个 RD，点间 2 位），数组末尾与主数据流末尾对齐。
    // withName：模拟加密对象——ReadBase 之后文本流里依次是编号、房间名的明文 UTF-16 串。
    std::unique_ptr<UnknownEntity> SpaceObject(std::initializer_list<std::pair<double, double>> points, int tailBits = 0, bool withName = false)
    {
        CipherWriter w;
        w.Base(0x12, 94, 100, 94);
        if (withName)
        {
            w.Strings().WriteVariableText("1001");
            w.Strings().WriteVariableText("房间");   // 通用中文名（房间名提取按 CJK 判定）
        }
        w.Layer(2, 0x12);
        w.Double(3.5);
        w.Main.WriteBitDouble(2800); w.Main.WriteByte(1);
        w.Main.WriteBitShort(static_cast<std::int16_t>(points.size()));
        std::size_t i = 0;
        for (const auto& [x, y] : points)
        {
            w.Main.WriteRawDouble(x); w.Main.WriteRawDouble(y);
            if (++i < points.size()) w.Main.Write2Bits(0);
        }
        for (int k = 0; k < tailBits; ++k) w.Main.WriteBit(false);
        return w.Take("TCH_SPACE");
    }

    // TCH_CURVEMESH2：版本 → (加密版另有密钥) → BS 点数 → 每点 3BD + 凸度 BD → 闭合位 → 法向 3BD
    // bareRun：没有点数、只有一串 3BD 的数据，走盲扫兜底
    std::unique_ptr<UnknownEntity> CurveMeshObject(bool plain, std::initializer_list<MiniDWG::XYZ> points,
                                                   bool closed = false, bool bareRun = false)
    {
        CipherWriter w;
        if (plain && bareRun)
        {
            w.PlainBase(100); w.Main.WriteByte(1);
            for (const auto& p : points) w.Main.Write3BitDouble(p);
            w.Main.Write3BitDouble({0, 0, 0});
            w.Main.WriteBitShort(0);
        }
        else if (plain)
        {
            w.PlainBase(100); w.Main.WriteByte(0);
            w.Main.WriteBitShort(static_cast<short>(points.size()));
            for (const auto& p : points) { w.Main.Write3BitDouble(p); w.Main.WriteBitDouble(0); }
            w.Main.WriteBit(closed);
            w.Main.Write3BitDouble({0, 0, 1});
        }
        else
        {
            w.Base(0x12, 94, 100, 94);
            w.Layer(1, 0x12);
            w.Handle(0);
            w.Short(static_cast<int>(points.size()));
            for (const auto& p : points) { w.Point3(p.X, p.Y, p.Z); w.Double(0); }
            w.Bool(closed);
            w.Point3(0, 0, 1);
        }
        return w.Take("TCH_CURVEMESH2");
    }

    // 合法的天正基类与版本 / 密钥之后接随机位：解析器不能从里面"扫"出几何
    std::unique_ptr<UnknownEntity> NoiseObject(const char* dxf, bool plain, std::uint64_t& seed, int bits)
    {
        CipherWriter w;
        if (plain) { w.PlainBase(100); w.Main.WriteByte(1); }
        else { w.Base(0x12, 94, 100, 94); w.Layer(1, 0x12); }
        for (int i = 0; i < bits; ++i)
        {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            w.Main.WriteBit((seed >> 33) & 1);
        }
        return w.Take(dxf);
    }

    // 自带密钥、第二个密钥为 0 的子层（管线接口、标注样式）：写完恢复外层密钥
    template <class Body>
    void SubLayer(CipherWriter& w, int version, int key, Body body)
    {
        const int outer = w.Key, outer2 = w.Key2;
        w.Layer(version, key);
        w.Key2 = 0;
        body();
        w.Key = outer; w.Key2 = outer2;
    }

    // 管线节点 v1：每个接口 v3（方向、所连管线）
    void PipeNode(CipherWriter& w, int key, std::initializer_list<std::tuple<double, double, Handle>> ports, const char* last = "")
    {
        w.Main.WriteByte(1);
        w.Main.WriteByte(0);
        w.Main.WriteByte(static_cast<std::uint8_t>(ports.size()));
        for (const auto& [dx, dy, pipe] : ports)
            SubLayer(w, 3, key, [&] {
                for (int k = 0; k < 4; ++k) w.Byte(0);
                w.Text.WriteVariableText("");
                w.Handle(pipe);
                w.Point3(0, 0, 0);
                w.Point3(dx, dy, 0);
                w.Text.WriteVariableText("");
            });
        w.Text.WriteVariableText(last);
    }

    std::unique_ptr<UnknownEntity> PipeObject(int key, bool doubleLine, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Byte(0); w.Byte(0); w.Byte(0);
        w.Point3(ox, oy, 0); w.Point3(ox + 4000, oy, 0);
        w.Double(0);
        w.Text.WriteVariableText("");
        w.Handle(0); w.Byte(0); w.Handle(0); w.Byte(0);
        w.Byte(1); w.Handle(0x2e1);
        w.Byte(0);
        w.Byte(0);
        w.Handle(0); w.Handle(0);
        w.Double(3.5);
        w.Point3(ox + 2000, oy + 300, 0);
        w.Bool(!doubleLine);
        w.Layer(4, key);
        w.Double(0.5); w.Double(0); w.Double(100); w.Double(0);
        w.Short(0);
        w.Text.WriteVariableText("DN");
        w.Double(2000); w.Double(3000); w.Double(0);
        w.Text.WriteVariableText("");
        w.Text.WriteVariableText("");
        w.Byte(1); w.Text.WriteVariableText("A"); w.Byte(0);
        if (doubleLine)
        {
            w.Layer(3, key);
            w.Double(200); w.Double(1); w.Short(0); w.Double(0); w.Double(0); w.Double(0);
            w.Handle(0);
        }
        return Finish(w, extraBit, doubleLine ? "TCH_DOUBLELINEPIPE" : "TCH_PIPE");
    }

    std::unique_ptr<UnknownEntity> PipeFittingObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        PipeNode(w, key, {{-1, 0, 0x2e1}, {1, 0, 0x2e2}});
        w.Layer(2, key);
        w.Point3(ox, oy, 0);
        return Finish(w, extraBit, "TCH_PIPEFITTING");
    }

    // kind：0 设备，1 井，2 水泵
    std::unique_ptr<UnknownEntity> EquipmentObject(int key, int kind, bool extraBit = false, double ox = 0, double oy = 0, Handle block = 0x2f0, double scaleY = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        PipeNode(w, key, {{1, 0, 0x2e1}});
        w.Layer(5, key);
        w.Handle(block); w.Handle(0);
        w.Text.WriteVariableText("");
        w.Point3(ox, oy, 0); w.Point3(0, 0, 0); w.Point3(0, 1, 0);
        w.Double(2); w.Bool(false); w.Byte(2);
        for (double v : {0.0, 0.0, 0.0, scaleY, 0.0, 0.0}) w.Double(v);
        w.Bool(false);
        w.Byte(0); w.Byte(0); w.Handle(0); w.Byte(0);
        if (kind == 1)
        {
            SubLayer(w, 2, key, [&] { w.Handle(0); w.Double(3); w.Double(0); });
            w.Layer(3, key);
            w.Text.WriteVariableText("");
            w.Point3(ox + 500, oy + 500, 0);
            w.Double(-2.5); w.Double(0.3); w.Double(0);
            w.Bool(false);
            w.Handle(0);
        }
        if (kind == 2)
        {
            w.Layer(4, key);
            for (int i = 0; i < 4; ++i) w.Text.WriteVariableText("");
            for (double v : {0.0, 0.0, 165.0, 700.0, 500.0, 165.0}) w.Double(v);
            w.Double(0);
        }
        return Finish(w, extraBit, kind == 1 ? "TCH_WELL" : kind == 2 ? "TCH_PUMP" : "TCH_EQUIPMENT");
    }

    void PipeAttachment(CipherWriter& w, int key, double x, double y)
    {
        w.Base(key, 94, 100, 94);
        w.Layer(2, key);
        w.Byte(0);
        w.Point3(x, y, 0);
        w.Point3(-1, 0, 0);
        w.Handle(0x2e1);
    }

    std::unique_ptr<UnknownEntity> PipeValveObject(int key, bool extraBit = false, double ox = 0, double oy = 0, Handle block = 0x2f1, int quadrant = 0,
                                                   double scaleY = 0)
    {
        CipherWriter w;
        PipeAttachment(w, key, ox, oy);
        w.Layer(3, key);
        w.Handle(block); w.Handle(0);
        w.Text.WriteVariableText("");
        w.Double(1.5); w.Double(0); w.Bool(false); w.Byte(quadrant);
        for (double v : {scaleY, 0.0, 0.0, 0.0}) w.Double(v);
        w.Bool(false);
        return Finish(w, extraBit, "TCH_PIPEVALVE");
    }

    std::unique_ptr<UnknownEntity> PipeTextObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        PipeAttachment(w, key, ox, oy);
        SubLayer(w, 2, key, [&] { w.Handle(0); w.Double(3.5); w.Double(0); });
        w.Main.WriteByte(2);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Text.WriteVariableText("DN100");
        return Finish(w, extraBit, "TCH_PIPETEXT");
    }

    // 符号基类 v4，fields 写属性包
    template <class Fields>
    void PipeSymbol(CipherWriter& w, int key, double angle, int count, Fields fields)
    {
        w.BaseBegin(key, 94, 100, 94, angle, count);
        fields();
        w.MapBegin();
        w.BaseEnd();
        w.Layer(4, key);
        w.Handle(0);
        w.Double(3);
        w.Double(0);
        w.Handle(0);
        w.Short(3);
    }

    std::unique_ptr<UnknownEntity> VPipeDimObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        PipeSymbol(w, key, 0, 2, [&] { w.FieldText("VPIPE_DIM_FLOOR", "1"); w.FieldText("VPIPE_DIM_TEXT", "JL-1"); });
        w.Layer(2, key);
        w.Short(0); w.Short(0);
        w.Point3(ox, oy, 0); w.Point3(ox + 1000, oy + 1000, 0); w.Point3(1, 0, 0);
        w.Double(10); w.Double(0);
        w.Handle(0x2e1);
        w.Text.WriteVariableText("");
        return Finish(w, extraBit, "TCH_VPIPEDIM");
    }

    std::unique_ptr<UnknownEntity> WellDimObject(int key, bool extraBit = false, double ox = 0, double oy = 0, Handle well = 0x2e3)
    {
        CipherWriter w;
        PipeSymbol(w, key, 0, 0, [] {});
        w.Layer(2, key);
        w.Short(0);
        w.Double(10);
        w.Point2(ox, oy); w.Point2(ox + 500, oy + 1000);
        w.Handle(well);
        return Finish(w, extraBit, "TCH_WELLDIM");
    }

    std::unique_ptr<UnknownEntity> PipeMultDimObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        PipeSymbol(w, key, std::numbers::pi / 2, 0, [] {});
        w.Layer(3, key);
        w.Byte(2); w.Handle(0x2e1); w.Handle(0x2e2);
        w.Double(20);
        w.Point2(ox, oy); w.Point2(ox + 1000, oy + 1000);
        w.Short(0);
        w.Text.WriteVariableText("");
        return Finish(w, extraBit, "TCH_PIPEMULTDIM");
    }

    std::unique_ptr<UnknownEntity> FlangeObject(int key, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.Base(key, 94, 100, 94);
        PipeNode(w, key, {{-1, 0, 0x2e1}, {0, 1, 0x2e2}}, "1.5");
        w.Layer(2, key);
        w.Point3(ox, oy, 0);
        w.Layer(3, key);
        w.Handle(0);
        return Finish(w, extraBit, "TCH_FLANGE");
    }

    // 接口基类 v3（版本为 BS）
    void InterfacesObj(CipherWriter& w, int key)
    {
        w.Main.WriteBitShort(3);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Handle(0); w.Handle(0);
        w.Byte(0); w.Short(0);
    }

    // 桥架 / 风管：管线基类 v4 在末尾存端点，v3 取属性表里的 Start、End
    std::unique_ptr<UnknownEntity> CableTrayObject(int key, bool duct, int version, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.BaseBegin(key, 94, 100, 94, 0, 0);
        w.MapBegin();
        if (duct) w.MapNumber("R2", 400);
        else { w.MapNumber("B", 300); w.MapInt("HasCenLine", 1); }
        if (version == 3) { w.MapPoint("Start", ox, oy); w.MapPoint("End", ox + 5000, oy); }
        w.BaseEnd();
        InterfacesObj(w, key);
        w.Main.WriteBitShort(static_cast<std::int16_t>(version));
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Long(1);
        w.Handle(0);
        w.Main.WriteBitLong(1);
        for (int i = 0; i < 9; ++i) w.Main.WriteBitDouble(0);
        w.Main.WriteBitShort(0); w.Main.WriteBitLong(0); w.Main.WriteBitDouble(0);
        w.Long(1);
        w.Handle(0);
        if (version == 4)
        {
            w.Point3(ox, oy, 0); w.Point3(ox + 5000, oy, 0); w.Point3(0, 0, 1);
            w.Double(0);
            w.Short(0);
        }
        w.Main.WriteBitShort(duct ? 4 : 1);
        return Finish(w, extraBit, duct ? "TCH_DBHVACDUCT" : "TCH_CABLETRY");
    }

    // 桥架弯通 / 风管弯头：管件基类 v3，软指针之后的列表另带密钥
    std::unique_ptr<UnknownEntity> CableFittingObject(int key, bool hvac, bool extraBit = false, double ox = 0, double oy = 0)
    {
        CipherWriter w;
        w.BaseBegin(key, 94, 100, 94, 0, 0);
        w.MapBegin();
        if (hvac)
        {
            w.MapPoint("Pt0", ox - 1000, oy); w.MapPoint("Pt1", ox, oy); w.MapPoint("Pt2", ox, oy + 1000);
            w.MapNumber("R5", 400); w.MapNumber("R21", 1); w.MapNumber("R0", 0);
        }
        else
        {
            w.MapPoint("Mid", ox, oy); w.MapPoint("MajBr", -1, 0); w.MapPoint("MinBr", 0, 1);
            w.MapNumber("B", 300); w.MapNumber("L", 500); w.MapInt("Elbow", 1); w.MapInt("Corner", 0);
            w.MapHandle("IdCNT1", 0x2e4);
        }
        w.BaseEnd();
        InterfacesObj(w, key);
        w.Main.WriteBitShort(3);
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key = key;
        w.Handle(0);
        const int outer2 = w.Key2;
        w.Main.WriteByte(static_cast<std::uint8_t>(key));
        w.Key2 = 0;
        w.Long(0);
        w.Long(1);
        w.Handle(0);
        w.Key2 = outer2;
        w.Main.WriteBitShort(hvac ? 2 : 1);
        return Finish(w, extraBit, hvac ? "TCH_DBHFITTING" : "TCH_EFITTING");
    }

    template <class T, class Decode, class Make>
    bool DecodesExactly(const char* label, Decode decode, Make make, T& out)
    {
        std::string why;
        const bool ok = decode(*make(false), out, &why);
        T scratch;
        Check(ok, Label(label, ok, why).c_str());
        Check(!decode(*make(true), scratch, nullptr), (std::string(label) + ": a leftover bit is rejected").c_str());
        return ok;
    }

    bool NearPoint(const Tch::TchGraphics::Point& p, double x, double y, double tol = 1e-6) { return Near(p.X, x, tol) && Near(p.Y, y, tol); }

    std::string EnvPath(const char* name)
    {
#ifdef _WIN32
        const std::wstring wname(name, name + std::strlen(name));
        const wchar_t* wide = _wgetenv(wname.c_str());
        const std::u8string u8 = wide ? std::filesystem::path(wide).u8string() : std::u8string{};
        return {u8.begin(), u8.end()};
#else
        const char* env = std::getenv(name);
        return env ? env : "";
#endif
    }

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
        if (n == "TCH_MOUNTROOF") return run(Tch::DecodeMountRoof, Tch::TchMountRoof{});
        if (n == "TCH_CUSPROOF") return run(Tch::DecodeCuspRoof, Tch::TchCuspRoof{});
        if (n == "TCH_SLOPEROOF") return run(Tch::DecodeSlopeRoof, Tch::TchSlopeRoof{});
        if (n == "TCH_DUALROOF") return run(Tch::DecodeDualRoof, Tch::TchDualRoof{});
        if (n == "TCH_SLAB") return run(Tch::DecodeSlab, Tch::TchSlab{});
        if (n == "TCH_APRON") return run(Tch::DecodeApron, Tch::TchApron{});
        if (n == "TCH_CORNER_WINDOW") return run(Tch::DecodeCornerWindow, Tch::TchCornerWindow{});
        if (n == "TCH_ASCENT") return run(Tch::DecodeAscent, Tch::TchAscent{});
        if (n == "TCH_BALCONY") return run(Tch::DecodeBalcony, Tch::TchBalcony{});
        if (n == "TCH_STEP") return run(Tch::DecodeStep, Tch::TchStep{});
        if (n == "TCH_LINESTAIR") return run(Tch::DecodeLineStair, Tch::TchLineStair{});
        if (n == "TCH_ARCSTAIR") return run(Tch::DecodeArcStair, Tch::TchArcStair{});
        if (n == "TCH_CURVESTAIR") return run(Tch::DecodeCurveStair, Tch::TchCurveStair{});
        if (n == "TCH_AUTOSTAIR") return run(Tch::DecodeAutoStair, Tch::TchAutoStair{});
        if (n == "TCH_HANDRAIL") return run(Tch::DecodeHandRail, Tch::TchHandRail{});
        if (n == "TCH_PARALLELSTAIR") return run(Tch::DecodeParallelStair, Tch::TchParallelStair{});
        if (n == "TCH_RUPTURE") return run(Tch::DecodeRupture, Tch::TchRupture{});
        if (n == "TCH_CUT") return run(Tch::DecodeCut, Tch::TchCut{});
        if (n == "TCH_MODI") return run(Tch::DecodeModi, Tch::TchModi{});
        if (n == "TCH_RADIUSDIM") return run(Tch::DecodeRadiusDim, Tch::TchRadiusDim{});
        if (n == "TCH_PIPE" || n == "TCH_DOUBLELINEPIPE") return run(Tch::DecodePipe, Tch::TchPipe{});
        if (n == "TCH_PIPEFITTING") return Tch::DecodePipeFitting(e, &why) ? 1 : 0;
        if (n == "TCH_EQUIPMENT" || n == "TCH_WELL" || n == "TCH_PUMP") return run(Tch::DecodeEquipment, Tch::TchPipeBlock{});
        if (n == "TCH_PIPEVALVE") return run(Tch::DecodePipeValve, Tch::TchPipeBlock{});
        if (n == "TCH_PIPETEXT") return run(Tch::DecodePipeText, Tch::TchPipeText{});
        if (n == "TCH_VPIPEDIM") return run(Tch::DecodeVPipeDim, Tch::TchVPipeDim{});
        if (n == "TCH_WELLDIM") return run(Tch::DecodeWellDim, Tch::TchWellDim{});
        if (n == "TCH_PIPEMULTDIM") return run(Tch::DecodePipeMultDim, Tch::TchPipeMultDim{});
        if (n == "TCH_FLANGE") return run(Tch::DecodeFlange, Tch::TchFlange{});
        if (n == "TCH_CABLETRY") return run(Tch::DecodeCableTray, Tch::TchCableTray{});
        if (n == "TCH_EFITTING") return run(Tch::DecodeCableFitting, Tch::TchCableFitting{});
        if (n == "TCH_DBHVACDUCT") return run(Tch::DecodeHvacDuct, Tch::TchHvacDuct{});
        if (n == "TCH_DBHFITTING") return run(Tch::DecodeHvacFitting, Tch::TchHvacFitting{});
        if (n == "TCH_HVACDIMENSION") return run(Tch::DecodeHvacDimension, Tch::TchHvacDimension{});
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
        // DWG 2004：GBK 里有的字符存为双字节，没有的存为 \U+XXXX
        for (const auto& k : keys)
        {
            Tch::TchSymmetry m;
            std::string why;
            const std::string label = "synthetic encrypted symmetry in a DWG 2004 file decodes (base key " + std::to_string(k[0]) +
                                      ", class key " + std::to_string(k[1]) + ", second key " + std::to_string(k[2]) + ")";
            const bool ok = Tch::DecodeSymmetry(*SymmetryObject(k[0], k[1], k[2], false, -1, true), m, &why);
            Check(ok && Near(m.X1, 1000) && Near(m.Y1, 2000) && Near(m.X2, 11000) && Near(m.Y2, 2000) && Near(m.Scale, 100) &&
                  Near(m.TickLength, 8) && Near(m.Gap, 2) && Near(m.Spacing, 2.5), (label + (ok ? "" : ": " + why)).c_str());
        }
        Tch::TchSymmetry m;
        Check(!Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94, true), m), "an encrypted object with a leftover bit is rejected");
        Check(!Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94, true, -1, true), m), "an encrypted DWG 2004 object with a leftover bit is rejected");
        Check(!Tch::DecodeSymmetry(*SymmetryObject(0x12, 0x12, 94, false, 93), m) || !Near(m.TickLength, 8), "a wrong second key does not restore the values");
                
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
        std::string why;
        Tch::TchMountRoof m;
        bool ok = Tch::DecodeMountRoof(*MountRoofObject(0x12), m, &why);
        Check(ok && Near(m.Length, 6000) && Near(m.Width, 4000) && Near(m.EndSlope, std::numbers::pi / 4) && Near(m.Eaves, 600) &&
              Near(m.RoofHeight, 3000) && Near(m.MountHeight, 1100), Label("encrypted mount roof decodes (roof base with its own key)", ok, why).c_str());
        Check(Tch::DecodeMountRoof(*MountRoofObject(0x0c), m) && !Tch::DecodeMountRoof(*MountRoofObject(0x12, true), m),
              "mount roof: plain numbers decode, a leftover bit is rejected");
        Tch::TchCuspRoof c;
        ok = Tch::DecodeCuspRoof(*CuspRoofObject(0x14), c, &why);
        Check(ok && c.Sides == 6 && Near(c.Rotation, 0.25) && Near(c.Height, 3000) && Near(c.Radius, 2000) && Near(c.Eaves, 600),
              Label("encrypted cusp roof decodes", ok, why).c_str());
        Check(!Tch::DecodeCuspRoof(*CuspRoofObject(0x12, true), c), "cusp roof: a leftover bit is rejected");
        Tch::TchSlopeRoof sr;
        ok = Tch::DecodeSlopeRoof(*SlopeRoofObject(0x12), sr, &why);
        Check(ok && sr.Edges.size() == 4 && Near(sr.Edges[2].X, 6000) && Near(sr.Edges[2].Y, 4000) && Near(sr.Edges[1].Slope, 0.5) &&
              Tch::BuildSlopeRoof(sr).Lines.size() == 9, Label("encrypted slope roof decodes and builds hips and a ridge", ok, why).c_str());
        Check(!Tch::DecodeSlopeRoof(*SlopeRoofObject(0x12, true), sr), "slope roof: a leftover bit is rejected");
        Tch::TchDualRoof d;
        ok = Tch::DecodeDualRoof(*DualRoofObject(0x13), d, &why);
        Check(ok && d.Outline.size() == 4 && Near(d.X2, 6000) && Near(d.Y2, 2000), Label("encrypted dual-slope roof decodes", ok, why).c_str());
        Check(!Tch::DecodeDualRoof(*DualRoofObject(0x13, true), d), "dual-slope roof: a leftover bit is rejected");
    }

    // 其余新格式分支：合成对象恰好读完、多一位即拒绝
    {
        Tch::TchSlab slab;
        if (DecodesExactly("encrypted slab decodes", Tch::DecodeSlab, [](bool x) { return SlabObject(0x12, x); }, slab))
            Check(slab.Outline.size() == 4 && slab.Holes.size() == 1 && Near(slab.Holes[0][2].X, 2000), "slab outline and hole");
        Tch::TchApron apron;
        if (DecodesExactly("encrypted apron decodes", Tch::DecodeApron, [](bool x) { return ApronObject(0x13, x); }, apron))
            Check(apron.Closed && apron.Path.size() == 4 && apron.Widths.size() == 4 && Near(apron.Widths[3], 600), "apron path and widths");
        Tch::TchCornerWindow corner;
        if (DecodesExactly("encrypted corner window decodes", Tch::DecodeCornerWindow, [](bool x) { return CornerWindowObject(0x12, x); }, corner))
            Check(corner.Label == "ZJC1" && Near(corner.Length1, 1800) && Near(corner.Angle2, std::numbers::pi / 2) && Near(corner.Thickness, 240) &&
                  Near(corner.LabelY, -600) && corner.Style == 0x82a, "corner window lengths, label and style");
        Tch::TchAscent ascent;
        if (DecodesExactly("encrypted ascent decodes", Tch::DecodeAscent, [](bool x) { return AscentObject(0x14, x); }, ascent))
            Check(ascent.Slant && Near(ascent.Length, 2100) && Near(ascent.Indent, 300) && Near(ascent.Scale, 100), "ascent size");
        Tch::TchBalcony balcony;
        if (DecodesExactly("encrypted balcony decodes", Tch::DecodeBalcony, [](bool x) { return BalconyObject(0x12, x); }, balcony))
            Check(balcony.Outline.size() == 4 && Near(balcony.RailWidth, 100), "balcony outline and rail width");
        {
            Tch::TchCornerWindow w;
            Check(Tch::DecodeCornerWindow(*CornerWindowObject(0x12, false, 0, 0, -120), w) && Near(w.Thickness2, -120) &&
                  Near(Tch::BuildCornerWindow(w).Paths[3].Points[1].P.X, -120) && Near(Tch::BuildCornerWindow(w).Paths[3].Points[1].P.Y, -240) &&
                  Near(Tch::BuildCornerWindow(w).Paths[3].Points[2].P.X, -120) && Near(Tch::BuildCornerWindow(w).Paths[3].Points[2].P.Y, 1500),
                  "corner window legs offset by their own wall thickness");
            Tch::TchBalcony b;
            Check(Tch::DecodeBalcony(*BalconyObject(0x12, false, 0, 0, 8, true), b) && b.Closed && b.NoRailEdges == 8,
                  "balcony edges without rail and closed flag");
            const auto g = Tch::BuildBalcony(b);
            Check(g.Paths.size() == 2 && g.Paths[1].Points.size() == 4 && Near(g.Paths[1].Points[0].P.X, 100) && Near(g.Paths[1].Points[0].P.Y, 0) &&
                  Near(g.Paths[1].Points[2].P.X, 2900) && Near(g.Paths[1].Points[2].P.Y, -1400), "balcony skips the wall edge and ends the rail on it");
            Check(Tch::DecodeBalcony(*BalconyObject(0x12, false, 0, 0, 2), b) && Tch::BuildBalcony(b).Paths.size() == 4,
                  "balcony edge without rail splits the rail");
            Tch::TchHandRail h;
            Check(Tch::DecodeHandRail(*HandRailObject(0x13, false, 0, 0, 1), h) && h.Align == 1 && h.Draw2d && Near(Tch::BuildHandRail(h).Paths[0].Points[0].P.Y, 0) &&
                  Near(Tch::BuildHandRail(h).Paths[1].Points[0].P.Y, -60), "handrail aligned to its path on the left");
            Check(Tch::DecodeHandRail(*HandRailObject(0x13, false, 0, 0, 2), h) && Near(Tch::BuildHandRail(h).Paths[0].Points[0].P.Y, 60) &&
                  Near(Tch::BuildHandRail(h).Paths[1].Points[0].P.Y, 0), "handrail aligned to its path on the right");
            Check(Tch::DecodeHandRail(*HandRailObject(0x13, false, 0, 0, 0, 0), h) && Tch::BuildHandRail(h).Paths.empty(), "handrail without 2D draws nothing");
            Tch::TchAutoStair e;
            Check(Tch::DecodeAutoStair(*AutoStairObject(0x12), e) && Tch::BuildAutoStair(e).Arrows.size() == 2 &&
                  Tch::DecodeAutoStair(*AutoStairObject(0x12, false, 0, 0, 0), e) && Tch::BuildAutoStair(e).Arrows.empty(), "escalator arrow flag");
            Tch::TchParallelStair ps;
            Check(Tch::DecodeParallelStair(*ParallelStairObject(0x12), ps) && !Tch::BuildParallelStair(ps).Arrows.empty() &&
                  Tch::DecodeParallelStair(*ParallelStairObject(0x12, false, 0, 0, 0), ps) && Tch::BuildParallelStair(ps).Arrows.empty(), "parallel stair arrow flag");
        }
        Tch::TchStep step;
        if (DecodesExactly("encrypted step decodes", Tch::DecodeStep, [](bool x) { return StepObject(0x12, x); }, step))
            Check(step.NoStepMask == 8 && step.Lines == 3 && Near(step.Tread, 300) && step.Outline.size() == 4, "step outline and treads");
        Tch::TchWallPatch patch;
        if (DecodesExactly("new-format wall patch decodes", Tch::DecodeWallPatch, [](bool x) { return WallPatchObject(0x12, x); }, patch))
            Check(patch.Outline.size() == 4 && patch.Walls.size() == 1 && patch.Walls[0] == 0x2b0, "wall patch outline and wall");
        Tch::TchOpeningSlot slot;
        if (DecodesExactly("encrypted opening slot decodes", Tch::DecodeOpeningSlot, [](bool x) { return OpeningSlotObject(0x13, x); }, slot))
            Check(slot.Opening == 0x2a0 && Near(slot.ProfileWidth, 60) && Near(slot.ProfileDepth, 15), "opening slot owner and profile");
        Tch::TchLineStair line;
        if (DecodesExactly("encrypted line stair decodes", Tch::DecodeLineStair, [](bool x) { return LineStairObject(0x12, x); }, line))
            Check(line.Flight.Steps == 12 && Near(line.Tread, 270) && Near(line.Width, 1200), "line stair flight");
        Tch::TchArcStair arc;
        if (DecodesExactly("encrypted arc stair decodes", Tch::DecodeArcStair, [](bool x) { return ArcStairObject(0x13, x); }, arc))
            Check(arc.Flight.Steps == 10 && Near(arc.OuterRadius, 2200) && Near(arc.Sweep, std::numbers::pi / 2), "arc stair radii");
        Tch::TchCurveStair curve;
        if (DecodesExactly("encrypted curve stair decodes", Tch::DecodeCurveStair, [](bool x) { return CurveStairObject(0x14, x); }, curve))
            Check(Near(curve.Right.Y1, 1200) && Near(curve.Left.X2, 3000), "curve stair edges");
        Tch::TchAutoStair escalator;
        if (DecodesExactly("encrypted escalator decodes", Tch::DecodeAutoStair, [](bool x) { return AutoStairObject(0x12, x); }, escalator))
            Check(Near(escalator.Tread, 400) && Near(escalator.AngleDeg, 30) && escalator.TextStyle == 0x82a && escalator.ArrowLayer == 0x289,
                  "escalator size and handles");
        Tch::TchHandRail rail;
        if (DecodesExactly("encrypted handrail decodes", Tch::DecodeHandRail, [](bool x) { return HandRailObject(0x13, x); }, rail))
            Check(rail.Path.size() == 2 && Near(rail.Width, 60), "handrail path and width");
        Tch::TchParallelStair parallel;
        if (DecodesExactly("encrypted parallel stair decodes", Tch::DecodeParallelStair, [](bool x) { return ParallelStairObject(0x12, x); }, parallel))
            Check(parallel.Steps1 == 10 && parallel.BreakSteps == 5 && Near(parallel.Landing, 1300) && parallel.TextStyle == 0x82a, "parallel stair settings");
        Tch::TchPathArray array;
        if (DecodesExactly("encrypted path array decodes", Tch::DecodePathArray, [](bool x) { return PathArrayObject(0x12, x); }, array))
            Check(array.Block == 0x2c0 && array.Vertices.size() == 2 && Near(array.Width, 100), "path array block and path");
        Tch::TchRupture rupture;
        if (DecodesExactly("encrypted rupture line decodes", Tch::DecodeRupture, [](bool x) { return RuptureObject(0x14, x); }, rupture))
            Check(Near(rupture.X2, 3000) && Near(rupture.Scale, 100), "rupture line ends");
        {
            Tch::TchRupture r;
            Check(Tch::DecodeRupture(*RuptureObject(0x14, false, 0, 0, 2), r) && r.Count == 2 && Tch::BuildRupture(r).Paths.size() == 2 &&
                  Near(Tch::BuildRupture(r).Paths[1].Points[0].P.X, 1500), "rupture count splits the line into zigzag segments");
            Check(Tch::DecodeRupture(*RuptureObject(0x14, false, 0, 0, 20), r) && Tch::BuildRupture(r).Paths.empty() && Tch::BuildRupture(r).Lines.size() == 20,
                  "rupture segments shorter than the zigzag are straight");
            Check(Tch::DecodeRupture(*RuptureObject(0x14, false, 0, 0, 0), r) && Tch::BuildRupture(r).Paths.empty() && Tch::BuildRupture(r).Lines.empty(),
                  "rupture count 0 draws nothing");
        }
        Tch::TchCut cutLine;
        if (DecodesExactly("encrypted cut line decodes", Tch::DecodeCut, [](bool x) { return CutObject(0x12, x); }, cutLine))
            Check(cutLine.Vertices.size() == 3 && cutLine.Vertices[0].Breaks == 1 && cutLine.Vertices[2].Breaks == 0, "cut line breaks");
        Tch::TchModi modi;
        if (DecodesExactly("encrypted revision cloud decodes", Tch::DecodeModi, [](bool x) { return ModiObject(0x13, x); }, modi))
            Check(modi.Points.size() == 4 && Near(modi.Bulge, 0.5), "revision cloud points and bulge");
        Tch::TchRadiusDim radius;
        if (DecodesExactly("encrypted radius dimension decodes", Tch::DecodeRadiusDim, [](bool x) { return RadiusDimObject(0x12, x); }, radius))
            Check(radius.TextOverride == "R1000" && radius.HasTextPosition && Near(radius.TipX, 1000) && radius.DimStyle == 0x2d0, "radius dimension text and tip");

        Tch::TchText text;
        if (DecodesExactly("encrypted text class version 3 (one handle) decodes", Tch::DecodeText, [](bool x) { return TextObject(3, 1, x); }, text))
            Check(text.Value == "ABC" && Near(text.X, 100) && text.Style == 0x2d0 && text.Attachment == 7, "text version 3 fields");
        DecodesExactly("encrypted text class version 4 (three handles) decodes", Tch::DecodeText, [](bool x) { return TextObject(4, 3, x); }, text);
        Check(!Tch::DecodeText(*TextObject(4, 1), text), "text class version 4 needs its two extra handles");
        Check(!Tch::DecodeText(*TextObject(5, 3), text), "text class version 5 is not guessed");

        std::string why;
        Tch::TchDormer dormer;
        Check(Tch::DecodeDormer(*DormerObject(0x12), dormer, &why) && Near(dormer.X, 5000) && Near(dormer.Y, 3000) &&
              Near(dormer.Width, 1800) && Near(dormer.Depth, 1400) && Near(std::abs(dormer.Rotation), std::numbers::pi),
              "encrypted dormer: parameters, insertion point, rotation from the roof normal azimuth");
        Check(Tch::DecodeDormer(*DormerObject(0x13, 0, -0.6), dormer) && Near(dormer.Rotation, -std::numbers::pi / 2) && Near(dormer.Z, 750),
              "encrypted dormer with key >= 17 reorders the points");
        Check(Tch::DecodeDormer(*PlainDormerObject(), dormer, &why) && Near(dormer.X, 5000) && Near(dormer.Rotation, std::numbers::pi / 2) &&
              Near(dormer.Width, 1800), "plain dormer reads BS + 10 BD parameters + insertion point + normal");
        {
            const auto g = Tch::BuildDormer(dormer);
            Check(g.Paths.size() == 4 && g.Paths[0].Points.size() == 5, "dormer draws the window outline, ridge and two slope lines");
        }
        {
            auto bad = PlainDormerObject();
            bad->Raw.Dwg.MainBits -= 40; bad->Raw.Dwg.Main.resize((bad->Raw.Dwg.MainBits + 7) / 8);
            Check(!Tch::DecodeDormer(*bad, dormer), "truncated plain dormer is rejected");
        }

        Tch::TchSpace space;
        Check(Tch::DecodeSpace(*SpaceObject({{1000, 1000}, {5000, 1000}, {5000, 4000}, {1000, 4000}}), space, &why) &&
              space.Outline.size() == 4 && Near(space.Outline[2].X, 5000) && Near(space.Outline[2].Y, 4000),
              "space outline is the vertex array that ends at the end of the main stream");
        Check(Tch::DecodeSpace(*SpaceObject({{0, 500}, {3000, 0}, {3000, 2000}}, 6), space) && space.Outline.size() == 3,
              "space outline may end up to 8 bits before the stream end and have a zero coordinate");
        Check(!Tch::DecodeSpace(*SpaceObject({{1000, 1000}, {5000, 1000}, {5000, 4000}}, 20), space),
              "space vertex array that does not reach the stream end is not taken");
        Check(!Tch::DecodeSpace(*SpaceObject({{1000, 1000}, {2000, 1000}, {3000, 1000}}), space), "collinear (zero area) outline is rejected");
        Check(!Tch::DecodeSpace(*SpaceObject({{1000, 1000}, {1e-200, 1000}, {3000, 4000}}), space), "denormal-scale coordinates are rejected");
        {
            const auto g = Tch::BuildSpace(space.Outline.empty() ? Tch::TchSpace{} : space);
            Check(!g.Paths.empty(), "space builds a closed outline path");
        }
        // 房间名从加密文本流里的 CJK 明文串提取；面积按轮廓多边形算（mm² → m²），画成两行居中文字。
        {
            Tch::TchSpace named;
            const bool ok = Tch::DecodeSpace(*SpaceObject({{1000, 1000}, {5000, 1000}, {5000, 4000}, {1000, 4000}}, 0, true), named, &why);
            Check(ok && named.Name == "房间", "space name is the plain CJK string in the text stream");
            const auto g = Tch::BuildSpace(named);
            const bool twoLines = g.Texts.size() == 2 && g.Texts[0].Value == "房间" &&
                                  g.Texts[1].Value.find("12.00m") != std::string::npos;
            Check(twoLines, "space draws the name and the area as two centered lines");
        }

        Tch::TchCurveMesh mesh;
        Check(Tch::DecodeCurveMesh(*CurveMeshObject(false, {{12000, 5000, 0}, {12500, 5000, 300}, {13000, 5200, 600}, {13500, 5400, 300}}), mesh, &why) &&
              mesh.Points.size() == 4 && Near(mesh.Points[3].X, 13500) && Near(mesh.Points[3].Y, 5400),
              "encrypted curve mesh reads count + Point3/bulge vertices through CipherReader");
        Check(Tch::DecodeCurveMesh(*CurveMeshObject(true, {{0, 8000, 0}, {2000, 8000, 0}, {4000, 9000, 0}}, false, true), mesh, &why) && mesh.Points.size() == 3,
              "plain curve mesh without a vertex count falls back to the BD run scan");
        Check(!Tch::DecodeCurveMesh(*CurveMeshObject(false, {{12000, 5000, 0}, {12500, 5000, 0}}), mesh), "two points are not a mesh");
        {
            Tch::TchCurveMesh m;
            const bool ok = Tch::DecodeCurveMesh(*CurveMeshObject(false,
                {{1000, 2000, 0}, {3000, 2000, 0}, {3000, 4000, 0}, {1000, 4000, 0}}, true), m, &why);
            Check(ok && m.Points.size() == 4 && m.Closed && Near(m.Points[0].X, 1000) && Near(m.Points[0].Y, 2000),
                  "structured curve mesh keeps the real XY coordinates and the closed flag");
            const auto g = Tch::BuildCurveMesh(m);
            const bool drawn = g.Paths.size() == 1 && g.Paths[0].Points.size() == 5 &&
                               Near(g.Paths[0].Points.front().P.X, g.Paths[0].Points.back().P.X) &&
                               Near(g.Paths[0].Points.front().P.Y, g.Paths[0].Points.back().P.Y);
            Check(drawn, "closed curve mesh draws a closed XY outline");
        }
        {
            Tch::TchCurveMesh m;
            const bool ok = Tch::DecodeCurveMesh(*CurveMeshObject(true,
                {{66355, 34804, 0}, {63903, 34804, 0}, {63903, 44464, 0}, {70383, 44464, 0}, {70383, 34804, 0}, {67255, 34804, 0}}), m, &why);
            Check(ok && m.Points.size() == 6 && !m.Closed && Near(m.Points[0].X, 66355) && Near(m.Points[5].X, 67255),
                  "plain curve mesh reads the vertex count, vertices and closed flag");
            const auto g = Tch::BuildCurveMesh(m);
            Check(g.Paths.size() == 1 && g.Paths[0].Points.size() == 6, "open curve mesh keeps the gap between its ends");
        }

        int falseSpaces = 0, falseMeshes = 0, falsePlainMeshes = 0;
        std::uint64_t seed = 0x9e3779b97f4a7c15ull;
        for (int trial = 0; trial < 400; ++trial)
        {
            const int bits = 200 + trial * 7;
            falseSpaces += Tch::DecodeSpace(*NoiseObject("TCH_SPACE", false, seed, bits), space) ? 1 : 0;
            falseMeshes += Tch::DecodeCurveMesh(*NoiseObject("TCH_CURVEMESH2", false, seed, bits), mesh) ? 1 : 0;
            falsePlainMeshes += Tch::DecodeCurveMesh(*NoiseObject("TCH_CURVEMESH2", true, seed, bits), mesh) ? 1 : 0;
        }
        std::printf("noise: %d spaces, %d meshes, %d plain meshes recognised\n", falseSpaces, falseMeshes, falsePlainMeshes);
        Check(falseSpaces == 0, "random payloads after a valid header are never taken as a space outline");
        Check(falseMeshes == 0 && falsePlainMeshes == 0, "random payloads after a valid header are never taken as a curve mesh");

        Tch::TchPipe pipe;
        if (DecodesExactly("encrypted pipe decodes", Tch::DecodePipe, [](bool x) { return PipeObject(0x12, false, x); }, pipe))
            Check(Near(pipe.End.X, 4000) && Near(pipe.Diameter, 100) && pipe.Label && pipe.LabelPrefix == "DN" && Near(pipe.VisibleLength, 3000) &&
                  Near(pipe.Scale, 100), "pipe ends and label");
        Tch::TchPipe twin;
        if (DecodesExactly("encrypted double-line pipe decodes", Tch::DecodePipe, [](bool x) { return PipeObject(0x13, true, x); }, twin))
            Check(Near(twin.Width, 200) && Near(twin.BendRatio, 1) && !twin.Label, "double-line pipe width");
        {
            std::string why;
            const bool ok = Tch::DecodePipeFitting(*PipeFittingObject(0x14), &why);
            Check(ok, Label("encrypted pipe fitting decodes", ok, why).c_str());
            Check(!Tch::DecodePipeFitting(*PipeFittingObject(0x14, true)), "encrypted pipe fitting: a leftover bit is rejected");
        }
        Tch::TchPipeBlock equipment;
        if (DecodesExactly("encrypted equipment decodes", Tch::DecodeEquipment, [](bool x) { return EquipmentObject(0x12, 0, x, 100, 200); }, equipment))
            Check(equipment.Blocks.size() == 2 && equipment.Blocks[0] == 0x2f0 && Near(equipment.X, 100) && Near(equipment.Rotation, std::numbers::pi / 2) &&
                  Near(equipment.ScaleX, -2) && Near(equipment.ScaleY, 2), "equipment block, rotation and mirror");
        Tch::TchPipeBlock well;
        if (DecodesExactly("encrypted well decodes", Tch::DecodeEquipment, [](bool x) { return EquipmentObject(0x13, 1, x); }, well))
            Check(Near(well.WellInvert, -2.5) && Near(well.WellGround, 0.3) && Near(well.ScaleY, 200), "well elevations and unit scale");
        Tch::TchPipeBlock pump;
        if (DecodesExactly("encrypted pump decodes", Tch::DecodeEquipment, [](bool x) { return EquipmentObject(0x14, 2, x); }, pump))
            Check(Near(pump.ScaleX, -700) && Near(pump.ScaleY, 500), "pump size");
        Tch::TchPipeBlock valve;
        if (DecodesExactly("encrypted valve decodes", Tch::DecodePipeValve, [](bool x) { return PipeValveObject(0x12, x, 300, 0); }, valve))
            Check(valve.Blocks.size() == 2 && valve.Blocks[0] == 0x2f1 && Near(valve.X, 300) && Near(valve.ScaleX, 1.5) && Near(valve.Rotation, std::numbers::pi),
                  "valve block and placement");
        {
            Tch::TchPipeBlock b;
            Check(Tch::DecodeEquipment(*EquipmentObject(0x12, 0, false, 0, 0, 0x2f0, 3), b) && Near(b.ScaleX, -2) && Near(b.ScaleY, 3),
                  "equipment Y scale overrides the X scale");
            Check(Tch::DecodePipeValve(*PipeValveObject(0x12, false, 0, 0, 0x2f1, 2), b) && Near(b.ScaleX, -1.5) && Near(b.ScaleY, -1.5),
                  "valve quadrant 2 mirrors both axes");
            Check(Tch::DecodePipeValve(*PipeValveObject(0x12, false, 0, 0, 0x2f1, 3, 2), b) && Near(b.ScaleX, 1.5) && Near(b.ScaleY, -2),
                  "valve quadrant 3 mirrors Y with its own Y scale");
        }
        Tch::TchPipeText pipeText;
        if (DecodesExactly("encrypted pipe text decodes", Tch::DecodePipeText, [](bool x) { return PipeTextObject(0x13, x); }, pipeText))
            Check(pipeText.Value == "DN100" && Near(pipeText.Height, 3.5) && Near(pipeText.Rotation, 0), "pipe text value and upright angle");
        Tch::TchVPipeDim riser;
        if (DecodesExactly("encrypted riser dimension decodes", Tch::DecodeVPipeDim, [](bool x) { return VPipeDimObject(0x14, x); }, riser))
            Check(riser.Value == "1JL-1" && Near(riser.Run, 1000) && riser.Color == 3, "riser dimension text from the property bag");
        Tch::TchWellDim wellDim;
        if (DecodesExactly("encrypted well dimension decodes", Tch::DecodeWellDim, [](bool x) { return WellDimObject(0x12, x); }, wellDim))
            Check(wellDim.Well == 0x2e3 && Near(wellDim.Y2, 1000) && Near(wellDim.Run, 1000), "well dimension leader and well");
        Tch::TchPipeMultDim multi;
        if (DecodesExactly("encrypted multi-pipe dimension decodes", Tch::DecodePipeMultDim, [](bool x) { return PipeMultDimObject(0x13, x); }, multi))
            Check(multi.Pipes.size() == 2 && multi.Pipes[1] == 0x2e2 && Near(multi.Angle, std::numbers::pi / 2) && Near(multi.Run, 2000) &&
                  Near(multi.Spacing, 350), "multi-pipe dimension pipes, angle and spacing");
        Tch::TchFlange flangeFitting;
        if (DecodesExactly("encrypted flange decodes", Tch::DecodeFlange, [](bool x) { return FlangeObject(0x14, x, 50, 60); }, flangeFitting))
            Check(flangeFitting.Directions.size() == 2 && flangeFitting.Pipes[0] == 0x2e1 && Near(flangeFitting.BendRatio, 1.5) && Near(flangeFitting.Y, 60),
                  "flange ports and bend ratio");
        Tch::TchCableTray tray;
        if (DecodesExactly("encrypted cable tray decodes", Tch::DecodeCableTray, [](bool x) { return CableTrayObject(0x12, false, 4, x); }, tray))
            Check(Near(tray.End.X, 5000) && Near(tray.Width, 300) && tray.Stroke.CenterLine, "cable tray ends from the class fields");
        Tch::TchCableTray tray3;
        if (DecodesExactly("encrypted version 3 cable tray decodes", Tch::DecodeCableTray, [](bool x) { return CableTrayObject(0x13, false, 3, x, 0, 100); }, tray3))
            Check(Near(tray3.End.X, 5000) && Near(tray3.End.Y, 100), "version 3 cable tray ends from the property map");
        Tch::TchCableFitting elbow;
        if (DecodesExactly("encrypted cable tray elbow decodes", Tch::DecodeCableFitting, [](bool x) { return CableFittingObject(0x14, false, x); }, elbow))
            Check(elbow.Type == Tch::TchCableFitting::Kind::Elbow && elbow.Elbow == 1 && Near(elbow.Length, 500) && elbow.Connected[0] == 0x2e4,
                  "cable tray elbow properties and connected tray");
        Tch::TchHvacDuct duct;
        if (DecodesExactly("encrypted duct decodes", Tch::DecodeHvacDuct, [](bool x) { return CableTrayObject(0x13, true, 4, x); }, duct))
            Check(Near(duct.End.X, 5000) && Near(duct.Width, 400), "duct ends and width");
        Tch::TchHvacFitting ductElbow;
        if (DecodesExactly("encrypted duct elbow decodes", Tch::DecodeHvacFitting, [](bool x) { return CableFittingObject(0x12, true, x); }, ductElbow))
            Check(ductElbow.Type == Tch::TchHvacFitting::Kind::Elbow && Near(ductElbow.Width0, 400) && Near(ductElbow.Pt2.Y, 1000), "duct elbow ends and width");
    }

    // 风管标注、乙字弯、弧形三通的图形
    {
        auto hasPoint = [](const Tch::TchGraphics& g, double x, double y, double tol = 0.5) {
            for (const auto& p : g.Paths)
                for (const auto& q : p.Points)
                    if (std::hypot(q.P.X - x, q.P.Y - y) <= tol) return true;
            for (const auto& l : g.Lines)
                if (std::hypot(l.A.X - x, l.A.Y - y) <= tol || std::hypot(l.B.X - x, l.B.Y - y) <= tol) return true;
            return false;
        };
        Tch::TchHvacDimension label;
        label.Anchor = {100, 200, 0};
        label.Angle = 3 * std::numbers::pi / 2;
        label.Values = {"100\xc3\x97" "100", "", "", ""};
        const auto single = Tch::BuildHvacDimension(label, {});
        Check(!single.Texts.empty() && Near(single.Texts[0].Rotation, std::numbers::pi / 2, 1e-9), "duct size label is turned upright");

        Tch::TchHvacDimension table;
        table.Style = 4;
        table.Anchor = {0, 0, 0};
        table.Corner = {-1000, 500, 0};
        table.Angle = std::numbers::pi;
        table.Values = {"A", "100X100", "1", "200"};
        const auto t = Tch::BuildHvacDimension(table, {});
        bool leader = false, rows = false;
        for (const auto& l : t.Lines) leader = leader || (Near(l.A.X, 0) && Near(l.B.X, -1000) && Near(l.B.Y, 500));
        for (const auto& p : t.Paths)
            for (const auto& q : p.Points) rows = rows || (Near(q.P.X, -1000) && Near(q.P.Y, 980));
        Check(leader && rows && t.Texts.size() == 4 && t.Texts[0].Center.X < t.Texts[1].Center.X && Near(t.Texts[0].Center.Y, 740) &&
                  Near(t.Texts[2].Center.Y, 260),
              "duct table: leader to the corner, two rows 1.6 x text height, extends to the left when the angle points left");

        Tch::TchHvacFitting offset;
        offset.Type = Tch::TchHvacFitting::Kind::Offset;
        offset.Width0 = offset.Width2 = 250;
        offset.Width3 = 160;
        offset.Bend = std::numbers::pi / 3;
        offset.Rise = 300;
        offset.Pt2 = {100 + 600 * std::sin(std::numbers::pi / 3), 300, 0};
        offset.Axis = {-1, 0, 0};
        const auto o = Tch::BuildHvacFitting(offset);
        Check(hasPoint(o, 50, 125) && hasPoint(o, 50 + 300 * std::sin(std::numbers::pi / 3), 150, 0.1) &&
                  hasPoint(o, offset.Pt2.X - 50, 300 - 125),
              "duct offset: two 60-degree arcs with radius equal to the offset and 50 straight at both ends");

        Tch::TchHvacFitting rise = offset;
        rise.Pt0 = {0, 0, 3000};
        rise.Pt2 = {-1200, 0, 2600};
        rise.Axis = {1, 0, 0};
        rise.Width3 = 1200;
        const auto r = Tch::BuildHvacFitting(rise);
        Check(hasPoint(r, -300, 200, 0.1) && hasPoint(r, -900, -200, 0.1) && hasPoint(r, -800, -186.83, 0.1) && hasPoint(r, -1200, 600),
              "vertical duct offset: straight duct with the Z mark (third of the half width, tick leaning in when it drops)");

        Tch::TchHvacFitting tee;
        tee.Type = Tch::TchHvacFitting::Kind::Tee;
        tee.TeeStyle = 3;
        tee.Pt2 = {-600, 0, 0};
        tee.Pt3 = {-300, -400, 0};
        tee.Width0 = tee.Width2 = 400;
        tee.Width3 = 200;
        const auto c = Tch::BuildHvacFitting(tee);
        Check(hasPoint(c, -50, -200) && hasPoint(c, -200, -350) && hasPoint(c, -400, -350) && hasPoint(c, -366.23, -200, 0.3) &&
                  hasPoint(c, -600, -200) && hasPoint(c, -600, 200),
              "curved-branch duct tee: inner quarter arc after the 50 straight, outer arc back to the main edge");
    }

    {
        CadDatabase db;
        db.SetVersion(CadVersion::AC1024);
        db.CreateDefaults();
        db.Header.ModelSpaceExtMin = {-500, -7000, 0};
        db.Header.ModelSpaceExtMax = {35500, 14500, 0};
        if (auto* vport = db.FindTableEntry<VPort>(db.VPorts(), "*Active"))
        {
            vport->Center = {17500, 3750};
            vport->ViewHeight = 22000;
            vport->AspectRatio = 1.8;
        }
        std::int16_t number = 500;
        for (const auto& [dxf, cpp] : {std::pair{"TCH_WALL", "TDbWall"}, {"TCH_CURTAIN_WALL", "TDbCurtainWall"}, {"TCH_COLUMN", "TDbColumn"},
                                       {"TCH_RECT", "TDbRect"}, {"TCH_MOUNTROOF", "TDbMountRoof"}, {"TCH_CUSPROOF", "TDbCuspRoof"},
                                       {"TCH_SLOPEROOF", "TDbSlopeRoof"}, {"TCH_DUALROOF", "TDbDualSlopeRoof"}, {"TCH_SLAB", "TDbSlab"},
                                       {"TCH_APRON", "TDbApron"}, {"TCH_CORNER_WINDOW", "TDbCornerWin"}, {"TCH_ASCENT", "TDbAscent"},
                                       {"TCH_BALCONY", "TDbBalcony"}, {"TCH_STEP", "TDbStep"}, {"TCH_LINESTAIR", "TDbLineStair"},
                                       {"TCH_ARCSTAIR", "TDbArcStair"}, {"TCH_CURVESTAIR", "TDbCurveStair"}, {"TCH_AUTOSTAIR", "TDbAutoStair"},
                                       {"TCH_HANDRAIL", "TDbHandRail"}, {"TCH_PARALLELSTAIR", "TDbParallelStair"}, {"TCH_RUPTURE", "TDbSymbRupture"},
                                       {"TCH_CUT", "TDbSymbCut"}, {"TCH_MODI", "TDbSymbModi"}, {"TCH_RADIUSDIM", "TDbRadiusDim"},
                                       {"TCH_PIPE", "TDbPipe"}, {"TCH_DOUBLELINEPIPE", "TDbDoubleLinePipe"}, {"TCH_PIPEFITTING", "TDbPipeFitting"},
                                       {"TCH_EQUIPMENT", "TDbEquipment"}, {"TCH_WELL", "TDbWell"}, {"TCH_PUMP", "TDbPump"},
                                       {"TCH_PIPEVALVE", "TDbPipeValve"}, {"TCH_PIPETEXT", "TDbPipeText"}, {"TCH_VPIPEDIM", "TDbVPipeDim"},
                                       {"TCH_WELLDIM", "TDbWellDim"}, {"TCH_PIPEMULTDIM", "TDbPipeMultDim"}, {"TCH_FLANGE", "TDbFlangeFitting"},
                                       {"TCH_CABLETRY", "TDbECabletry"}, {"TCH_EFITTING", "TDbEFitting"}, {"TCH_DBHVACDUCT", "TDbHvacDuct"},
                                       {"TCH_DBHFITTING", "TDbHvacFitting"}})
        {
            DxfClass c;
            c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
            c.ClassNumber = number++; c.IsAnEntity = true; c.ProxyFlags = 1; c.ItemClassId = 0x1f2;
            db.Classes.push_back(c);
        }
        Handle blocks[2];
        for (int i = 0; i < 2; ++i)
        {
            auto* block = db.CreateBlockRecord(i ? "TCH_SAMPLE_UNIT" : "TCH_SAMPLE_PART");
            auto circle = std::make_unique<Circle>();
            circle->Radius = i ? 0.5 : 150;
            db.AddEntity(block, std::move(circle));
            blocks[i] = block->ObjectHandle;
        }
        std::vector<std::unique_ptr<UnknownEntity>> samples;
        samples.push_back(WallObject(false, 0, 0x12, 0));
        samples.push_back(WallObject(true, 0x4, 0x12, 1500));
        samples.push_back(WallObject(true, 0x100 | 0x10000, 0x13, 3000));
        samples.push_back(ColumnObject(10, 0x12, 8, false, 4000));
        samples.push_back(ColumnObject(7, 0x0c, 0, false, 4000));
        samples.push_back(RectObject(0x12, 3, false, 6000));
        samples.push_back(MountRoofObject(0x12, false, 8000, 0));
        samples.push_back(CuspRoofObject(0x13, false, 15000, 2000));
        samples.push_back(SlopeRoofObject(0x12, false, 18000, 0));
        samples.push_back(DualRoofObject(0x14, false, 8000, 5000));
        samples.push_back(SlabObject(0x12, false, 13000, 5000));
        samples.push_back(ApronObject(0x13, false, 18000, 5000));
        samples.push_back(CornerWindowObject(0x12, false, 22500, 5500));
        samples.push_back(AscentObject(0x14, false, 9500, 12500));
        samples.push_back(BalconyObject(0x12, false, 12000, 12500));
        samples.push_back(StepObject(0x13, false, 16000, 10500));
        samples.push_back(LineStairObject(0x12, false, 19000, 10500));
        samples.push_back(ArcStairObject(0x13, false, 22000, 10000));
        samples.push_back(CurveStairObject(0x14, false, 8000, -3500));
        samples.push_back(AutoStairObject(0x12, false, 12000, -4500));
        samples.push_back(HandRailObject(0x13, false, 15000, -1500));
        samples.push_back(ParallelStairObject(0x12, false, 19000, -5500));
        samples.push_back(RuptureObject(0x14, false, 15000, -3000));
        samples.push_back(CutObject(0x12, false, 15000, -4500));
        samples.push_back(ModiObject(0x13, false, 22000, -4000));
        samples.push_back(RadiusDimObject(0x12, false, 23500, 1000));
        samples.push_back(PipeObject(0x12, false, false, 26500, 13000));
        samples.push_back(PipeObject(0x13, true, false, 26500, 11000));
        samples.push_back(PipeFittingObject(0x14, false, 30500, 13000));
        samples.push_back(EquipmentObject(0x12, 0, false, 31500, 13000, blocks[0]));
        const std::size_t wellIndex = samples.size();
        samples.push_back(EquipmentObject(0x13, 1, false, 33000, 13000, blocks[1]));
        samples.push_back(EquipmentObject(0x14, 2, false, 34500, 13000, blocks[1]));
        samples.push_back(PipeValveObject(0x12, false, 29000, 9000, blocks[0]));
        samples.push_back(PipeTextObject(0x13, false, 27500, 9000));
        samples.push_back(VPipeDimObject(0x14, false, 26500, 6500));
        samples.push_back(PipeMultDimObject(0x13, false, 33000, 6000));
        samples.push_back(FlangeObject(0x14, false, 30500, 11000));
        samples.push_back(CableTrayObject(0x12, false, 4, false, 26500, 3500));
        samples.push_back(CableTrayObject(0x13, false, 3, false, 26500, 2000));
        samples.push_back(CableFittingObject(0x14, false, false, 33000, 3500));
        samples.push_back(CableTrayObject(0x12, true, 4, false, 26500, 0));
        samples.push_back(CableFittingObject(0x13, true, false, 33000, 0));
        std::vector<Handle> handles;
        for (auto& e : samples)
        {
            e->Raw.Dwg.Version = CadVersion::AC1024;
            handles.push_back(db.AddEntity(db.ModelSpace(), std::move(e))->ObjectHandle);
        }
        auto wellDim = WellDimObject(0x12, false, 33000, 13000, handles[wellIndex]);
        wellDim->Raw.Dwg.Version = CadVersion::AC1024;
        db.AddEntity(db.ModelSpace(), std::move(wellDim));
        const std::size_t count = samples.size() + 1;
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

    // 本机样图（T30 写的 2007 格式，不在版本库里）：47 个TCH对象（20 类）全部解出；图纸设置为版本 11
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

    // MINICAD_R2000_SAMPLE=<路径>：R2000 图纸里有解码函数的TCH对象全部解出
    {
        const std::string file = EnvPath("MINICAD_R2000_SAMPLE");
        const auto data = file.empty() ? std::vector<std::uint8_t>{} : ReadFile(file);
        if (data.empty()) std::printf("[SKIP] MINICAD_R2000_SAMPLE\n");
        else if (const auto db = ReadDwg(data); !db || db->GetVersion() != CadVersion::AC1015) Check(false, "R2000 sample reads as AC1015");
        else
        {
            int total = 0, decoded = 0;
            for (const auto& [handle, object] : db->Objects())
            {
                const auto* e = dynamic_cast<const UnknownEntity*>(object.get());
                if (!e || e->Raw.DxfName.rfind("TCH_", 0) != 0) continue;
                std::string why;
                const int r = DecodeByType(*e, why);
                if (r < 0) continue;
                ++total;
                decoded += r;
                if (r == 0) std::printf("  %s: %s\n", e->Raw.DxfName.c_str(), why.c_str());
            }
            Check(total > 0 && decoded == total,
                  ("R2000 sample: " + std::to_string(decoded) + " of " + std::to_string(total) + " Tianzheng objects decode").c_str());
        }
    }

    {
        const std::string file = EnvPath("MINICAD_HVAC_SAMPLE");
        const auto data = file.empty() ? std::vector<std::uint8_t>{} : ReadFile(file);
        if (data.empty()) std::printf("[SKIP] MINICAD_HVAC_SAMPLE\n");
        else if (const auto db = ReadDwg(data); !db) Check(false, "R2004 HVAC sample reads");
        else
        {
            std::map<std::string, std::pair<int, int>> counts;
            for (const auto& [handle, object] : db->Objects())
            {
                const auto* e = dynamic_cast<const UnknownEntity*>(object.get());
                if (!e || (e->Raw.DxfName != "TCH_PIPE" && e->Raw.DxfName != "TCH_PIPEFITTING" && e->Raw.DxfName != "TCH_PIPETEXT" &&
                           e->Raw.DxfName != "TCH_HVACDIMENSION" && e->Raw.DxfName != "TCH_DBHFITTING"))
                    continue;
                std::string why;
                const int r = DecodeByType(*e, why);
                ++counts[e->Raw.DxfName].first;
                counts[e->Raw.DxfName].second += r > 0;
                if (r <= 0) std::printf("  %llx %s: %s\n", static_cast<unsigned long long>(handle), e->Raw.DxfName.c_str(), why.c_str());
            }
            bool all = counts.size() == 5;
            for (const auto& [name, c] : counts) all = all && c.first > 0 && c.second == c.first;
            Check(all, "R2004 HVAC sample: pipes, pipe fittings, pipe texts, duct dimensions and all duct fitting kinds decode");
        }
    }

    std::printf("%s\n", g_failed ? "FAILED" : "ALL PASSED");
    return g_failed ? 1 : 0;
}
