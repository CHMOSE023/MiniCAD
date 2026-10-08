// ── DWG / DXF 交换测试：场景 → 导出 → 导入 → 比对，以及读取 MiniDWG 的样例图 ──────────
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }

    template <class T>
    const T* FindOne(const Scene& scene)
    {
        const T* found = nullptr;
        scene.ForEachObject([&](const Object& o) { if (o.IsKindOf<T>()) found = static_cast<const T*>(&o); });
        return found;
    }

    int CountAll(const Scene& scene)
    {
        int n = 0;
        scene.ForEachObject([&](const Object&) { ++n; });
        return n;
    }

    void BuildScene(Scene& scene)
    {
        LayerID walls = scene.GetLayerManager().AddLayer("Walls");
        scene.GetLayerManager().GetLayer(walls)->SetColor({ 1.0, 0.0, 0.0, 1.0 });

        auto line = std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 10, 5, 0 });
        EntityAttr a = line->GetAttr();
        a.LayerId = walls;
        a.Color   = EntityColor::FromAci(3);
        line->SetAttr(a);
        scene.AddEntity(std::move(line));

        scene.AddEntity(std::make_unique<CircleEntity>(scene.NextObjectID(), Math::Point3{ 20, 20, 0 }, 4.0));
        scene.AddEntity(std::make_unique<ArcEntity>(scene.NextObjectID(), Math::Point3{ 0, 30, 0 }, 7.0, 0.5, 2.0));
        scene.AddEntity(std::make_unique<EllipseEntity>(scene.NextObjectID(), Math::Point3{ 50, 0, 0 }, 6.0, 3.0, 0.3));
        scene.AddEntity(std::make_unique<TextEntity>(scene.NextObjectID(), Math::Point3{ 1, 2, 0 }, "Hello 你好", 2.5f, 0.25f));
        scene.AddEntity(std::make_unique<MTextEntity>(scene.NextObjectID(), 0u, "line1\Pline2", Math::Point3{ 5, 5, 0 }, 3.0, 0.0, 40.0));

        // 闭合多段线（末点 = 起点）+ 圆弧段
        auto pl = std::make_unique<PolylineEntity>(scene.NextObjectID(),
            std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 0, 0 } },
            std::vector<double>{ 0.0, 0.5, 0.0 });
        pl->SetWidth(0.0);
        scene.AddEntity(std::move(pl));

        // 块与插入
        BlockID blk = scene.GetBlockTable().Create("BOX", { 1, 1, 0 });
        scene.GetBlockTable().Find(blk)->AddEntity(
            std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 2, 0, 0 }));
        auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), blk, "BOX", Math::Point3{ 100, 100, 0 });
        ins->SetRotation(0.5);
        ins->SetScale({ 2, 2, 1 });
        scene.AddEntity(std::move(ins));
    }

    void CheckRoundTrip(CadFileKind kind, const char* label, CadSaveVersion version = CadSaveVersion::R2018)
    {
        Scene src;
        BuildScene(src);

        std::string err;
        const auto bytes = ExportCad(src, kind, version, &err);
        Check(!bytes.empty(), (std::string(label) + "：导出成功").c_str());
        if (bytes.empty())
            return;

        Scene dst;
        CadSaveVersion readVersion = CadSaveVersion::R2018;
        Check(ImportCad(bytes, dst, &err, &readVersion), (std::string(label) + "：导入成功").c_str());
        Check(readVersion == version, (std::string(label) + "：读回的版本与写出的版本一致").c_str());

        Check(CountAll(dst) == CountAll(src), (std::string(label) + "：实体数量一致").c_str());

        const auto* line = FindOne<LineEntity>(dst);
        Check(line && Near(line->GetLine().End.x, 10) && Near(line->GetLine().End.y, 5), "直线端点一致");
        Check(line && line->GetAttr().Color.Method == ColorMethod::ByAci && line->GetAttr().Color.Aci == 3, "直线 ACI 颜色保留");
        const Layer* layer = line ? dst.GetLayerManager().GetLayer(line->GetAttr().LayerId) : nullptr;
        Check(layer && layer->GetName() == "Walls" && Near(layer->GetColor().r, 1.0), "图层名与颜色保留");

        const auto* circle = FindOne<CircleEntity>(dst);
        Check(circle && Near(circle->GetCircle().Radius, 4.0) && Near(circle->GetCircle().Center.x, 20), "圆一致");

        const auto* arc = FindOne<ArcEntity>(dst);
        Check(arc && Near(arc->GetArc().StartAngle, 0.5) && Near(arc->GetArc().EndAngle, 2.0), "圆弧角度一致");

        const auto* el = FindOne<EllipseEntity>(dst);
        Check(el && Near(el->GetEllipse().RadiusX, 6.0) && Near(el->GetEllipse().RadiusY, 3.0)
                 && Near(el->GetEllipse().Rotation, 0.3), "椭圆半轴与旋转一致");

        const auto* text = FindOne<TextEntity>(dst);
        Check(text && text->GetText() == "Hello 你好" && Near(text->GetHeight(), 2.5), "单行文字（含中文）一致");

        const auto* mtext = FindOne<MTextEntity>(dst);
        Check(mtext && mtext->GetText() == "line1\Pline2" && Near(mtext->GetBoxWidth(), 40.0), "多行文字一致");

        const auto* pl = FindOne<PolylineEntity>(dst);
        Check(pl && pl->GetPolyline().Points.size() == 4 && Near(pl->GetPolyline().Bulges[1], 0.5), "闭合多段线（含弧段）一致");

        const auto* ins = FindOne<InsertEntity>(dst);
        Check(ins && ins->GetBlock() && ins->GetBlock()->GetName() == "BOX" && ins->GetBlock()->EntityCount() == 1
                  && Near(ins->GetScale().x, 2) && Near(ins->GetRotation(), 0.5), "块定义与插入一致");
        Check(ins && ins->GetBlock() && Near(ins->GetBlock()->GetBasePoint().x, 1), "块基点一致");
    }

    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }
}

int RunCadExchangeTests()
{
    g_failures = 0;

    CheckRoundTrip(CadFileKind::Dxf, "DXF");
    CheckRoundTrip(CadFileKind::Dwg, "DWG");
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2013", CadSaveVersion::R2013);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2013", CadSaveVersion::R2013);

    // MiniDWG 样例图：能读入，且读入后能再导出、再读回，实体数不变
    const char* samples[] = { "sample_AC1015.dwg", "sample_AC1032.dwg", "sample_AC1015_ascii.dxf", "sample_AC1032_binary.dxf" };
    for (const char* name : samples)
    {
        const auto data = ReadFile(std::string(MINIDWG_SAMPLE_DIR) + "/" + name);
        if (data.empty())
        {
            std::printf("[跳过] 没有找到样例 %s\n", name);
            continue;
        }
        Scene scene;
        Check(ImportCad(data, scene, nullptr), (std::string("读取样例 ") + name).c_str());
        std::printf("       %s：%d 个实体\n", name, CountAll(scene));

        const auto out = ExportCad(scene, CadFileKind::Dwg);
        Scene again;
        Check(!out.empty() && ImportCad(out, again, nullptr) && CountAll(again) == CountAll(scene),
              (std::string("样例 ") + name + " 导出 DWG 后再读回，实体数不变").c_str());
    }

    std::printf("DWG/DXF 交换测试：%d 项失败\n", g_failures);
    return g_failures;
}
