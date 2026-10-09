#include "Import/TchBlockInsert.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Dwg/Read/DwgReader.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include <cstdio>
#include <limits>

namespace {
    using namespace MiniDWG;
    std::unique_ptr<UnknownEntity> Fixture(Handle block, int scaleKind = 0, int version = 4, int mode = 0)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_BLOCK_INSERT";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.Write3BitDouble({100, 200, 0}); r.Write2Bits(static_cast<std::uint8_t>(scaleKind));
        if (scaleKind == 0) {
            r.WriteRawDouble(-2); r.WriteBitDoubleWithDefault(-2, 3); r.WriteBitDoubleWithDefault(-2, 1);
        } else if (scaleKind == 1) {
            r.WriteBitDoubleWithDefault(1, 3); r.WriteBitDoubleWithDefault(1, 1);
        } else if (scaleKind == 2) r.WriteRawDouble(2);
        r.WriteBitDouble(0.5); r.Write3BitDouble(XYZ::AxisZ()); r.WriteBit(false);
        r.WriteByte(static_cast<std::uint8_t>(version)); r.WriteByte(static_cast<std::uint8_t>(mode));
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        r.WriteHandle(DwgRef::HardPointer, block);
        r.WriteHandle(DwgRef::SoftPointer, kNullHandle);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        raw.References.push_back(block);
        return e;
    }
}

int RunTchBlockInsertTests()
{
    using namespace MiniDWG;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; };
    MiniCAD::Tch::TchBlockInsert insert;
    for (int kind = 0; kind < 4; ++kind) {
        auto e = Fixture(0x123, kind);
        check(MiniCAD::Tch::DecodeBlockInsert(*e, insert) && insert.BlockHandle == 0x123 &&
            insert.Point.X == 100 && insert.Point.Y == 200 && insert.Rotation == 0.5 &&
            insert.XScale == (kind == 0 ? -2 : kind == 2 ? 2 : 1) &&
            insert.YScale == (kind < 2 ? 3 : kind == 2 ? 2 : 1), "INSERT prefix handles each scale encoding");
    }
    auto e = Fixture(0x123);
    for (std::size_t bits = 0; bits < e->Raw.Dwg.MainBits; ++bits) {
        const auto originalBits = e->Raw.Dwg.MainBits;
        e->Raw.Dwg.MainBits = bits;
        if (MiniCAD::Tch::DecodeBlockInsert(*e, insert)) ++failures;
        e->Raw.Dwg.MainBits = originalBits;
    }
    check(failures == 0, "every truncated prefix rejected at declared bit boundary");
    e->Raw.Dwg.Handles.clear();
    check(!MiniCAD::Tch::DecodeBlockInsert(*e, insert), "missing block handle rejected");
    e = Fixture(0x123); e->Raw.Dwg.Main.pop_back();
    check(!MiniCAD::Tch::DecodeBlockInsert(*e, insert), "truncated buffer rejected");
    check(!MiniCAD::Tch::DecodeBlockInsert(*Fixture(0x123, 0, 5), insert), "unknown custom version rejected");
    check(!MiniCAD::Tch::DecodeBlockInsert(*Fixture(0x123, 0, 4, 1), insert), "database-dependent mode rejected");

    CadDatabase db; db.SetVersion(CadVersion::AC1027); db.CreateDefaults();
    auto* block = db.CreateBlockRecord("TchTestSymbol");
    auto line = std::make_unique<Line>(); line->StartPoint = {0, 0, 0}; line->EndPoint = {10, 0, 0};
    db.AddEntity(block, std::move(line));
    DxfClass cls; cls.DxfName = "TCH_BLOCK_INSERT"; cls.CppClassName = "TDbBlockInsert";
    cls.ApplicationName = "TCH_KERNAL"; cls.ClassNumber = 500; cls.IsAnEntity = true; cls.ItemClassId = 0x1f2;
    db.Classes.push_back(cls);
    db.AddEntity(db.ModelSpace(), Fixture(block->ObjectHandle));
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    const auto decoded = ReadDwg(bytes);
    bool preserved = false;
    if (decoded) for (const auto& [handle, object] : decoded->Objects()) {
        const auto* unknown = dynamic_cast<const UnknownEntity*>(object.get());
        if (unknown && unknown->Raw.DxfName == "TCH_BLOCK_INSERT") {
            const auto expected = Fixture(block->ObjectHandle);
            preserved = unknown->Raw.Dwg.Main == expected->Raw.Dwg.Main &&
                unknown->Raw.Dwg.MainBits == expected->Raw.Dwg.MainBits;
        }
    }
    check(preserved, "MiniDWG retains custom class identity and proprietary payload");
    MiniCAD::Scene scene;
    check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 1,
        "TCH_BLOCK_INSERT full reader/import path resolves referenced block");
    int matches = 0;
    scene.ForEachObject([&](const MiniCAD::Object& object) {
        const auto* geometry = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
        if (geometry && geometry->GetBlock() && geometry->GetBlock()->EntityCount() == 1 &&
            geometry->GetScale().x == -2 && geometry->GetScale().y == 3 && geometry->GetRotation() == 0.5)
            ++matches;
    });
    check(matches == 1, "block contents, reflection, nonuniform scale and rotation retained");
    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf}) {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 1,
            "block geometry survives export/reimport");
    }
    return failures;
}

