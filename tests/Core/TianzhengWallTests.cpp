#include "Import/TchWall.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include <cstdio>
#include <limits>

using namespace MiniDWG;
namespace {
    std::unique_ptr<UnknownEntity> Fixture(double radius = 0, double width = 120, double x = 1000, bool compressed = false,
                                         std::uint8_t protectionKey = 0xeb, std::int16_t openings = 0)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_WALL";
        e->Raw.Dwg.Version = CadVersion::AC1027;
        DwgBitWriter g(CadVersion::AC1027, Codec::CodePage::Gbk);
        g.Write3RawDouble({x, 2000, 0}); g.Write3RawDouble({5000, 2000, 0});
        g.WriteRawDouble(radius); g.WriteRawLong(0);
        g.WriteRawDouble(width); g.WriteRawDouble(120); g.WriteRawDouble(3000);
        auto protectedData = g.Take();
        for (auto& byte : protectedData) byte ^= protectionKey;
        DwgBitWriter r(CadVersion::AC1027, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(compressed ? 1 : 100); r.WriteBitShort(0); r.WriteByte(3);
        r.Write2Bits(0); r.WriteRawShort(512); r.WriteByte(8); r.WriteByte(protectionKey);
        r.WriteBytes(protectedData);
        r.WriteByte(0); r.WriteBitShort(openings); r.Write2Bits(0); r.Write2Bits(2); r.Write2Bits(2);
        r.WriteByte(0); r.Write2Bits(0);
        r.WriteBitDouble(compressed ? 0 : 80.08);
        for (double join : {10.0, 20.0, -10.0, -20.0}) r.WriteBitDouble(compressed ? 0 : join);
        r.WriteByte(0); r.Write3Bits(2);
        e->Raw.Dwg.MainBits = r.PositionInBits(); e->Raw.Dwg.Main = r.Take();
        return e;
    }
}
int main()
{
    int failed = 0;
    auto check = [&](bool ok, const char* label) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); failed += !ok; };
    MiniCAD::Tch::TchWall wall;
    auto good = Fixture();
    const auto original = good->Raw.Dwg.Main;
    check(MiniCAD::Tch::DecodeWall(*good, wall) && wall.Start.X == 1000 && wall.End.X == 5000 &&
        wall.LeftWidth == 120 && wall.RightWidth == 120 && wall.Joins[2] == -10,
        "synthetic protected wall decodes endpoints, widths and connection fields");
    check(good->Raw.Dwg.Main == original, "decoding preserves proprietary payload");
    for (int key = 0; key < 256; ++key) {
        const auto keyed = Fixture(0, 120, 1000, false, static_cast<std::uint8_t>(key));
        if (!MiniCAD::Tch::DecodeWall(*keyed, wall) || wall.Start.X != 1000 || wall.End.X != 5000) ++failed;
    }
    check(failed == 0, "protection key is read from stream for every byte value, including resaved drawing key 3B");
    check(MiniCAD::Tch::DecodeWall(*Fixture(0, 120, 1000, true), wall),
        "standard BD compression changes bit positions without breaking framing");
    for (std::size_t size = 0; size < original.size(); ++size) {
        auto truncated = Fixture(); truncated->Raw.Dwg.Main.resize(size);
        if (MiniCAD::Tch::DecodeWall(*truncated, wall)) ++failed;
    }
    check(failed == 0, "all truncated buffers rejected");
    auto wrong = Fixture(); wrong->Raw.Dwg.Main[0] = 6;
    check(!MiniCAD::Tch::DecodeWall(*wrong, wall), "unknown schema rejected");
    wrong = Fixture(); wrong->Raw.Dwg.Version = CadVersion::AC1032;
    check(!MiniCAD::Tch::DecodeWall(*wrong, wall), "unverified DWG version rejected");
    wrong = Fixture(); wrong->Raw.DxfName = "TCH_COLUMN";
    check(!MiniCAD::Tch::DecodeWall(*wrong, wall), "other Tianzheng entities rejected");
    wrong = Fixture(); wrong->Raw.Dwg.Main.back() ^= 0x10;
    check(!MiniCAD::Tch::DecodeWall(*wrong, wall), "inconsistent tail rejected");
    check(MiniCAD::Tch::DecodeWall(*Fixture(), wall) && wall.OpeningCount == 0 && Fixture()->Raw.Dwg.MainBits == 1157,
        "wall without openings keeps the 1157-bit layout");
    check(MiniCAD::Tch::DecodeWall(*Fixture(0, 120, 1000, false, 0xeb, 1), wall) && wall.OpeningCount == 1 &&
        Fixture(0, 120, 1000, false, 0xeb, 1)->Raw.Dwg.MainBits == 1165 && wall.Start.X == 1000,
        "wall hosting one opening (1165 bits) decodes with its opening count");
    check(MiniCAD::Tch::DecodeWall(*Fixture(0, 120, 1000, false, 0xeb, 300), wall) && wall.OpeningCount == 300,
        "large opening count uses the raw-short form");
    check(!MiniCAD::Tch::DecodeWall(*Fixture(100), wall), "curved wall rejected");
    check(!MiniCAD::Tch::DecodeWall(*Fixture(0, -1), wall), "negative width rejected");
    check(!MiniCAD::Tch::DecodeWall(*Fixture(0, 120, 5000), wall), "zero-length wall rejected");
    check(!MiniCAD::Tch::DecodeWall(*Fixture(0, 120, std::numeric_limits<double>::quiet_NaN()), wall), "nonfinite coordinate rejected");
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027); db.CreateDefaults();
    DxfClass cls;
    cls.DxfName = "TCH_WALL"; cls.CppClassName = "TDbWall"; cls.ApplicationName = "TCH_KERNAL";
    cls.ClassNumber = 500; cls.IsAnEntity = true; cls.ItemClassId = 0x1f2;
    db.Classes.push_back(cls);
    db.AddEntity(db.ModelSpace(), Fixture());
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 1,
        "full DWG reader and scene importer recover one logical wall");
    int edges = 0;
    scene.ForEachObject([&](const MiniCAD::Object& object) {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
        if (!insert || !insert->GetBlock()) return;
        for (const auto& entity : insert->GetBlock()->GetEntities()) {
            const auto* line = dynamic_cast<const MiniCAD::LineEntity*>(entity.get());
            if (!line) continue;
            const auto& g = line->GetLine();
            if (g.Start.x == 1010 && g.Start.y == 2120 && g.End.x == 4980 && g.End.y == 2120) ++edges;
            if (g.Start.x == 990 && g.Start.y == 1880 && g.End.x == 5020 && g.End.y == 1880) ++edges;
        }
        check(insert->GetBlock()->EntityCount() == 2, "connected wall contains no invented end caps");
    });
    check(edges == 2, "imported side edges apply all four signed join offsets");
    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf}) {
        const auto saved = MiniCAD::ExportCad(scene, kind);
        MiniCAD::Scene restored;
        check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 1,
            "converted wall geometry survives export and reimport");
    }
    return failed ? 1 : 0;
}
