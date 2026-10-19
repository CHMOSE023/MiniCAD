// ── 图案填充的"点"（.pat dash = 0）可见性测试 ──────────────────────────────
//
// 回归点：用户报 DOTS 之类的点阵填充看不见。
//
// 根因：HatchEntity 把 dash = 0 的"点"画成 `0.1 * m_scale` **世界单位**的短线段。
// 图案填充的几何常是毫米量级、视图又常缩放，0.1 世界单位在正常缩放开下远小于
// 1 个屏幕像素 ⇒ 整片点阵消失。大坐标图纸（百万量级）里更极端。
//
// 修法：与 DrawContext::EmitStyled 的虚线点同一套规则 ——
//   点的可见长度按**屏幕像素**换算成世界尺寸（至少 ~1.5px）。
//   拿不到 worldPerPixel 时（离屏探针）退到旧的 0.1 × scale，保证有输出。
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include <algorithm>
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
        if (!ok) ++g_failures;
    }
    bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

    struct Seg { Math::Point3 a, b; };

    // 记录线段，并可指定 worldPerPixel（模拟视口缩放）
    class Sink : public IDrawSink
    {
    public:
        std::vector<Seg> segs;
        double           wpp = 0.0;

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override
        { segs.push_back({ a, b }); }

        double GetWorldPerPixel() const override { return wpp; }
    };

    double Len(const Seg& s) { return std::hypot(s.b.x - s.a.x, s.b.y - s.a.y); }

    // 只有一个 dash = 0 的图案族：沿 v 方向每隔 spacing 铺一个点
    HatchPattern MakeDots(double spacing)
    {
        HatchPattern p;
        p.Name        = "DOTS_PROBE";
        p.Description = "点阵探针";
        p.Solid       = false;             // 必须显式关掉，默认是实心填充
        HatchLineFamily fam;
        fam.AngleDeg = 0.0;
        fam.Spacing  = spacing;
        fam.Dashes.push_back(0.0);          // dash = 0 ⇒ 点
        p.Families.push_back(fam);
        return p;
    }

    // 铺一条横跨 [x0,x1]×[y0,y1] 的点阵，返回所有线段（每个点一段）
    std::vector<Seg> RenderDots(double spacing, double scale, double wpp,
                                double x0, double y0, double x1, double y1)
    {
        HatchEntity h(1, Polyline({ { x0, y0, 0 }, { x1, y0, 0 },
                                    { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } }),
                      MakeDots(spacing));
        h.SetScale(scale);
        Sink sink;
        sink.wpp = wpp;
        h.Draw(sink, false, false);
        return sink.segs;
    }
}

