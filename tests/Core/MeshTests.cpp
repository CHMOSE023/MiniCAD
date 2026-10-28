// ── SubD 元数据（MESH / AcDbSubDMesh）测试 ──────────────────────────────
//
// 用户报：「sample_AC1032.dwg 图纸中，网格未显示」
//    ((-1 . <图元名>) (0 . "MESH") ... (5 . "343") (8 . "0")
//     (100 . "AcDbSubDMesh") (71 . 2) (72 . 0) (91 . 0) (92 . 62) …)
//
// 根因：MESH（AcDbSubDMesh）在 MiniCAD 里**全栈零支持** —— MiniDWG 没有 Mesh 类、
// DWG/DXF 读取器都把它当"未建模实体"原样保留字节、CadExchange 落到 Skip()，
// 于是日志里「不支持的实体 MESH × 2 已跳过」，图上什么都没有。
//
// 该图（经权威工具转出，作为对照基准）：
//    句柄 343：顶点 62 / 面 72（24 三角 + 48 四边形）/ 面数据项数 336 / 边 132 / 折痕 132
//    句柄 380：顶点 64 / 面 64（全四边形）    / 面数据项数 320 / 边 128 / 折痕 128
//
// dve 与 V0.1.0 的实现差异：dve 的 MeshEntity 继承 Solid3DEntity，几何只有
// **三角形**表达（SurfaceMesh），N 边面由导入侧扇形拆成三角形。所以这里测的是
//  ① SubD 元数据字段（版本/混合折痕/细分级别/覆盖选项/显式边表/折痕值）的存取与 Clone
//  ② 非 SubD 来源（节点图烘焙 / PolyfaceMesh）的默认值 —— 必须是"未设置"语义
//  ③ 面数据项数公式：Σ(每面顶点数 + 1)
//  ④ 绘制仍走线框（不因新增字段而改变既有观感）
//
// 本文件用**合成数据**守住这些不变量（不依赖外部图纸）。
#include "Core/Entity/MeshEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
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

    // 记录画了哪些线段
    struct MeshSink : IDrawSink
    {
        std::vector<std::pair<Math::Point3, Math::Point3>> lines;
        Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override
        { lines.push_back({ a, b }); }
    };

    // 一个单位立方体的三角形网格（6 个四边形面 × 扇形三角化 = 12 个三角形）
    // 顶点共用，索引从 0 开始。
    MeshEntity MakeUnitCube(Object::ObjectID id)
    {
        Solid3DEntity::SurfaceMesh sm;
        sm.Positions = {
            {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
            {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},
        };
        // 6 个四边形面，每个拆 2 个三角形
        const int quads[6][4] = {
            {0,1,2,3}, {4,5,6,7}, {0,1,5,4},
            {1,2,6,5}, {2,3,7,6}, {3,0,4,7},
        };
        for (const auto& q : quads)
        {
            sm.Indices.push_back(static_cast<uint32_t>(q[0]));
            sm.Indices.push_back(static_cast<uint32_t>(q[1]));
            sm.Indices.push_back(static_cast<uint32_t>(q[2]));
            sm.Indices.push_back(static_cast<uint32_t>(q[0]));
            sm.Indices.push_back(static_cast<uint32_t>(q[2]));
            sm.Indices.push_back(static_cast<uint32_t>(q[3]));
        }
        return MeshEntity(id, std::move(sm), Math::Mat4::Identity());
    }
}

