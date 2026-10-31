#include "Import/TchWall.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>

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

    // 幕墙：墙的字段（两侧各 100）之后是 TDbCurtainWall::dwgInFields 的 RC 版本、RC、6 个 BD、2 个 RC；
    // 数值按 8#楼-A平面图 的幕墙（4 个 BD 非 0、1，这一段共 300 位）。flags 0x1810c 为玻璃幕墙，0x1c100 为普通
    std::unique_ptr<UnknownEntity> CurtainFixture(XYZ start, XYZ end, std::array<double, 4> joins, int material, std::uint32_t flags,
                                                  int curtainVersion = 2)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_CURTAIN_WALL";
        e->Raw.Dwg.Version = CadVersion::AC1024;
        DwgBitWriter g(CadVersion::AC1024, Codec::CodePage::Gbk);
        g.Write3RawDouble(start); g.Write3RawDouble(end);
        g.WriteRawDouble(0); g.WriteRawLong(static_cast<std::int32_t>(flags));
        g.WriteRawDouble(100); g.WriteRawDouble(100); g.WriteRawDouble(3000);
        auto protectedData = g.Take();
        for (auto& byte : protectedData) byte ^= 0xeb;
        DwgBitWriter r(CadVersion::AC1024, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitShort(0); r.WriteByte(3);
        r.Write2Bits(0); r.WriteRawShort(512); r.WriteByte(8); r.WriteByte(0xeb);
        r.WriteBytes(protectedData);
        r.WriteByte(0); r.WriteBitShort(0); r.WriteByte(static_cast<std::uint8_t>(material)); r.WriteByte(0);
        r.WriteBitDouble(80.08);
        for (double join : joins) r.WriteBitDouble(join);
        r.WriteByte(0); r.Write3Bits(2);
        r.WriteByte(static_cast<std::uint8_t>(curtainVersion)); r.WriteByte(1);
        for (double v : {3000.0, 1200.0, 0.0, 1.0, 50.0, 60.0}) r.WriteBitDouble(v);
        r.WriteByte(0); r.WriteByte(0);
        e->Raw.Dwg.MainBits = r.PositionInBits(); e->Raw.Dwg.Main = r.Take();
        return e;
    }

    DxfClass TchClass(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
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
    auto v2010 = Fixture(); v2010->Raw.Dwg.Version = CadVersion::AC1024;
    check(MiniCAD::Tch::DecodeWall(*v2010, wall) && wall.Start.X == 1000, "AC1024 (2010) drawing uses the same layout");
    check(MiniCAD::Tch::DecodeWall(*Fixture(0, 0), wall) && wall.LeftWidth == 0 && wall.RightWidth == 120,
        "wall with one zero-width side (axis on wall face) decodes");
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

    // ── 幕墙（8#楼-A平面图 外窗所在的墙）：玻璃幕墙 (0,0)→(0,5200) 起点两侧缩进 100（丁字接到墙上），
    // 普通幕墙 (0,5500)→(0,5200) 是转角处的填充墙段，起点斜接（一侧缩进 100、一侧伸出 100）──
    const auto plain = CurtainFixture({0, 5500, 0}, {0, 5200, 0}, {100, 0, -100, 0}, 30, 0x1c100);
    const auto glass = CurtainFixture({0, 0, 0}, {0, 5200, 0}, {100, 0, 100, 0}, 20, 0x1810c);
    check(plain->Raw.Dwg.MainBits == 1029 + 300, "synthetic curtain wall reproduces the 8#楼 layout (1029 wall bits + 300 curtain bits)");
    check(MiniCAD::Tch::DecodeCurtainWall(*plain, wall) && wall.Start.Y == 5500 && wall.Joins[0] == 100 && wall.Joins[2] == -100 &&
        wall.Material == 30 && wall.Flags == 0x1c100 && !MiniCAD::Tch::DecodeWall(*plain, wall),
        "curtain wall decodes the wall fields (joins, material, flags); DecodeWall still rejects curtain walls");
    check(MiniCAD::Tch::DecodeCurtainWall(*glass, wall) && (wall.Flags & 0x4) != 0 && wall.Material == 20 &&
        !MiniCAD::Tch::DecodeCurtainWall(*good, wall), "glass curtain flag is kept; ordinary walls are not decoded as curtain walls");
    int curtainAccepted = 0;
    for (std::size_t size = 0; size < plain->Raw.Dwg.Main.size(); ++size) {
        auto truncated = CurtainFixture({0, 5500, 0}, {0, 5200, 0}, {100, 0, -100, 0}, 30, 0x1c100);
        truncated->Raw.Dwg.Main.resize(size);
        curtainAccepted += MiniCAD::Tch::DecodeCurtainWall(*truncated, wall);
    }
    auto longer = CurtainFixture({0, 5500, 0}, {0, 5200, 0}, {100, 0, -100, 0}, 30, 0x1c100);
    longer->Raw.Dwg.MainBits += 1;
    check(curtainAccepted == 0 && !MiniCAD::Tch::DecodeCurtainWall(*longer, wall) &&
        !MiniCAD::Tch::DecodeCurtainWall(*CurtainFixture({0, 5500, 0}, {0, 5200, 0}, {100, 0, -100, 0}, 30, 0x1c100, 3), wall),
        "truncated, overlong and unverified-version curtain payloads are rejected");

    CadDatabase cdb;
    cdb.SetVersion(CadVersion::AC1024); cdb.CreateDefaults();
    cdb.Classes.push_back(TchClass("TCH_CURTAIN_WALL", "TDbCurtainWall", 500));
    const Handle glassHandle = cdb.AddEntity(cdb.ModelSpace(), CurtainFixture({0, 0, 0}, {0, 5200, 0}, {100, 0, 100, 0}, 20, 0x1810c))->ObjectHandle;
    const Handle plainHandle = cdb.AddEntity(cdb.ModelSpace(), CurtainFixture({0, 5500, 0}, {0, 5200, 0}, {100, 0, -100, 0}, 30, 0x1c100))->ObjectHandle;
    // 起点与另一段玻璃幕墙斜接（左侧缩进 100、右侧伸出 100）
    const Handle miterHandle = cdb.AddEntity(cdb.ModelSpace(), CurtainFixture({2000, 0, 0}, {2000, 5200, 0}, {100, 0, -100, 0}, 20, 0x1810c))->ObjectHandle;
    DwgWriteOptions coptions; coptions.Version = CadVersion::AC1024;
    MiniCAD::Scene cscene;
    check(MiniCAD::ImportCad(WriteDwg(cdb, coptions), cscene) && cscene.EntityCount() == 3, "all curtain walls are imported");
    // 各幕墙显示块里的线段（端点按 x、y 排序，坐标取整到 0.1）
    using Seg = std::array<double, 4>;
    std::map<Handle, std::vector<Seg>> segs;
    cscene.ForEachObject([&](const MiniCAD::Object& object) {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
        if (!insert || !insert->GetBlock() || !insert->GetBlock()->GetName().starts_with("*U_TCH_")) return;
        const Handle h = std::stoull(insert->GetBlock()->GetName().substr(7));
        for (const auto& entity : insert->GetBlock()->GetEntities())
            if (const auto* line = dynamic_cast<const MiniCAD::LineEntity*>(entity.get())) {
                auto r = [](double v) { return std::round(v * 10) / 10; };
                std::array<double, 2> a{r(line->GetLine().Start.x), r(line->GetLine().Start.y)}, b{r(line->GetLine().End.x), r(line->GetLine().End.y)};
                if (b < a) std::swap(a, b);
                segs[h].push_back({a[0], a[1], b[0], b[1]});
            }
        std::sort(segs[h].begin(), segs[h].end());
    });
    // 玻璃幕墙：沿墙厚三等分的 4 条线从 y = 100（缩进后的起点）画到 5200，两端封口
    const std::vector<Seg> glassExpected{{-100, 100, -100, 5200}, {-100, 100, 100, 100}, {-100, 5200, 100, 5200},
                                         {-33.3, 100, -33.3, 5200}, {33.3, 100, 33.3, 5200}, {100, 100, 100, 5200}};
    check(segs[glassHandle] == glassExpected, "glass curtain wall lines start at the join-trimmed end (not 100 into the host wall)");
    // 普通幕墙按墙画：左侧（x = 100）自 5400、右侧（x = -100）自 5600 起，与相邻墙斜接；墙与墙相接处不封口
    const std::vector<Seg> plainExpected{{-100, 5200, -100, 5600}, {100, 5200, 100, 5400}};
    check(segs[plainHandle] == plainExpected, "plain curtain wall is drawn like a wall: mitered by its joins, no end caps");
    // 斜接的一端：各线的起点在斜接线上（连接量按偏移插值），不画端线；方头的终点照常封口
    const std::vector<Seg> miterExpected{{1900, 100, 1900, 5200}, {1900, 5200, 2100, 5200}, {1966.7, 33.3, 1966.7, 5200},
                                         {2033.3, -33.3, 2033.3, 5200}, {2100, -100, 2100, 5200}};
    check(segs[miterHandle] == miterExpected, "glass curtain lines meet the miter line at a mitered end, which gets no end line");
    return failed ? 1 : 0;
}
