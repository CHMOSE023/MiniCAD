// 天正门窗（TCH_OPENING）：私有布局解码、墙体断口与门窗显示
#include "Import/TchOpening.h"
#include "Import/TchColumn.h"
#include "Core/Entity/PolylineEntity.hpp"
#include "Import/TchWall.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniDWG;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    struct OpeningSpec
    {
        XYZ    Position{47362.925244676284, 25106.878374984874, 0};
        double Width = 900, Height = 2100, Angle = 4.971049166241167;
        int    Mirror = 3, Kind = 0, Version = 5, BaseVersion = 5;
        int    Flags = 0x400;                       // 门窗标志：0x400 门槛线在门扇对面（样例 321），0x200 在门扇一侧
        bool   NestedBag = false;
        Handle Block2D = 0x2b1, Block3D = 0x2bc, Wall = 0x2ab;
    };

    // 按版本 < 6 布局写出；数值取自真实样例句柄 321
    std::unique_ptr<UnknownEntity> Opening(const OpeningSpec& s = {})
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_OPENING";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(static_cast<std::uint8_t>(s.BaseVersion)); r.WriteBitDouble(100); r.WriteBitDouble(0);
        r.WriteByte(3); r.WriteByte(0); r.WriteByte(0);                 // 标志 3，两组句柄各 0 个
        if (s.NestedBag)
        {
            r.WriteBitLong(3);
            r.WriteBitLong(0xca); r.WriteBitDouble(1); r.WriteBitDouble(2); r.WriteBitDouble(3);
            r.WriteBitLong(100); r.WriteBitLong(1); r.WriteBitLong(4); r.WriteBitShort(7);
            r.WriteBitLong(0); r.WriteBit(true);
        }
        else
        {
            r.WriteBitLong(2); r.WriteBitLong(200); r.WriteBitDouble(0); r.WriteBitLong(0x65);
        }
        r.WriteByte(static_cast<std::uint8_t>(s.Version));
        r.WriteBitDouble(s.Position.X); r.WriteBitDouble(s.Position.Y); r.WriteBitDouble(s.Position.Z);
        r.WriteBitDouble(s.Width); r.WriteBitDouble(100); r.WriteBitDouble(s.Height); r.WriteBitDouble(s.Angle);
        r.WriteByte(static_cast<std::uint8_t>(s.Mirror)); r.WriteByte(static_cast<std::uint8_t>(s.Kind));
        r.WriteBitDouble(0);
        r.WriteBitDouble(47744.78); r.WriteBitDouble(25207.91); r.WriteBitDouble(0);
        r.WriteBitShort(static_cast<std::int16_t>(s.Flags));
        r.WriteBitDouble(3.5); r.WriteBitDouble(100); r.WriteBitDouble(40); r.WriteBitDouble(40);
        r.WriteBitShort(0);
        if (s.Version > 2) { r.WriteBitLong(0); r.WriteBitShort(7); r.WriteBit(false); for (int i = 0; i < 4; ++i) r.WriteBitDouble(0); }
        if (s.Version > 3) r.WriteBitDouble(100);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        r.WriteHandle(DwgRef::HardPointer, s.Block2D);
        r.WriteHandle(DwgRef::HardPointer, s.Block3D);
        r.WriteHandle(DwgRef::SoftPointer, s.Wall);
        for (int i = 0; i < 5; ++i) r.WriteHandle(DwgRef::HardPointer, 0x25c);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    // 带 openings 个门窗的直墙（布局同 TianzhengWallTests），起点 (1000,2000)、终点 (5000,2000)、两侧各 120、接头 0；
    // material 为材料码（40 砖、60 钢筋混凝土），scale 为出图比例
    std::unique_ptr<UnknownEntity> Wall(int openings, int material = 40, double scale = 100)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_WALL";
        e->Raw.Dwg.Version = CadVersion::AC1027;
        DwgBitWriter g(CadVersion::AC1027, Codec::CodePage::Gbk);
        g.Write3RawDouble({1000, 2000, 0}); g.Write3RawDouble({5000, 2000, 0});
        g.WriteRawDouble(0); g.WriteRawLong(0);
        g.WriteRawDouble(120); g.WriteRawDouble(120); g.WriteRawDouble(3000);
        auto protectedData = g.Take();
        for (auto& b : protectedData) b ^= 0x3b;
        DwgBitWriter r(CadVersion::AC1027, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(scale); r.WriteBitShort(0); r.WriteByte(3);
        r.Write2Bits(0); r.WriteRawShort(512); r.WriteByte(8); r.WriteByte(0x3b);
        r.WriteBytes(protectedData);
        r.WriteByte(0); r.WriteBitShort(static_cast<std::int16_t>(openings));
        r.WriteByte(static_cast<std::uint8_t>(material)); r.WriteByte(0);   // 材料码、墙体造型数
        r.WriteBitDouble(0);
        for (int i = 0; i < 4; ++i) r.WriteBitDouble(0);
        r.WriteByte(0); r.Write3Bits(2);
        e->Raw.Dwg.MainBits = r.PositionInBits(); e->Raw.Dwg.Main = r.Take();
        return e;
    }

    DxfClass Class(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }

    // 柱：按版本 < 7 布局写出；数值取自真实样例句柄 2AA（600×600 方柱，948 位）
    std::unique_ptr<UnknownEntity> Column(double bulge = 0)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_COLUMN";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(3); r.WriteByte(0); r.WriteByte(0);
        r.WriteBitLong(0);
        r.WriteByte(6);
        r.WriteBitDouble(46453.98983148155); r.WriteBitDouble(28363.369764099378); r.WriteBitDouble(0);
        r.WriteByte(1);
        r.WriteBitShort(4);
        const double pts[4][2] = {{46153.98983148155, 28063.369764099378}, {46753.98983148155, 28063.369764099378},
                                  {46753.98983148155, 28663.369764099378}, {46153.98983148155, 28663.369764099378}};
        for (const auto& p : pts) { r.WriteRawDouble(p[0]); r.WriteRawDouble(p[1]); r.WriteBitDouble(bulge); }
        r.WriteBitDouble(3000); r.WriteByte(1); r.WriteByte(2); r.WriteByte(60);
        r.WriteBitShort(0); r.WriteByte(0); r.WriteBitDouble(80.08);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        return e;
    }

    // 图纸设置（TCH_DBCONFIG，非图形对象）：按 TDbConfig::dwgInFields 版本 9 写到填充墙2 的图案为止；
    // names 为空时没有文字流（图案名取默认值），否则是文字流里依次的字符串
    std::unique_ptr<UnknownObject> Config(bool fill, double detailScale = 100, const std::vector<std::string>& names = {})
    {
        auto o = std::make_unique<UnknownObject>();
        o->Raw.DxfName = "TCH_DBCONFIG";
        auto& raw = o->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(9); r.WriteBitDouble(0.4); r.WriteBitDouble(0); r.WriteBitShort(36);
        r.WriteByte(fill ? 1 : 0); r.WriteByte(0); r.WriteByte(0); r.WriteByte(0);
        r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteByte(0); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteBitDouble(100); r.WriteBitDouble(detailScale);
        for (int i = 0; i < 14; ++i) r.WriteBitDouble(0.25);      // 12 个加粗宽度等，版本 6 起又 2 个
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        if (!names.empty())
        {
            DwgBitWriter text(raw.Version, Codec::CodePage::Gbk);
            for (const auto& s : names) text.WriteVariableText(s);
            raw.TextBits = text.PositionInBits(); raw.Text = text.Take();
        }
        return o;
    }

    // 墙 + 门（门窗标志 flags，门扇在 -Y 一侧）导入后，门槛线的 y 坐标（墙沿 x 轴，两侧墙面 y = 1880 / 2120）
    std::vector<double> ThresholdLines(int flags)
    {
        CadDatabase db;
        db.SetVersion(CadVersion::AC1027);
        db.CreateDefaults();
        db.Classes.push_back(Class("TCH_WALL", "TDbWall", 500));
        db.Classes.push_back(Class("TCH_OPENING", "TDbOpening", 501));
        BlockRecord* doorBlock = db.CreateBlockRecord("$TCHSYS$DOOR2D");
        auto leaf = std::make_unique<Line>(); leaf->StartPoint = {0.45, 0, 0}; leaf->EndPoint = {0.45, 0.975, 0};
        db.AddEntity(doorBlock, std::move(leaf));
        OpeningSpec spec;
        spec.Position = {3000, 2000, 0}; spec.Angle = 0; spec.Mirror = 3; spec.Flags = flags;
        spec.Block2D = doorBlock->ObjectHandle; spec.Block3D = 0; spec.Wall = db.AddEntity(db.ModelSpace(), Wall(1))->ObjectHandle;
        db.AddEntity(db.ModelSpace(), Opening(spec));
        DwgWriteOptions options; options.Version = CadVersion::AC1027;
        MiniCAD::Scene scene;
        std::vector<double> ys;
        if (!MiniCAD::ImportCad(WriteDwg(db, options), scene)) return {-1};
        scene.ForEachObject([&](const MiniCAD::Object& obj)
        {
            const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
            if (!insert || !insert->GetBlock() || !insert->GetBlock()->GetName().starts_with("*U_TCH_OPENING_")) return;
            for (const auto& e : insert->GetBlock()->GetEntities())
                if (const auto* line = dynamic_cast<const MiniCAD::LineEntity*>(e.get()))
                    ys.push_back(line->GetLine().Start.y);
        });
        std::sort(ys.begin(), ys.end());
        return ys;
    }
}

