// ── 文字样式测试：样式表规则、场景序列化、使用统计、宽度因子 / 倾斜角作用于字形、TrueType 单点轮廓、\U+XXXX 转义、下划线 ─────────
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/DrawContext.hpp"
#include "Editor/Picking/Picking.h"
#include "Scene/Scene.h"
#include "Scene/TextStyleTable.h"
#include "Serialization/JsonSerializer.h"
#include "Text/Font/FontEngine.h"
#include "Text/Font/TTFFont.h"
#include "Text/Layout/TextLayoutEngine.h"
#include "Viewport/Viewport.h"
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

    // ── 4c. 旋转的多行文字：附着点的偏移随文字旋转。管综样图 车位块里转 180° 的 "A"（左上附着）
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

    // ── 4d. 不限宽的居中 / 右对齐多行文字按实际排版宽度定起点（天正表格的居中文字以前按估算宽度整行偏左）─────────
    {
        struct MeasureSink : IDrawSink
        {
            Math::Point3 pos{};
            double measuredFactor = 0.0;
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
            bool MeasureRichMText(const MTextBlock& b, double& w, double& h) override { measuredFactor = b.widthFactor; w = 2.0 * b.height; h = b.height; return true; }
            void EmitMText(const Math::Point3& p, const std::string&, uint32_t, double, double, double, const Math::Color4&) override { pos = p; }
            Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
            bool IsLayerVisible(LayerID) const override { return true; }
        };
        MeasureSink sink;
        MTextEntity center(1, 0, "ABCDEF", Math::Point3(100, 200, 0), 10.0, 0.0, 0.0);
        center.SetAttachment(MTextAttachment::BottomCenter);
        center.SetWidthFactor(0.7);
        center.Draw(sink, false, false);
        const bool centered = Near(sink.pos.x, 90.0) && Near(sink.measuredFactor, 0.7);    // 量得宽 20：起点在插入点左 10，量时已按自身宽度因子
        MTextEntity right(2, 0, "ABCDEF", Math::Point3(100, 200, 0), 10.0, 0.0, 0.0);
        right.SetAttachment(MTextAttachment::BottomRight);
        right.Draw(sink, false, false);
        const bool flush = Near(sink.pos.x, 80.0);
        MTextEntity boxed(3, 0, "ABCDEF", Math::Point3(100, 200, 0), 10.0, 0.0, 50.0);    // 有框宽：仍按框宽对齐
        boxed.SetAttachment(MTextAttachment::BottomCenter);
        boxed.Draw(sink, false, false);
        Check(centered && flush && Near(sink.pos.x, 75.0), "不限宽的居中 / 右对齐多行文字按量得的宽度定起点，有框宽时按框宽");
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

    // ── 8. \U+XXXX（代码页外字符）：管综样图的 MTEXT 存成 "\L\U+8D1F\U+4E00..."，
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

    // ── 9. 下划线 / 上划线 / 删除线：管综样图的图名是 "\L\U+8D1F..."，以前 \L 直接丢掉。
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

        // ── 9c. 带格式的多行文字排版（LayoutRich）─────────
        auto rich = [&](const std::string& text, TextLayoutEngine::RichOptions o, const std::vector<double>& widths = {})
        {
            const MTextContent c = ParseMTextFormat(text, o.height);
            std::vector<TextLayoutEngine::RunStyle> styles(c.runs.size());
            for (size_t k = 0; k < styles.size(); ++k)
                styles[k] = { &font, k < widths.size() ? widths[k] : 1.0, 0.0 };
            return engine.LayoutRich(c, styles, o);
        };
        TextLayoutEngine::RichOptions o;
        o.height = 10.0;
        r = rich("A\\PB\\H2x;C", o);
        Check(r.m_glyphs.size() == 3 && near(r.m_glyphs[0].m_position.y, 0.0) && near(r.m_glyphs[1].m_position.y, -80.0 / 3.0) &&
              near(r.m_glyphs[2].m_scale, 20.0) && near(r.m_height, 10.0 + 80.0 / 3.0),
              "带格式排版：行距\"至少\" = 上一行下沉（字高 / 3）+ 本行上升（最高的字 20）+ 字高 / 3，\\H2x; 的字两倍高");
        o.exact = true;
        r = rich("A\\PB\\H2x;C", o);
        Check(near(r.m_glyphs[1].m_position.y, -50.0 / 3.0), "带格式排版：精确行距按基准字高，不随大字增大");
        o.exact = false;
        r = rich("{\\H2x;A}\\PB", o);
        Check(near(r.m_glyphs[0].m_position.y, -10.0) && near(r.m_glyphs[1].m_position.y, -30.0),
              "带格式排版：首行有大字时整体下移，字顶仍在原来的首行顶；大字行下沉也大（20 / 3）");
        o.lineSpacing = 1.5;
        r = rich("A\\P{\\H1.5x;B}\\PC", o);
        Check(near(r.m_glyphs[1].m_position.y, -30.0) && near(r.m_glyphs[2].m_position.y, -60.0),
              "带格式排版：行距比例 1.5 时混合字高的行距与 AutoCAD 炸开量得的一致（30、30）");
        o.lineSpacing = 1.0;
        {
            struct TallFont : FixedFont { Glyph tall; TallFont() { tall.Filled = true; tall.MaxY = 1.2; } const Glyph& GetGlyph(uint32_t) override { return tall; } } tallFont;
            const MTextContent c = ParseMTextFormat("A\\PB", 10.0);
            const auto t = engine.LayoutRich(c, std::vector<TextLayoutEngine::RunStyle>(c.runs.size(), { &tallFont, 1.0, 0.0 }), o);
            Check(t.m_glyphs.size() == 2 && near(t.m_glyphs[0].m_position.y, -2.0) && near(t.m_glyphs[1].m_position.y, -2.0 - 56.0 / 3.0),
                  "带格式排版：TrueType 字形顶高出字高时，行的上升按字形顶（12），下沉仍是字高 / 3");
        }
        r = rich("A\\S1/2;\\PB", o);
        Check(near(r.m_glyphs[0].m_position.y, -40.0 / 3.0) && near(r.m_glyphs[3].m_position.y, -80.0 / 3.0),
              "带格式排版：首行有分数时按分子顶定首行基线；有堆叠的行不算下沉");
        o.boxWidth = 50.0;
        r = rich("\\pxi-1,l1;AAA BBB", o);
        Check(r.m_glyphs.size() == 7 && near(r.m_glyphs[0].m_position.x, 0.0) && near(r.m_glyphs[4].m_position.x, 10.0) &&
              near(r.m_glyphs[4].m_position.y, -50.0 / 3.0),
              "带格式排版：悬挂缩进（\\pxi-1,l1;）首行顶格、折下来的行缩进 1 个字高，折在空格处");
        o.boxWidth = 0.0;
        o.justify = TextLayoutEngine::HAlign::Center;
        r = rich("A\\PBBB\\P\\pql;C", o);
        Check(near(r.m_glyphs[0].m_position.x, 10.0) && near(r.m_glyphs[1].m_position.x, 0.0) && near(r.m_glyphs[4].m_position.x, 0.0) &&
              near(r.m_width, 30.0),
              "带格式排版：居中附着的各行在块宽内居中，\\pql; 的段落靠左");
        o.justify = TextLayoutEngine::HAlign::Left;
        r = rich("1\\S2/3;", o);
        Check(r.m_glyphs.size() == 3 && near(r.m_glyphs[1].m_scale, 10.0) && near(r.m_glyphs[1].m_position.x, 10.0) &&
              near(r.m_glyphs[0].m_position.y, -40.0 / 3.0) && near(r.m_glyphs[1].m_position.y, 0.0) && near(r.m_glyphs[2].m_position.y, -40.0 / 3.0) &&
              r.m_decorations.size() == 1 && seg(r.m_decorations[0], 10.0, 20.0, -5.0 / 3.0),
              "带格式排版：分数 \\S2/3; 两半用当前字高，分母坐在基线上，分子基线高 4/3 字高，横线高 7/6 字高");
        r = rich("m\\S2^;", o);
        Check(r.m_glyphs.size() == 2 && near(r.m_glyphs[1].m_position.y - r.m_glyphs[0].m_position.y, 10.0 / 3.0),
              "带格式排版：上标 \\S2^; 抬高 1/3 字高");
        r = rich("\\S3#4;", o);
        Check(r.m_glyphs.size() == 2 && near(r.m_glyphs[0].m_position.x, 0.0) && near(r.m_glyphs[1].m_position.x, 10.0) &&
              near(r.m_glyphs[0].m_position.y - r.m_glyphs[1].m_position.y, 10.0) && r.m_decorations.size() == 1 &&
              near(r.m_decorations[0].m_rise, 16.4),
              "带格式排版：斜线分数 \\S3#4; 分子在左上、分母在右下，中间一条斜线");
        r = rich("\\A1;A\\S1/2;", o);
        Check(r.m_glyphs.size() == 3 && near(r.m_glyphs[1].m_position.y - r.m_glyphs[0].m_position.y, 20.0 / 3.0) &&
              near(r.m_glyphs[2].m_position.y - r.m_glyphs[0].m_position.y, -20.0 / 3.0) && near(r.m_decorations[0].m_y - r.m_glyphs[0].m_position.y, 5.0),
              "带格式排版：\\A1; 居中时分数横线对准行中线");
        r = rich("{\\W0.5;AB}C", o, { 0.5, 1.0 });
        Check(r.m_glyphs.size() == 3 && near(r.m_glyphs[1].m_position.x, 5.0) && near(r.m_glyphs[2].m_position.x, 10.0) &&
              near(r.m_glyphs[0].m_widthFactor, 0.5) && r.m_glyphs[2].m_run == 1,
              "带格式排版：各段按自己的宽度因子步进，字形记下所属的段");
    }

    // ── 9d. 多行文字格式码解析（以前 {\W0.7;…} 只认开头的 \W，\f \H \S 等整段丢掉）─────────
    {
        const MTextContent a = ParseMTextFormat("{\\fSimSun|b0|i0|c134|p2;\\W0.7;\\C1;AB}C", 10.0);
        Check(a.runs.size() == 2 && a.runs[0].text == "AB" && a.runs[0].font == 0 && Near(a.runs[0].widthFactor, 0.7) &&
              a.runs[0].colorKind == MTextRun::Color::Rgb && Near(a.runs[0].color.r, 1.0) && a.runs[1].text == "C" && a.runs[1].font == -1 &&
              a.runs[1].widthFactor == 0.0 && a.runs[1].colorKind == MTextRun::Color::Entity && a.fonts.size() == 1 && a.fonts[0].family == "SimSun",
              "格式码：组里的 \\f 族名、\\W、\\C 只作用到组内");
        const MTextContent b = ParseMTextFormat("\\H2.5;X\\H0.5x;Y\\Fsimplex8,hzdx|c134;Z\\c255;R\\C256;L", 10.0);
        Check(b.runs.size() == 5 && Near(b.runs[0].height, 0.25) && Near(b.runs[1].height, 0.125) && b.fonts.size() == 1 &&
              b.fonts[0].file == "simplex8" && b.fonts[0].bigFont == "hzdx" && b.runs[2].font == 0 && Near(b.runs[3].color.r, 1.0) &&
              Near(b.runs[3].color.b, 0.0) && b.runs[4].colorKind == MTextRun::Color::ByLayer,
              "格式码：\\H 绝对 / 相对字高，\\F 主字体与大字体，\\c 真彩低字节是红，\\C256 随层");
        const MTextContent c = ParseMTextFormat("m\\S2^;\\P\\pxi-3,l3;1.A", 10.0);
        Check(c.runs.size() == 4 && c.runs[1].kind == MTextRun::Kind::Stack && c.runs[1].text == "2" && c.runs[1].stackType == '^' &&
              c.runs[2].kind == MTextRun::Kind::Break && Near(c.runs[3].indentFirst, -3.0) && Near(c.runs[3].indentLeft, 3.0) &&
              c.PlainText() == "m2\n1.A",
              "格式码：\\S2^; 上标、\\px 段落缩进是字高的倍数");
        const MTextContent d = ParseMTextFormat("\\pi2;A{\\A1;B}C", 10.0);
        Check(d.runs.size() == 3 && Near(d.runs[0].indentFirst, 0.2) && d.runs[0].valign == 0 && d.runs[1].valign == 1 && d.runs[2].valign == 0,
              "格式码：不带 x 的 \\pi 是绝对长度；\\A 竖向对齐只作用到组内");
        Check(MTextEntity::StripInlineCodes("m\\S2^;") == "m2" && MTextEntity::StripInlineCodes("\\S1/2;") == "1/2",
              "纯文本：堆叠留下内容（以前 m\\S2^; 只剩 m）");
    }

    // ── 9e. 多行文字交给绘图接口：行距（issue #40：单倍是 5/3 个字高，再乘组码 44）、附着点定段落对齐、选中时不用段的颜色、按量得的高定下附着 ─────────
    {
        struct BlockSink : IDrawSink
        {
            MTextBlock   last;
            Math::Point3 pos{};
            double       measuredH = 0.0;
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
            void EmitRichMText(const Math::Point3& p, const MTextBlock& b, double, const Math::Color4&) override { last = b; pos = p; }
            bool MeasureRichMText(const MTextBlock&, double& w, double& h) override
            {
                if (measuredH <= 0.0) return false;
                w = 30.0; h = measuredH; return true;
            }
            Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
            bool IsLayerVisible(LayerID) const override { return true; }
        };
        BlockSink sink;
        MTextEntity single(1, 0, "A\\PB\\PC", Math::Point3(0, 0, 0), 10.0, 0.0, 0.0);
        single.Draw(sink, false, false);
        const bool byDefault = Near(sink.last.lineSpacing, 1.0) && sink.last.justify == 0 && sink.last.runColors && sink.last.plain == "A\nB\nC";
        const AABB box1 = single.GetBoundingBox();
        MTextEntity wide(2, 0, "A\\PB\\PC", Math::Point3(0, 0, 0), 10.0, 0.0, 0.0);
        wide.SetLineSpacing(MTextLineSpacing::AtLeast, 1.5);
        wide.SetAttachment(MTextAttachment::TopCenter);
        wide.Draw(sink, true, false);
        const AABB box2 = wide.GetBoundingBox();
        Check(byDefault && Near(sink.last.lineSpacing, 1.5) && sink.last.justify == 1 && !sink.last.runColors &&
              Near(box1.Max.y - box1.Min.y, 10.0 + 2 * 50.0 / 3.0) && Near(box2.Max.y - box2.Min.y, 60.0),
              "多行文字：行距 = 5/3 × 字高 × 行距系数，居中附着时段落居中，选中时整段同色；包围盒高 = 字高 + 其余各行行距");

        MTextEntity big(3, 0, "A\\P\\H2x;B", Math::Point3(0, 0, 0), 10.0, 0.0, 0.0);
        const AABB box3 = big.GetBoundingBox();
        MTextEntity bottom(4, 0, "A\\PB", Math::Point3(0, 0, 0), 10.0, 0.0, 0.0);
        bottom.SetAttachment(MTextAttachment::BottomLeft);
        sink.measuredH = 50.0;
        bottom.Draw(sink, false, false);
        Check(Near(box3.Max.y - box3.Min.y, 10.0 + 80.0 / 3.0) && Near(sink.pos.y, 40.0),
              "多行文字：包围盒按大字的行距估算；下附着按排版量得的高（50）定首行基线");
    }

    // ── 9f. DrawContext 画带格式的多行文字：内联字体经 InlineFontResolver，段的颜色（随层取图层色）─────────
    {
        Scene s;
        StrokeFont font;
        struct WideFont : StrokeFont { double GetAdvance(uint32_t) override { return 3.0; } uint64_t GetFontId() const override { return 9998; } } wide;
        std::vector<Vertex_P3_C4> lines, fills;
        std::vector<Vertex_P3_C4_UV> tex;
        Overlay ov;
        FontResolver resolver = [&](uint32_t) { return ResolvedTextStyle{ &font, 1.0, 0.0 }; };
        DrawContext ctx(lines, fills, tex, ov, s.GetLayerManager(), s.GetLineTypeTable(), 0.0, nullptr, resolver);
        std::string asked;
        ctx.SetInlineFontResolver([&](const MTextFont& f) -> IFont* { asked = f.family; return &wide; });
        MTextEntity e(1, 0, "{\\fArial|b0;\\C1;A}B{\\C256;C}", Math::Point3(0, 0, 0), 10.0, 0.0, 0.0);
        e.SetAttr([] { EntityAttr a; a.Color = EntityColor::FromAci(3); return a; }());
        e.Draw(ctx, false, false);
        bool ok = lines.size() == 6;
        if (ok)
        {
            ok = asked == "Arial" && Near(lines[2].pos.x, 30.0) && Near(lines[4].pos.x, 40.0);   // A 用宽字体步进 30
            const VertexColor red = lines[0].color, green = lines[2].color, layer = lines[4].color;
            ok = ok && red.r == 255 && red.g == 0 && green.g == 255 && green.r == 0 && layer.r == 255 && layer.g == 255 && layer.b == 255;
        }
        Check(ok, "DrawContext：\\f 字体经内联字体解析、按它步进；\\C1 红色、其余实体色（绿）、\\C256 取图层色");
    }

