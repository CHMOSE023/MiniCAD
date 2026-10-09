// 另存为与原图相同的格式、版本时以原图为底：未修改的天正对象与跳过的实体原样写回，改过的实体按场景重写
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Database/UnknownObjects.h"
#include "Document/Document.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace MiniDWG;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    // 可显示的天正直墙（布局同 TianzhengWallTests）
    std::unique_ptr<UnknownEntity> Wall()
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_WALL";
        e->Raw.Dwg.Version = CadVersion::AC1027;
        DwgBitWriter g(CadVersion::AC1027, Codec::CodePage::Gbk);
        g.Write3RawDouble({1000, 2000, 0}); g.Write3RawDouble({5000, 2000, 0});
        g.WriteRawDouble(0); g.WriteRawLong(0);
        g.WriteRawDouble(120); g.WriteRawDouble(120); g.WriteRawDouble(3000);
        auto protectedData = g.Take();
        for (auto& byte : protectedData) byte ^= 0xeb;
        DwgBitWriter r(CadVersion::AC1027, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitShort(0); r.WriteByte(3);
        r.Write2Bits(0); r.WriteRawShort(512); r.WriteByte(8); r.WriteByte(0xeb);
        r.WriteBytes(protectedData);
        r.WriteByte(0); r.WriteByte(0x8a); r.WriteByte(0); r.Write2Bits(0);
        r.WriteBitDouble(80.08);
        for (double join : {10.0, 20.0, -10.0, -20.0}) r.WriteBitDouble(join);
        r.WriteByte(0); r.Write3Bits(2);
        e->Raw.Dwg.MainBits = r.PositionInBits(); e->Raw.Dwg.Main = r.Take();
        return e;
    }

    // 无法显示的天正对象（私有数据未知、没有代理图形）：导入时跳过
    std::unique_ptr<UnknownEntity> Column()
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_COLUMN";
        e->Raw.Dwg.Version = CadVersion::AC1027;
        e->Raw.Dwg.Main = {0x12, 0x34, 0x56, 0x78, 0x9a};
        e->Raw.Dwg.MainBits = 37;
        return e;
    }

    DxfClass Class(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
    }

    struct Fixture
    {
        std::vector<std::uint8_t> Bytes;
        Handle WallHandle = kNullHandle, ColumnHandle = kNullHandle, LineHandle = kNullHandle;
    };

    Fixture MakeDrawing()
    {
        CadDatabase db;
        db.SetVersion(CadVersion::AC1027);
        db.CreateDefaults();
        db.Classes.push_back(Class("TCH_WALL", "TDbWall", 500));
        db.Classes.push_back(Class("TCH_COLUMN", "TDbColumn", 501));

        auto layer = std::make_unique<Layer>();
        layer->Name = "WALL";
        layer->Color = Color(std::int16_t(3));
        layer->PlotFlag = false;    // MiniCAD 不保存的属性
        Layer* wallLayer = db.AddTableEntry(db.Layers(), std::move(layer));

        Fixture f;
        auto wall = Wall();
        wall->LayerHandle = wallLayer->ObjectHandle;
        f.WallHandle = db.AddEntity(db.ModelSpace(), std::move(wall))->ObjectHandle;
        f.ColumnHandle = db.AddEntity(db.ModelSpace(), Column())->ObjectHandle;
        auto line = std::make_unique<Line>();
        line->StartPoint = {0, 0, 0};
        line->EndPoint = {100, 0, 0};
        f.LineHandle = db.AddEntity(db.ModelSpace(), std::move(line))->ObjectHandle;

        DwgWriteOptions options;
        options.Version = CadVersion::AC1027;
        f.Bytes = WriteDwg(db, options);
        return f;
    }

    bool SameRaw(const CadDatabase& a, const CadDatabase& b, Handle h)
    {
        const RawObjectData* ra = a.Find(h) ? RawDataOf(*a.Find(h)) : nullptr;
        const RawObjectData* rb = b.Find(h) ? RawDataOf(*b.Find(h)) : nullptr;
        return ra && rb && ra->DxfName == rb->DxfName && ra->Dwg.Main == rb->Dwg.Main && ra->Dwg.MainBits == rb->Dwg.MainBits
            && ra->Dwg.Handles == rb->Dwg.Handles;
    }

    int CountTch(const CadDatabase& db)
    {
        int n = 0;
        for (Handle h : db.ModelSpace()->Entities)
            if (const CadObject* o = db.Find(h); o && RawDataOf(*o) && RawDataOf(*o)->DxfName.starts_with("TCH_"))
                ++n;
        return n;
    }

    template <class T>
    T* FindScene(MiniCAD::Scene& scene)
    {
        T* found = nullptr;
        scene.ForEachObject([&](const MiniCAD::Object& o)
        {
            if (!found)
                if (auto* t = dynamic_cast<const T*>(&o))
                    found = const_cast<T*>(t);
        });
        return found;
    }

    struct Opened
    {
        MiniCAD::Scene Scene;
        std::shared_ptr<const MiniCAD::CadSource> Source;
    };

    std::unique_ptr<Opened> Open(const Fixture& f)
    {
        auto o = std::make_unique<Opened>();
        MiniCAD::CadSaveVersion version{};
        if (!MiniCAD::ImportCad(f.Bytes, o->Scene, nullptr, &version, &o->Source) || version != MiniCAD::CadSaveVersion::R2013)
            return nullptr;
        return o;
    }

    std::unique_ptr<CadDatabase> Save(const Opened& o, MiniCAD::CadSaveVersion version = MiniCAD::CadSaveVersion::R2013)
    {
        const auto bytes = MiniCAD::ExportCad(o.Scene, MiniCAD::CadFileKind::Dwg, version, nullptr, o.Source.get());
        return bytes.empty() ? nullptr : ReadDwg(bytes, {});
    }
}

