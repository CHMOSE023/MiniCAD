// ── 轴号：块属性（ATTDEF / ATTRIB）必须导入、落在正确位置、按对齐居中 ───────────────
//
// 用户报「轴号问题」，要求「根据 V0.1.0 版本修复」。
//
// V0.1.0 侧记录的三处缺陷（docs/修复记录.md 的 V54），本文件逐条锁住：
//
// 【缺陷① · 属性文字整批丢失】
//   `CadExchange::Convert` 把 `Dwg::AttributeBase`（ATTDEF/ATTRIB）整个 Skip()，
//   而 INSERT 分支也**完全没有处理** `e->Attributes`
//   ⇒ 块引用的属性文字（轴号、图签字段…）从不进入场景。
//   实测 `test0.dwg` 的轴号块 `_AXISO`（块定义 = 圆 + 一个 ATTDEF）：
//     | 侧 | _AXISO 实例 | 带 ATTRIB 的 |
//     | DWG 真值 | 417 | 417（100%）|
//     | v0.2.0 修前 | 241 | 0 |
//   属性值就是轴号本身（「1」「24」「E」…）⇒ 界面上只剩一个个空圆圈。
//   → v0.2.0 只有「游离的 ATTRIB 一律跳过」，ATTDEF 与 INSERT 上的 ATTRIB 都没有落点。
//
// 【缺陷② · 位置被二次变换】
//   ATTRIB 的坐标**已是世界坐标**（AutoCAD 在 INSERT 里算好了），
//   而 `InsertEntity::Draw` 用 xsink（会乘 CellMatrix）画它 ⇒ 又变换一次。
//   实测：存储锚点 (2046863.04, -1682314.24)
//         经 xsink 落到 **(2458282511.89, -2020459066.72)**（大 1200 倍 = 插入比例）。
//
// 【缺陷③ · Fit / Aligned 的对齐码被丢】
//   72 = 3(Aligned) / 5(Fit) 是**两端对齐**：文字在 10↔11 之间拉伸铺满，
//   视觉中心 = 两点**中点**。取锚点之外还必须把对齐码交给排版引擎，
//   否则文字仍从中点**向右**排，偏出半个字宽。
//
// 【本条 v0.2.0 特有 · 每个轴号旁多出一份模板】
//   `_AXISO` 的 ATTDEF 是 tag="A"、默认值空。若把块定义里的 ATTDEF 当普通文字画，
//   每个轴号旁都会多画一个「A」（V0.0.1 的 R35：全图 1616 个块参照）。
//   可变属性的取值由随附的 ATTRIB 提供，模板本身不该画；常量属性（70&2）没有 ATTRIB，
//   取值就是模板，要画。
//
// 本测试走**真实导入器**（自足，不依赖外部图纸）。
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Scene/BlockTable.h"
#include "Scene/LineTypeTable.h"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/AttribEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_fail = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_fail;
    }

    // 记录绘制落点的 sink：文字只记锚点与对齐方式，其余几何忽略
    struct RecordSink : IDrawSink
    {
        struct Text { Math::Point3 pos; std::string value; int hAlign; int vAlign; bool aligned; };
        std::vector<Text> texts;
        int lines = 0, circles = 0;

        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void FillTriangle(const Math::Point3&, const Math::Point3&, const Math::Point3&, const Math::Color4&) override {}
        void EmitMText(const Math::Point3& p, const std::string& t, uint32_t, double, double, double, const Math::Color4&) override
        { texts.push_back({ p, t, 0, 0, false }); }
        void EmitAlignedText(const Math::Point3& p, const std::string& t, uint32_t, double, double,
                             int h, int v, double, const Math::Color4&) override
        { texts.push_back({ p, t, h, v, true }); }
    };
}
// 构造一份最小 DXF：块 _AXISO（圆 + ATTDEF）+ 一个带 ATTRIB(72=5) 的 INSERT
//   圆心在块原点、R=0.5；INSERT 比例 1200、插入点 (1000, 2000)。
//   ATTRIB 的 10/11 中点 = (1000, 2000)（即圆心），对齐码 72=5(Fit)。
static const char* kAxisDxf = R"DXF(0
SECTION
2
HEADER
9
$ACADVER
1
AC1015
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
1
0
LAYER
2
0
70
0
62
7
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
BLOCKS
0
BLOCK
5
B1
8
0
2
_AXISO
70
0
10
0.0
20
0.0
30
0.0
3
_AXISO
1

