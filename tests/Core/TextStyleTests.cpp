// ── 文字样式测试：样式表规则、场景序列化、使用统计、宽度因子 / 倾斜角作用于字形 ─────────
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Document/DrawContext.hpp"
#include "Scene/Scene.h"
#include "Scene/TextStyleTable.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

    bool Near(double a, double b, double eps = 1e-4) { return std::abs(a - b) <= eps; }

    // 每个字符都是一条竖线 (0,0)-(0,1)，字高 1、步进 1
    class StrokeFont : public IFont
    {
    public:
        Glyph GetGlyph(uint32_t) override
        {
            Glyph g;
            g.Lines.push_back(Line{ { 0, 0, 0 }, { 0, 1, 0 } });
            g.Advance = 1.0;
            return g;
        }
        double      GetAdvance(uint32_t) override { return 1.0; }
        double      GetHeight() const override    { return 1.0; }
        uint64_t    GetFontId() const override    { return 9999; }
        const char* GetName() const override      { return "stroke"; }
        bool        HasGlyph(uint32_t) override   { return true; }
    };

    bool Has(const std::vector<TextStyleID>& v, TextStyleID id) { return std::find(v.begin(), v.end(), id) != v.end(); }
}

int RunTextStyleTests()
{
    g_failures = 0;
    std::printf("── 文字样式 ──\n");

    // ── 1. 样式表规则 ───────────────────────────────────────────
    {
        TextStyleTable t;
        Check(t.Records().size() == 3 && t.Find(0)->Name == "Standard" && t.Find(1)->Name == "Simplex" && t.Find(2)->Name == "GB2312",
              "预置 Standard / Simplex / GB2312，编号与旧版本一致");
        Check(t.Find(0)->IsShx() && t.Find(0)->BigFontFile == "TSSDCHN.SHX" && !t.Find(2)->IsShx(),
              "Standard 为 SHX + 大字体，GB2312 为 TrueType");

        TextStyleRecord r; r.Name = "机械"; r.FontFile = "simplex.shx";
        const TextStyleID a = t.Add(r);
        r.Name = "建筑";
        const TextStyleID b = t.Add(r);
        r.Name = "STANDARD";
        Check(a == 3 && b == 4 && t.Add(r) == TextStyleTable::InvalidID, "新增样式分配新 ID；名称不区分大小写，重名拒绝");
        r.Name = "   ";
        Check(t.Add(r) == TextStyleTable::InvalidID, "空名称拒绝");

        Check(t.Remove(a) && t.Find(b) && t.Find(b)->Name == "建筑", "删除后其余样式 ID 不变");
        r.Name = "新样式";
        Check(t.Add(r) == 5, "删除后新 ID 不复用旧编号");
        Check(!t.Remove(TextStyleTable::StandardID), "Standard 不能删除");

        Check(!t.Rename(0, "X") && !t.Rename(b, "simplex") && t.Rename(b, "建筑2") && t.Find(b)->Name == "建筑2",
              "改名：Standard 不能改，不能与其他样式重名");

        TextStyleRecord u = *t.Find(b);
        u.WidthFactor = -1.0; u.ObliqueDeg = 120.0; u.Height = -3.0;
        t.Update(b, u);
        Check(t.Find(b)->WidthFactor == 1.0 && t.Find(b)->ObliqueDeg == 85.0 && t.Find(b)->Height == 0.0, "参数越界时修正");
        Check(t.Resolve(12345).Name == "Standard", "找不到的 ID 按 Standard 解析");
    }

    // ── 2. 场景序列化 ───────────────────────────────────────────
    {
        Scene a;
        TextStyleRecord r; r.Name = "说明"; r.FontFile = "GB2312.ttf"; r.WidthFactor = 0.7; r.ObliqueDeg = 15.0; r.Height = 3.5;
        const TextStyleID id = a.GetTextStyleTable().Add(r);
        a.SetCurrentTextStyle(id);
        a.AddEntity(std::make_unique<TextEntity>(a.NextObjectID(), Math::Point3(0, 0, 0), "abc", 2.5f, 0.0f, id));

        JsonSerializer w;
        a.Serialize(w);
        JsonSerializer rd;
        Scene b;
        const bool parsed = rd.Parse(w.Dump());
        if (parsed) b.Deserialize(rd);
        const TextStyleRecord* got = b.GetTextStyleTable().Find(id);
        bool textOk = false;
        b.ForEachObject([&](const Object& o) { if (o.IsKindOf<TextEntity>()) textOk = static_cast<const TextEntity&>(o).GetStyleId() == id; });
        Check(parsed && got && got->Name == "说明" && got->WidthFactor == 0.7 && got->ObliqueDeg == 15.0 && got->Height == 3.5
                     && b.GetCurrentTextStyle() == id && textOk,
              "样式表、当前样式、文字的样式 ID 随文档往返");

        TextStyleRecord r2; r2.Name = "再加一个"; r2.FontFile = "simplex.shx";
        Check(b.GetTextStyleTable().Add(r2) == id + 1, "读入后继续分配不冲突的 ID");

        JsonSerializer old;          // 旧文件：没有 textStyles / currentTextStyle
        Scene c;
        c.GetTextStyleTable().Add(r2);
        c.SetCurrentTextStyle(3);
        if (old.Parse("{\"entities\": []}"))
            c.Deserialize(old);
        Check(c.GetTextStyleTable().Records().size() == 3 && c.GetCurrentTextStyle() == TextStyleTable::StandardID,
              "旧文件读入：样式表恢复为预置样式，当前样式为 Standard");
    }

    // ── 3. 使用统计 ─────────────────────────────────────────────
    {
        Scene s;
        s.AddEntity(std::make_unique<TextEntity>(s.NextObjectID(), Math::Point3(0, 0, 0), "a", 2.5f, 0.0f, 4));
        s.AddEntity(std::make_unique<MTextEntity>(s.NextObjectID(), 1, "b", Math::Point3(0, 0, 0)));
        DimStyle ds; ds.TextStyle = 2;
        s.AddEntity(DimensionEntity::MakeAligned(s.NextObjectID(), { 0, 0, 0 }, { 10, 0, 0 }, { 5, 5, 0 }, ds));
        const auto used = s.CollectUsedTextStyles();
        Check(Has(used, 4) && Has(used, 1) && Has(used, 2) && !Has(used, 0), "统计到文字、多行文字、标注各自使用的样式");
    }

    // ── 4. 宽度因子与倾斜角作用于字形 ──────────────────────────────
    {
        Scene s;
        StrokeFont font;
        std::vector<Vertex_P3_C4> lines, fills;
        std::vector<Vertex_P3_C4_UV> tex;
        Overlay ov;
        FontResolver resolver = [&](uint32_t) { return ResolvedTextStyle{ &font, 2.0, std::atan(1.0) }; };   // 宽度 2，倾斜 45°
        DrawContext ctx(lines, fills, tex, ov, s.GetLayerManager(), s.GetLineTypeTable(), 0.0, nullptr, resolver);
        ctx.EmitMText({ 0, 0, 0 }, "ab", 0, 10.0, 0.0, 0.0, Math::Color4::White());

        bool ok = lines.size() == 4;
        if (ok)
        {
            const auto& a0 = lines[0].pos; const auto& a1 = lines[1].pos;
            const auto& b0 = lines[2].pos;
            ok = Near(a1.x - a0.x, 10.0) && Near(a1.y - a0.y, 10.0)      // 竖线倾斜 45°：顶端右移一个字高
              && Near(b0.x - a0.x, 20.0);                                 // 字距 = 步进 × 字高 × 宽度因子
        }
        Check(ok, "宽度因子 2：字距加倍；倾斜 45°：竖笔画顶端右移一个字高");
    }

    return g_failures;
}
