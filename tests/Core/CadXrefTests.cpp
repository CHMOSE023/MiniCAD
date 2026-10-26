// 外部参照（XREF）导入测试：宿主 tests/Data/Xref.dwg 以相对路径参照 Xref-A.dwg 与 Xref-B.dwg
// （用户图纸，不在版本库里；文件不存在时跳过）。核对：
//   · 外部参照块带标志与路径，内容从外部文件读入
//   · 依赖图层「参照名|图层名」复用宿主里已有的，不重复
//   · 外部参照里的天正坐标标注按实线组装
//   · 重新生成导出时外部参照仍是外部参照（不绑定），依赖块不写出
// 另有不依赖用户图纸的部分（RunXrefResolveTests，在临时目录里现做宿主与参照图纸）：
//   · 原机器上的绝对路径按后缀在宿主的上级目录里找到
//   · 找不到的参照在插入点显示保存的路径，范围取插入点
//   · ReloadUnresolvedXrefs 在另外指定的目录里递归找到并读入
//   · 卸载的参照不读、不显示路径，卸载标志随 DWG 往返
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Document/Document.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    int CountLayersNamed(const Scene& scene, const std::string& name)
    {
        int n = 0;
        const LayerManager& layers = scene.GetLayerManager();
        for (LayerID id : layers.GetAllLayerIDs())
            if (const Layer* l = layers.GetLayer(id); l && l->GetName() == name)
                ++n;
        return n;
    }

    bool AllLinesContinuous(const BlockEntity& blk)
    {
        bool any = false, ok = true;
        for (const auto& e : blk.GetEntities())
            if (e && e->IsKindOf<LineEntity>())
            {
                any = true;
                ok = ok && e->GetAttr().LineType == LineTypeTable::ContinuousID;
            }
        return any && ok;
    }

    namespace fs = std::filesystem;

    // UTF-8 文字 → 路径（窄字符串直接构造 path 会按系统代码页解释，中文成乱码）
    fs::path P(const std::string& utf8)
    {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
    }

    std::string U8(const fs::path& p)
    {
        const auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    bool WriteBytes(const fs::path& p, const std::vector<std::uint8_t>& bytes)
    {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return !bytes.empty() && out.good();
    }

    // 宿主图纸：外部参照块「轴网」（路径 xrefPath）与它在 (100, 200) 的块参照
    std::vector<std::uint8_t> MakeHost(const std::string& xrefPath, bool unloaded)
    {
        Scene scene;
        BlockTable& tbl = scene.GetBlockTable();
        const BlockID bid = tbl.Create("轴网", { 0, 0, 0 });
        BlockEntity* blk = tbl.Find(bid);
        blk->SetFlags(BlockEntity::XRef);
        blk->SetXRefPath(xrefPath);
        blk->SetXRefUnloaded(unloaded);
        auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), bid, "轴网", Math::Point3{ 100, 200, 0 });
        ins->SetBlock(blk);
        scene.AddEntity(std::move(ins));
        return ExportCad(scene, CadFileKind::Dwg, CadSaveVersion::R2018);
    }

    // 参照图纸：模型空间一条 (0,0)-(10,0) 的线
    std::vector<std::uint8_t> MakeXrefDrawing()
    {
        Scene scene;
        scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 10, 0, 0 }));
        return ExportCad(scene, CadFileKind::Dwg, CadSaveVersion::R2018);
    }

    const InsertEntity* FirstInsert(const Scene& scene)
    {
        const InsertEntity* found = nullptr;
        scene.ForEachObject([&](const Object& o) { if (!found && o.IsKindOf<InsertEntity>()) found = static_cast<const InsertEntity*>(&o); });
        return found;
    }

    // 记下多行文字输出的 sink；1 像素 = 0.5 图纸单位
    struct TextSink : IDrawSink
    {
        struct Text { Math::Point3 Pos; std::string Str; double Height; };
        std::vector<Text> Texts;
        void   DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
        double GetWorldPerPixel() const override { return 0.5; }
        void   EmitMText(const Math::Point3& pos, const std::string& text, uint32_t, double height, double, double, const Math::Color4&) override
        {
            Texts.push_back({ pos, text, height });
        }
    };

    void RunXrefResolveTests()
    {
        std::error_code ec;
        fs::path dir = fs::temp_directory_path(ec) / P("minicad_xref_test");
        fs::remove_all(dir, ec);
        if (!fs::create_directories(dir, ec))
        {
            dir = fs::current_path(ec) / P("minicad_xref_test");
            fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
        }
        const std::vector<std::uint8_t> xref = MakeXrefDrawing();

        // 原机器上的绝对路径：整个项目搬到了别处，按后缀「共用/xref/轴网.dwg」在宿主的上级目录 proj 里找到
        {
            const fs::path host = dir / P("proj") / P("建筑") / P("平面.dwg");
            Check(WriteBytes(host, MakeHost("D:\\别人的电脑\\proj\\共用\\xref\\轴网.dwg", false)) &&
                  WriteBytes(dir / P("proj") / P("共用") / P("xref") / P("轴网.dwg"), xref), "写出宿主与参照图纸（项目目录）");
            Document doc;
            Check(doc.LoadFromFile(U8(host)), "打开宿主（参照存的是原机器上的绝对路径）");
            const BlockEntity* blk = doc.GetScene().GetBlockTable().Get("轴网");
            Check(blk != nullptr && blk->EntityCount() == 1, "绝对路径失效时按路径后缀在上级目录里找到参照并读入");
            Check(FindUnresolvedXrefs(doc.GetScene()).empty(), "没有未读入的外部参照");
        }

        // 找不到：插入点显示保存的路径；再在另外指定的目录里找到
        {
            const std::string saved = "..\\..\\不存在\\轴网.dwg";
            const fs::path host = dir / P("b") / P("宿主.dwg");
            Check(WriteBytes(host, MakeHost(saved, false)), "写出宿主（参照文件不存在）");
            Document doc;
            Check(doc.LoadFromFile(U8(host)), "打开宿主（参照文件不存在）");
            Scene& scene = doc.GetScene();
            const auto missing = FindUnresolvedXrefs(scene);
            Check(missing.size() == 1 && missing[0].Name == "轴网" && missing[0].Path == saved, "FindUnresolvedXrefs 列出未读入的参照与保存的路径");
            const InsertEntity* ins = FirstInsert(scene);
            Check(ins != nullptr, "宿主里有块参照");
            if (ins != nullptr)
            {
                const AABB box = ins->GetBoundingBox();
                Check(box.Min.x == 100 && box.Min.y == 200 && box.Max.x == 100 && box.Max.y == 200, "未读入的参照范围取插入点 (100, 200)");
                TextSink sink;
                ins->Draw(sink, false, false);
                Check(sink.Texts.size() == 1 && sink.Texts[0].Str == saved && sink.Texts[0].Pos.x == 100 && sink.Texts[0].Pos.y == 200,
                      "未读入的参照在插入点写出保存的路径");
                Check(!sink.Texts.empty() && sink.Texts[0].Height == 7.0, "路径文字按屏幕 14 像素高（0.5 单位 / 像素 → 7）");
            }

            WriteBytes(dir / P("资料库") / P("某处") / P("深层") / P("轴网.dwg"), xref);
            WriteBytes(dir / P("资料库") / P("别的") / P("无关.dwg"), xref);
            const int loaded = ReloadUnresolvedXrefs(scene, U8(host), { U8(dir / P("资料库")) });
            Check(loaded == 1, "ReloadUnresolvedXrefs 在指定目录里递归找到并读入 1 个参照");
            Check(FindUnresolvedXrefs(scene).empty(), "读入后没有未读入的参照");
            if (ins != nullptr)
            {
                const AABB box = ins->GetBoundingBox();
                Check(box.Min.x == 100 && box.Max.x == 110, "读入后块参照的范围是参照里的线");
                TextSink sink;
                ins->Draw(sink, false, false);
                Check(sink.Texts.empty(), "读入后不再显示路径");
            }
        }

        // 卸载的参照：不读、不显示路径；卸载标志随 DWG 往返
        {
            const fs::path host = dir / P("c") / P("宿主.dwg");
            Check(WriteBytes(host, MakeHost(".\\轴网.dwg", true)) && WriteBytes(dir / P("c") / P("轴网.dwg"), xref), "写出宿主（参照已卸载）");
            Document doc;
            Check(doc.LoadFromFile(U8(host)), "打开宿主（参照已卸载）");
            const BlockEntity* blk = doc.GetScene().GetBlockTable().Get("轴网");
            Check(blk != nullptr && blk->IsXRefUnloaded() && blk->IsEmpty() && !blk->IsUnresolvedXRef(), "卸载标志读回，内容不读入");
            Check(FindUnresolvedXrefs(doc.GetScene()).empty(), "卸载的参照不算未读入");
        }

        fs::remove_all(dir, ec);
    }
}

