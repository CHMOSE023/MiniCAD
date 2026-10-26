// 外部参照（XREF）导入测试：宿主 tests/Data/Xref.dwg 以相对路径参照 Xref-A.dwg 与 Xref-B.dwg
// （用户图纸，不在版本库里；文件不存在时跳过）。核对：
//   · 外部参照块带标志与路径，内容从外部文件读入
//   · 依赖图层「参照名|图层名」复用宿主里已有的，不重复
//   · 外部参照里的天正坐标标注按实线组装
//   · 重新生成导出时外部参照仍是外部参照（不绑定），依赖块不写出
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Document/Document.h"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include <cstdio>
#include <filesystem>
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
}

int RunCadXrefTests()
{
    g_failures = 0;
    std::printf("\n=== 外部参照（XREF）导入 ===\n");
    const std::filesystem::path path = std::filesystem::path(MINIDWG_SAMPLE_DIR).parent_path() / "Xref.dwg";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        std::printf("[跳过] 没有 tests/Data/Xref.dwg（用户图纸，不在版本库里）\n");
        return 0;
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
