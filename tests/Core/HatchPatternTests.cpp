// ── 填充图案测试：.pat 解析、内置图案、线族绘制（错位 / 虚线 / 角度）、过密保护、序列化往返 ─────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Import/CadTextFile.h"
#include "Scene/HatchPatternLibrary.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

    struct Seg { Math::Point3 a, b; };

    class RecordingSink : public IDrawSink
    {
    public:
        std::vector<Seg>          segs;
        std::vector<Math::Point3> points;   // 点图元（.pat 的 dash = 0）
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { segs.push_back({ a, b }); }
        void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override { points.push_back(p); }
    };

    // 只统计实心填充的三角形个数与总面积
    struct AreaSink : IDrawSink
    {
        double area = 0.0;
        int    tris = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
        {
            area += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
            ++tris;
        }
    };

    // 正 n 边形（逆时针，首尾相同）
    std::vector<Math::Point3> Ring(int n, double r)
    {
        std::vector<Math::Point3> pts;
        for (int i = 0; i <= n; ++i)
        {
            const double a = Math::TwoPI * (i % n) / n;
            pts.push_back({ r * std::cos(a), r * std::sin(a), 0.0 });
        }
        return pts;
    }

    double Len(const Seg& s) { return std::hypot(s.b.x - s.a.x, s.b.y - s.a.y); }

    std::vector<Seg> Render(const HatchPattern& p, double scale, double angle, double x0, double y0, double x1, double y1)
    {
        HatchEntity h(1, Polyline({ { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } }), p);
        h.SetScale(scale);
        h.SetAngle(angle);
        RecordingSink sink;
        h.Draw(sink, false, false);
        return sink.segs;
    }

    double TotalLen(const std::vector<Seg>& v)
    {
        double t = 0.0;
        for (const auto& s : v) t += Len(s);
        return t;
    }

    // 线段规范化后排序，便于比较两组线段是否相同（与方向、顺序无关）
    std::vector<std::array<long long, 4>> Canon(const std::vector<Seg>& v)
    {
        std::vector<std::array<long long, 4>> out;
        auto q = [](double x) { return static_cast<long long>(std::llround(x * 1000.0)); };
        for (const auto& s : v)
        {
            std::array<long long, 4> a{ q(s.a.x), q(s.a.y), q(s.b.x), q(s.b.y) };
            if (std::make_pair(a[0], a[1]) > std::make_pair(a[2], a[3]))
                a = { a[2], a[3], a[0], a[1] };
            out.push_back(a);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}

int RunHatchPatternTests()
{
    g_failures = 0;
    std::printf("── 填充图案 ──\n");
    auto& lib = HatchPatternLibrary::Instance();

    // ── 1. 内置图案 ─────────────────────────────────────────────
    {
        const char* expected[] = { "SOLID", "ANSI31", "ANSI32", "ANSI33", "ANSI34", "ANSI35", "ANSI36", "ANSI37", "ANSI38",
                                   "LINE", "NET", "NET3", "DASH", "DOTS", "SQUARE", "BRICK", "ZIGZAG", "EARTH" };
        bool all = true;
        for (const char* n : expected) all = all && lib.Find(n) != nullptr;
        Check(all, "内置图案齐全（18 个）");
    }

    {
        const char* own[] = { "钢筋混凝土", "普通砖", "加气混凝土", "卵石砌体", "天然石材", "金属", "空心砖", "隔热材料03", "AN31C", "COBBLE" };
        bool all = true;
        for (const char* n : own) all = all && lib.Find(n) && !lib.Find(n)->Families.empty();
        Check(all, "TCH图案编进了图案库");
        const HatchPattern* brick = lib.Find("普通砖");
        Check(brick && brick->Families.size() == 1 && brick->Families[0].AngleDeg == 45.0 && std::abs(brick->Families[0].Spacing - 2.5) < 1e-5
                    && brick->Description.empty(),
              "普通砖：45° 斜线、间距 2.5（图面毫米），说明里的“Tangent”去掉");
        const HatchPattern* plant = lib.Find("半荒植物地");
        Check(plant && plant->Families.size() == 36, "半荒植物地：原文少逗号的两行补上后 36 个线族都在");
        const HatchPattern* ansi = lib.Find("ANSI31");
        const HatchPattern* tchAnsi = lib.FindTianzheng("ansi31");
        Check(ansi && tchAnsi && ansi->Families[0].Spacing == 3.175 && tchAnsi->Families[0].Spacing == 1.5875,
              "ANSI31：图案库里按 acadiso（间距 3.175）");
        Check(lib.FindTianzheng("AR-CONC") && !lib.Find("AR-CONC") && lib.FindTianzheng("普通砖") && lib.FindTianzheng("SOLID")->Solid,
              "TCH的标准图案只给TCH填充；SOLID 也从这里取");
        lib.LoadText("*TCH_EXTRA_PROBE\n0,0,0,0,1\n");
        Check(lib.FindTianzheng("tch_extra_probe") != nullptr, "TCH表里没有的再找图案库（patterns 目录另加的）");
    }

    // ── 实心填充：面积正确（含孔岛），相邻条带的梯形合并，三角形数与边数同阶 ──
    {
        // 40 边的近似圆（半径 10）中间挖一个 4×4 的方孔
        HatchEntity h(2, std::vector<Polyline>{ Polyline(Ring(40, 10.0)), Polyline({ { -2, -2, 0 }, { 2, -2, 0 }, { 2, 2, 0 }, { -2, 2, 0 }, { -2, -2, 0 } }) });
        AreaSink sink;
        h.Draw(sink, false, false);
        const double expected = 0.5 * 40 * 100.0 * std::sin(Math::TwoPI / 40.0) - 16.0;
        Check(std::abs(sink.area - expected) < 1e-6, "实心填充：含孔岛时三角形总面积 = 外环面积 - 孔面积");
        Check(sink.tris <= 2 * 44, "实心填充：相邻条带的梯形合并，三角形数不超过 2 × 边数");
        Check(lib.Find("solid") && lib.Find("solid")->Solid && lib.Find("solid")->Families.empty(), "SOLID 为实心，名称不区分大小写");
        const HatchPattern* brick = lib.Find("BRICK");
        Check(brick && brick->Families.size() == 3 && brick->Families[2].Dashes.size() == 2
                    && brick->Families[2].Dashes[0] < 0.0 && !brick->Description.empty(),
              "BRICK：3 个线族，虚线与说明解析正确");
        Check(lib.Get("NO_SUCH").Solid, "找不到的图案退回实心");
    }

    // ── 梳形边界：同一条带里有上百条活动边，梯形跨条带延续 ──────────────
    {
        // 底座 99×2，上面 50 个宽 1、高 8 的齿：面积 198 + 400
        std::vector<Math::Point3> comb{ { 0, 0, 0 }, { 99, 0, 0 } };
        for (int k = 49; k >= 0; --k)
        {
            comb.push_back({ 2.0 * k + 1, 10, 0 });
            comb.push_back({ 2.0 * k, 10, 0 });
            if (k > 0)
            {
                comb.push_back({ 2.0 * k, 2, 0 });
                comb.push_back({ 2.0 * k - 1, 2, 0 });
            }
        }
        comb.push_back({ 0, 0, 0 });
        AreaSink sink;
        HatchEntity(3, Polyline(comb)).Draw(sink, false, false);
        Check(std::abs(sink.area - 598.0) < 1e-9 && sink.tris == 2 * 51, "梳形实心填充：面积 598，底座 1 个梯形 + 每个齿 1 个梯形");
    }

    // ── 大边界（样条细分出上万个点）：耗时与点数近似线性 ──────────────────
    // 原来实心填充每个条带遍历全部边、图案填充每条扫描线遍历全部边，都是平方复杂度：
    // 图签 logo 里一个 1.6 万点的实心填充要 360 ms，6.4 万点要 4.6 s（Release）
    {
        const int n = 64000;
        HatchEntity solid(4, std::vector<Polyline>{ Polyline(Ring(n, 100.0)), Polyline(Ring(n, 50.0)) });
        AreaSink sink;
        auto t0 = std::chrono::steady_clock::now();
        solid.Draw(sink, false, false);
        const double solidMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const double ringArea = 0.5 * n * std::sin(Math::TwoPI / n);      // 半径 1 的正 n 边形面积
        std::printf("    %d × 2 点的实心填充（圆环）：%d 个三角形，%.1f ms\n", n, sink.tris, solidMs);
        Check(std::abs(sink.area - ringArea * (100.0 * 100.0 - 50.0 * 50.0)) < 1e-6 * ringArea * 7500.0 && sink.tris <= 4 * n,
              "大边界实心填充：圆环面积正确，三角形数与边数同阶");
        Check(solidMs < 1000.0, "大边界实心填充：12.8 万条边 1 秒内完成（不是平方复杂度）");

        HatchEntity lines(5, Polyline(Ring(n, 100.0)), HatchPattern::MakeLines(0.0, 0.025));    // 8000 条扫描线
        RecordingSink rec;
        t0 = std::chrono::steady_clock::now();
        lines.Draw(rec, false, false);
        const double linesMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    %d 点边界、8000 条扫描线的图案填充：%zu 段，%.1f ms\n", n, rec.segs.size(), linesMs);
        // 上下两端的扫描线是否正好擦过顶点取决于舍入，条数允许差一两条
        Check(rec.segs.size() >= 7998 && rec.segs.size() <= 8002 && std::abs(TotalLen(rec.segs) * 0.025 - ringArea * 10000.0) < 1.0,
              "大边界图案填充：每条扫描线一段，总长 × 间距 ≈ 面积");
        Check(linesMs < 1000.0, "大边界图案填充：6.4 万条边 × 8000 条扫描线 1 秒内完成");
    }

    // ── 2. 解析：注释、BOM、CRLF、错误行 ───────────────────────────
    {
        std::vector<HatchPattern> out;
        std::string err;
        const bool ok = HatchPatternLibrary::Parse("\xEF\xBB\xBF;; 注释\r\n*mine , 测试图案\r\n0, 0,0, .5,1, .25,-.25 ; 行尾注释\r\n\r\n*EMPTY\r\n", out, &err);
        Check(ok && out.size() == 1 && out[0].Name == "MINE" && out[0].Description == "测试图案"
                 && out[0].Families.size() == 1 && out[0].Families[0].DeltaX == 0.5 && out[0].Families[0].Dashes.size() == 2,
              "BOM / CRLF / 注释 / 小数省略整数位；无线族的图案丢弃");

        out.clear();
        const bool bad = HatchPatternLibrary::Parse("*A\n0,0,0,0,1\n0,0,abc,0,1\n*B\n45,0,0,0,2\n", out, &err);
        Check(!bad && out.size() == 2 && err.find("第 3 行") != std::string::npos, "错误行报告行号，其余图案仍载入");
    }

    {
        const std::string gbk  = "*GBK_PROBE, \xBB\xEC\xC4\xFD\xCD\xC1\r\n45, 0,0, 0,3.175\r\n";                 // 说明"混凝土"（GBK）
        const std::string utf8 = "*GBK_PROBE, \xE6\xB7\xB7\xE5\x87\x9D\xE5\x9C\x9F\r\n45, 0,0, 0,3.175\r\n";     // 同一段（UTF-8）
        Check(CadTextToUtf8(gbk) == utf8, "GBK 文本转成 UTF-8");
        Check(CadTextToUtf8(utf8) == utf8 && CadTextToUtf8("*LINE\n0, 0,0, 0,1\n") == "*LINE\n0, 0,0, 0,1\n", "UTF-8 与纯 ASCII 原样保留");

        // 名称是中文的 GBK 文件经 LoadFile 读入，按 UTF-8 名称找得到
        const std::filesystem::path file = std::filesystem::temp_directory_path() / "minicad_gbk_probe.pat";
        {
            std::ofstream f(file, std::ios::binary);
            f << "*\xBB\xEC\xC4\xFD\xCD\xC1, Tangent \xBB\xEC\xC4\xFD\xCD\xC1\r\n45, 0,0, 0,3.175\r\n";       // *混凝土, Tangent 混凝土
        }
        const std::u8string u8 = file.u8string();
        std::string error;
        const size_t n = lib.LoadFile(std::string(u8.begin(), u8.end()), &error);
        const HatchPattern* p = lib.Find("\xE6\xB7\xB7\xE5\x87\x9D\xE5\x9C\x9F");
        Check(n == 1 && error.empty() && p && p->Description == "Tangent \xE6\xB7\xB7\xE5\x87\x9D\xE5\x9C\x9F" && p->Families.size() == 1,
              "GBK 的 .pat 文件：中文名称与说明读成 UTF-8");
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }

    // ── 3. 连续线：条数、长度、角度 ───────────────────────────────
    {
        const auto segs = Render(*lib.Find("LINE"), 1.0, 0.0, 0, 0, 100, 100);
        bool horizontal = true;
        for (const auto& s : segs) horizontal = horizontal && std::abs(s.a.y - s.b.y) < 1e-9 && std::abs(Len(s) - 100.0) < 1e-6;
        // 32 条：v = 0, 3.175, ..., 98.425（nFirst=0 起、nLast=floor(100/3.175)=31）。
        // v = 0 撞底边，修好后也出图（原来是两端都开的区间判定，这条被漏掉，
        // 那时是 31 条 —— 缺陷不是特性）。
        Check(segs.size() == 32 && horizontal, "LINE：100×100 区域内 32 条完整水平线（间距 3.175）");

        const auto rot = Render(*lib.Find("ANSI31"), 1.0, 45.0, 0, 0, 100, 100);
        bool vertical = !rot.empty();
        for (const auto& s : rot) vertical = vertical && std::abs(s.a.x - s.b.x) < 1e-6;
        Check(vertical, "ANSI31 旋转 45° 后为竖线");

        const auto scaled = Render(*lib.Find("LINE"), 2.0, 0.0, 0, 0, 100, 100);
        Check(scaled.size() == 16, "比例 2：间距加倍，条数减半（32 → 16）");
    }

    // ── 4. 虚线：实线占比 ────────────────────────────────────────
    {
        const auto dash = Render(*lib.Find("DASH"), 1.0, 0.0, 0, 0, 127, 127);      // 127 = 20 个周期
        // 41 条：v = 0, 3.175, ..., 127（nFirst=0、nLast=floor(127/3.175)=40）。
        // v = 0 撞底边、v = 127 撞顶边，两端都出图（修前 39 条，两端都漏）。
        const double full = 41.0 * 127.0;
        Check(std::abs(TotalLen(dash) / full - 0.5) < 0.01, "DASH：实线占一半");

        bool inside = true;
        for (const auto& s : dash)
            inside = inside && s.a.x >= -1e-9 && s.b.x <= 127.0 + 1e-9 && Len(s) <= 3.175 + 1e-9;
        Check(inside, "DASH：每段不超过实线长，且裁剪在边界内");

        const auto dots = Render(*lib.Find("DOTS"), 1.0, 0.0, 0, 0, 50, 50);
        // .pat 的 dash = 0 是"点"，走**点图元**（IDrawSink::DrawPoint / POINTLIST），
        // 由渲染器画成恒定 1 屏幕像素 —— 不画成任何线段。
        // （旧实现画成 0.1 世界单位的短线段，缩小视图时 < 1px 整片消失。）
        Check(dots.empty(), "DOTS：点走点图元，不画线段");
    }

    // ── 5. 多族图案：原点随填充角度旋转 ─────────────────────────────
    {
        // 以原点为中心的正方形旋转 90° 后与自身重合：旋转后的填充 = 原填充的线段旋转 90°
        const HatchPattern& brick = *lib.Find("BRICK");
        const auto base = Render(brick, 1.0, 0.0,  -50, -50, 50, 50);
        const auto rot  = Render(brick, 1.0, 90.0, -50, -50, 50, 50);
        std::vector<Seg> turned;
        for (const auto& s : base)
            turned.push_back({ { -s.a.y, s.a.x, 0 }, { -s.b.y, s.b.x, 0 } });
        Check(!base.empty() && Canon(turned) == Canon(rot), "BRICK 旋转 90°：各线族原点随之旋转，砖缝位置一致");
    }

    // ── 6. 过密保护 ─────────────────────────────────────────────
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto dense = Render(*lib.Find("DASH"), 1e-5, 0.0, 0, 0, 1000, 1000);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    比例 1e-5 的 DASH 填充 1000×1000：输出 %zu 段，%.1f ms\n", dense.size(), ms);
        Check(dense.size() <= 500000 && ms < 5000.0, "图案过密时线段数有上限，不会卡死");
    }

    // ── 7. 序列化往返 ───────────────────────────────────────────
    {
        HatchEntity h(7, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 0, 0 } }), *lib.Find("BRICK"));
        h.SetScale(2.5);
        h.SetAngle(30.0);
        JsonSerializer w;
        EntityIO::Write(w, h);
        JsonSerializer r;
        auto back = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
        const auto* hb = back && back->IsKindOf<HatchEntity>() ? static_cast<HatchEntity*>(back.get()) : nullptr;
        bool same = hb && hb->GetScale() == 2.5 && hb->GetAngle() == 30.0 && hb->GetPattern().Families.size() == 3;
        if (same)
            for (size_t i = 0; i < 3; ++i)
            {
                const auto& a = hb->GetPattern().Families[i];
                const auto& b = lib.Find("BRICK")->Families[i];
                same = same && a.DeltaX == b.DeltaX && a.Dashes == b.Dashes && a.OffsetX == b.OffsetX;
            }
        Check(same, "BRICK 填充 JSON 往返：错位、虚线、比例、角度保持");

        JsonSerializer w2;
        EntityIO::Write(w2, HatchEntity(8, Polyline({ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 0, 0 } }), *lib.Find("ANSI31")));
        const std::string text = w2.Dump();
        Check(text.find("\"dx\"") == std::string::npos && text.find("dashes") == std::string::npos,
              "连续线、无错位的线族不写 dx / dashes（与旧格式一致）");
    }

    // ── 8. 渐变填充 ────────────────────────────────────────────────────────
    {
        // 8.1 类型名称解析（大小写不敏感；未知名称按线性兜底）
        Check(HatchGradient::ParseType("LINEAR")      == GradientType::Linear,       "类型名称解析：LINEAR");
        Check(HatchGradient::ParseType("SPHERICAL")   == GradientType::Spherical,    "类型名称解析：SPHERICAL");
        Check(HatchGradient::ParseType("spherical")   == GradientType::Spherical,    "类型名称大小写不敏感");
        Check(HatchGradient::ParseType("INVCYLINDER") == GradientType::InvCylinder,  "类型名称解析：INVCYLINDER");
        Check(HatchGradient::ParseType("BOGUS")       == GradientType::Linear,       "未知名称按线性处理（与 AutoCAD 一致）");
        Check(std::string(HatchGradient::TypeName(GradientType::InvHemispherical)) == "INVHEMISPHERICAL",
              "类型 → 名称（写 DXF 时用）");

        // 8.2 单色渐变的两端补色：tint > 0 提亮、tint < 0 压暗
        const Math::Color4 mid{ 0.5, 0.25, 0.75, 1.0 };
        const Math::Color4 up = HatchGradient::TintedColor(mid,  0.5);
        const Math::Color4 dn = HatchGradient::TintedColor(mid, -0.5);
        Check(up.r > mid.r && up.g > mid.g && up.b > mid.b, "单色渐变 ColorTint > 0：向白色提亮");
        Check(dn.r < mid.r && dn.g < mid.g && dn.b < mid.b, "单色渐变 ColorTint < 0：向黑色压暗");
        Check(HatchGradient::TintedColor(mid,  1.0) == Math::Color4(1, 1, 1, 1), "tint = 1 到达纯白");
        Check(HatchGradient::TintedColor(mid, -1.0) == Math::Color4(0, 0, 0, 1), "tint = -1 到达纯黑");
        const Math::Color4 clamped = HatchGradient::TintedColor(mid, 9.0);
        Check(clamped.r <= 1.0 && clamped.g <= 1.0 && clamped.b <= 1.0, "tint 超范围时夹到 0~1");

        // 8.3 Sample：双色分段、单色单边、越界钳到端点
        {
            HatchGradient g;
            g.Enabled = true;
            g.Stops.push_back({ 0.0, Math::Color4(1, 0, 0, 1) });   // 红
            g.Stops.push_back({ 1.0, Math::Color4(0, 0, 1, 1) });   // 蓝
            Math::Color4 a, b;
            double t = 0.0;
            g.Sample(0.0, a, b, t);
            Check(a == Math::Color4(1, 0, 0, 1) && t == 0.0, "Sample 起点：v=0 取左端红");
            g.Sample(1.0, a, b, t);
            Check(b == Math::Color4(0, 0, 1, 1) && t == 0.0, "Sample 终点：v=1 取右端蓝");
            g.Sample(0.25, a, b, t);
            Check(a == Math::Color4(1, 0, 0, 1) && b == Math::Color4(0, 0, 1, 1) && std::abs(t - 0.25) < 1e-12,
                  "Sample 段内：v=0.25 结束 t=0.25");
            g.Sample(-5.0, a, b, t);
            Check(a == Math::Color4(1, 0, 0, 1), "Sample 越界：v < 0 钳到左端点");
            g.Sample(99.0, a, b, t);
            Check(b == Math::Color4(0, 0, 1, 1), "Sample 越界：v > 1 钳到右端点");
        }
        {
            // 单色：只有一个停靠点，另一端按 ColorTint 补出来
            HatchGradient sg; sg.Enabled = true; sg.ColorTint = -1.0;
            sg.Stops.push_back({ 0.5, Math::Color4(1, 1, 1, 1) });
            Math::Color4 a, b; double t = 0.0;
            sg.Sample(0.0, a, b, t);
            Check(a == Math::Color4(1, 1, 1, 1) && b == Math::Color4(0, 0, 0, 1),
                  "单色 Sample：v=0 在色门口，右端用 ColorTint 补成黑色");
            sg.Sample(1.0, a, b, t);
            Check(a == Math::Color4(1, 1, 1, 1) && b == Math::Color4(0, 0, 0, 1) && t == 1.0,
                  "单色 Sample：v=1 时 t=1，插值后取到 tinted 端（黑）");
        }

        // 8.4 SetGradient 把图案自动改成实心并清空线族
        {
            HatchEntity h(91, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            Check(!h.IsGradient(), "空壳时不是渐变填充");
            HatchGradient g;
            g.Enabled = true;
            g.Stops.push_back({ 0.0, Math::Color4(0, 0, 1, 1) });
            g.Stops.push_back({ 1.0, Math::Color4(1, 1, 0, 1) });
            h.SetGradient(g);
            Check(h.IsGradient() && h.GetPattern().Solid && h.GetPattern().Families.empty(),
                  "SetGradient：图案自动改成实心并清空线族");
            h.ClearGradient();
            Check(!h.IsGradient(), "ClearGradient 回到空壳");
            // 空 Stops 的梯度不算人：防止上游给了一个 IsValid() 为假的渐变
            HatchGradient empty;
            empty.Enabled = true;
            HatchEntity h2(92, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            h2.SetGradient(empty);
            Check(!h2.IsGradient(), "空 Stops 的梯度不算人（IsValid 为假）");
        }

        // 8.5 渐变渲染：线性只切 2 个三角形且顶点异色；球面要网格细分
        {
            class TriSink : public IDrawSink
            {
            public:
                struct Tri { Math::Point3 a, b, c; Math::Color4 ca, cb, cc; bool plain; };
                std::vector<Tri> tris;
                void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
                void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
                { tris.push_back({ a, b, c, {}, {}, {}, true }); }
                void FillTriangleColored(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c,
                                         const Math::Color4& ca, const Math::Color4& cb, const Math::Color4& cc) override
                { tris.push_back({ a, b, c, ca, cb, cc, false }); }
            };

            HatchEntity h(93, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            HatchGradient g;
            g.Enabled = true;
            g.Type = GradientType::Linear;
            g.Angle = 0.0;                          // 从左到右
            g.Stops.push_back({ 0.0, Math::Color4(0, 0, 1, 1) });   // 蓝
            g.Stops.push_back({ 1.0, Math::Color4(1, 1, 0, 1) });   // 黄
            h.SetGradient(g);

            TriSink sink;
            h.Draw(sink, false, false);
            Check(sink.tris.size() == 2, "矩形渐变：只切出 2 个三角形（上下两条带）");
            bool allColored = !sink.tris.empty();
            bool anyDiff = false;
            for (const auto& tr : sink.tris)
            {
                allColored = allColored && !tr.plain;
                anyDiff = anyDiff || !(tr.ca == tr.cc);
            }
            Check(allColored, "矩形渐变：进来的是 FillTriangleColored");
            Check(anyDiff, "矩形渐变：顶点确实异色（左蓝右黄）");

            // 非线性：SPHERICAL 按 24×16 网格切分，矩形上就不会只切 2 个三角形
            TriSink sphere;
            HatchGradient sg = g;
            sg.Type = GradientType::Spherical;
            HatchEntity hs(94, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            hs.SetGradient(sg);
            hs.Draw(sphere, false, false);
            Check(sphere.tris.size() > 100, "球面渐变：按网格细分，不是 2 个三角形");
            bool sphereAnyDiff = false;
            for (const auto& tr : sphere.tris)
                sphereAnyDiff = sphereAnyDiff || !(tr.ca == tr.cc);
            Check(sphereAnyDiff, "球面渐变：周边边顶点确实异色");

            // 选中状态：换成高亮色实心填充（与其它实体一致）
            TriSink sel;
            h.Draw(sel, true, false);
            bool selPlain = !sel.tris.empty();
            for (const auto& tr : sel.tris)
                selPlain = selPlain && tr.plain;
            Check(selPlain, "选中状态的渐变换成高亮色实心填充（不过 FillTriangleColored）");
        }

        // 8.6 序列化往返：JSON / .mcad 保存后可读回
        {
            HatchEntity h(95, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            HatchGradient g;
            g.Enabled = true;
            g.Name = "SPHERICAL";
            g.Type = GradientType::Spherical;
            g.Angle = 30.0;
            g.Shift = 0.25;
            g.ColorTint = -0.5;
            g.SingleColor = true;
            g.Stops.push_back({ 0.0, Math::Color4(0.1, 0.2, 0.3, 1.0) });
            h.SetGradient(g);

            JsonSerializer w;
            EntityIO::Write(w, h);
            const std::string text = w.Dump();
            Check(text.find("\"gradient\"") != std::string::npos, "序列化写入 gradient 键");
            Check(text.find("SPHERICAL") != std::string::npos, "序列化写入类型名 SPHERICAL");

            JsonSerializer r;
            auto back = r.Parse(text) ? EntityIO::Read(r) : nullptr;
            const auto* hb = back && back->IsKindOf<HatchEntity>() ? static_cast<HatchEntity*>(back.get()) : nullptr;
            bool same = hb && hb->IsGradient();
            if (same)
            {
                const auto& gg = hb->GetGradient();
                same = same && gg.Type == GradientType::Spherical
                            && std::abs(gg.Angle - 30.0) < 1e-12
                            && std::abs(gg.Shift - 0.25) < 1e-12
                            && std::abs(gg.ColorTint + 0.5) < 1e-12
                            && gg.SingleColor
                            && gg.Stops.size() == 1
                            && gg.Stops[0].Color == Math::Color4(0.1, 0.2, 0.3, 1.0);
            }
            Check(same, "渐变 JSON 往返：类型、角度、位移、色调、单色开关、停靠点颜色全保留");
        }

        // 8.7 渐变和图案填充互不干扰
        {
            HatchEntity h(97, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }));
            Check(!h.IsGradient(), "有图案时不是渐变");
            HatchGradient g;
            g.Enabled = true;
            g.Stops.push_back({ 0.0, Math::Color4(0, 0, 1, 1) });
            h.SetGradient(g);
            Check(h.IsGradient() && h.GetPattern().Solid, "同时带图案和渐变；实心标记仍为真");
            h.SetPattern(*lib.Find("ANSI31"));
            Check(h.GetPattern().Name == "ANSI31", "SetPattern 只改图案不影响渐变");
        }

        // 8.8 极窄竖长条：扫描线恰好压在边界的极值 v 上时，不能画出「整条边界」。
        // 句柄 3539（ANSI37，x 宽 250、y 高 27400）：
        // 线族 a=90° 的 v = -x，v 跨度只有 250 < 间距 625 ⇒ 只有一条扫描线，
        // 且它正好等于左边界 x=x0 的 v 值，求交退化成沿整条左边界的线段。
        // CAD 不会把贴着边界的扫描线画成图案线（那条线是 HATCH 自己的边界环）。
        {
            const double x0 = 293339, x1 = 293589, y0 = 379766, y1 = 407166;
            HatchPattern pat;
            pat.Name  = "ANSI37";
            pat.Solid = false;
            pat.Families.push_back({ 90.0,  625.0, x0, y0, 0.0 });
            pat.Families.push_back({ 180.0, 625.0, x0, y0, 0.0 });
            HatchEntity h(98, Polyline({ { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } }), pat);
            RecordingSink sink;
            h.Draw(sink, false, false);

            // 找出「整条贴着某条边界」的线：端点都在同一条边上，且长度等于该边长
            int boundaryLines = 0;
            for (const auto& s : sink.segs)
            {
                const bool onLeft   = std::abs(s.a.x - x0) < 1e-6 && std::abs(s.b.x - x0) < 1e-6 && Len(s) > 1000;
                const bool onRight  = std::abs(s.a.x - x1) < 1e-6 && std::abs(s.b.x - x1) < 1e-6 && Len(s) > 1000;
                const bool onBottom = std::abs(s.a.y - y0) < 1e-6 && std::abs(s.b.y - y0) < 1e-6 && Len(s) > 1000;
                const bool onTop    = std::abs(s.a.y - y1) < 1e-6 && std::abs(s.b.y - y1) < 1e-6 && Len(s) > 1000;
                if (onLeft || onRight || onBottom || onTop) ++boundaryLines;
            }
            std::printf("    极窄竖条：线段 %zu，贴边整线 %d\n", sink.segs.size(), boundaryLines);
            Check(boundaryLines == 0, "极窄竖条：不把贴着 HATCH 边界的扫描线画成整条图案线");
            Check(sink.segs.size() > 20, "极窄竖条：其它正常图案线照常出图");
        }
    }
    return g_failures;
}
