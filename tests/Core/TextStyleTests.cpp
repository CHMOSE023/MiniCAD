// ── 文字样式测试：样式表规则、场景序列化、使用统计、宽度因子 / 倾斜角作用于字形、TrueType 单点轮廓 ─────────
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Document/DrawContext.hpp"
#include "Scene/Scene.h"
#include "Scene/TextStyleTable.h"
#include "Serialization/JsonSerializer.h"
#include "Text/Font/FontEngine.h"
#include "Text/Font/TTFFont.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

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
        const Glyph& GetGlyph(uint32_t) override
        {
            static const Glyph g = [] {
                Glyph s;
                s.Lines.push_back(Line{ { 0, 0, 0 }, { 0, 1, 0 } });
                s.Advance = 1.0;
                return s;
            }();
            return g;
        }
        double      GetAdvance(uint32_t) override { return 1.0; }
        double      GetHeight() const override    { return 1.0; }
        uint64_t    GetFontId() const override    { return 9999; }
        const char* GetName() const override      { return "stroke"; }
        bool        HasGlyph(uint32_t) override   { return true; }
    };

    bool Has(const std::vector<TextStyleID>& v, TextStyleID id) { return std::find(v.begin(), v.end(), id) != v.end(); }

    // 最小的 TrueType 字体：'A' 对应 1 号字形，它只有一个轮廓、轮廓只有一个曲线外的点 (100, 200)（宋体的空格就是这样）。
    // ascent 800、descent -200，没有 'H'，归一化比例为 1/1000
    std::vector<uint8_t> MakeSinglePointFont()
    {
        auto u16 = [](std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); };
        auto u32 = [&](std::vector<uint8_t>& v, uint32_t x) { u16(v, x >> 16); u16(v, x & 0xFFFF); };

        std::vector<uint8_t> head(54, 0);           // indexToLocFormat（偏移 50）为 0：短格式 loca
        head[18] = 0x03; head[19] = 0xE8;           // unitsPerEm 1000
        std::vector<uint8_t> hhea(36, 0);
        hhea[4] = 0x03; hhea[5] = 0x20;             // ascent 800
        hhea[6] = 0xFF; hhea[7] = 0x38;             // descent -200
        hhea[35] = 2;                               // numberOfHMetrics
        std::vector<uint8_t> maxp; u32(maxp, 0x00005000); u16(maxp, 2);
        std::vector<uint8_t> hmtx; u16(hmtx, 500); u16(hmtx, 0); u16(hmtx, 500); u16(hmtx, 0);
        std::vector<uint8_t> glyf;
        u16(glyf, 1); u16(glyf, 100); u16(glyf, 200); u16(glyf, 100); u16(glyf, 200);  // 轮廓数、外包框
        u16(glyf, 0);                               // 轮廓最后一个点的编号
        u16(glyf, 0);                               // 指令长度
        glyf.push_back(0x00);                       // 标志：曲线外的点，x / y 为 16 位增量
        u16(glyf, 100); u16(glyf, 200);
        glyf.push_back(0);                          // 补齐到偶数
        std::vector<uint8_t> loca; u16(loca, 0); u16(loca, 0); u16(loca, static_cast<uint32_t>(glyf.size() / 2));   // 0 号字形为空
        std::vector<uint8_t> cmap;
        u16(cmap, 0); u16(cmap, 1);                 // 版本、编码表数
        u16(cmap, 3); u16(cmap, 1); u32(cmap, 12);  // Windows Unicode BMP，子表在偏移 12
        u16(cmap, 6); u16(cmap, 12); u16(cmap, 0);  // 格式 6、长度、语言
        u16(cmap, 'A'); u16(cmap, 1); u16(cmap, 1); // 起始码位、个数、字形编号

        const std::pair<const char*, const std::vector<uint8_t>*> tables[] =
            { { "cmap", &cmap }, { "glyf", &glyf }, { "head", &head }, { "hhea", &hhea }, { "hmtx", &hmtx }, { "loca", &loca }, { "maxp", &maxp } };
        std::vector<uint8_t> font;
        u32(font, 0x00010000); u16(font, 7); u16(font, 0); u16(font, 0); u16(font, 0);
        uint32_t offset = 12 + 16 * 7;
        for (const auto& [tag, data] : tables)
        {
            font.insert(font.end(), tag, tag + 4);
            u32(font, 0); u32(font, offset); u32(font, static_cast<uint32_t>(data->size()));
            offset += (static_cast<uint32_t>(data->size()) + 3) & ~3u;
        }
        for (const auto& [tag, data] : tables)
        {
            font.insert(font.end(), data->begin(), data->end());
            font.resize((font.size() + 3) & ~size_t(3), 0);
        }
        return font;
    }
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

