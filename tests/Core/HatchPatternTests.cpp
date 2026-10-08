// ── 填充图案测试：.pat 解析、内置图案、线族绘制（错位 / 虚线 / 角度）、过密保护、序列化往返 ─────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Scene/HatchPatternLibrary.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
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
        std::vector<Seg> segs;
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { segs.push_back({ a, b }); }
    };

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
        Check(lib.Find("solid") && lib.Find("solid")->Solid && lib.Find("solid")->Families.empty(), "SOLID 为实心，名称不区分大小写");
        const HatchPattern* brick = lib.Find("BRICK");
        Check(brick && brick->Families.size() == 3 && brick->Families[2].Dashes.size() == 2
                    && brick->Families[2].Dashes[0] < 0.0 && !brick->Description.empty(),
              "BRICK：3 个线族，虚线与说明解析正确");
        Check(lib.Get("NO_SUCH").Solid, "找不到的图案退回实心");
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

    // ── 3. 连续线：条数、长度、角度 ───────────────────────────────
    {
        const auto segs = Render(*lib.Find("LINE"), 1.0, 0.0, 0, 0, 100, 100);
        bool horizontal = true;
        for (const auto& s : segs) horizontal = horizontal && std::abs(s.a.y - s.b.y) < 1e-9 && std::abs(Len(s) - 100.0) < 1e-6;
        Check(segs.size() == 31 && horizontal, "LINE：100×100 区域内 31 条完整水平线（间距 3.175）");

        const auto rot = Render(*lib.Find("ANSI31"), 1.0, 45.0, 0, 0, 100, 100);
        bool vertical = !rot.empty();
        for (const auto& s : rot) vertical = vertical && std::abs(s.a.x - s.b.x) < 1e-6;
        Check(vertical, "ANSI31 旋转 45° 后为竖线");

        const auto scaled = Render(*lib.Find("LINE"), 2.0, 0.0, 0, 0, 100, 100);
        Check(scaled.size() == 15, "比例 2：间距加倍，条数减半");
    }

    // ── 4. 虚线：实线占比 ────────────────────────────────────────
    {
        const auto dash = Render(*lib.Find("DASH"), 1.0, 0.0, 0, 0, 127, 127);      // 127 = 20 个周期
        const double full = 39.0 * 127.0;                                           // 39 条线（127/3.175=40，开区间去掉一条）
        Check(std::abs(TotalLen(dash) / full - 0.5) < 0.01, "DASH：实线占一半");

        bool inside = true;
        for (const auto& s : dash)
            inside = inside && s.a.x >= -1e-9 && s.b.x <= 127.0 + 1e-9 && Len(s) <= 3.175 + 1e-9;
        Check(inside, "DASH：每段不超过实线长，且裁剪在边界内");

        const auto dots = Render(*lib.Find("DOTS"), 1.0, 0.0, 0, 0, 50, 50);
        bool tiny = !dots.empty();
        for (const auto& s : dots) tiny = tiny && Len(s) < 0.2;
        Check(tiny, "DOTS：点画成极短线段");
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

    return g_failures;
}