int RunHatchDotsTests()
{
    g_failures = 0;
    std::printf("\n=== 图案填充的点（dash = 0）===\n");
    const int before = g_failures;

    // ── 1. 点必须被画出来（不是整片消失）────────────────────────
    {
        const auto segs = RenderDots(1.0, 1.0, 0.001, 0.0, 0.0, 20.0, 20.0);
        Check(!segs.empty(), "点阵被画出来（至少 1 个点）");
        std::printf("    点数 = %zu\n", segs.size());
        Check(segs.size() >= 10, "20×20 范围、间距 1 的点数 ≥ 10");
    }

    // ── 2. 点的可见长度随 worldPerPixel 缩放（核心修复）──────────
    //
    // 规则是三档夹逼：max(0.1 × scale, 1.5 × wpp) 再 min(半个间距)。
    // 所以：
    //   · 缩小视图（wpp 大）⇒ 点按 1.5 × wpp 变大，保持可见（修前的病症）；
    //   · 放大视图（wpp 小）⇒ 退到 0.1 × scale 的世界尺寸兜底。
    // 这里验证"缩小那一侧"真的在按 wpp 放大。
    {
        const double wppCoarse = 1.0;      // 缩小视图：1px = 1 世界单位
        const double wppTiny   = 0.001;    // 放大视图：1px = 0.001 世界单位

        // 间距取大（10）让 dy/2 = 5 的上限不干扰，专门看像素那一档
        const auto coarse = RenderDots(10.0, 1.0, wppCoarse, 0.0, 0.0, 200.0, 200.0);
        const auto tiny   = RenderDots(10.0, 1.0, wppTiny,   0.0, 0.0, 200.0, 200.0);

        Check(!coarse.empty() && !tiny.empty(), "两种缩放下都画得出点");
        if (!coarse.empty() && !tiny.empty())
        {
            const double lc = Len(coarse.front());
            const double lt = Len(tiny.front());
            std::printf("    wpp=1 → 点长 %.6f；wpp=0.001 → 点长 %.6f\n", lc, lt);

            Check(lc > lt, "缩小视图（wpp 大）时点更大（保持可见）");
            Check(Near(lc, 1.5 * wppCoarse, 1e-6),
                  "wpp=1 时点长 = 1.5 × wpp（屏幕约 1.5px）");
            Check(Near(lt, 0.1, 1e-6),
                  "wpp=0.001 时点长退到 0.1 × scale 的世界尺寸兜底");
        }

        // 再取一个中间值，确认"点长 / wpp"在像素那一档是常数
        const auto mid = RenderDots(10.0, 1.0, 0.1, 0.0, 0.0, 200.0, 200.0);
        if (!mid.empty())
        {
            const double lm = Len(mid.front());
            std::printf("    wpp=0.1 → 点长 %.6f（1.5 × 0.1 = 0.15）\n", lm);
            Check(Near(lm, 1.5 * 0.1, 1e-6), "wpp=0.1 时点长 = 1.5 × wpp");
        }
    }

    // ── 3. 修前的具体病症：wpp 大时旧实现画不出东西 ──────────────
    //
    // 旧实现点长 = 0.1 × scale，与 wpp 无关。
    // wpp = 1（1 像素 = 1 世界单位）时旧点长 0.1 世界单位 = 0.1px ⇒ 看不见。
    // 新实现：下限 1.5 × wpp = 1.5，上限 dy/2 = 5（间距 10）⇒ 取 1.5。
    {
        const auto segs = RenderDots(10.0, 1.0, 1.0, 0.0, 0.0, 200.0, 200.0);
        Check(!segs.empty(), "wpp=1（缩小视图）下仍画得出点");
        if (!segs.empty())
        {
            const double l = Len(segs.front());
            std::printf("    wpp=1 时点长 %.6f（旧实现为 0.1 × scale）\n", l);
            Check(l > 0.5, "点长 > 0.5 世界单位（旧实现只有 0.1，亚像素）");
            Check(Near(l, 1.5, 1e-6), "点长 = 1.5 × worldPerPixel（与 EmitStyled 同规则）");
        }
    }

    // ── 3b. 点长不超过半个线间距（否则连成实线）─────────────────
    //
    // DOTS 的间距只有 1.5875。缩小到 wpp 很大时 1.5px 对应的世界尺寸会超过
    // 半个间距，此时必须被间距兜住，否则点阵糊成一条实线。
    {
        const auto segs = RenderDots(1.0, 1.0, 1.0, 0.0, 0.0, 20.0, 20.0);
        Check(!segs.empty(), "小间距点阵仍画得出点");
        if (!segs.empty())
        {
            const double l = Len(segs.front());
            std::printf("    间距 1、wpp=1 时点长 %.6f（上限 = 半个间距 0.5）\n", l);
            Check(Near(l, 0.5, 1e-6), "点长被半个间距兜住（不糊成实线）");
        }
    }

    // ── 4. 拿不到视口信息时退到旧行为（不崩、有输出）─────────────
    {
        const auto segs = RenderDots(1.0, 1.0, 0.0, 0.0, 0.0, 20.0, 20.0);
        Check(!segs.empty(), "worldPerPixel 未知时仍画得出点（退到 0.1 × scale）");
        if (!segs.empty())
        {
            const double l = Len(segs.front());
            std::printf("    wpp 未知时点长 %.6f\n", l);
            Check(Near(l, 0.1, 1e-6), "wpp 未知时点长 = 0.1 × scale（旧行为兜底）");
        }
    }

    // ── 5. 图案比例仍参与（scale 影响间距与点长下限）─────────────
    {
        const auto s1 = RenderDots(1.0, 1.0, 0.001, 0.0, 0.0, 20.0, 20.0);
        const auto s2 = RenderDots(1.0, 2.0, 0.001, 0.0, 0.0, 20.0, 20.0);
        Check(!s1.empty() && !s2.empty(), "两种比例下都画得出点");
        if (!s1.empty() && !s2.empty())
        {
            // scale=2 ⇒ 间距翻倍 ⇒ 同样范围内的点数减半
            std::printf("    scale=1 点数 %zu；scale=2 点数 %zu\n", s1.size(), s2.size());
            Check(s2.size() < s1.size(), "比例放大后点数减少（间距变大）");
            // 点长下限 = max(0.1 × scale, 1.5 × wpp)：scale=2 时是 0.2
            Check(Near(Len(s2.front()), 0.2, 1e-6), "scale=2 时点长下限 = 0.1 × 2 = 0.2");
        }
    }

    // ── 6. 大坐标图纸（百万量级）也不消失 ────────────────────────
    {
        // 图纸坐标在百万量级，正常缩放下 wpp 可能到几十
        const auto segs = RenderDots(100.0, 1.0, 10.0, 1e6, 1e6, 1e6 + 20000.0, 1e6 + 20000.0);
        Check(!segs.empty(), "大坐标图纸（1e6）下仍画得出点");
        if (!segs.empty())
        {
            const double l = Len(segs.front());
            std::printf("    大坐标图纸点长 %.6f（= 1.5 × wpp = 15）\n", l);
            Check(Near(l, 15.0, 1e-6), "大坐标图纸点长 = 1.5 × worldPerPixel");
        }
    }

    const int n = g_failures - before;
    std::printf("图案填充点用例失败：%d\n", n);
    return n;
}