#ifdef MINICAD_ASSET_DIR
    // ── 5. SHX 形名不当作画笔指令；缺失字体用自带字体替代 ─────────────
    {
        const std::string dir = std::string(MINICAD_ASSET_DIR) + "/fonts/";
        SHXFont simplex("simplex", dir + "simplex.shx", 1);
        const Glyph eq = simplex.GetGlyph('=');
        bool bars = eq.Lines.size() == 2;
        for (const auto& l : eq.Lines) bars = bars && Near(l.Start.y, l.End.y);
        Check(bars, "simplex.shx 的“=”（形名 EQUALS SIGN）画成两条横线");

        FontEngine engine;
        engine.SetFontDir(dir);
        FontStyle missing;
        missing.name = "COMPLEX+GBCBIG"; missing.fontFile = "COMPLEX.shx"; missing.bigFontFile = "GBCBIG.shx"; missing.isShx = true;
        IFont& font = engine.Resolve(missing);
        FontStyle upper;
        upper.name = "SIMPLEX"; upper.fontFile = "SIMPLEX.SHX"; upper.isShx = true;
        Check(font.HasGlyph('1') && font.HasGlyph(0x4e0a) && engine.Resolve(upper).HasGlyph('='),
              "缺失的 complex / gbcbig 换成 simplex / TSSDCHN，数字与汉字都能显示；字体名大小写不同也能找到");
    }

    // ── 6. TrueType 字形填充：三角形覆盖的面积等于按非零环绕规则采样的面积（汉字笔画轮廓互相重叠），
    //       三角形数与轮廓边数同一量级（不再每个顶点高度横切一刀）─────────────
    {
        TTFFont ttf("gb2312", std::string(MINICAD_ASSET_DIR) + "/fonts/GB2312.ttf", 3);
        bool ok = true;
        for (uint32_t cp : { 0x56feu, 0x5efau, uint32_t('H'), uint32_t('8') })
        {
            const Glyph& gl = ttf.GetGlyph(cp);
            double filled = 0.0;
            for (const auto& t : gl.Triangles)
                filled += 0.5 * std::abs((t.b.x - t.a.x) * (t.c.y - t.a.y) - (t.c.x - t.a.x) * (t.b.y - t.a.y));

            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (const auto& l : gl.Lines)
            {
                x0 = std::min({ x0, l.Start.x, l.End.x }); x1 = std::max({ x1, l.Start.x, l.End.x });
                y0 = std::min({ y0, l.Start.y, l.End.y }); y1 = std::max({ y1, l.Start.y, l.End.y });
            }
            constexpr int N = 400;
            long inside = 0;
            for (int iy = 0; iy < N; ++iy)
            {
                const double py = y0 + (y1 - y0) * (iy + 0.5) / N;
                for (int ix = 0; ix < N; ++ix)
                {
                    const double px = x0 + (x1 - x0) * (ix + 0.5) / N;
                    int winding = 0;
                    for (const auto& l : gl.Lines)
                        if ((l.Start.y <= py) != (l.End.y <= py)
                            && l.Start.x + (py - l.Start.y) / (l.End.y - l.Start.y) * (l.End.x - l.Start.x) < px)
                            winding += l.End.y > l.Start.y ? 1 : -1;
                    inside += winding != 0;
                }
            }
            const double sampled = (x1 - x0) * (y1 - y0) * inside / (double(N) * N);
            ok = ok && gl.Filled && sampled > 0 && std::abs(filled - sampled) < sampled * 0.01
                    && gl.Triangles.size() < gl.Lines.size() * 3;
        }
        Check(ok, "TTF 字形三角形面积与非零环绕规则采样面积一致，三角形数少于轮廓边数的 3 倍");
    }
#endif

    // ── 7. TrueType 只有一个曲线外的点的轮廓（宋体的空格）：stb_truetype 以前照样去读"下一个点"，
    //       越界读到未初始化的堆内存，字形的起点随机——布局页上空格画成一串长斜线，每次运行都不同 ──
    {
        const std::vector<uint8_t> bytes = MakeSinglePointFont();
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "minicad_single_point.ttf";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const std::u8string u8 = path.u8string();
        bool ok = true;
        size_t lines = 0;
        for (int round = 0; round < 32 && ok; ++round)
        {
            TTFFont ttf("single-point", std::string(u8.begin(), u8.end()), 100 + round);
            {
                // 让堆里刚释放的小块（3 个 stbtt_vertex，42 字节）都是非零字节：修复前越界读到的就是它们
                std::vector<std::unique_ptr<uint8_t[]>> blocks;
                for (int i = 0; i < 64; ++i)
                {
                    blocks.emplace_back(new uint8_t[42]);
                    std::memset(blocks.back().get(), 0x5A, 42);
                }
            }
            const Glyph& g = ttf.GetGlyph('A');
            lines = std::max(lines, g.Lines.size());
            ok = ttf.IsLoaded() && g.Lines.size() <= 1;
            for (const auto& l : g.Lines)
                ok = ok && Near(l.Start.x, 0.1) && Near(l.Start.y, 0.2) && Near(l.End.x, 0.1) && Near(l.End.y, 0.2);
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::printf("    单点轮廓的字形：最多 %zu 条线段\n", lines);
        Check(ok, "只有一个曲线外的点的轮廓：字形只剩这个点上的零长度线段，不再越界读出随机的起点");
    }

    return g_failures;
}
