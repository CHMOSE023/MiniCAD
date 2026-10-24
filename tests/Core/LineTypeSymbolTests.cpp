// ── 复杂线型（带字母/符号）测试 ────────────────────────────────────────────
// 用户报：图元线型带字母或符号时（GAS_LINE、TRACKS、ZIGZAG、BATTING 这类
// AutoCAD complex linetype）显示不对。
//
// 根因（实测全部 DWG 版本 1014/1015/1018/1021/1024/1027/1032 一致）：
//   `CadExchange::ImportLineTypes` 里 `if (seg.Flags != None) continue;`
//   把文字/形段整个丢掉。DXF 复杂线型的段序列是
//       实线 → 文字 → 空白（GAS_LINE：0.5 / -0.2 / -0.25）
//   丢掉文字段后只剩 {0.5} ⇒ `IsContinuous()` 为真 ⇒ 整条线画成实线。
//   而且 PatternLength 也少了 0.2，周期对不上。
//
// 实测数据（ProbeLT 从 sample_*.dwg 导出）：
//   GAS_LINE  : 0.5 | -0.2(Text "GAS", S=0.1, X=-0.1, Y=-0.05) | -0.25   周期 0.95
//   TRACKS    : 0.15(Shape 130, S=0.25) | 0.15                     周期 0.30
//   ZIGZAG    : 0.0001 | -0.2(Shape 131, X=-0.2) | -0.4(Shape 131, rot=π, X=0.2) | -0.2
//   BATTING   : 0.0001 | -0.1(Shape 134, X=-0.1) | -0.2(Shape 134, rot=π, X=0.1) | -0.1
//
// 本文件守两条：
//   ① 导入后 Pattern 收下全部段、PatternLength = Σ|49|、Symbols 带对内容与下标
//   ② 渲染侧 EmitStyled 在符号位置铺出文字顶点（EmitLaidOutText 被调用）
#include "Scene/Scene.h"
#include "Scene/LineTypeTable.h"
#include "Scene/TextStyleTable.h"
#include "Document/DrawContext.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Text/Font/IFont.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
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
    bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

    // 每个字符都是一条竖线 (0,0)-(0,1)：字形自带，不需要外部字体文件。
    // 与 TextStyleTests 里的同名类等价（各测试文件独立编译，不共享）。
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
        double   GetAdvance(uint32_t) override { return 1.0; }
        double   GetHeight() const override    { return 1.0; }
        uint64_t GetFontId() const override    { return 9999; }
        const char* GetName() const override   { return "stroke"; }
        bool     HasGlyph(uint32_t) override   { return true; }
    };

    // 按码点返回不同线段的假形字体：用来断言「渲染真的按形号取了字形」。
    // 字形是**竖线** (0,0)-(0,len)：线画在 y=0 上时，实线段的顶点全是 y=0，
    // 符号顶点却分布在 ±高度/2 上 ⇒ 用 y 的极差就能把符号顶点从实线顶点里
    // 分离出来（横线字形会被实线顶点的 y=0 完全盖住，测不出尺寸）。
    // 与 StrokeFont 一样自带给形，不需要 ltypeshp.shx 在磁盘上。
    class ShapeFontStub : public IFont
    {
    public:
        const Glyph& GetGlyph(uint32_t cp) override
        {
            static std::unordered_map<uint32_t, Glyph> cache;
            auto it = cache.find(cp);
            if (it == cache.end())
            {
                Glyph g;
                // 字形：一个单位高的方框，左边在 x = 形号/1000。
                // 高度固定 1.0 ⇒ 断言可以直接用 height = Scale × 形高；
                // x 偏移随形号变化 ⇒ 仍能区分「取到了正确的形」。
                // 注意调用方传进来的 key 是 kRawShapeFlag | 形号，必须屏蔽后再用。
                const double x0 = static_cast<double>(cp & ~kRawShapeFlag) / 1000.0;
                g.Lines.push_back(Line{ { x0, 0, 0 }, { x0, 1.0, 0 } });
                g.Lines.push_back(Line{ { x0 + 1.0, 0, 0 }, { x0 + 1.0, 1.0, 0 } });
                g.Advance = 1.0;
                it = cache.emplace(cp, std::move(g)).first;
            }
            return it->second;
        }
        double   GetAdvance(uint32_t) override { return 1.0; }
        double   GetHeight() const override        { return 1.0; }
        uint64_t GetFontId() const override        { return 7777; }
        const char* GetName() const override       { return "shapestub"; }
        bool     HasGlyph(uint32_t cp) override    { return true; }
    };

    // ── 1. LineTypeRecord 模型 ──────────────────────────────────────
    void TestModel()
    {
        LineTypeTable t;
        LineTypeRecord rec;
        rec.Name          = "GAS_LINE";
        rec.Description   = "Gas line ----GAS----GAS----";
        rec.Pattern       = { 0.5, -0.2, -0.25 };
        rec.PatternLength = 0.95;
        LineTypeSymbol sym;
        sym.PatternIndex = 1;
        sym.Text         = "GAS";
        sym.Scale        = 0.1;
        sym.OffsetX      = -0.1;
        sym.OffsetY      = -0.05;
        sym.StyleId      = TextStyleTable::StandardID;
        rec.Symbols.push_back(sym);

        const LineTypeID id = t.Add(rec);
        Check(id != LineTypeTable::ContinuousID, "带符号线型能加入线型表");
        const LineTypeRecord* got = t.Find(id);
        Check(got != nullptr && got->Pattern.size() == 3, "Pattern 保留全部 3 段");
        Check(got != nullptr && Near(got->PatternLength, 0.95), "PatternLength = 0.95");
        // 关键：非空 Pattern ⇒ IsContinuous() 为假 ⇒ 不会退化成实线
        Check(got != nullptr && !got->IsContinuous(), "含符号线型不是连续线");
        Check(got != nullptr && got->HasSymbols(), "HasSymbols() 识别文字段");
        Check(got != nullptr && got->Symbols.size() == 1, "Symbols 收下 1 个文字段");
        Check(got != nullptr && got->Symbols[0].Text == "GAS", "文字内容 = GAS");
        Check(got != nullptr && got->Symbols[0].PatternIndex == 1, "符号下标指向 Pattern[1]");

        // 纯虚线不该带出符号
        const LineTypeID dash = t.FindByName("DASHED");
        const LineTypeRecord* d = t.Find(dash);
        Check(d != nullptr && !d->HasSymbols() && !d->IsContinuous(), "内置 DASHED 仍是纯虚线");
        const LineTypeRecord* c = t.Find(LineTypeTable::ContinuousID);
        Check(c != nullptr && c->IsContinuous(), "Continuous 仍是连续线");
    }

    // ── 2. 序列化往返（Scene 走完整保存/打开）──────────────────────
    // 同时守向后兼容：旧文件没有 symbols 键时不能崩、Pattern 不能变。
    // 符号段混两种：文字段（Text）与形段（Shape + ShapeFont，如 ZIGZAG/BATTING），
    // 两个新键 shape / shapeFont 必须一起往返，且旧文件读回时 Shape 保持 0。
    void TestSerialize()
    {
        Scene a;
        LineTypeRecord rec;
        rec.Name          = "ZIGZAG_TEST";
        rec.Description   = "Zig zag /\\/\\/";
        rec.Pattern       = { 0.0001, -0.2, -0.4, -0.2 };
        rec.PatternLength = 0.8001;
        for (size_t i = 1; i < rec.Pattern.size(); ++i)
        {
            LineTypeSymbol sym;
            sym.PatternIndex     = i;
            sym.Scale            = 0.2;
            sym.OffsetX          = (i == 1) ? -0.2 : 0.2;
            sym.Rotation         = (i == 1) ? 0.0 : 3.14159265358979;
            sym.StyleId          = 1;
            sym.AbsoluteRotation = (i == 2);
            // 真 ZIGZAG 的全部符号段都是形段（形号 131，形文件 ltypeshp.shx）
            sym.Shape            = 131;
            sym.ShapeFont        = "ltypeshp.shx";
            rec.Symbols.push_back(sym);
        }
        a.GetLineTypeTable().Add(rec);

        JsonSerializer w;
        a.Serialize(w);
        const std::string json = w.Dump();
        Check(json.find("symbols") != std::string::npos, "序列化写出 symbols 键");

        JsonSerializer rd;
        Scene b;
        const bool parsed = rd.Parse(json);
        if (parsed) b.Deserialize(rd);
        Check(parsed, "含复杂线型的文档可解析");
        const LineTypeID id = b.GetLineTypeTable().FindByName("ZIGZAG_TEST");
        Check(id != LineTypeTable::ContinuousID, "往返后线型仍可查回");
        if (const LineTypeRecord* got = b.GetLineTypeTable().Find(id))
        {
            Check(got->Pattern.size() == 4 && Near(got->PatternLength, 0.8001), "往返后 Pattern 不变");
            Check(got->Symbols.size() == 3, "往返后 3 个符号段");
            Check(got->Symbols.size() == 3 && Near(got->Symbols[1].Rotation, 3.14159265358979, 1e-6)
                        && got->Symbols[1].AbsoluteRotation, "往返后旋转与绝对旋转标志保留");
            Check(got->Symbols.size() == 3 && got->Symbols[0].PatternIndex == 1, "往返后符号下标保留");
            Check(got->Symbols.size() == 3 && got->Symbols[0].StyleId == 1, "往返后文字样式 ID 保留");
            // 形段（组码 75 / 形文件）：这两个键是本轮补上的
            Check(got->Symbols.size() == 3 && got->Symbols[0].Shape == 131
                        && got->Symbols[0].ShapeFont == "ltypeshp.shx",
                  "往返后形号与形文件保留");
            Check(got->Symbols.size() == 3 && got->Symbols[0].Text.empty(),
                  "形段不写文字内容");
        }

        // 旧文件：线型项里没有 symbols 键 ⇒ 读成空 Pattern 也不能崩。
        // items 是全量表（保留 ID 0..2 + 预置项），旧文件照样覆盖写。
        JsonSerializer old;
        Scene c;
        const std::string oldJson =
            "{\"lineTypes\":{\"items\":["
            "{\"name\":\"ByLayer\",\"pattern\":[],\"patternLength\":0},"
            "{\"name\":\"ByBlock\",\"pattern\":[],\"patternLength\":0},"
            "{\"name\":\"Continuous\",\"pattern\":[],\"patternLength\":0},"
            "{\"name\":\"OLD\",\"pattern\":[0.5,-0.25],\"patternLength\":0.75}"
            "]}}";
        Check(old.Parse(oldJson), "旧格式线型 JSON 可解析");
        if (old.IsLoading())
        {
            // 与 Scene::Deserialize 一致：线型表挂在场景的 "lineTypes" 对象下
            if (old.BeginObject("lineTypes"))
            {
                c.GetLineTypeTable().Deserialize(old);
                old.EndObject();
            }
        }
        const LineTypeID oldId = c.GetLineTypeTable().FindByName("OLD");
        Check(oldId != LineTypeTable::ContinuousID, "旧文件读入新线型");
        const LineTypeRecord* o = c.GetLineTypeTable().Find(oldId);
        Check(o != nullptr && o->Pattern.size() == 2 && o->Symbols.empty(), "旧文件：无符号段、Pattern 原样");
        Check(o != nullptr && !o->IsContinuous(), "旧文件：非连续线（未退化成实线）");
    }

    // ── 3. 渲染：符号位置铺出文字顶点 ───────────────────────────────
    // 用一个假 FontResolver + 真 TextLayoutEngine 走 EmitLaidOutText 分支；
    // 只要 m_verts/m_fillVerts/m_textVerts/m_glyphs 有新增，就说明符号真的画了。
    void TestRender()
    {
        Scene s;
        LineTypeTable& t = s.GetLineTypeTable();
        LineTypeRecord rec;
        rec.Name          = "GAS_LINE";
        rec.Pattern       = { 0.5, -0.2, -0.25 };
        rec.PatternLength = 0.95;
        LineTypeSymbol sym;
        sym.PatternIndex = 1;
        sym.Text         = "GAS";
        sym.Scale        = 0.1;
        rec.Symbols.push_back(sym);
        const LineTypeID lt = t.Add(rec);

        std::vector<Vertex_P3_C4>    verts;
        std::vector<Vertex_P3_C4>    fills;
        std::vector<Vertex_P3_C4_UV> texts;
        Overlay                      overlay;

        // StrokeFont：自带字形，不需要外部字体文件
        StrokeFont font;
        int fontCalls = 0;
        FontResolver resolver = [&font, &fontCalls](uint32_t)
        {
            ++fontCalls;
            return ResolvedTextStyle{ &font, 1.0, 0.0 };
        };
        // wpp 取 0.001：周期 0.95 ⇒ 950px，远离「不足 4px 退化为实线」的护栏
        DrawContext ctx(verts, fills, texts, overlay,
                        s.GetLayerManager(), t,
                        0.001, nullptr, std::move(resolver));
        ctx.SetLineTypeScale(1.0);
        ctx.SetTextStyleTable(&s.GetTextStyleTable());

        EntityAttr a;
        a.LayerId  = 0;
        a.LineType = lt;
        ctx.BeginEntity(&a);

        // 基线画 3 个周期（3 × 0.95 = 2.85），用 3.0 长的线
        const size_t before = verts.size();
        ctx.DrawLine({ 0, 0, 0 }, { 3.0, 0, 0 }, Math::Color4::White(), false);
        Check(verts.size() > before, "虚线本体现出实线段");

        // FontResolver 被调用 ⇒ 符号段走到了文字发射路径（不是被 continue 掉）
        Check(fontCalls > 0, "文字符号段被发射（FontResolver 被调用）");

        // 线长不足一个周期时符号也要铺（跨周期补铺）
        fontCalls = 0;
        verts.clear();
        ctx.DrawLine({ 0, 0, 0 }, { 0.6, 0, 0 }, Math::Color4::White(), false);
        Check(fontCalls > 0, "不足一个周期也铺符号");
    }

    // ── 3b. 渲染：形段按形号取字形 ─────────────────────────────────
    // TRACKS = 0.15(Shape 130, S=0.25) | 0.15 —— 整条线只有形段符号。
    // 形段不能退化成空白：必须经 ShapeFontResolver 拿到形字体、按形号取字形、
    // 输出几何。这里用 ShapeFontStub（自带字形），不依赖 ltypeshp.shx 在磁盘上。
    void TestShapeRender()
    {
        Scene s;
        LineTypeTable& t = s.GetLineTypeTable();
        LineTypeRecord rec;
        rec.Name          = "TRACKS";
        rec.Description   = "Tracks - - - -";
        rec.Pattern       = { 0.15, 0.15 };
        rec.PatternLength = 0.30;
        LineTypeSymbol sym;
        sym.PatternIndex = 0;
        sym.Shape        = 130;
        sym.ShapeFont    = "ltypeshp.shx";
        sym.Scale        = 0.25;
        rec.Symbols.push_back(sym);
        const LineTypeID lt = t.Add(rec);

        std::vector<Vertex_P3_C4>    verts;
        std::vector<Vertex_P3_C4>    fills;
        std::vector<Vertex_P3_C4_UV> texts;
        Overlay                      overlay;

        ShapeFontStub shapeFont;
        ShapeFontResolver shapeResolver = [&](const std::string&) -> IFont*
        {
            return &shapeFont;
        };
        int fontCalls = 0;
        FontResolver fontResolver = [&fontCalls](uint32_t)
        {
            ++fontCalls;
            return ResolvedTextStyle{ nullptr, 1.0, 0.0 };
        };

        DrawContext ctx(verts, fills, texts, overlay,
                        s.GetLayerManager(), t,
                        0.001, nullptr, std::move(fontResolver));
        ctx.SetLineTypeScale(1.0);
        ctx.SetTextStyleTable(&s.GetTextStyleTable());
        ctx.SetShapeFontResolver(std::move(shapeResolver));

        EntityAttr a;
        a.LayerId  = 0;
        a.LineType = lt;
        ctx.BeginEntity(&a);

        // 1.2 长 ⇒ 4 个周期 ⇒ 4 个形符号（每周期 2 段都是正的，符号在段 0）。
        // 注意：TRACKS 的两段都是实线段，光看 verts 非空会被实线顶点骗过去，
        // 所以用「y 的极差」验证：实线段顶点全在 y=0，只有符号顶点偏离。
        verts.clear();
        ctx.DrawLine({ 0, 0, 0 }, { 1.2, 0, 0 }, Math::Color4::White(), false);
        Check(!verts.empty(), "形段铺出几何（不是空白）");

        // 形符号的 y 范围就是字形高度：Scale 0.25 × 形高 1.0 = 0.25，
        // 符号中心在线上 ⇒ y ∈ [0, 0.25]。用这个范围验证真的按形号取了字形。
        double minY = 1e30, maxY = -1e30;
        for (const auto& v : verts)
        {
            minY = std::min(minY, static_cast<double>(v.pos.y));
            maxY = std::max(maxY, static_cast<double>(v.pos.y));
        }
        Check(maxY - minY > 0.2 && maxY - minY < 0.3,
              "形符号尺寸 = Scale × 形高（约 0.25）");

        // 形号 130 ⇒ 桩字形的 x 偏移 0.13，缩放 s=0.25 ⇒ 世界 x 偏移 0.0325。
        // 符号锚点在第一个实线段的终点 x=0.15 处 ⇒ 期望出现 x≈0.1825 的顶点。
        // 用来验证「真的按形号取了字形」，而不是随便画了个东西。
        const double expectX = 0.15 + 0.13 * 0.25;
        bool anyAtExpectX = false;
        for (const auto& v : verts)
            if (std::fabs(static_cast<double>(v.pos.x) - expectX) < 1e-4)
                anyAtExpectX = true;
        Check(anyAtExpectX, "形符号按形号取到对应字形（x 偏移 = 形号/1000）");
        Check(fontCalls == 0, "形段不请求文字字体（FontResolver 未被调用）");

        // 形文件缺失（解析回调返回空）时不能崩，只退化成不画符号
        verts.clear();
        DrawContext ctx2(verts, fills, texts, overlay,
                         s.GetLayerManager(), t,
                         0.001, nullptr, nullptr);
        ctx2.SetShapeFontResolver([](const std::string&) -> IFont* { return nullptr; });
        ctx2.SetLineTypeScale(1.0);
        EntityAttr a2;
        a2.LayerId  = 0;
        a2.LineType = lt;
        ctx2.BeginEntity(&a2);
        const size_t before2 = verts.size();
        ctx2.DrawLine({ 0, 0, 0 }, { 1.2, 0, 0 }, Math::Color4::White(), false);
        Check(verts.size() > before2, "形文件缺失时虚线本体现在（只是没有符号）");
    }

    // ── 3c. 护栏退化：缩得过小也不能丢掉符号 ─────────────────────────
    // 旧行为：cycle/wpp < 4 ⇒ 整条线压成实线，符号一个都不画 ⇒ GAS_LINE
    // 缩小时看起来就是一条普通实线，用户认不出线型。
    // 新行为：实线照画，符号按 pitch 补铺一行（数量有上限）。
    void TestGuardedSymbols()
    {
        Scene s;
        LineTypeTable& t = s.GetLineTypeTable();
        LineTypeRecord rec;
        rec.Name          = "GAS_LINE_SMALL";
        rec.Pattern       = { 0.5, -0.2, -0.25 };
        rec.PatternLength = 0.95;
        LineTypeSymbol sym;
        sym.PatternIndex = 1;
        sym.Text         = "GAS";
        sym.Scale        = 0.1;
        rec.Symbols.push_back(sym);
        const LineTypeID lt = t.Add(rec);

        std::vector<Vertex_P3_C4>    verts;
        std::vector<Vertex_P3_C4>    fills;
        std::vector<Vertex_P3_C4_UV> texts;
        Overlay                      overlay;

        StrokeFont font;
        int fontCalls = 0;
        FontResolver resolver = [&font, &fontCalls](uint32_t)
        {
            ++fontCalls;
            return ResolvedTextStyle{ &font, 1.0, 0.0 };
        };

        // 关键：wpp 取 1.0 ⇒ cycle 0.95 ⇒ cycle/wpp ≈ 0.95 < 4.0 ⇒ 走实线护栏
        DrawContext ctx(verts, fills, texts, overlay,
                        s.GetLayerManager(), t,
                        1.0, nullptr, std::move(resolver));
        ctx.SetLineTypeScale(1.0);
        ctx.SetTextStyleTable(&s.GetTextStyleTable());

        EntityAttr a;
        a.LayerId  = 0;
        a.LineType = lt;
        ctx.BeginEntity(&a);

        verts.clear();
        ctx.DrawLine({ 0, 0, 0 }, { 100.0, 0, 0 }, Math::Color4::White(), false);
        // 实线本身只要 2 个顶点；护栏补铺后顶点数必然远超
        Check(verts.size() > 2, "护栏退化成实线，但线还是连续实线");
        // 文字符号走了排版路径 ⇒ 至少铺一次；且数量被 512 上限压住
        Check(fontCalls > 0, "护栏退化后符号仍被铺出（不是一条光实线）");
        Check(fontCalls <= 512, "补铺数量有上限，顶点数不会爆");
    }

    // ── 4. 导入：真实样例的 GAS_LINE ─────────────────────────────────
    // 探针输出（全部版本一致）：GAS_LINE = 0.5 | -0.2 Text "GAS" S=0.1 X=-0.1 Y=-0.05 | -0.25
    // 导入后必须：Pattern.size()==3、PatternLength≈0.95、不是连续线、Symbols[0].Text=="GAS"。
    void TestRealSampleConstants()
    {
        // 与 CadExchange::ImportLineTypes 的段处理逻辑一致的重放：
        // 段 (Length, isText, Text) → 期望 Pattern
        struct Seg { double len; bool text; const char* str; };
        const Seg segs[] = {
            {  0.5,  false, "" },
            { -0.2,  true,  "GAS" },
            { -0.25, false, "" },
        };
        std::vector<double> pattern;
        double patternLength = 0.0;
        std::vector<std::string> texts;
        std::vector<size_t> idx;
        for (size_t i = 0; i < 3; ++i)
        {
            pattern.push_back(segs[i].len);
            patternLength += std::abs(segs[i].len);
            if (segs[i].text)
            {
                texts.push_back(segs[i].str);
                idx.push_back(i);
            }
        }
        Check(pattern.size() == 3, "GAS_LINE 导入后 3 段");
        Check(Near(patternLength, 0.95), "GAS_LINE 周期 0.95");
        Check(!pattern.empty(), "非空 Pattern ⇒ 不退化成实线");
        Check(texts.size() == 1 && texts[0] == "GAS", "文字段保留 GAS");
        Check(idx.size() == 1 && idx[0] == 1, "文字段下标 = 1");
        Check(pattern[0] > 0.0 && pattern[1] < 0.0 && pattern[2] < 0.0,
              "符号约定：正=实线段、负=空白（文字段落在负段上）");

        // ── TRACKS：全部符号段都是形段（组码 75 = 130，形文件 ltypeshp.shx）──
        // 探针输出：TRACKS = 0.15(Shape 130, S=0.25, style=639) | 0.15
        struct ShapeSeg { double len; int shape; double scale; const char* font; };
        const ShapeSeg tsegs[] = {
            { 0.15, 130, 0.25, "ltypeshp.shx" },
            { 0.15,   0, 1.00, "" },
        };
        std::vector<double> tpattern;
        double tlen = 0.0;
        std::vector<int> shapes;
        std::vector<std::string> fonts;
        for (size_t i = 0; i < 2; ++i)
        {
            tpattern.push_back(tsegs[i].len);
            tlen += std::abs(tsegs[i].len);
            if (tsegs[i].shape != 0)
            {
                shapes.push_back(tsegs[i].shape);
                fonts.push_back(tsegs[i].font);
            }
        }
        Check(tpattern.size() == 2 && Near(tlen, 0.30), "TRACKS 导入后 2 段、周期 0.30");
        Check(shapes.size() == 1 && shapes[0] == 130, "TRACKS 形号 130 被收下（不是空白）");
        Check(fonts.size() == 1 && fonts[0] == "ltypeshp.shx",
              "TRACKS 形文件从组码 340 的 STYLE 反查得到");

        // ── ZIGZAG：两个形段，第二个绝对旋转（rot = π，与第一个反向）──
        // 探针输出：0.0001 | -0.2(131, X=-0.2) | -0.4(131, rot=π, X=0.2) | -0.2
        const ShapeSeg zsegs[] = {
            {   0.0001,   0, 1.00, "" },
            { -0.2,     131, 0.20, "ltypeshp.shx" },
            { -0.4,     131, 0.20, "ltypeshp.shx" },
            { -0.2,       0, 1.00, "" },
        };
        std::vector<int> zshapes;
        std::vector<size_t> zidx;
        for (size_t i = 0; i < 4; ++i)
            if (zsegs[i].shape != 0)
            {
                zshapes.push_back(zsegs[i].shape);
                zidx.push_back(i);
            }
        Check(zshapes.size() == 2, "ZIGZAG 两个形段都被收下");
        Check(zidx.size() == 2 && zidx[0] == 1 && zidx[1] == 2,
              "ZIGZAG 形段下标 = 1、2（按 Pattern 下标落位）");
    }
}

int RunLineTypeSymbolTests()
{
    g_failures = 0;
    std::printf("\n=== 复杂线型（带字母/符号）===\n");
    const int before = g_failures;
    TestModel();
    TestSerialize();
    TestRender();
    TestShapeRender();
    TestGuardedSymbols();
    TestRealSampleConstants();
    const int n = g_failures - before;
    std::printf("复杂线型用例失败：%d\n", n);
    return n;
}
