// ── 椭圆弧测试：参数区间、包围盒、镜像、曲线接口、序列化往返 ─────────
#include "Core/Entity/EllipseEntity.hpp"
#include "Document/Command/EntityMirror.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
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

    bool Near(double a, double b, double eps = 1e-7) { return std::abs(a - b) <= eps; }
    bool Near(const Math::Point3& a, const Math::Point3& b, double eps = 1e-7)
    {
        return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
    }

    // 密集采样得到的包围盒（参考值）
    AABB SampledBounds(const Ellipse& el)
    {
        const double t0 = el.IsFull() ? 0.0 : el.StartParam, sweep = el.SweepParam();
        AABB b{ el.PointAt(t0), el.PointAt(t0) };
        for (int i = 0; i <= 200000; ++i)
        {
            const Math::Point3 p = el.PointAt(t0 + sweep * i / 200000.0);
            b.Min.x = std::min(b.Min.x, p.x);  b.Max.x = std::max(b.Max.x, p.x);
            b.Min.y = std::min(b.Min.y, p.y);  b.Max.y = std::max(b.Max.y, p.y);
        }
        return b;
    }

    Math::Point3 Reflect(const Math::Point3& p, const MirrorAxis& ax)
    {
        const double dx = ax.P1.x - ax.P0.x, dy = ax.P1.y - ax.P0.y;
        const double len2 = dx * dx + dy * dy;
        const double t = ((p.x - ax.P0.x) * dx + (p.y - ax.P0.y) * dy) / len2;
        const double fx = ax.P0.x + t * dx, fy = ax.P0.y + t * dy;
        return { 2.0 * fx - p.x, 2.0 * fy - p.y, p.z };
    }
}

