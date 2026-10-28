// RAY / XLINE / TOLERANCE / LEADER 的 DWG/DXF 导入导出映射。
//
// 这四个此前在 `CadExchange.cpp` 里没有分支，落到 Skip() ——
// 日志刷「不支持的实体 RAY / XLINE / TOLERANCE / LEADER × N 已跳过」，
// 图上什么参照线、公差框格、引线都看不到（用户报 sample_AC1032.dwg 这批实体显示不了）。
//
// 这里验三件事：
//   1. DWG 与 DXF 两条读入路径都能把它们变成对应实体（不再被跳过）
//   2. 几何 / 关键属性往返不走样
//   3. 另存后再导入，实体类型不变
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/ToleranceEntity.hpp"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include <cstdio>

using namespace MiniCAD;

namespace
{
    struct CountingSink : IDrawSink
    {
        int Lines = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++Lines; }
    };

    int g_failures = 0;

    void check(bool okay, const char* message)
    {
        std::printf("[%s] %s\n", okay ? "PASS" : "FAIL", message);
        g_failures += !okay;
    }

    // 建一个只含四个目标实体的场景
    void MakeScene(Scene& scene)
    {
        auto ray = std::make_unique<RayEntity>(scene.NextObjectID(),
            Math::Point3{ 10.0, 20.0, 0.0 }, Math::Vec3{ 1.0, 1.0, 0.0 });
        scene.AddEntity(std::move(ray));

        auto xl = std::make_unique<XLineEntity>(scene.NextObjectID(),
            Math::Point3{ -5.0, 3.0, 0.0 }, Math::Vec3{ 0.0, 1.0, 0.0 });
        scene.AddEntity(std::move(xl));

        auto tol = std::make_unique<ToleranceEntity>(scene.NextObjectID(),
            Math::Point3{ 30.0, 40.0, 0.0 }, std::string("0.05%%vA"));
        scene.AddEntity(std::move(tol));

        auto ld = std::make_unique<LeaderEntity>(scene.NextObjectID(), std::vector<Math::Point3>{
            Math::Point3{ 0.0, 0.0, 0.0 }, Math::Point3{ 10.0, 0.0, 0.0 },
            Math::Point3{ 14.0, 5.0, 0.0 } });
        ld->SetArrowEnabled(true);
        ld->SetPath(LeaderPath::Straight);
        scene.AddEntity(std::move(ld));
    }

    template <class T>
    int CountOf(const Scene& scene)
    {
        int n = 0;
        scene.ForEachObject([&](const Object& o) {
            if (dynamic_cast<const T*>(&o)) ++n;
        });
        return n;
    }
}

int RunReferenceEntityTests()
{
    // ── 1. DWG / DXF 两条读入路径都能识别 ────────────────────────────────
    for (auto kind : { CadFileKind::Dwg, CadFileKind::Dxf })
    {
        const char* name = (kind == CadFileKind::Dwg) ? "DWG" : "DXF";
        Scene src;
        MakeScene(src);
        const auto bytes = ExportCad(src, kind);
        check(!bytes.empty(), (std::string(name) + ": export produced data").c_str());

        Scene back;
        const bool ok = ImportCad(bytes, back, nullptr);
        check(ok, (std::string(name) + ": reimport succeeded (previously these were skipped)").c_str());

        check(CountOf<RayEntity>(back) == 1, (std::string(name) + ": RAY no longer skipped").c_str());
        check(CountOf<XLineEntity>(back) == 1, (std::string(name) + ": XLINE no longer skipped").c_str());
        check(CountOf<ToleranceEntity>(back) == 1, (std::string(name) + ": TOLERANCE no longer skipped").c_str());
        check(CountOf<LeaderEntity>(back) == 1, (std::string(name) + ": LEADER no longer skipped").c_str());
    }

    // ── 2. 几何 / 属性往返不走样（用 DXF，便于定位问题）───────────────────
    {
        Scene src;
        MakeScene(src);
        const auto bytes = ExportCad(src, CadFileKind::Dxf);
        Scene back;
        if (ImportCad(bytes, back, nullptr))
        {
            back.ForEachObject([&](const Object& o) {
                if (const auto* r = dynamic_cast<const RayEntity*>(&o))
                {
                    check(r->GetOrigin().x == 10.0 && r->GetOrigin().y == 20.0,
                        "ray keeps start point (10,20)");
                    check(std::abs(r->GetDirection().x - r->GetDirection().y) < 1e-9,
                        "ray keeps 45-degree direction");
                    CountingSink sink; r->Draw(sink, false, false);
                    check(sink.Lines == 1, "ray draws one ray segment (not empty)");
                }
                if (const auto* x = dynamic_cast<const XLineEntity*>(&o))
                {
                    check(x->GetOrigin().y == 3.0 && std::abs(x->GetDirection().y - 1.0) < 1e-9,
                        "xline keeps base point and vertical direction");
                    CountingSink sink; x->Draw(sink, false, false);
                    check(sink.Lines == 1, "xline draws one segment (not empty)");
                }
                if (const auto* t = dynamic_cast<const ToleranceEntity*>(&o))
                {
                    check(t->GetInsertion().x == 30.0 && t->GetInsertion().y == 40.0,
                        "tolerance keeps insertion point");
                    check(t->GetText() == "0.05%%vA", "tolerance keeps encoded feature-control text");
                    CountingSink sink; t->Draw(sink, false, false);
                    check(sink.Lines > 0, "tolerance frame draws (not empty)");
                }
                if (const auto* l = dynamic_cast<const LeaderEntity*>(&o))
                {
                    check(l->GetVertices().size() == 3, "leader keeps all three path vertices");
                    check(l->GetVertices().front().x == 0.0 && l->GetVertices().back().x == 14.0,
                        "leader path endpoints survive");
                    check(l->GetArrowEnabled(), "leader keeps arrowhead flag");
                    check(l->GetPath() == LeaderPath::Straight, "leader keeps path type");
                    CountingSink sink; l->Draw(sink, false, false);
                    check(sink.Lines >= 2, "leader draws its path (not empty)");
                }
            });
        }
    }

    // ── 3. 另存后再导入（DWG → DXF → DWG 二次往返）──────────────────────
    {
        Scene a;
        MakeScene(a);
        const auto dwg = ExportCad(a, CadFileKind::Dwg);
        Scene b;
        if (ImportCad(dwg, b, nullptr))
        {
            const auto dxf = ExportCad(b, CadFileKind::Dxf);
            Scene c;
            if (ImportCad(dxf, c, nullptr))
            {
                check(CountOf<RayEntity>(c) == 1 && CountOf<XLineEntity>(c) == 1
                   && CountOf<ToleranceEntity>(c) == 1 && CountOf<LeaderEntity>(c) == 1,
                    "DWG -> reimport -> DXF -> reimport keeps all four entity types");
            }
            else
                check(false, "second reimport succeeded");
        }
        else
            check(false, "first reimport succeeded");
    }

    std::printf("\nRAY/XLINE/TOLERANCE/LEADER mapping: %d failures\n", g_failures);
    return g_failures;
}