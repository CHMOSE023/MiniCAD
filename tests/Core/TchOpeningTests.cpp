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
#include <cmath>
#include <cstdio>

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
        r.WriteBitShort(1024);
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

    // 带 openings 个门窗的直墙（布局同 TianzhengWallTests），起点 (1000,2000)、终点 (5000,2000)、两侧各 120、接头 0
    std::unique_ptr<UnknownEntity> Wall(int openings)
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
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitShort(0); r.WriteByte(3);
        r.Write2Bits(0); r.WriteRawShort(512); r.WriteByte(8); r.WriteByte(0x3b);
        r.WriteBytes(protectedData);
        r.WriteByte(0); r.WriteBitShort(static_cast<std::int16_t>(openings)); r.Write2Bits(0); r.Write2Bits(2); r.Write2Bits(2);
        r.WriteByte(0); r.Write2Bits(0);
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
    Check(!MiniCAD::Tch::DecodeOpening(*Opening(hole), o), "opening kinds other than door and window are not drawn");
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

    int wallPieces = 0, gapOk = 0, threshold = 0;
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
                ++wallPieces;
                const double lo = std::min(g.Start.x, g.End.x), hi = std::max(g.Start.x, g.End.x);
                gapOk += (Near(hi, 2550) && Near(lo, 1000)) || (Near(lo, 3450) && Near(hi, 5000));
            }
            else if (const auto* nestedInsert = dynamic_cast<const MiniCAD::InsertEntity*>(e.get()))
                doorInsert = nestedInsert;
        }
    });
    Check(wallPieces == 4 && gapOk == 4, "both wall sides are cut over the door width (centre 3000, width 900)");
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
    return g_failed;
}
