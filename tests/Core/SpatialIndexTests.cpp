// ── 拾取空间索引测试：随机图形 + 随机查询，与暴力遍历逐一比对；增量更新；标脏；性能 ─────────
// 用法：MiniCADTests.exe，退出码为失败项数
#include "Editor/Picking/SpatialIndex.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Scene/Scene.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace MiniCAD;

int RunEllipseArcTests();   // EllipseArcTests.cpp，返回失败项数
int RunHatchPatternTests(); // HatchPatternTests.cpp，返回失败项数
int RunDimensionTests();    // DimensionTests.cpp，返回失败项数
int RunTextStyleTests();    // TextStyleTests.cpp，返回失败项数
int RunSolidTests();        // SolidTests.cpp，返回失败项数
int RunWipeoutTests();      // WipeoutTests.cpp，返回失败项数
int RunImageTests();        // ImageTests.cpp，返回失败项数
int RunMLineTests();        // MLineTests.cpp，返回失败项数
int RunTableTests();        // TableTests.cpp，返回失败项数
int RunRegionTests();       // RegionTests.cpp，返回失败项数
int RunRegionBooleanTests();// RegionBooleanTests.cpp，返回失败项数
int RunPropertyTableTests();   // PropertyTableTests.cpp，返回失败项数
int RunPolylineOpsTests();   // PolylineOpsTests.cpp，返回失败项数
int RunExplodeTests();   // ExplodeTests.cpp，返回失败项数
int RunChamferTests();   // ChamferTests.cpp，返回失败项数
int RunFilletTests();   // FilletTests.cpp，返回失败项数
int RunStretchTests();   // StretchTests.cpp，返回失败项数
int RunScaleTests();   // ScaleTests.cpp，返回失败项数
int RunChangeAttrTests();   // ChangeAttrTests.cpp，返回失败项数
int RunLayerCommandTests(); // LayerCommandTests.cpp，返回失败项数
int RunDimAssocTests();     // DimAssocTests.cpp，返回失败项数
int RunCadExchangeTests();   // CadExchangeTests.cpp，返回失败项数

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    double Ms(std::chrono::steady_clock::time_point from)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
    }

    // 暴力参考：包围盒与 q 相交的有界实体 + 全部无界实体
    std::vector<Object::ObjectID> BruteForce(const Scene& scene, const AABB& q)
    {
        std::vector<Object::ObjectID> out;
        scene.ForEachObject([&](const Object& obj)
        {
            if (!obj.IsKindOf<Entity>()) return;
            const auto& e = static_cast<const Entity&>(obj);
            const AABB b = e.GetBoundingBox();
            const bool finite = std::isfinite(b.Min.x) && std::isfinite(b.Min.y) && std::isfinite(b.Max.x) && std::isfinite(b.Max.y);
            if (e.IsBoundless() || !finite || b.Intersects(q))
                out.push_back(obj.GetID());
        });
        std::sort(out.begin(), out.end());
        return out;
    }

    // 随机查询窗口：点、小框、大框、整个范围之外、退化（宽或高为 0）
    AABB RandomQuery(std::mt19937& rng, double extent)
    {
        std::uniform_real_distribution<double> pos(-extent * 1.2, extent * 1.2);
        std::uniform_int_distribution<int> kind(0, 4);
        const double x = pos(rng), y = pos(rng);
        double w = 0.0, h = 0.0;
        switch (kind(rng))
        {
        case 0: break;                                                   // 点
        case 1: w = h = extent * 0.01; break;                            // 小框（悬停、捕捉）
        case 2: w = extent * 0.3; h = extent * 0.2; break;               // 框选
        case 3: w = h = extent * 5.0; break;                             // 比整个图形还大
        case 4: w = extent * 0.5; h = 0.0; break;                        // 退化为线段
        }
        return AABB({ x, y, 0.0 }, { x + w, y + h, 0.0 });
    }

    bool SameAsBruteForce(const SpatialIndex& index, const Scene& scene, std::mt19937& rng, double extent, int queries)
    {
        std::vector<Object::ObjectID> got;
        for (int i = 0; i < queries; ++i)
        {
            const AABB q = RandomQuery(rng, extent);
            index.Query(q, got);
            std::vector<Object::ObjectID> sorted = got;
            std::sort(sorted.begin(), sorted.end());
            if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
            {
                std::printf("    第 %d 次查询有重复的候选\n", i);
                return false;
            }
            if (sorted != BruteForce(scene, q))
            {
                std::printf("    第 %d 次查询与暴力遍历不一致：索引 %zu 个，暴力 %zu 个\n", i, sorted.size(), BruteForce(scene, q).size());
                return false;
            }
        }
        return true;
    }

    // 尺寸跨几个数量级的随机线段和圆，几条横贯全图的长线，几条无限直线
    void FillRandom(Scene& scene, std::mt19937& rng, int count, double extent)
    {
        std::uniform_real_distribution<double> pos(-extent, extent);
        std::lognormal_distribution<double>    size(0.0, 1.5);
        std::uniform_real_distribution<double> angle(0.0, 6.283185307);
        for (int i = 0; i < count; ++i)
        {
            const Math::Point3 p(pos(rng), pos(rng), 0.0);
            const double len = size(rng) * extent * 0.002;
            if (i % 5 == 0)
                scene.AddEntity(std::make_unique<CircleEntity>(scene.NextObjectID(), p, len));
            else
            {
                const double a = angle(rng);
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), p, Math::Point3(p.x + len * std::cos(a), p.y + len * std::sin(a), 0.0)));
            }
        }
        for (int i = 0; i < 5; ++i)     // 图框式的长线：跨越整个网格，应进入 large
            scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3(-extent, pos(rng), 0), Math::Point3(extent, pos(rng), 0)));
        for (int i = 0; i < 3; ++i)
            scene.AddEntity(std::make_unique<XLineEntity>(scene.NextObjectID(), Math::Point3(pos(rng), pos(rng), 0), Math::Vec3(1.0, 0.5, 0.0)));
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    std::setvbuf(stdout, nullptr, _IONBF, 0);      // 不缓冲：中途崩溃时也能看到已输出的结果
    std::mt19937 rng(20261001);
    const double extent = 1000.0;

    // ── 1. 整表重建后的查询与暴力遍历一致 ───────────────────────
    Scene scene;
    FillRandom(scene, rng, 5000, extent);
    SpatialIndex index;
    index.Rebuild(scene);
    {
        const auto s = index.GetStats();
        std::printf("    网格 %d×%d，单元 %.3f，网格引用 %zu（%zu 个条目），large %zu，overflow %zu\n",
                    s.gridW, s.gridH, s.cell, s.gridRefs, s.entries, s.large, s.overflow);
        Check(s.large >= 5 && s.overflow == 3, "横贯全图的长线进 large，无限直线进 overflow");
    }
    Check(SameAsBruteForce(index, scene, rng, extent, 3000), "整表重建：3000 次随机查询与暴力遍历一致");

    // ── 2. 增量：新增（含远离原有范围的）、修改、删除 ─────────────
    {
        std::uniform_real_distribution<double> pos(-extent, extent);
        std::vector<Object::ObjectID> ids = scene.GetAllIDs();
        for (int i = 0; i < 200; ++i)       // 新增，其中一部分在原网格之外
        {
            const double off = (i % 4 == 0) ? extent * 3.0 : 0.0;
            const Math::Point3 p(pos(rng) + off, pos(rng) - off, 0.0);
            const auto id = scene.NextObjectID();
            scene.AddEntity(std::make_unique<LineEntity>(id, p, Math::Point3(p.x + 5.0, p.y + 3.0, 0.0)));
            index.Update(id, scene);
        }
        for (int i = 0; i < 100; ++i)       // 修改：同一 ID 换成新几何
        {
            const auto id = ids[static_cast<size_t>(i) * 7 % ids.size()];
            if (!scene.GetEntity(id))
                continue;
            scene.RemoveEntity(id);
            const Math::Point3 p(pos(rng), pos(rng), 0.0);
            scene.AddEntity(std::make_unique<LineEntity>(id, p, Math::Point3(p.x + 50.0, p.y, 0.0)));
            index.Update(id, scene);
        }
        for (int i = 0; i < 100; ++i)       // 删除（含无限直线）
        {
            const auto id = ids[static_cast<size_t>(i) * 13 % ids.size()];
            if (scene.GetEntity(id))
            {
                scene.RemoveEntity(id);
                index.Update(id, scene);
            }
        }
        const auto s = index.GetStats();
        std::printf("    增量后：extra %zu，墓碑 %d\n", s.extra, s.tombstones);
        Check(SameAsBruteForce(index, scene, rng, extent * 4.0, 3000), "增量新增 / 修改 / 删除后，查询仍与暴力遍历一致");
        Check(!index.PreferRebuild(), "少量增量不要求整表重建");
    }
    {
        for (int i = 0; i < 600; ++i)       // 积累足够多的增量条目
        {
            const auto id = scene.NextObjectID();
            scene.AddEntity(std::make_unique<LineEntity>(id, Math::Point3(i, -i, 0), Math::Point3(i + 1.0, -i + 1.0, 0)));
            index.Update(id, scene);
        }
        Check(index.PreferRebuild(), "增量条目积累过多时提示整表重建");
        index.Rebuild(scene);
        Check(index.GetStats().extra == 0 && SameAsBruteForce(index, scene, rng, extent, 1000), "再次整表重建后查询一致");
    }

    // ── 3. 边界情况：空场景、只有无限直线、全部重合 ───────────────
    {
        Scene empty;
        SpatialIndex e;
        e.Rebuild(empty);
        std::vector<Object::ObjectID> out;
        e.Query(AABB({ 0, 0, 0 }, { 1, 1, 0 }), out);
        Check(e.Empty() && out.empty(), "空场景");

        Scene same;
        for (int i = 0; i < 50; ++i)
            same.AddEntity(std::make_unique<LineEntity>(same.NextObjectID(), Math::Point3(2, 2, 0), Math::Point3(2, 2, 0)));
        same.AddEntity(std::make_unique<XLineEntity>(same.NextObjectID(), Math::Point3(0, 0, 0), Math::Vec3(0.0, 1.0, 0.0)));
        SpatialIndex s;
        s.Rebuild(same);
        s.Query(AABB({ 1, 1, 0 }, { 3, 3, 0 }), out);
        const size_t hit = out.size();
        s.Query(AABB({ 10, 10, 0 }, { 11, 11, 0 }), out);
        Check(hit == 51 && out.size() == 1, "全部实体重合在一点（退化为单格）+ 无限直线");
    }

    // ── 4. 标脏：只改显示属性不改几何版本号 ─────────────────────
    {
        Scene sc;
        sc.AddEntity(std::make_unique<LineEntity>(sc.NextObjectID(), Math::Point3(0, 0, 0), Math::Point3(1, 1, 0)));
        sc.ClearDirty();
        const uint64_t v = sc.GeometryVersion();
        sc.MarkDisplayDirty();
        Check(sc.IsAllDirty() && !sc.IsGeometryAllDirty() && sc.GeometryVersion() == v, "MarkDisplayDirty：显示全部失效，几何版本不变");
        sc.ClearDirty();
        sc.MarkDirty();
        Check(sc.IsAllDirty() && sc.IsGeometryAllDirty() && sc.GeometryVersion() != v, "MarkDirty：几何版本递增，要求整表重建");
    }

    // ── 5. 性能：引用数随实体数线性增长（只打印耗时） ──────────────
    // 与 MiniCADWin 自测相同的密集螺旋图形：旧实现在 20 万条线时整表重建约 2.9 秒
    for (const int count : { 20000, 200000, 1000000 })
    {
        Scene big;
        for (int i = 0; i < count; ++i)
        {
            const double a = i * 0.0031;
            const double r = 5.0 + (i % 200) * 0.1;
            big.AddEntity(std::make_unique<LineEntity>(big.NextObjectID(),
                Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
        }
        SpatialIndex bi;
        const auto t = std::chrono::steady_clock::now();
        bi.Rebuild(big);
        const double ms = Ms(t);
        const auto s = bi.GetStats();

        std::vector<Object::ObjectID> out;
        size_t hits = 0;
        const auto tq = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i)
        {
            const double x = std::cos(i * 0.37) * 20.0, y = std::sin(i * 0.53) * 20.0;
            bi.Query(AABB({ x, y, 0 }, { x + 0.2, y + 0.2, 0 }), out);       // 光标附近的小窗口（悬停、捕捉）
            hits += out.size();
        }
        const double qus = Ms(tq) * 1000.0 / 1000.0;
        std::printf("    %d 条线：整表重建 %.1f ms，网格 %d×%d，每个实体平均跨 %.2f 格，小窗口查询 %.1f µs/次（平均命中 %zu 个包围盒）\n",
                    count, ms, s.gridW, s.gridH, static_cast<double>(s.gridRefs) / static_cast<double>(s.entries), qus, hits / 1000);
        char what[96];
        std::snprintf(what, sizeof(what), "%d 条线：每个实体平均跨格数不超过 9", count);
        Check(static_cast<double>(s.gridRefs) <= 9.0 * static_cast<double>(s.entries), what);
    }

    g_failures += RunEllipseArcTests();
    g_failures += RunHatchPatternTests();
    g_failures += RunDimensionTests();
    g_failures += RunTextStyleTests();
    g_failures += RunSolidTests();
    g_failures += RunWipeoutTests();
    g_failures += RunImageTests();
    g_failures += RunMLineTests();
    g_failures += RunTableTests();
    g_failures += RunRegionTests();
    g_failures += RunRegionBooleanTests();
    g_failures += RunChangeAttrTests();
    g_failures += RunLayerCommandTests();
    g_failures += RunPropertyTableTests();
    g_failures += RunScaleTests();
    g_failures += RunStretchTests();
    g_failures += RunFilletTests();
    g_failures += RunChamferTests();
    g_failures += RunExplodeTests();
    g_failures += RunPolylineOpsTests();
    g_failures += RunDimAssocTests();
    g_failures += RunCadExchangeTests();

    std::printf("%s：%d 项失败\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures;
}