int main()
{
    using MiniCAD::Tch::TchOpening;
    TchOpening o;
    std::string why;

    // ── 解码 ──
    const auto door = Opening();
    Check(MiniCAD::Tch::DecodeOpening(*door, o, &why) && o.Type == TchOpening::Kind::Door && Near(o.Width, 900) &&
          Near(o.Height, 2100) && Near(o.Angle, 4.971049166241167) && Near(o.Position.X, 47362.925244676284),
          "door from the real-sample layout decodes position, size and angle");
    Check(o.Block2D == 0x2b1 && o.Wall == 0x2ab, "2D block and host wall come from the handle stream in order");
    Check(o.Mirror == 3 && !o.MirrorX() && o.MirrorY(), "mirror mode 3 flips Y only (matches Tianzheng 2014 explode)");
    OpeningSpec windowSpec; windowSpec.Kind = 1; windowSpec.Width = 1800; windowSpec.Position.Z = 900;
    Check(MiniCAD::Tch::DecodeOpening(*Opening(windowSpec), o) && o.Type == TchOpening::Kind::Window && Near(o.Position.Z, 900),
          "window decodes with sill height");
    for (int m = 0; m < 4; ++m)
    {
        OpeningSpec spec; spec.Mirror = m;
        MiniCAD::Tch::DecodeOpening(*Opening(spec), o);
        const bool expectX = m == 1 || m == 2, expectY = m == 2 || m == 3;
        if (o.MirrorX() != expectX || o.MirrorY() != expectY) ++g_failed;
    }
    Check(g_failed == 0, "mirror modes map to X / X+Y / Y as in the conversion code");
    OpeningSpec nested; nested.NestedBag = true;
    Check(MiniCAD::Tch::DecodeOpening(*Opening(nested), o) && Near(o.Width, 900),
          "variable-length base property bag (points, nested bag) is consumed exactly");

    int accepted = 0;
    for (std::uint64_t bits = 0; bits < door->Raw.Dwg.MainBits; ++bits)
    {
        auto cut = Opening();
        cut->Raw.Dwg.MainBits = bits;
        cut->Raw.Dwg.Main.resize((bits + 7) / 8);
        accepted += MiniCAD::Tch::DecodeOpening(*cut, o) ? 1 : 0;
    }
    Check(accepted == 0, "every truncated payload is rejected");
    auto extra = Opening(); extra->Raw.Dwg.MainBits += 1; extra->Raw.Dwg.Main.push_back(0);
    Check(!MiniCAD::Tch::DecodeOpening(*extra, o), "unconsumed trailing bits are rejected");
    OpeningSpec v6; v6.Version = 6;
    Check(!MiniCAD::Tch::DecodeOpening(*Opening(v6), o), "protected version-6 openings are not guessed");
    OpeningSpec b6; b6.BaseVersion = 6;
    Check(!MiniCAD::Tch::DecodeOpening(*Opening(b6), o), "unverified base version is rejected");
    OpeningSpec hole; hole.Kind = 2;
    Check(MiniCAD::Tch::DecodeOpening(*Opening(hole), o) && o.Type == MiniCAD::Tch::TchOpening::Kind::Hole,
          "holes (kind 2) decode; their display is checked against Tianzheng in MiniCADTchGeometryTests");
    OpeningSpec other; other.Kind = 3;
    Check(!MiniCAD::Tch::DecodeOpening(*Opening(other), o), "opening kinds without verified display are not drawn");
    auto noBlock = Opening(); noBlock->Raw.Dwg.Handles.clear(); noBlock->Raw.Dwg.HandleBits = 0;
    Check(!MiniCAD::Tch::DecodeOpening(*noBlock, o), "missing handle stream is rejected");

    // ── 柱 ──
    MiniCAD::Tch::TchColumn col;
    const auto column = Column();
    Check(column->Raw.Dwg.MainBits == 948, "synthetic column reproduces the 948-bit real layout");
    Check(MiniCAD::Tch::DecodeColumn(*column, col) && col.Outline.size() == 4 && Near(col.Height, 3000) &&
          Near(col.Outline[2].X - col.Outline[0].X, 600) && Near(col.Outline[2].Y - col.Outline[0].Y, 600),
          "column outline vertices are 2RD world coordinates with height");
    Check(MiniCAD::Tch::DecodeColumn(*Column(0.4142), col) && Near(col.Outline[1].Bulge, 0.4142), "column vertex bulge is kept");
    accepted = 0;
    for (std::uint64_t bits = 0; bits < column->Raw.Dwg.MainBits; ++bits)
    {
        auto cut = Column();
        cut->Raw.Dwg.MainBits = bits;
        cut->Raw.Dwg.Main.resize((bits + 7) / 8);
        accepted += MiniCAD::Tch::DecodeColumn(*cut, col) ? 1 : 0;
    }
    Check(accepted == 0, "every truncated column payload is rejected");

    // ── 端到端：墙 + 门 + 门图块 → DWG → Scene ──
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027);
    db.CreateDefaults();
    db.Classes.push_back(Class("TCH_WALL", "TDbWall", 500));
    db.Classes.push_back(Class("TCH_OPENING", "TDbOpening", 501));
    BlockRecord* doorBlock = db.CreateBlockRecord("$TCHSYS$DOOR2D");
    auto leaf = std::make_unique<Line>(); leaf->StartPoint = {0.45, 0, 0}; leaf->EndPoint = {0.45, 0.975, 0};
    db.AddEntity(doorBlock, std::move(leaf));
    const Handle wallHandle = db.AddEntity(db.ModelSpace(), Wall(1))->ObjectHandle;
    OpeningSpec spec;
    spec.Position = {3000, 2000, 0}; spec.Angle = 0; spec.Mirror = 3;
    spec.Block2D = doorBlock->ObjectHandle; spec.Block3D = 0; spec.Wall = wallHandle;
    db.AddEntity(db.ModelSpace(), Opening(spec));
    db.Classes.push_back(Class("TCH_COLUMN", "TDbColumn", 502));
    db.AddEntity(db.ModelSpace(), Column());
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    Check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 3, "wall, door and column all appear in the scene");

    int wallPieces = 0, gapOk = 0, threshold = 0, caps = 0;
    const MiniCAD::InsertEntity* doorInsert = nullptr;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
        if (!insert || !insert->GetBlock()) return;
        for (const auto& e : insert->GetBlock()->GetEntities())
        {
            if (const auto* line = dynamic_cast<const MiniCAD::LineEntity*>(e.get()))
            {
                const auto& g = line->GetLine();
                if (insert->GetBlock()->GetName().starts_with("*U_TCH_OPENING_"))
                {
                    // 门扇在 -Y 一侧（镜像 Y），门槛线在 +Y 墙面（y = 2120），横跨门洞 2550..3450
                    threshold += Near(g.Start.y, 2120) && Near(g.End.y, 2120) && Near(std::min(g.Start.x, g.End.x), 2550) &&
                                 Near(std::max(g.Start.x, g.End.x), 3450);
                    continue;
                }
                if (Near(g.Start.x, g.End.x))
                {
                    // 门洞两侧的墙端封口：x = 2550 / 3450，横穿墙厚 1880..2120
                    caps += (Near(g.Start.x, 2550) || Near(g.Start.x, 3450)) && Near(std::min(g.Start.y, g.End.y), 1880) &&
                            Near(std::max(g.Start.y, g.End.y), 2120);
                    continue;
                }
                ++wallPieces;
                const double lo = std::min(g.Start.x, g.End.x), hi = std::max(g.Start.x, g.End.x);
                gapOk += (Near(hi, 2550) && Near(lo, 1000)) || (Near(lo, 3450) && Near(hi, 5000));
            }
            else if (const auto* nestedInsert = dynamic_cast<const MiniCAD::InsertEntity*>(e.get()))
                doorInsert = nestedInsert;
        }
    });
    Check(wallPieces == 4 && gapOk == 4, "both wall sides are cut over the door width (centre 3000, width 900)");
    Check(caps == 2, "the wall ends on both sides of the door are closed across the wall thickness");
    Check(doorInsert && Near(doorInsert->GetPosition().x, 3000) && Near(doorInsert->GetPosition().y, 2000) &&
          Near(doorInsert->GetScale().x, 900) && Near(doorInsert->GetScale().y, -900) && Near(doorInsert->GetRotation(), 0),
          "door 2D block is inserted at the axis point with scale (width, -width)");
    Check(threshold == 1, "door threshold line spans the opening on the wall face opposite the leaf");
    int columnOutlines = 0;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        if (const auto* pl = dynamic_cast<const MiniCAD::PolylineEntity*>(&obj))
            columnOutlines += pl->GetPolyline().Points.size() == 5 && Near(pl->GetPolyline().Points.front().x, pl->GetPolyline().Points.back().x);
    });
    Check(columnOutlines == 1, "column is displayed as one closed outline polyline");

    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf})
    {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        Check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 3,
              "displayed wall, door and column survive plain export and reimport");
    }

    // ── 门槛线：门窗标志 0x200 在门扇一侧、0x400 在门扇对面，都没有时不画（tianzheng.dwg 的门及其变体在天正 2014 里核对）──
    Check(ThresholdLines(0).empty(), "a door without door-line flags has no threshold line");
    Check(ThresholdLines(0x200) == std::vector<double>{1880}, "flag 0x200 puts the threshold line on the leaf side");
    Check(ThresholdLines(0x400) == std::vector<double>{2120}, "flag 0x400 puts the threshold line opposite the leaf");
    Check(ThresholdLines(0x600) == (std::vector<double>{1880, 2120}), "both flags draw threshold lines on both wall faces");


    // ── 图纸设置（TCH_DBCONFIG）──
    MiniCAD::Tch::TchConfig config;
    Check(MiniCAD::Tch::DecodeConfig(*Config(true, 100), config) && config.Fill && Near(config.DetailScale, 100) &&
          config.Standard[6] == "SOLID" && config.Detail[4] == "普通砖" && config.Detail[0].empty(),
          "config decodes the fill switch and detail scale; pattern names keep the defaults without a string stream");
    // 文字流：轻质隔墙、填充墙、砖、石、钢筋混凝土、钢的标准图案，同序的详图图案，2 个其他字符串，填充墙1、填充墙2 的标准与详图图案
    const std::vector<std::string> names{"", "", "", "", "SOLID", "", "卵石砌体", "加气混凝土", "普通砖", "天然石材", "钢筋混凝土", "金属",
                                         "", "TArch", "", "多孔材料", "", "空心砖"};
    MiniCAD::Tch::TchConfig custom;
    Check(MiniCAD::Tch::DecodeConfig(*Config(false, 50, names), custom) && !custom.Fill && Near(custom.DetailScale, 50) &&
          custom.Detail[0] == "卵石砌体" && custom.Detail[1] == "加气混凝土" && custom.Detail[2] == "多孔材料" &&
          custom.Detail[3] == "空心砖" && custom.Detail[6] == "钢筋混凝土" && custom.Standard[6] == "SOLID",
          "pattern names are read from the string stream in material order (8#楼-A平面图 layout)");
    Check(MiniCAD::Tch::MaterialHatch(custom, 10, true) == "卵石砌体" && MiniCAD::Tch::MaterialHatch(custom, 60, false) == "SOLID" &&
          MiniCAD::Tch::MaterialHatch(custom, 40, false).empty() && MiniCAD::Tch::MaterialHatch(custom, 20, true).empty(),
          "material codes pick their standard or detail pattern; glass curtain walls are never filled");

    // ── 墙柱填充：钢筋混凝土 1:100 实心，砖 1:50 详图“普通砖”（图案比例 50），砖 1:100 不填；关掉“填充”时都不填 ──
    for (const bool fill : {true, false})
    {
        CadDatabase fdb;
        fdb.SetVersion(CadVersion::AC1027);
        fdb.CreateDefaults();
        fdb.Classes.push_back(Class("TCH_WALL", "TDbWall", 500));
        fdb.Classes.push_back(Class("TCH_COLUMN", "TDbColumn", 502));
        DxfClass configClass = Class("TCH_DBCONFIG", "TDbConfig", 503);
        configClass.IsAnEntity = false; configClass.ItemClassId = 0x1f3;
        fdb.Classes.push_back(configClass);
        auto configObject = Config(fill);
        configObject->OwnerHandle = fdb.RootDictionary()->ObjectHandle;   // 写出时只写根字典之下的对象
        fdb.AddObject(std::move(configObject));
        fdb.AddEntity(fdb.ModelSpace(), Wall(0, 60, 100));
        fdb.AddEntity(fdb.ModelSpace(), Wall(0, 40, 50));
        fdb.AddEntity(fdb.ModelSpace(), Wall(0, 40, 100));
        fdb.AddEntity(fdb.ModelSpace(), Column());
        DwgWriteOptions fo; fo.Version = CadVersion::AC1027;
        MiniCAD::Scene fs;
        const bool imported = MiniCAD::ImportCad(WriteDwg(fdb, fo), fs);
        int solid = 0, brick = 0, other = 0, solidQuads = 0;
        fs.ForEachObject([&](const MiniCAD::Object& obj)
        {
            const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
            if (!insert || !insert->GetBlock()) return;
            for (const auto& e : insert->GetBlock()->GetEntities())
                if (const auto* h = dynamic_cast<const MiniCAD::HatchEntity*>(e.get()))
                {
                    const auto& pattern = h->GetPattern();
                    if (pattern.Solid)
                    {
                        ++solid;
                        // 墙体四边形：两侧墙线 y = 2120 / 1880，x 1000..5000
                        const auto pts = h->GetLoops().front().Tessellate();
                        int corners = 0;
                        for (const auto& p : pts)
                            corners += (Near(p.x, 1000) || Near(p.x, 5000)) && (Near(p.y, 1880) || Near(p.y, 2120));
                        solidQuads += corners >= 4;
                    }
                    else if (pattern.Name == "普通砖" && Near(h->GetScale(), 50) && !pattern.Families.empty()) ++brick;
                    else ++other;
                }
        });
        if (fill)
            Check(imported && solid == 2 && solidQuads == 1 && brick == 1 && other == 0,
                  "with fill on: the concrete wall body and column are solid, the 1:50 brick wall uses the detail brick pattern");
        else
            Check(imported && solid + brick + other == 0, "with fill off no wall or column is hatched");
    }
    return g_failed;
}