#ifdef MINICAD_ASSET_DIR
    // ── 10. TrueType 缺字：管综样图的 Arial_5（arial.ttf）样式写中文，以前画成 .notdef 方块；
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

    // ── 11. 繁体、日文、韩文：缺字回退按字体的文字找，缺失的字体换成同一种文字的字体，SHX 大字体缺的字改用 TrueType ─────────
    {
        const std::string dir = std::string(MINICAD_ASSET_DIR) + "/fonts/";
        Check(FontEngine::ScriptOfFont("mingliu.ttc") == CjkScript::Traditional && FontEngine::ScriptOfFont("標楷體.ttf") == CjkScript::Traditional
              && FontEngine::ScriptOfFont("ＭＳ ゴシック.ttf") == CjkScript::Japanese && FontEngine::ScriptOfFont("C:/Windows/Fonts/msgothic.ttc") == CjkScript::Japanese
              && FontEngine::ScriptOfFont("굴림.ttf") == CjkScript::Korean && FontEngine::ScriptOfFont("malgun.ttf") == CjkScript::Korean
              && FontEngine::ScriptOfFont("simsun.ttc") == CjkScript::Simplified && FontEngine::ScriptOfFont("arial.ttf") == CjkScript::Simplified,
              "按字体名认出繁体、日文、韩文字体");

        Check(SHXFont::DetectBigFontCodePage("chineset.shx", {}) == 950 && SHXFont::DetectBigFontCodePage("extfont2.shx", {}) == 932
              && SHXFont::DetectBigFontCodePage("whgtxt.shx", {}) == 949 && SHXFont::DetectBigFontCodePage("gbcbig.shx", { 0xB0A1, 0xD6D0 }) == 936
              && SHXFont::DetectBigFontCodePage("x.shx", { 0x889F, 0x9A40, 0xE040 }) == 932 && SHXFont::DetectBigFontCodePage("x.shx", { 0xA440, 0xC57B }) == 950,
              "大字体的代码页：按文件名，认不出时按形编码的区间");

        constexpr uint32_t kTi = 0x9AD4;     // 體：GB2312.ttf 里没有
        FontEngine engine;
        engine.SetFontDir(dir);
        FontStyle arial;
        arial.name = "Arial"; arial.fontFile = dir + "GB2312.ttf"; arial.isShx = false;
        IFont& gb = engine.Resolve(arial);
        FontStyle ming;
        ming.name = "TC"; ming.fontFile = "mingliu.ttc"; ming.isShx = false;
        IFont& tc = engine.Resolve(ming);
        Check(gb.HasGlyph(kTi) && !gb.GetGlyph(kTi).Lines.empty() && tc.HasGlyph(kTi) && !tc.GetGlyph(kTi).Lines.empty(),
              "GB2312.ttf 没有的繁体字从回退字体取；缺失的繁体字体换成同一种文字的字体");

        FontStyle shx;
        shx.name = "Standard"; shx.fontFile = "tssdeng.shx"; shx.bigFontFile = "TSSDCHN.SHX"; shx.isShx = true;
        IFont& big = engine.Resolve(shx);
        const Glyph& ti = big.GetGlyph(kTi);
        const Glyph& missing = big.GetGlyph(0x9F00);
        Check(!big.GetGlyph(0x4E2D).Lines.empty() && !ti.Lines.empty() && ti.Triangles.empty() && Near(big.GetAdvance(kTi), big.GetAdvance(0x4E2D))
              && !big.GetGlyph(0xD55C).Lines.empty() && !big.GetGlyph(0x3042).Lines.empty()
              && big.GetGlyph(0xC5B4).Lines.size() != big.GetGlyph(0x7CEF).Lines.size()   // 어 不是 GB 编码同为 C5B4 的"糯"
              && missing.Lines.size() == big.GetGlyph('?').Lines.size() && !missing.Lines.empty()
              && big.GetGlyph(kRawShapeFlag | 0x7FFF).Lines.empty(),
              "SHX 缺的繁体字、韩文、假名从单线字体 minicad_*.shx 取（不用 TrueType 填充字），都没有时画 ?；没有的形号不画");

        FontStyle simplex;
        simplex.name = "Simplex"; simplex.fontFile = "simplex.shx"; simplex.bigFontFile = "TSSDCHN.SHX"; simplex.isShx = true;
        FontStyle shapes;
        shapes.name = "simplex.shx"; shapes.fontFile = "simplex.shx"; shapes.isShx = true; shapes.shapesOnly = true;
        Check(!engine.Resolve(simplex).GetGlyph(kRawShapeFlag | 0x82).Lines.empty() && engine.Resolve(shapes).GetGlyph(kRawShapeFlag | 0x82).Lines.empty(),
              "钢筋符号 %%130：字体里没有时从单线字体取；线型的形文件缺形不回退");
    }