int RunCadXrefTests()
{
    g_failures = 0;
    std::printf("\n=== 外部参照（XREF）导入 ===\n");
    RunXrefResolveTests();
    const std::filesystem::path path = std::filesystem::path(MINIDWG_SAMPLE_DIR).parent_path() / "Xref.dwg";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        std::printf("[跳过] 没有 tests/Data/Xref.dwg（用户图纸，不在版本库里）\n");
        return g_failures;
    }

    Document doc;
    std::string err;
    const auto u8 = path.u8string();
    Check(doc.LoadFromFile(std::string(u8.begin(), u8.end()), &err), "打开 Xref.dwg");
    Scene& scene = doc.GetScene();
    const BlockTable& blocks = scene.GetBlockTable();

    const BlockEntity* a = blocks.Get("Xref-A");
    const BlockEntity* b = blocks.Get("Xref-B");
    Check(a != nullptr && b != nullptr, "两个外部参照块 Xref-A / Xref-B");
    if (a != nullptr)
    {
        Check((a->GetFlags() & BlockEntity::XRef) != 0 && a->GetXRefPath() == ".\\Xref-A.dwg", "Xref-A 带外部参照标志与路径 .\\Xref-A.dwg");
        Check(a->GetEntities().size() >= 5, "Xref-A 的内容已从 Xref-A.dwg 读入（模型空间 8 个实体）");
        bool onDependentLayer = false;
        for (const auto& e : a->GetEntities())
            if (const Layer* l = scene.GetLayerManager().GetLayer(e->GetAttr().LayerId); l && l->GetName().rfind("Xref-A|", 0) == 0)
                onDependentLayer = true;
        Check(onDependentLayer, "外部参照里的实体落在依赖图层 Xref-A|… 上");
    }
    if (b != nullptr)
        Check(b->GetEntities().size() >= 3, "Xref-B 的内容已从 Xref-B.dwg 读入");
    Check(CountLayersNamed(scene, "Xref-A|景观") == 1, "依赖图层 Xref-A|景观 复用宿主里已有的，只有一个");

    int inserts = 0;
    scene.ForEachObject([&](const Object& o) { if (o.IsKindOf<InsertEntity>()) ++inserts; });
    Check(inserts == 2, "模型空间两个块参照");

    // Xref-A.dwg 里的 TCH_COORD 句柄 2FB：组装成 Xref-A|*U_TCH_763，引线按实线（对象线型不是实线时也是）
    const BlockEntity* coord = blocks.Get("Xref-A|*U_TCH_763");
    Check(coord != nullptr, "外部参照里的天正坐标标注已组装（Xref-A|*U_TCH_763）");
    if (coord != nullptr)
        Check(AllLinesContinuous(*coord), "坐标标注的线是实线");

    // 重新生成导出：外部参照仍是空的外部参照块（不绑定），依赖块（名字带 |）不写出
    std::vector<std::uint8_t> bytes = ExportCad(scene, CadFileKind::Dwg, CadSaveVersion::R2018, &err);
    Check(!bytes.empty(), "重新生成导出 DWG 2018");
    if (!bytes.empty())
    {
        Document back;
        Check(ImportCad(bytes, back.GetScene(), &err), "导出结果能读回（没有路径，不解析外部文件）");
        const BlockEntity* ra = back.GetScene().GetBlockTable().Get("Xref-A");
        Check(ra != nullptr && (ra->GetFlags() & BlockEntity::XRef) != 0 && ra->GetXRefPath() == ".\\Xref-A.dwg" && ra->IsEmpty(),
              "读回的 Xref-A 仍是外部参照：标志、路径都在，内容为空");
        bool dependent = false;
        back.GetScene().GetBlockTable().ForEach([&](BlockID, const BlockEntity& blk) { if (blk.GetName().find('|') != std::string::npos) dependent = true; });
        Check(!dependent, "依赖块（Xref-A|…）没有写进导出结果");
    }
    return g_failures;
}