int RunMeshTests()
{
    g_failures = 0;

    // ── 1. 非 SubD 来源的默认值必须是"未设置"语义 ──────────────────────
    //
    // 节点图烘焙 / PolyfaceMesh 导入都走这条路。若默认值不是中性的
    // （例如 SubDVersion 默认 0 或 BlendCrease 默认 true），
    // 另存为 DWG 时会把这些假元数据写出去，污染原图。
    {
        const MeshEntity cube = MakeUnitCube(1);
        Check(cube.GetSubDVersion() == 2,
              "SubD 版本默认 2（DXF 组码 71 的 AutoCAD 默认值）");
        Check(!cube.GetBlendCrease(),
              "混合折痕默认关（组码 72 默认 0）");
        Check(cube.GetSubdivisionLevel() == 0,
              "细分级别默认 0（组码 91，不做 Catmull-Clark 细分）");
        Check(cube.GetOverrideOption() == 0,
              "覆盖选项默认 0");
        Check(cube.GetExplicitEdges().empty(),
              "显式边表默认空（非 SubD 来源）");
        Check(cube.GetCreaseValues().empty(),
              "折痕值默认空（非 SubD 来源）");
    }

    // ── 2. SubD 元数据的存取 ───────────────────────────────────────────
    {
        MeshEntity cube = MakeUnitCube(2);
        cube.SetSubDVersion(2);
        cube.SetBlendCrease(true);
        cube.SetSubdivisionLevel(3);
        cube.SetOverrideOption(1);
        cube.SetExplicitEdges({ {0,1}, {1,2}, {2,3} });
        cube.SetCreaseValues({ 0.5, 0.25, 1.0 });

        Check(cube.GetBlendCrease(), "混合折痕：写入后读回一致");
        Check(cube.GetSubdivisionLevel() == 3, "细分级别：写入后读回一致");
        Check(cube.GetOverrideOption() == 1, "覆盖选项：写入后读回一致");
        Check(cube.GetExplicitEdges().size() == 3, "显式边表：写入后读回一致");
        Check(cube.GetCreaseValues().size() == 3, "折痕值：写入后读回一致");
        Check(cube.GetCreaseValues()[0] == 0.5 && cube.GetCreaseValues()[2] == 1.0,
              "折痕值：逐个读回一致（每个 crease 对应同下标的边）");
    }

    // ── 3. Clone 必须带上全部 SubD 元数据 ──────────────────────────────
    //
    // 复制 / 粘贴网格时漏一个字段，用户看到的就是"属性莫名丢了"。
    {
        MeshEntity cube = MakeUnitCube(3);
        cube.SetSubDVersion(2);
        cube.SetBlendCrease(true);
        cube.SetSubdivisionLevel(4);
        cube.SetOverrideOption(2);
        cube.SetExplicitEdges({ {0,1}, {5,6} });
        cube.SetCreaseValues({ 0.75, 0.125 });

        auto cloned = cube.Clone(99);
        const auto* m = dynamic_cast<const MeshEntity*>(cloned.get());
        Check(m != nullptr, "Clone 得到 MeshEntity");
        if (m)
        {
            Check(m->GetSubDVersion() == 2, "Clone：版本一致");
            Check(m->GetBlendCrease(), "Clone：混合折痕一致");
            Check(m->GetSubdivisionLevel() == 4, "Clone：细分级别一致");
            Check(m->GetOverrideOption() == 2, "Clone：覆盖选项一致");
            Check(m->GetExplicitEdges().size() == 2 &&
                  m->GetExplicitEdges()[1].first == 5 && m->GetExplicitEdges()[1].second == 6,
                  "Clone：显式边表一致");
            Check(m->GetCreaseValues().size() == 2 && m->GetCreaseValues()[1] == 0.125,
                  "Clone：折痕值一致");
        }
    }

    // ── 4. 面数据项数公式：Σ(每面顶点数 + 1) ──────────────────────────
    //
    // DWG 位流与 DXF 组码 93 都是"项数"而不是"面数"：
    // 每个面的"个数"那一项也占一项。用错公式会让读取器提前结束，
    // 表现为"只读出前几个面"。
    {
        const std::vector<std::vector<int>> faces = {
            {0,1,2},          // 三角：4 项（1 个数 + 3 索引）
            {0,1,2,3},        // 四边：5 项
            {0,1,2,3,4},      // 五边：6 项
        };
        int items = 0;
        for (const auto& f : faces)
            items += static_cast<int>(f.size()) + 1;
        Check(items == 15, "面数据项数 = Σ(每面顶点数 + 1) = 4+5+6 = 15（不是面数 3）");
    }

    // ── 5. 包围盒含 Z（网格是三维的）───────────────────────────────────
    {
        const MeshEntity cube = MakeUnitCube(4);
        const AABB box = cube.GetBoundingBox();
        Check(box.Min.x == 0.0 && box.Max.x == 1.0, "包围盒 X：[0,1]");
        Check(box.Min.y == 0.0 && box.Max.y == 1.0, "包围盒 Y：[0,1]");
        Check(box.Min.z == 0.0 && box.Max.z == 1.0, "包围盒 Z：[0,1]（不能退化成 2D 盒）");
    }

    // ── 6. 线框按**显式边表**画，不画扇形三角化的对角线 ────────────────
    //
    // 用户报：「sample_AC1032.dwg 图纸中的网格图元为什么显示出这么多三角形出来了？」
    //
    // 根因：我们的 SurfaceMesh 只有三角形表达，导入时把 N 边面扇形拆成三角形，
    // 而基类 Solid3DEntity::Draw 从**三角形**收边 ⇒ 每条四边形面对角线也变成
    // 一条网格边被画出来。实测该图：
    //     句柄 343：24 三角 + 48 四边 ⇒ 三角形收边 180 条，原图显式边表 132 条
    //     句柄 380：64 四边             ⇒ 三角形收边 192 条，原图显式边表 128 条
    // 多出的正好等于四边形个数（48 / 64）。
    //
    // 原图的显式边表是权威的（实测「面边界去重边集」与它完全相等），
    // 有它就按它画，不重不漏。
    {
        // 造一个四边形面网格：4 顶点 1 个四边形面，扇形拆成 2 个三角形
        Solid3DEntity::SurfaceMesh sm;
        sm.Positions = { {0,0,0}, {2,0,0}, {2,1,0}, {0,1,0} };
        sm.Indices = { 0,1,2, 0,2,3 };
        MeshEntity quad(7, std::move(sm), Math::Mat4::Identity());

        // 不设显式边表（非 SubD 来源）：从三角形收边 ⇒ 4 条边 + 1 条对角线 = 5
        MeshSink s1;
        quad.Draw(s1, false, false);
        Check(s1.lines.size() == 5,
              "无显式边表：从三角形收边（含扇形对角线）—— 节点图/PolyfaceMesh 路径不变");

        // 设了显式边表：只画那 4 条外边，对角线不画
        quad.SetExplicitEdges({ {0,1}, {1,2}, {2,3}, {3,0} });
        MeshSink s2;
        quad.Draw(s2, false, false);
        Check(s2.lines.size() == 4,
              "**有显式边表：只画 4 条外边，不画扇形对角线**（修 sample_AC1032 多出一堆三角形）");

        // 逐条核对：画出来的确实是显式边表那几条
        bool allMatch = s2.lines.size() == 4;
        for (const auto& [a, b] : s2.lines)
        {
            const double len = std::sqrt((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y));
            if (std::abs(len - 2.0) > 1e-9 && std::abs(len - 1.0) > 1e-9) allMatch = false;
        }
        Check(allMatch, "画出的边长度符合 4 条外边（2/2/1/1），不含长 2.236 的对角线");

        // 越界索引必须跳过，不能让绘制崩
        quad.SetExplicitEdges({ {0,1}, {99, 0}, {1,2} });
        MeshSink s3;
        quad.Draw(s3, false, false);
        Check(s3.lines.size() == 2,
              "显式边表含越界索引：跳过该条，其余照画（损坏文件不崩）");
    }

    // ── 7. 绘制仍是线框：新增 SubD 元数据不改变既有观感 ──────────────────
    //
    // 注意：dve 的 MeshEntity 从**三角形**求边（Solid3DEntity::AllEdges），
    // 所以扇形三角化引入的对角线也会被画出来 —— 这是既有行为
    // （6 个四边形面 × 1 条对角线 = 6 条，加上立方体本身的 12 条 = 18）。
    // 本用例守住的是"SubD 元数据不改变绘制结果"，不是"等于 12"。
    {
        MeshEntity cube = MakeUnitCube(5);
        const size_t before = [&]{ MeshSink s; cube.Draw(s, false, false); return s.lines.size(); }();

        cube.SetSubdivisionLevel(9);      // 即使"平滑度"拉满
        cube.SetBlendCrease(true);
        cube.SetExplicitEdges({ {0,1} });
        cube.SetCreaseValues({ 1.0 });

        MeshSink sink;
        cube.Draw(sink, false, false);
        Check(sink.lines.size() == 1,
              "设了显式边表后只画那 1 条（元数据/边表决定线框，不掺三角形收边）");

        // 与不设任何 SubD 元数据的同形状网格对比，必须完全一致
        const MeshEntity plain = MakeUnitCube(6);
        MeshSink sink2;
        plain.Draw(sink2, false, false);
        Check(sink2.lines.size() == before,
              "不设边表的同形状网格走三角形收边（与非 SubD 来源完全一致）");
        std::printf("    三角形收边线段数 = %zu\n", before);
    }

    // ── 8. 显式边表也决定捕捉用的线框，克隆共享的数据不被改写 ──────────────
    {
        MeshEntity cube = MakeUnitCube(8);
        const auto clone = cube.Clone(9);
        const size_t wireBefore = cube.GetWorldWire().size();
        cube.SetExplicitEdges({ {0,1}, {1,2}, {99,0} });
        Check(cube.GetWorldWire().size() == 2, "世界线框按显式边表重建（越界的跳过），捕捉不会吸到对角线");
        Check(static_cast<const MeshEntity*>(clone.get())->GetWorldWire().size() == wireBefore,
              "改边表前克隆出的网格线框不变（数据写前复制）");
        cube.SetExplicitEdges({});
        Check(cube.GetWorldWire().size() == wireBefore, "清空边表后退回三角形收边");
    }

    // ── 9. 面顶点数：只有有边表与面的才算 SubD 来源，克隆保留 ─────────────
    {
        MeshEntity cube = MakeUnitCube(10);
        Check(!cube.IsSubD(), "没有边表与面的网格不是 SubD 来源（导出写多面网格）");
        cube.SetExplicitEdges({ {0,1} });
        cube.SetFaceSizes({ 4, 4, 4, 4, 4, 4 });
        Check(cube.IsSubD(), "有边表与面的网格是 SubD 来源");
        const auto c = cube.Clone(11);
        Check(static_cast<const MeshEntity*>(c.get())->GetFaceSizes().size() == 6, "克隆保留面顶点数");
    }

    std::printf("SubD 网格用例失败：%d\n", g_failures);
    return g_failures;
}