0
CIRCLE
5
B2
8
0
10
0.0
20
0.0
30
0.0
40
0.5
0
ATTDEF
5
B3
8
0
10
0.0
20
0.0
30
0.0
40
0.56
1

2
A
70
0
3
axis number
0
ENDBLK
5
B4
8
0
0
ENDSEC
0
SECTION
2
ENTITIES
0
INSERT
5
I1
8
0
66
1
2
_AXISO
10
1000.0
20
2000.0
30
0.0
41
1200.0
42
1200.0
43
1200.0
50
0.0
0
ATTRIB
5
A1
8
0
10
995.0
20
2000.0
30
0.0
40
678.823
1
7
2
A
70
0
72
5
11
1005.0
21
2000.0
31
0.0
0
SEQEND
5
S1
8
0
0
ENDSEC
0
EOF
)DXF";

int RunAttribAxisTests()
{
    g_fail = 0;
    std::printf("── 轴号（块属性 ATTRIB）──\n");

    const std::string text(kAxisDxf);
    Scene scene;
    std::string err;
    const bool ok = ImportCad(std::vector<std::uint8_t>(text.begin(), text.end()), scene, &err, nullptr);
    std::printf("    导入结果 = %d  err=%s\n", ok ? 1 : 0, err.c_str());
    Check(ok, "含 _AXISO + ATTRIB 的 DXF 能导入");
    if (!ok) { std::printf("轴号用例失败：%d\n", g_fail); return g_fail; }

    // ── 找 INSERT，并核对块定义里留着一个 ATTDEF（缺陷①的块内那一半）────────
    const InsertEntity* ins = nullptr;
    int attdefsInBlock = 0;
    scene.ForEachObject([&](const Object& o)
    {
        auto* i = dynamic_cast<const InsertEntity*>(&o);
        if (i && i->GetBlock() && i->GetBlock()->GetName() == "_AXISO") ins = i;
    });
    Check(ins != nullptr, "找到 _AXISO 的 InsertEntity");
    if (!ins) { std::printf("轴号用例失败：%d\n", g_fail); return g_fail; }

    for (const auto& e : ins->GetBlock()->GetEntities())
        if (dynamic_cast<const AttDefEntity*>(e.get()) != nullptr) ++attdefsInBlock;
    std::printf("    块定义内 ATTDEF = %d\n", attdefsInBlock);
    Check(attdefsInBlock == 1, "**块定义里的 ATTDEF 被导入为 AttDefEntity（修前为 0 —— 整批 Skip）**");

    // ── 1. ATTRIB 必须被导入（缺陷①）──────────────────────────────────
    const auto& atts = ins->GetAttribs();
    std::printf("    _AXISO 随附 ATTRIB = %zu\n", atts.size());
    Check(atts.size() == 1, "**块引用的 ATTRIB 被导入（修前为 0 —— 轴号文字整批丢失）**");
    if (atts.empty()) { std::printf("轴号用例失败：%d\n", g_fail); return g_fail; }

    std::printf("    ATTRIB tag=「%s」 值=「%s」 锚点=(%.3f,%.3f) 高=%.3f 对齐 h=%d v=%d\n",
                atts[0]->GetTag().c_str(), atts[0]->GetText().c_str(),
                atts[0]->GetPosition().x, atts[0]->GetPosition().y, atts[0]->GetHeight(),
                atts[0]->GetHAlign(), atts[0]->GetVAlign());
    Check(atts[0]->GetText() == "7", "ATTRIB 取值正确（「7」）");

    // ── 2. Fit(72=5) 的锚点必须取 10↔11 的**中点**（= 圆心）────────────────
    const Math::Point3 p = atts[0]->GetPosition();
    std::printf("    锚点 = (%.3f, %.3f)   期望中点 = (1000.000, 2000.000)\n", p.x, p.y);
    Check(std::abs(p.x - 1000.0) < 1e-6 && std::abs(p.y - 2000.0) < 1e-6,
          "**Fit(72=5) 取 10↔11 中点 ⇒ 文字居中于圆心（取 11 会偏 0.8×半径）**");

    // ── 3. 对齐码必须带出来并按居中输出（缺陷③）──────────────────────
    Check(atts[0]->GetHAlign() == 1, "**Fit(72=5) 换算为居中(1) 并带到实体上 ⇒ 渲染按中心排**");
    Check(atts[0]->GetVAlign() == 0, "垂直对齐保持 Baseline(0)");
    {
        RecordSink sink;
        atts[0]->Draw(sink, false, false);
        const bool aligned = !sink.texts.empty() && sink.texts[0].aligned && sink.texts[0].hAlign == 1;
        std::printf("    ATTRIB 自身绘制：%zu 段文字，aligned=%d h=%d\n", sink.texts.size(),
                    sink.texts.empty() ? -1 : static_cast<int>(sink.texts[0].aligned),
                    sink.texts.empty() ? -1 : sink.texts[0].hAlign);
        Check(aligned, "**属性文字走 EmitAlignedText（只换锚点不传对齐 ⇒ 偏出半个字宽）**");
    }

    // ── 4. 绘制位置必须等于存储锚点（不得二次变换，缺陷②）────────────────
    //   INSERT 比例 1200；若被 CellMatrix 二次变换，(1000,2000) 会变成 (1200000, 2400000)。
    {
        RecordSink r;
        ins->Draw(r, false, false);
        std::printf("    INSERT 绘制后文字落点 = %zu 处", r.texts.size());
        if (!r.texts.empty())
            std::printf("  首点=(%.3f,%.3f) 值=「%s」", r.texts[0].pos.x, r.texts[0].pos.y, r.texts[0].value.c_str());
        std::printf("\n");
        Check(!r.texts.empty(), "绘制块参照时输出了属性文字");
        if (!r.texts.empty())
        {
            const double dx = std::abs(r.texts[0].pos.x - 1000.0);
            const double dy = std::abs(r.texts[0].pos.y - 2000.0);
            std::printf("    与存储锚点的偏差 = (%.3f, %.3f)\n", dx, dy);
            Check(dx < 1e-6 && dy < 1e-6,
                  "**属性文字按世界坐标绘制，未被 CellMatrix 二次变换（否则会放大 1200 倍）**");
        }
    }

    // ── 5. 可变属性的模板不许画（否则每个轴号旁多一个「A」）────────────────
    //   ATTRIB 只有一条；若模板也被画出来，sink 里会出现两段文字。
    {
        RecordSink r;
        ins->Draw(r, false, false);
        std::printf("    块参照绘制出的文字段数 = %zu（应为 1：只有 ATTRIB，不含 tag=\"A\" 的模板）\n", r.texts.size());
        const bool onlyValue = r.texts.size() == 1 && r.texts[0].value == "7";
        Check(onlyValue, "**可变属性的 ATTDEF 模板未绘制 ⇒ 不会在每个轴号旁多画一个「A」**");
    }

    // ── 6. 常量属性（70&2）的取值就是模板，必须画 ──────────────────────────
    {
        Scene s2;
        Check(ImportCad(std::vector<std::uint8_t>(text.begin(), text.end()), s2, nullptr), "（常量属性用例）重新导入");
        const InsertEntity* i2 = nullptr;
        s2.ForEachObject([&](const Object& o)
        {
            auto* i = dynamic_cast<const InsertEntity*>(&o);
            if (i && i->GetBlock() && i->GetBlock()->GetName() == "_AXISO") i2 = i;
        });
        const AttDefEntity* def = nullptr;
        if (i2 && i2->GetBlock())
            for (const auto& e : i2->GetBlock()->GetEntities())
                if (auto* d = dynamic_cast<const AttDefEntity*>(e.get())) def = d;
        Check(def != nullptr, "（常量属性用例）块内 ATTDEF 可取到");
        if (def)
        {
            // 直接判约定：可变属性不画、常量+有值画
            Check(!def->ShowsInBlockReference(), "tag=\"A\"、默认值空的可变属性：块展开时不画模板");
            const_cast<AttDefEntity*>(def)->SetFlags(static_cast<uint16_t>(AttribFlags::Constant));
            const_cast<AttDefEntity*>(def)->SetText("CONST");
            Check(def->ShowsInBlockReference(), "常量属性 + 有默认值：块展开时画模板（没有对应 ATTRIB）");
        }
    }

    // ── 6b. 空值的 ATTRIB 什么都不画（图签里没填的栏位不许印出字段名）────────
    //   实测 `test0.dwg` 的 2343 个 ATTRIB 里 990 个是空值（「专业负责人1」「图名」…）。
    //   若沿用「空则显示 tag 占位」的规则，会凭空多出 990 处文字。
    {
        AttribEntity empty(1, "图名", "", { 0, 0, 0 }, 3.0, 0.0);
        RecordSink sink;
        empty.Draw(sink, false, false);
        Check(sink.texts.empty(), "**空值 ATTRIB 不画任何字（画 tag 会把「图名」这类字段名印到图上）**");

        AttDefEntity tmpl(2, "TAGNAME", "", { 0, 0, 0 }, 3.0, 0.0);
        RecordSink sink2;
        tmpl.Draw(sink2, false, false);
        Check(sink2.texts.size() == 1 && sink2.texts[0].value == "TAGNAME",
              "空值 ATTDEF 仍显示 tag 作占位（属性模板的既有语义不变）");
    }

    // ── 7. 另存往返要留住属性（否则「打开 → 另存 → 再打开」轴号整批消失）──────
    for (const auto kind : { CadFileKind::Dxf, CadFileKind::Dwg })
    {
        const char* label = kind == CadFileKind::Dxf ? "DXF" : "DWG";
        Scene s3;
        Check(ImportCad(std::vector<std::uint8_t>(text.begin(), text.end()), s3, nullptr),
              (std::string("（") + label + " 往返）重新导入").c_str());
        const auto bytes = ExportCad(s3, kind);
        Check(!bytes.empty(), (std::string("（") + label + " 往返）导出成功").c_str());
        Scene back;
        const bool backOk = !bytes.empty() && ImportCad(bytes, back, nullptr);
        Check(backOk, (std::string("（") + label + " 往返）导出后再导入").c_str());
        if (!backOk) continue;

        const InsertEntity* bi = nullptr;
        back.ForEachObject([&](const Object& o)
        {
            auto* i = dynamic_cast<const InsertEntity*>(&o);
            if (i && i->GetBlock() && i->GetBlock()->GetName() == "_AXISO") bi = i;
        });
        const size_t n = bi ? bi->GetAttribs().size() : 0;
        std::printf("    %s 往返后 ATTRIB = %zu，锚点 = (%.3f, %.3f)\n", label, n,
                    n ? bi->GetAttribs()[0]->GetPosition().x : 0.0,
                    n ? bi->GetAttribs()[0]->GetPosition().y : 0.0);
        Check(n == 1, (std::string("**") + label + " 导出再导入后 ATTRIB 仍在（与导入成对，否则另存即丢）**").c_str());
        if (n == 1)
        {
            const Math::Point3 bp = bi->GetAttribs()[0]->GetPosition();
            Check(std::abs(bp.x - 1000.0) < 1e-6 && std::abs(bp.y - 2000.0) < 1e-6,
                  (std::string(label) + " 往返后 ATTRIB 锚点不变（仍是 10↔11 的中点）").c_str());
            Check(bi->GetAttribs()[0]->GetHAlign() == 1,
                  (std::string(label) + " 往返后对齐码（居中）保留").c_str());
            Check(bi->GetAttribs()[0]->GetText() == "7",
                  (std::string(label) + " 往返后属性取值保留（「7」）").c_str());
        }
    }

    std::printf("轴号用例失败：%d\n", g_fail);
    return g_fail;
}