int RunTchProxyGraphicsTests();
int RunTchProxyImportTests()
{
    using namespace MiniDWG;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; };
    for (const char* name : {"TCH_COLUMN", "TCH_RECT", "TCH_TEXT"}) {
        for (bool unsupported : {false, true}) {
            CadDatabase db; db.SetVersion(CadVersion::AC1027); db.CreateDefaults();
            DxfClass cls; cls.DxfName = name; cls.CppClassName = "SyntheticTchProxy";
            cls.ApplicationName = "TCH_KERNAL"; cls.ClassNumber = 500;
            cls.IsAnEntity = true; cls.ItemClassId = 0x1f2; db.Classes.push_back(cls);
            auto e = std::make_unique<UnknownEntity>(); e->Raw.DxfName = name;
            e->Raw.Dwg.Version = CadVersion::AC1027;
            e->Raw.Dwg.MainBits = 8; e->Raw.Dwg.Main = {0xff};
            DwgBitWriter proxy(CadVersion::AC1027, Codec::CodePage::Gbk);
            proxy.WriteRawLong(0); proxy.WriteRawLong(0);
            proxy.WriteRawLong(64); proxy.WriteRawLong(2);
            for (double value : {100., 200., 0., 10., 0., 0., 1.}) proxy.WriteRawDouble(value);
            if (unsupported) { proxy.WriteRawLong(8); proxy.WriteRawLong(99); }
            const auto expectedProxy = proxy.Take(); e->ProxyGraphics = expectedProxy;
            db.AddEntity(db.ModelSpace(), std::move(e));
            DwgWriteOptions options; options.Version = CadVersion::AC1027;
            const auto bytes = WriteDwg(db, options);
            const auto decoded = ReadDwg(bytes);
            bool preserved = false;
            if (decoded) for (const auto& [handle, object] : decoded->Objects()) {
                const auto* unknown = dynamic_cast<const UnknownEntity*>(object.get());
                if (unknown && unknown->Raw.DxfName == name)
                    preserved = unknown->Raw.Dwg.Main == std::vector<std::uint8_t>{0xff} &&
                        unknown->ProxyGraphics == expectedProxy;
            }
            check(preserved, "opaque native data and proxy stream survive DWG reader/writer");
            MiniCAD::Scene scene;
            check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) &&
                scene.EntityCount() == (unsupported ? 0 : 1),
                unsupported ? "unknown proxy record rejects complete scene preview" :
                    "TCH class without native decoder imports proxy through full DWG path");
            if (unsupported) continue;
            int matches = 0;
            scene.ForEachObject([&](const MiniCAD::Object& object) {
                const auto* geometry = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
                if (geometry && geometry->GetBlock() && geometry->GetBlock()->EntityCount() == 1) ++matches;
            });
            check(matches == 1, "proxy primitive resolves inside generated scene block");
            for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf}) {
                MiniCAD::Scene restored;
                const auto saved = MiniCAD::ExportCad(scene, kind);
                check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 1,
                    "proxy preview geometry survives export/reimport");
            }
        }
    }
    return failures;
}
int main() { return RunTchBlockInsertTests() + RunTchProxyGraphicsTests() + RunTchProxyImportTests() ? 1 : 0; }