int main()
{
    const Fixture f = MakeDrawing();
    const auto original = ReadDwg(f.Bytes, {});
    Check(!f.Bytes.empty() && original != nullptr, "fixture drawing is written and readable");
    if (!original)
        return 1;

    // 1. 不修改直接另存
    {
        auto o = Open(f);
        Check(o != nullptr && o->Scene.EntityCount() == 2 && MiniCAD::CadSourceTchCount(o->Source.get()) == 2,
              "import shows wall and line, records both Tianzheng objects");
        const auto saved = o ? Save(*o) : nullptr;
        Check(saved && SameRaw(*original, *saved, f.WallHandle), "unmodified displayed TCH_WALL is written back bit for bit");
        Check(saved && SameRaw(*original, *saved, f.ColumnHandle), "undisplayable TCH_COLUMN is preserved");
        Check(saved && saved->FindAs<Line>(f.LineHandle) != nullptr, "unmodified line keeps its handle");
        const auto* layer = saved ? saved->FindTableEntry<Layer>(saved->Layers(), "WALL") : nullptr;
        Check(layer && !layer->Color.IsTrueColor() && layer->Color.Index() == 3 && !layer->PlotFlag,
              "unchanged layer keeps color index and properties MiniCAD does not model");
        bool tchBlock = false;
        if (saved)
            for (const auto& [h, obj] : saved->Objects())
                if (const auto* rec = dynamic_cast<const BlockRecord*>(obj.get()); rec && rec->Name.starts_with("*U_TCH_"))
                    tchBlock = true;
        Check(!tchBlock, "display block generated for the wall is not written when the wall is preserved");
    }

    // 2. 修改直线、删除墙、新增直线
    {
        auto o = Open(f);
        auto* line = o ? FindScene<MiniCAD::LineEntity>(o->Scene) : nullptr;
        auto* wall = o ? FindScene<MiniCAD::InsertEntity>(o->Scene) : nullptr;
        if (line && wall)
        {
            line->SetLine(MiniCAD::Line({0, 0, 0}, {0, 50, 0}));
            o->Scene.RemoveEntity(wall->GetID());
            o->Scene.AddEntity(std::make_unique<MiniCAD::LineEntity>(o->Scene.NextObjectID(),
                MiniCAD::Math::Point3{10, 10, 0}, MiniCAD::Math::Point3{20, 20, 0}));
        }
        const auto saved = o ? Save(*o) : nullptr;
        const auto* edited = saved ? saved->FindAs<Line>(f.LineHandle) : nullptr;
        Check(edited && edited->EndPoint.Y == 50 && edited->EndPoint.X == 0, "edited line is rewritten in place with its original handle");
        Check(saved && saved->Find(f.WallHandle) == nullptr, "deleted wall is removed");
        Check(saved && SameRaw(*original, *saved, f.ColumnHandle), "skipped column survives unrelated edits");
        int lines = 0;
        if (saved)
            for (Handle h : saved->ModelSpace()->Entities)
                lines += saved->FindAs<Line>(h) != nullptr;
        Check(lines == 2, "new line is appended to model space");
    }

    // 3. 移动墙：原对象的位流不再对应场景，写为普通几何
    {
        auto o = Open(f);
        auto* wall = o ? FindScene<MiniCAD::InsertEntity>(o->Scene) : nullptr;
        if (wall)
            wall->SetPosition({500, 0, 0});
        const auto saved = o ? Save(*o) : nullptr;
        Check(saved && saved->Find(f.WallHandle) == nullptr && CountTch(*saved) == 1,
              "moved wall no longer writes the stale TCH payload");
        MiniCAD::Scene restored;
        const auto bytes = o ? MiniCAD::ExportCad(o->Scene, MiniCAD::CadFileKind::Dwg, MiniCAD::CadSaveVersion::R2013, nullptr, o->Source.get())
                             : std::vector<std::uint8_t>{};
        Check(MiniCAD::ImportCad(bytes, restored) && restored.EntityCount() == 2, "moved wall reopens as plain geometry");
    }

    // 4. 换版本：自定义对象不能写回，重新生成整张图纸
    {
        auto o = Open(f);
        const auto saved = o ? Save(*o, MiniCAD::CadSaveVersion::R2018) : nullptr;
        Check(saved && CountTch(*saved) == 0 && saved->ModelSpace()->Entities.size() == 2,
              "saving as another version writes only the displayed geometry");
    }

    // 5. 经 Document 打开再保存
    {
        const auto path = std::filesystem::temp_directory_path() / "MiniCADPreserve.dwg";
        { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(f.Bytes.data()), f.Bytes.size()); }
        const auto u8 = path.u8string();
        const std::string p(u8.begin(), u8.end());
        MiniCAD::Document doc;
        const bool ok = doc.LoadFromFile(p) && doc.SaveToFile(p);
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
        in.close();
        std::filesystem::remove(path);
        const auto saved = ReadDwg(bytes, {});
        Check(ok && saved && SameRaw(*original, *saved, f.WallHandle) && SameRaw(*original, *saved, f.ColumnHandle),
              "Document open and save keeps Tianzheng objects");
    }

    return g_failed;
}
