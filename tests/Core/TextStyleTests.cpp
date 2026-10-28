// ── 文字样式测试：样式表规则、场景序列化、使用统计、宽度因子 / 倾斜角作用于字形、TrueType 单点轮廓、\U+XXXX 转义、下划线 ─────────
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Document/DrawContext.hpp"
#include "Scene/Scene.h"
#include "Scene/TextStyleTable.h"
#include "Serialization/JsonSerializer.h"
#include "Text/Font/FontEngine.h"
#include "Text/Font/TTFFont.h"
#include "Text/Layout/TextLayoutEngine.h"
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

    // ── 4b. 镜像的块参照里（ToggleTextMirror）文字沿基线上下翻转，下划线跟着到基线上方 ─────────
    {
        Scene s;
        StrokeFont font;
        std::vector<Vertex_P3_C4> lines, fills;
        std::vector<Vertex_P3_C4_UV> tex;
        Overlay ov;
        FontResolver resolver = [&](uint32_t) { return ResolvedTextStyle{ &font, 1.0, 0.0 }; };
        DrawContext ctx(lines, fills, tex, ov, s.GetLayerManager(), s.GetLineTypeTable(), 0.0, nullptr, resolver);
        ctx.ToggleTextMirror();
        ctx.EmitMText({ 0, 0, 0 }, std::string(1, kUnderlineToggle) + "a", 0, 10.0, 0.0, 0.0, Math::Color4::White());
        ctx.ToggleTextMirror();
        ctx.EmitMText({ 100, 0, 0 }, "a", 0, 10.0, 0.0, 0.0, Math::Color4::White());

        // 竖笔画 (0,0)-(0,-10)，下划线 (0,2)-(10,2)；翻回后的字照常 (100,0)-(100,10)
        const bool ok = lines.size() == 6 && Near(lines[1].pos.y, -10.0) && Near(lines[2].pos.y, 2.0) && Near(lines[3].pos.x, 10.0)
                     && Near(lines[4].pos.x, 100.0) && Near(lines[5].pos.y, 10.0);
        Check(ok, "镜像：字形与下划线沿基线上下翻转，翻回后照常");
    }

    // ── 4c. 旋转的多行文字：附着点的偏移随文字旋转。管综.dwg 车位块里转 180° 的 "A"（左上附着）
    //        以前首行基线落在插入点下方一个字高，画到了 "电动车" 上，差出两个字高 ─────────
    {
        struct PosSink : IDrawSink
        {
            Math::Point3 pos{};
            double rotation = 0.0;
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
            void EmitMText(const Math::Point3& p, const std::string&, uint32_t, double, double r, double, const Math::Color4&) override { pos = p; rotation = r; }
            Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
            bool IsLayerVisible(LayerID) const override { return true; }
        };
        const double pi = 3.14159265358979323846;
        PosSink sink;
        MTextEntity a(1, 0, "A", Math::Point3(100, 200, 0), 10.0, pi, 0.0);   // 左上附着
        a.Draw(sink, false, false);
        const bool flipped = Near(sink.pos.x, 100.0) && Near(sink.pos.y, 210.0);
        const AABB box = a.GetBoundingBox();
        MTextEntity b(2, 0, "A", Math::Point3(100, 200, 0), 10.0, pi / 2, 0.0);
        b.SetAttachment(MTextAttachment::BottomLeft);
        b.Draw(sink, false, false);
        const bool quarter = Near(sink.pos.x, 100.0) && Near(sink.pos.y, 200.0);   // 单行左下附着：原点就是插入点
        Check(flipped && quarter && box.Min.y > 199.9 && box.Max.y < 220.1,
              "旋转的多行文字：附着点偏移随文字旋转，转 180° 的左上附着画在插入点上方");
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

    // ── 8. \U+XXXX（代码页外字符）：管综.dwg 的 MTEXT 存成 "\L\U+8D1F\U+4E00..."，
    //       以前去格式码时把 "\U" 当未知转义丢掉，画出 "+8D1F+4E00..." ───────────
    {
        Check(MTextEntity::StripInlineCodes("\\L\\U+8D1F\\u+4e00层\\P\\U+00B0") == "负一层\n°",
              "多行文字：\\U+XXXX（大小写均可）还原为字符，与其他格式码混用");
        Check(MTextEntity::StripInlineCodes("\\U+12G4") == "+12G4", "多行文字：不足 4 位十六进制的 \\U 按未知转义处理");

        TextLayoutEngine engine;
        const std::vector<uint32_t> cps = engine.DecodeLine("A\\U+8D1FB%%d\\U+12");
        Check(cps == std::vector<uint32_t>{ 'A', 0x8D1F, 'B', kRawShapeFlag | 0x00B0u, '\\', 'U', '+', '1', '2' },
              "单行文字：\\U+XXXX 还原为码点，%% 控制码照旧，不完整的转义原样显示");
    }

    // ── 9. 下划线 / 上划线 / 删除线：管综.dwg 的图名是 "\L\U+8D1F..."，以前 \L 直接丢掉。
    //       位置按 AutoCAD 2020 TXTEXP 分解量得：基线下 0.2、上 1.2、上 0.5 倍字高 ─────────
    {
        const std::string U(1, kUnderlineToggle), O(1, kOverlineToggle), K(1, kStrikeToggle);
        Check(MTextEntity::StripInlineCodes("{\\LAB}C", true) == U + "AB" + U + "C" && MTextEntity::StripInlineCodes("{\\LAB}C") == "ABC",
              "多行文字：\\L 在绘制用的文本里换成开关，分组结束时关掉；纯文本不含开关");
        Check(MTextEntity::StripInlineCodes("\\L{\\lA}B\\OC\\o\\KD", true) == "A" + U + "B" + O + "C" + O + K + "D",
              "多行文字：组内关掉的下划线出组后恢复；上划线、删除线各自开关");

        // 每个字符步进 1 个字体单位、字高 10 ⇒ 每字宽 10
        struct FixedFont : IFont
        {
            Glyph empty;
            const Glyph& GetGlyph(uint32_t) override { return empty; }
            double GetAdvance(uint32_t) override { return 1.0; }
            double GetHeight() const override { return 1.0; }
            uint64_t GetFontId() const override { return 1; }
            const char* GetName() const override { return "fixed"; }
            bool HasGlyph(uint32_t) override { return true; }
        } font;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
        auto seg = [&](const TextLayoutEngine::DecorationLine& d, double x0, double x1, double y) { return near(d.m_x0, x0) && near(d.m_x1, x1) && near(d.m_y, y); };

        TextLayoutEngine engine;
        auto r = engine.Layout(U + "AB" + U + "C", &font, 10.0, 1.0, 0.0, 0.0);
        Check(r.m_glyphs.size() == 3 && r.m_decorations.size() == 1 && seg(r.m_decorations[0], 0.0, 20.0, -2.0),
              "排版：下划线从开处画到关处，在基线下 0.2 倍字高，开关不占宽度");

        r = engine.Layout(U + "A\nBC", &font, 10.0, 1.0, 0.0, 0.0);
        Check(r.m_decorations.size() == 2 && seg(r.m_decorations[0], 0.0, 10.0, -2.0) && seg(r.m_decorations[1], 0.0, 20.0, -12.0),
              "排版：没关的下划线在行尾收口、下一行续上");

        r = engine.Layout("%%oAB%%o" + K + "C", &font, 10.0, 1.0, 0.0, 0.0, TextLayoutEngine::HAlign::Right);
        Check(r.m_glyphs.size() == 3 && r.m_decorations.size() == 2 && seg(r.m_decorations[0], -30.0, -10.0, 12.0) && seg(r.m_decorations[1], -10.0, 0.0, 5.0),
              "排版：单行文字的 %%o 上划线在基线上 1.2 倍字高，删除线 0.5 倍；右对齐时开关不计入行宽");
    }

#ifdef MINICAD_ASSET_DIR
    // ── 10. TrueType 缺字：管综.dwg 的 Arial_5（arial.ttf）样式写中文，以前画成 .notdef 方块；
    //        AutoCAD 经 Windows 字体链接照样显示汉字。现在缺的字用自带的 GB2312.ttf 补上 ─────────
    {
        const std::string dir = std::string(MINICAD_ASSET_DIR) + "/fonts/";
        const std::vector<uint8_t> bytes = MakeSinglePointFont();   // 只有 'A'
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "minicad_latin_only.ttf";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const std::u8string u8 = path.u8string();
        const std::string file(u8.begin(), u8.end());

        TTFFont alone("latin", file, 200);
        FontEngine engine;
        engine.SetFontDir(dir);
        FontStyle latin;
        latin.name = "Arial"; latin.fontFile = file; latin.isShx = false;
        IFont& font = engine.Resolve(latin);
        TTFFont gb("gb", dir + "GB2312.ttf", 201);
        const Glyph& fallback = font.GetGlyph(0x8D1F);
        const bool ok = alone.HasGlyph('A') && !alone.HasGlyph(0x8D1F) && font.HasGlyph('A') && font.HasGlyph(0x8D1F)
                     && !fallback.Lines.empty() && fallback.Lines.size() == gb.GetGlyph(0x8D1F).Lines.size()
                     && Near(font.GetAdvance(0x8D1F), gb.GetAdvance(0x8D1F)) && Near(font.GetAdvance('A'), alone.GetAdvance('A'));
        std::error_code ec;
        std::filesystem::remove(path, ec);
        Check(ok, "TrueType 缺字（西文字体里的汉字）用自带 GB2312.ttf 的字形与步进，本字体有的字照旧");
    }
#endif

    return g_failures;
}
