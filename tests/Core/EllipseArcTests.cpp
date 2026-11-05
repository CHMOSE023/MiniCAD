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

    return g_failures;
}