#endif

    {
        Scene scene;
        const auto id = scene.NextObjectID();
        auto mt = std::make_unique<MTextEntity>(id, 0, "AB", Math::Point3{ 0, 0, 0 }, 10.0, Math::PI / 2, 1000.0);
        mt->SetAttachment(MTextAttachment::TopRight);
        const AABB bb = mt->GetBoundingBox();
        Check(Near(bb.Min.x, 0.0) && Near(bb.Max.x, 10.0) && Near(bb.Min.y, -12.0) && Near(bb.Max.y, 0.0),
              "多行文字包围盒：参考框比文字宽很多、右上附着、旋转 90° 时只包住文字本身");
        scene.AddEntity(std::move(mt));

        Viewport viewport(800.f, 600.f);
        viewport.GetCamera().ZoomToBounds(-60, -60, 60, 60);
        Picking picking;
        picking.Bind(scene, viewport);
        auto at = [&](double x, double y) { return viewport.GetCamera().WorldToScreen({ x, y, 0 }); };
        Check(picking.HitTest(at(5, -6), 5.0) == id, "多行文字点选：点在文字上命中");
        Check(picking.HitTest(at(5, -40), 5.0) == Object::InvalidID, "多行文字点选：点在参考框里、文字外面不命中");
        const auto a = at(2, -3), b = at(8, -9);
        Check(picking.BoxSelect(b, a).count(id) == 1, "多行文字框选：交叉框整个落在文字里也选中");
    }

    {
        const std::string raw = "{\\fArial|b0|i0|c0|p34;12.50m}";
        Check(MTextEntity::StripInlineCodes(raw) == "12.50m", "原位编辑：编辑框显示去掉格式码的文字");
        Check(MTextEntity::ApplyPlainEdit(raw, "12.50m") == raw, "原位编辑：没改文字时原文不变");
        Check(MTextEntity::ApplyPlainEdit(raw, "13.60m") == "{\\fArial|b0|i0|c0|p34;13.60m}", "原位编辑：改了文字，外层的字体格式保留");
        Check(MTextEntity::ApplyPlainEdit(raw, "标高 12.50m") == "{\\fArial|b0|i0|c0|p34;标高 12.50m}", "原位编辑：在开头插入的文字落在格式组里");
        Check(MTextEntity::ApplyPlainEdit(raw, "12.50m\n{a}\\") == "{\\fArial|b0|i0|c0|p34;12.50m\\P\\{a\\}\\\\}",
              "原位编辑：换行写成 \\P，花括号、反斜杠转义");

        const std::string mixed = "{\\fA;ab}{\\H2x;cd}\\S1/2;";
        Check(MTextEntity::StripInlineCodes(mixed) == "abcd1/2", "原位编辑：多段格式与堆叠的去码文字");
        Check(MTextEntity::ApplyPlainEdit(mixed, "aXd1/2") == "{\\fA;aX}{\\H2x;d}\\S1/2;", "原位编辑：跨两个格式组改字，两组的格式码与括号都保留");
        Check(MTextEntity::ApplyPlainEdit(mixed, "abcd1/3") == "{\\fA;ab}{\\H2x;cd}1/3", "原位编辑：改到堆叠时整个堆叠换成新文字");
        Check(MTextEntity::ApplyPlainEdit("中文", "中央") == "中央" && MTextEntity::ApplyPlainEdit("\\U+4E2D文", "中央") == "\\U+4E2D央",
              "原位编辑：按整字替换，\\U+ 转义的字不拆开");
    }

    return g_failures;
}
