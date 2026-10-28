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
        std::vector<Seg>          segs;
        std::vector<Math::Point3> points;   // 点图元（.pat 的 dash = 0）
        // 逐顶点着色的三角形（渐变填充）：存位置 + 颜色，用于核对颜色确实随位置变化
        struct ColoredTri { Math::Color4 c[3]; double x[3]; double y[3]; };
        std::vector<ColoredTri>   tris;
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { segs.push_back({ a, b }); }
        void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override { points.push_back(p); }
        void FillTriangleColored(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c,
                                 const Math::Color4& ca, const Math::Color4& cb, const Math::Color4& cc) override
        {
            tris.push_back({ { ca, cb, cc }, { a.x, b.x, c.x }, { a.y, b.y, c.y } });
        }
        // 渐变测试里要遍历三角形的每个角：给出「第 i 个角的 x / y / 颜色」
        //（测试代码里直接写 tri.x[k] / tri.c[k]）
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
    }

    // ── 实心填充：面积正确（含孔岛），相邻条带的梯形合并，三角形数与边数同阶 ──
    {
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
        // 40 边的近似圆（半径 10）中间挖一个 4×4 的方孔
        std::vector<Math::Point3> ring;
        for (int i = 0; i <= 40; ++i)
        {
            const double a = Math::TwoPI * (i % 40) / 40.0;
            ring.push_back({ 10.0 * std::cos(a), 10.0 * std::sin(a), 0.0 });
        }
        HatchEntity h(2, std::vector<Polyline>{ Polyline(ring), Polyline({ { -2, -2, 0 }, { 2, -2, 0 }, { 2, 2, 0 }, { -2, 2, 0 }, { -2, -2, 0 } }) });
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

    // ── 8. 渐变填充（ACAD 的 GRADIENT）────────────────────────────
    //
    // 回归点：sample_AC1032.dwg 里的两处渐变（LINEAR / SPHERICAL，蓝 → 黄）以前整条
    // 降级成「单色实心」，图上只看见一块纯色。这里守住：停靠点颜色、逐顶点着色、
    // 非线性渐变的细分、序列化往返。
    {
        // 单色渐变的提亮 / 压暗
        const Math::Color4 base{ 0.2, 0.4, 0.6, 1.0 };
        const Math::Color4 lighter = HatchGradient::TintedColor(base, 0.5);
        const Math::Color4 darker  = HatchGradient::TintedColor(base, -0.5);
        Check(lighter.r > base.r && lighter.g > base.g && lighter.b > base.b, "单色渐变：正的色调值提亮");
        Check(darker.r < base.r && darker.g < base.g && darker.b < base.b, "单色渐变：负的色调值压暗");
        const Math::Color4 white = HatchGradient::TintedColor(base, 1.0);
        Check(std::abs(white.r - 1.0) < 1e-9 && std::abs(white.g - 1.0) < 1e-9, "色调 +1 到达纯白");

        // 停靠点求值：双色渐变在 0.5 处应当是两个端点色之间
        HatchGradient two;
        two.Enabled = true;
        two.Name    = "LINEAR";
        two.Type    = HatchGradient::ParseType(two.Name);
        two.Stops   = { { 0.0, Math::Color4{ 0, 0, 1, 1 } }, { 1.0, Math::Color4{ 1, 1, 0, 1 } } };
        Math::Color4 a, b;
        double t = 0.0;
        two.Sample(0.0, a, b, t);
        Check(std::abs(a.r) < 1e-9 && std::abs(a.b - 1.0) < 1e-9, "渐变 0 端是第一个停靠点（蓝）");
        two.Sample(1.0, a, b, t);
        Check(std::abs(b.r - 1.0) < 1e-9 && std::abs(b.b) < 1e-9, "渐变 1 端是最后一个停靠点（黄）");
        two.Sample(0.5, a, b, t);
        Check(std::abs(t - 0.5) < 1e-9, "中点落在两停靠点正中间");

        // 渐变类型名的解析（大小写不敏感）
        Check(HatchGradient::ParseType("SPHERICAL") == GradientType::Spherical, "类型名 SPHERICAL 解析");
        Check(HatchGradient::ParseType("invcylinder") == GradientType::InvCylinder, "类型名大小写不敏感");
        Check(HatchGradient::ParseType("no-such-type") == GradientType::Linear, "未知类型名退回线性");
        Check(std::string(HatchGradient::TypeName(GradientType::Hemispherical)) == "HEMISPHERICAL", "类型名回写");

        // 逐顶点着色：矩形上的线性渐变必须产出**不同颜色**的顶点
        //（三顶点同色 = 退化成实心，正是修之前的表现）
        {
            HatchEntity h(20, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } }),
                          HatchPattern::MakeSolid());
            h.SetGradient(two);
            RecordingSink sink;
            h.Draw(sink, false, false);
            Check(sink.tris.size() == 2, "线性渐变：矩形就够（2 个三角形，不需要细分）");
            bool varied = false;
            for (const auto& tri : sink.tris)
                if (tri.c[0].r != tri.c[1].r || tri.c[1].r != tri.c[2].r)
                    varied = true;
            Check(varied, "线性渐变：三角形三个角颜色不同（逐顶点着色，不是单色）");
            // 左边界蓝色、右边界黄色
            bool leftBlue = false, rightYellow = false;
            for (const auto& tri : sink.tris)
                for (int k = 0; k < 3; ++k)
                {
                    if (tri.x[k] < 1e-6 && tri.c[k].b > 0.99 && tri.c[k].r < 0.01) leftBlue = true;
                    if (tri.x[k] > 9.999 && tri.c[k].r > 0.99 && tri.c[k].b < 0.01) rightYellow = true;
                }
            Check(leftBlue && rightYellow, "线性渐变：0 端是蓝、1 端是黄（方向正确）");
        }

        // 非线性（球面）必须细分：只有 2 个三角形时中心点不存在，球的明暗出不来
        {
            HatchGradient sph = two;
            sph.Name = "SPHERICAL";
            sph.Type = HatchGradient::ParseType(sph.Name);
            HatchEntity h(21, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } }),
                          HatchPattern::MakeSolid());
            h.SetGradient(sph);
            RecordingSink sink;
            h.Draw(sink, false, false);
            Check(sink.tris.size() > 100, "球面渐变：矩形被细分（三角形数远多于 2）");
            // 中心附近应当是 0 端（蓝），四角是 1 端（黄）
            bool centerBlue = false, cornerYellow = false;
            for (const auto& tri : sink.tris)
                for (int k = 0; k < 3; ++k)
                {
                    const double dx = std::abs(tri.x[k] - 5.0), dy = std::abs(tri.y[k] - 2.5);
                    if (dx < 0.5 && dy < 0.3 && tri.c[k].b > 0.99 && tri.c[k].r < 0.01) centerBlue = true;
                    if (dx > 4.0 && dy > 2.0 && tri.c[k].r > 0.99 && tri.c[k].b < 0.01) cornerYellow = true;
                }
            Check(centerBlue, "球面渐变：中心是 0 端（蓝）");
            Check(cornerYellow, "球面渐变：四角是 1 端（黄）");
        }

        // 设为渐变后，图案本身按实心处理（渐变铺满边界，与 .pat 图案无关）
        {
            HatchEntity h(22, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 0, 0 } }), *lib.Find("ANSI31"));
            h.SetGradient(two);
            Check(h.GetPattern().Solid && h.GetPattern().Families.empty(), "设为渐变后图案变为实心、线族清空");
            Check(h.IsGradient(), "IsGradient() 为真");
            h.ClearGradient();
            Check(!h.IsGradient(), "ClearGradient() 后不再是渐变");
        }

        // 序列化往返
        {
            HatchEntity h(23, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } }),
                          HatchPattern::MakeSolid());
            HatchGradient g = two;
            g.Type  = HatchGradient::ParseType("SPHERICAL");
            g.Name  = "SPHERICAL";
            g.Angle = 45.0;
            g.Shift = 0.25;
            h.SetGradient(g);
            JsonSerializer w;
            EntityIO::Write(w, h);
            JsonSerializer r;
            auto back = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
            const auto* hb = back && back->IsKindOf<HatchEntity>() ? static_cast<HatchEntity*>(back.get()) : nullptr;
            const bool same = hb && hb->IsGradient() && hb->GetGradient().Type == GradientType::Spherical &&
                              std::abs(hb->GetGradient().Angle - 45.0) < 1e-9 &&
                              std::abs(hb->GetGradient().Shift - 0.25) < 1e-9 &&
                              hb->GetGradient().Stops.size() == 2 &&
                              std::abs(hb->GetGradient().Stops[0].Color.b - 1.0) < 1e-9 &&
                              std::abs(hb->GetGradient().Stops[1].Color.r - 1.0) < 1e-9;
            Check(same, "渐变填充 JSON 往返：类型、角度、位移、停靠点颜色保持");

            // 旧文件（没有 gradient 键）读回仍是普通填充
            JsonSerializer w2;
            EntityIO::Write(w2, HatchEntity(24, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 0, 0 } }),
                                            HatchPattern::MakeSolid()));
            Check(w2.Dump().find("\"gradient\"") != std::string::npos, "实心填充也写 gradient 对象（enabled=false）");
            JsonSerializer r2;
            auto plain = r2.Parse(w2.Dump()) ? EntityIO::Read(r2) : nullptr;
            const auto* pb = plain && plain->IsKindOf<HatchEntity>() ? static_cast<HatchEntity*>(plain.get()) : nullptr;
            Check(pb && !pb->IsGradient(), "非渐变填充读回后 IsGradient() 为假");
        }
    }

    return g_failures;
}