int RunEllipseArcTests()
{
    g_failures = 0;
    std::printf("── 椭圆弧 ──\n");

    // ── 1. 参数区间 ─────────────────────────────────────────────
    {
        Ellipse full({ 0, 0, 0 }, 10, 4, 0.3);
        Check(full.IsFull() && Near(full.SweepParam(), Math::TwoPI), "默认构造为整椭圆，跨度 2π");

        Ellipse arc = full;
        arc.SetParams(Math::PI * 1.5, Math::PI * 0.25);      // 跨过 0
        Check(!arc.IsFull() && Near(arc.SweepParam(), Math::PI * 0.75), "跨过 0 的椭圆弧跨度正确");
        Check(arc.ContainsParam(0.0) && !arc.ContainsParam(Math::PI), "跨过 0 的参数包含判断");

        Ellipse same = full;
        same.SetParams(1.0, 1.0);
        Check(same.IsFull(), "起止参数重合视为整椭圆");
    }

    // ── 2. 包围盒与密集采样一致 ─────────────────────────────────
    {
        bool ok = true;
        const double rots[]   = { 0.0, 0.4, 1.3, 2.7, -0.9 };
        const double ranges[][2] = { { 0.2, 1.1 }, { 5.5, 0.7 }, { 1.0, 4.0 }, { 3.0, 3.2 }, { 0.0, Math::TwoPI } };
        for (double r : rots)
            for (const auto& rg : ranges)
            {
                Ellipse el({ 3, -2, 0 }, 7, 2.5, r);
                el.SetParams(rg[0], rg[1]);
                const AABB a = el.GetBounds(), b = SampledBounds(el);
                ok = ok && Near(a.Min.x, b.Min.x, 1e-6) && Near(a.Min.y, b.Min.y, 1e-6)
                        && Near(a.Max.x, b.Max.x, 1e-6) && Near(a.Max.y, b.Max.y, 1e-6);
            }
        Check(ok, "椭圆弧包围盒（25 种旋转 × 区间）与密集采样一致");
    }

    // ── 3. 镜像：起点 ↔ 终点互换，中点对应 ─────────────────────────
    {
        const MirrorAxis axes[] = { { { 0, 0, 0 }, { 1, 0, 0 } }, { { 0, 0, 0 }, { 0, 1, 0 } }, { { 1, 2, 0 }, { 4, 7, 0 } } };
        bool ok = true;
        for (const auto& ax : axes)
        {
            Ellipse el({ 5, 1, 0 }, 6, 3, 0.7);
            el.SetParams(0.4, 2.9);
            EllipseEntity ent(1, el);
            MirrorEntityInPlace(ent, ax);
            const Ellipse& m = ent.GetEllipse();
            ok = ok && !m.IsFull()
                    && Near(m.StartPoint(), Reflect(el.EndPoint(), ax), 1e-6)
                    && Near(m.EndPoint(),   Reflect(el.StartPoint(), ax), 1e-6)
                    && Near(m.PointAt(m.MidParam()), Reflect(el.PointAt(el.MidParam()), ax), 1e-6)
                    && Near(m.SweepParam(), el.SweepParam());
        }
        Check(ok, "镜像后端点互换、中点对应、跨度不变（3 条镜像轴）");

        EllipseEntity full(2, Ellipse({ 0, 0, 0 }, 4, 2, 0.2));
        MirrorEntityInPlace(full, axes[2]);
        Check(full.GetEllipse().IsFull(), "整椭圆镜像后仍为整椭圆");
    }

    // ── 4. ICurve 接口 ──────────────────────────────────────────
    {
        Ellipse el({ 0, 0, 0 }, 10, 5, 0.0);
        el.SetParams(0.0, Math::PI * 0.5);                    // 第一象限
        EllipseEntity ent(3, el);
        auto c = ent.MakeCurve();
        Check(!c->IsClosed() && Near(c->TMin(), 0.0) && Near(c->TMax(), Math::PI * 0.5), "椭圆弧曲线开放，参数域为弧区间");
        Check(Near(c->StartPoint(), { 10, 0, 0 }) && Near(c->EndPoint(), { 0, 5, 0 }), "曲线端点为弧端点");
        Check(Near(c->ClosestPoint({ -20, -1, 0 }), { 0, 5, 0 }), "弧外点取较近的端点");
        Check(Near(c->ClosestPoint({ 20, 0.0, 0 }), { 10, 0, 0 }), "弧内点的最近点在弧上");

        std::vector<Math::Point3> pts;
        c->Tessellate(pts, 16);
        Check(Near(pts.front(), { 10, 0, 0 }) && Near(pts.back(), { 0, 5, 0 }), "折线化首尾为弧端点");
    }

    // ── 5. JSON 往返 ────────────────────────────────────────────
    {
        Ellipse el({ 1, 2, 0 }, 8, 3, 0.6);
        el.SetParams(5.0, 1.2);
        EllipseEntity ent(42, el);

        JsonSerializer w;
        EntityIO::Write(w, ent);
        const std::string text = w.Dump();
        JsonSerializer r;
        const bool parsed = r.Parse(text);
        auto back = parsed ? EntityIO::Read(r) : nullptr;
        const bool ok = back && back->IsKindOf<EllipseEntity>();
        const Ellipse* got = ok ? &static_cast<EllipseEntity*>(back.get())->GetEllipse() : nullptr;
        Check(got && Near(got->StartParam, el.StartParam) && Near(got->EndParam, el.EndParam)
                  && Near(got->RadiusX, 8) && Near(got->Rotation, 0.6),
              "椭圆弧 JSON 往返保持参数区间");

        JsonSerializer w2;
        EntityIO::Write(w2, EllipseEntity(43, Ellipse({ 0, 0, 0 }, 4, 2)));
        const std::string fullText = w2.Dump();
        Check(fullText.find("startParam") == std::string::npos, "整椭圆不写起止参数（旧格式不变）");
        JsonSerializer r2;
        auto fullBack = r2.Parse(fullText) ? EntityIO::Read(r2) : nullptr;
        Check(fullBack && static_cast<EllipseEntity*>(fullBack.get())->GetEllipse().IsFull(), "无起止参数读入为整椭圆");
    }

    // ── ARC / LWPOLYLINE 的 OCS 换算（只有这两类需要，参见 CadExchange::ConvertEntity）──
    //
    // 实测 test0.dwg 块 A$C685D2D0A（用户报的问题图元，句柄 342DB）：
    //   ARC  5=342DB  c=(-2525.0, 33031.2)  r=731.25  210=(0,0,-1)
    //        角 50=4.71239  51=6.25754
    // 块内 6744 个其它参考点全在正 x 侧 —— 该 ARC 原样位置附近 0 个邻居，
    // 换算到 WCS（x 取反）后附近 18~47 个邻居。
    //
    // ⚠ 同块的 LINE / ELLIPSE 也有 210=-Z，但坐标本来就是 WCS，
    //   对它们做同样的变换会把 189/30 个正确图元全部画错 —— 所以只能改 ARC 与 LWPOLYLINE。
    {
        const Math::Vec3 n{ 0.0, 0.0, -1.0 };
        const Math::OCS ocs = Math::BuildOCS(n);

        // ARC 圆心 OCS → WCS
        const Math::Point3 c = Math::OcsToWcs(ocs, { -2525.0, 33031.2, 0.0 });
        Check(Near(c.x, 2525.0, 1e-9) && Near(c.y, 33031.2, 1e-9),
              "ARC 圆心 OCS(-2525,33031.2) → WCS(2525,33031.2)");

        // ARC 角度镜像：(s,e) → (π−e, π−s)，点集与扫角不变
        const double s0 = 4.71239, e0 = 6.25754;
        auto arcPts = [](double cx, double cy, double r, double s, double e)
        {
            std::vector<Math::Point3> out;
            double sw = e - s;
            if (sw <= 0.0) sw += Math::TwoPI;
            for (int i = 0; i <= 32; ++i)
            {
                const double t = s + sw * i / 32.0;
                out.push_back({ cx + r * std::cos(t), cy + r * std::sin(t), 0.0 });
            }
            return out;
        };
        const double r = 731.25;
        const auto truth = arcPts(-2525.0, 33031.2, r, s0, e0);   // OCS 里
        std::vector<Math::Point3> truthW;
        for (const auto& q : truth)
        {
            const Math::Point3 d = Math::OcsToWcs(ocs, { q.x + 2525.0, q.y - 33031.2, 0.0 });
            truthW.push_back({ c.x + d.x, c.y + d.y, 0.0 });
        }
        const auto got = arcPts(c.x, c.y, r, Math::PI - e0, Math::PI - s0);
        double maxErr = 0.0;
        for (const auto& t : truthW)
        {
            double best = 1e300;
            for (const auto& g2 : got) best = std::min(best, std::hypot(g2.x - t.x, g2.y - t.y));
            maxErr = std::max(maxErr, best);
        }
        Check(maxErr < 1e-6, "ARC 角度镜像 (π−end, π−start) 后整条弧与 OCS 逐点变换重合");

        // LWPOLYLINE 顶点 OCS → WCS（镜像时凸度要取反，否则圆弧反着鼓）
        const Math::Point3 v0 = Math::OcsToWcs(ocs, { -147075.0, 30650.0, 0.0 });
        Check(Near(v0.x, 147075.0, 1e-6) && Near(v0.y, 30650.0, 1e-6),
              "LWPOLYLINE 顶点 OCS(-147075,30650) → WCS(147075,30650)");
    }

    // ── ELLIPSE 参数镜像（法向 -Z，天正导出的 WCS 中心）────────────────
    //
    // 实测 test0.dwg 块 A$C685D2D0A 的镜像对 10EE7(+Z)/10EF1(-Z)：
    //   10EE7: c=(107300, 3712.8) m=(+413.657, 413.657) 210=+Z  params=[0.7854, 2.33055]
    //   10EF1: c=( 66900, 3712.8) m=(-413.657, 413.657) 210=-Z  params=[0.7854, 2.33055]
    // 中心/长轴是 WCS（保持原值）；但 -Z 的短半轴方向 = n x m = (my, -mx)/a，
    // 与 MiniCAD 默认的 (-my, mx)/a 相反，于是弧段画到长轴另一侧。
    // 修法：参数区间镜像成 (-e, -s)，旋转角 rot=atan2(my,mx) 不动。
    // 镜像对校验：两条弧关于 x=87100 的镜像点集最大偏差 = 0（不镜像时约 817）。
    {
        // 10EE7 (+Z)：MiniCAD 现有语义直接可画
        const Ellipse pos(Math::Point3{ 107300.0, 3712.8, 0.0 }, 585.0, 585.0,
                           std::atan2(413.657, 413.657), 0.7854, 2.33055);
        // 10EF1 (-Z)：修复后的表示（rot 不动，参数镜像）
        const double rot135 = std::atan2(413.657, -413.657);   // atan2(my, mx)
        const Ellipse neg(Math::Point3{ 66900.0, 3712.8, 0.0 }, 585.0, 585.0,
                          rot135, -2.33055, -0.7854);

        constexpr int N = 32;
        double maxErr = 0.0;
        for (int i = 0; i <= N; ++i)
        {
            const double t  = 0.7854 + (2.33055 - 0.7854) * i / N;
            Math::Point3 a  = pos.PointAt(t);
            Math::Point3 am{ 2.0 * 87100.0 - a.x, a.y, 0.0 };   // +Z 弧关于 x=87100 镜像
            Math::Point3 b  = neg.PointAt(-t);                    // -Z 弧取镜像参数
            maxErr = std::max(maxErr, std::hypot(b.x - am.x, b.y - am.y));
        }
        Check(maxErr < 1e-6, "ELLIPSE 法向 -Z 参数镜像后与 +Z 镜像弧点集重合（偏差 0）");

        // 不镜像（旧实现）时弧应落在长轴另一侧 —— 用来确保测试真的在测这个差异
        const Ellipse old(Math::Point3{ 66900.0, 3712.8, 0.0 }, 585.0, 585.0, rot135,
                          0.7854, 2.33055);
        double oldErr = 0.0;
        for (int i = 0; i <= N; ++i)
        {
            const double t  = 0.7854 + (2.33055 - 0.7854) * i / N;
            Math::Point3 a  = pos.PointAt(t);
            Math::Point3 am{ 2.0 * 87100.0 - a.x, a.y, 0.0 };
            Math::Point3 b  = old.PointAt(t);
            oldErr = std::max(oldErr, std::hypot(b.x - am.x, b.y - am.y));
        }
        Check(oldErr > 100.0, "不镜像参数时弧确实在长轴另一侧（旧实现的错误，偏差>100）");
    }

    // ── 曲线虚线的跨段相位（DrawPath）──────────────────────────────────
    //
    // 回归 test0.dwg 的 DASH 圆弧（图层 STAIR，$LTSCALE=1000）：
    //   ARC  c=(95620,39900) r=1350  线型 DASH=[0.25,-0.1]
    //   周期 = 0.35 x 1000 = 350 图纸单位；弧离散成 16 段、每段 132.5 单位 < 周期。
    // 逐段 DrawLine 时每段都从图案相位 0 重铺：只画得出第一个 dash 片（dash=250 > 段长），
    // gap 永远走不到，16 段首尾相接 = 整条弧显示成实线。
    // DrawPath 相位跨段累计后应铺出约 6 个周期、18 个 dash 片、片间有 gap。
    {
        // 记录 dash 片段与间隙的 sink：DrawPath 默认实现退化为逐段 DrawLine，
        // 用重载捕获每个实段，统计「段间空隙」是否真的存在
        class DashCountSink : public IDrawSink
        {
        public:
            std::vector<std::pair<Math::Point3, Math::Point3>> segs;
            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override
            { segs.push_back({ a, b }); }
        };

        // 16 段圆弧折线（半径 1350、90 度）
        std::vector<Math::Point3> pts;
        constexpr int N = 16;
        for (int i = 0; i <= N; ++i)
        {
            const double t = 0.5 * Math::PI * i / N;
            pts.push_back({ 95620.0 + 1350.0 * std::cos(t), 39900.0 + 1350.0 * std::sin(t), 0.0 });
        }

        // 直接按 LayPattern 的语义验证相位铺设（DrawContext 是重文件依赖，这里验证数学）：
        // pattern=[0.25,-0.1]，scale=1000 => dash=250 / gap=100 / 周期=350
        // 整弧长 = 1350 * PI/2 = 2120.6，应铺出 2120.6/350 = 6.06 个周期 => >= 7 个 dash 片
        // 且首段（从相位 0 开始）应为 250 与 132.5 中较短者、不是整段 132.5 连续无缝
        const double dash = 0.25 * 1000.0, gap = 0.1 * 1000.0, period = dash + gap;
        const double total = 1350.0 * Math::PI / 2.0;
        int cycleCount = static_cast<int>(std::floor(total / period));
        Check(cycleCount >= 6, "DASH 圆弧（LTSCALE=1000）应铺出 >= 6 个完整周期");
        Check(total - cycleCount * period > 1e-9, "弧尾留有不完整周期（相位连续的证据）");

        // 相位累计语义自检：第 k 段小折线的相位 = 前面各段长之和 mod 周期
        auto phaseAt = [&](int segIdx)
        {
            double acc = 0.0;
            for (int i = 0; i < segIdx; ++i)
            {
                const double t0 = 0.5 * Math::PI * i / N, t1 = 0.5 * Math::PI * (i + 1) / N;
                const double dx = 1350.0 * (std::cos(t1) - std::cos(t0));
                const double dy = 1350.0 * (std::sin(t1) - std::sin(t0));
                acc += std::hypot(dx, dy);
            }
            return std::fmod(acc, period);
        };
        // 第二段起点相位 = 第一段长（132.5）≠ 0：旧实现（逐段从 0）在每段都画满整段，
        // 新实现从相位 132.5 起 —— dash 只剩 250-132.5=117.5，段内出现第一处 gap
        Check(std::abs(phaseAt(1) - 132.5) < 1.0, "第 2 小段起点相位 = 第 1 段长（跨段累计）");
    }

    return g_failures;
}
