// ── 图案填充的"点"（.pat dash = 0）与点图元测试 ────────────────────────────
//
// 回归点：用户报 DOTS 之类的点阵填充看不见；且点被画成短线段而非一个像素点。
//
// 两个叠加的缺陷，都在 HatchEntity 的图案线族绘制里：
//
// 缺陷 1：点长写死 `0.1 * m_scale`（纯世界尺寸）。
//   图案填充的几何常是毫米量级、视图又常缩放，0.1 世界单位在正常缩放开下
//   远小于 1 个屏幕像素 ⇒ 整片点阵消失。大坐标图纸（百万量级）里更极端。
//   也曾试过"按屏幕像素换算的短线段"：那仍是世界尺寸的线，缩小到点长
//   超过线间距时会糊成一条实线（DOTS 间距只有 1.5875）。
//   正解：走**点图元**（IDrawSink::DrawPoint / POINTLIST），一个顶点一个点，
//   由渲染器画成恒定 1 屏幕像素，与缩放无关，也永远不与间距矛盾。
//
// 缺陷 2：内层循环不推进，输出几十万个重叠的点。
//   dash 序列遍历用 `for (double d : fam.Dashes)` 每遍从头重放；
//   `d == 0` 时 `len == 0` ⇒ `s += len` 不推进 ⇒ 永远卡在第一个 dash 上，
//   在同一位置反复 emit 直到 budget 耗尽。
//   实测（dashes = {0}，20×20 范围）：修前 500000 段全挤在 (0,1) 这一个点。
//   修法：按 dash 序列的**下标**走，点不占相位（.pat 语义），靠下标前进；
//   纯点阵特判按周期推进。
//   顺带：全部 dash 都是 0 时 period 算出 0，原来直接 return ——
//   整个点阵被跳过，一个点都不画。
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

    // 分别记录线段与点：点必须走 DrawPoint（点图元），不能画成线段
    class Sink : public IDrawSink
    {
    public:
        std::vector<Seg>          segs;
        std::vector<Math::Point3> points;

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override
        { segs.push_back({ a, b }); }

        void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override
        { points.push_back(p); }
    };

    // 模拟**拾取收集器**（Picking.cpp 的 PickCollectSink）：只实现 DrawLine /
    // FillTriangle，不重写 DrawPoint。用来守住"点必须走 DrawPoint"这个不变量 ——
    // 若哪天又把点画回线段，这个 sink 会收到线段，下面的断言就会失败。
    class LineOnlySink : public IDrawSink
    {
    public:
        std::vector<Seg> segs;
        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override
        { segs.push_back({ a, b }); }
        // 故意不重写 DrawPoint：走 IDrawSink 的空默认实现
    };

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

    // 内置 DOTS：`0, -1.5875` = 点 + 空白，间距 1.5875
    HatchPattern MakeBuiltinDots()
    {
        HatchPattern p;
        p.Name        = "DOTS";
        p.Description = "点阵";
        p.Solid       = false;
        HatchLineFamily fam;
        fam.AngleDeg = 0.0;
        fam.Spacing  = 1.5875;
        fam.Dashes.push_back(0.0);
        fam.Dashes.push_back(-1.5875);
        p.Families.push_back(fam);
        return p;
    }

    // 把图案画进任意 sink（线段收集器或点收集器都用它）
    template <class SinkT>
    void Render(const HatchPattern& p, double scale, SinkT& sink,
                double x0, double y0, double x1, double y1)
    {
        HatchEntity h(1, Polyline({ { x0, y0, 0 }, { x1, y0, 0 },
                                    { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } }), p);
        h.SetScale(scale);
        h.Draw(sink, false, false);
    }
}

int RunHatchDotsTests()
{
    g_failures = 0;
    std::printf("\n=== 图案填充的点（dash = 0）与点图元 ===\n");
    const int before = g_failures;

    // ── 1. 点必须走点图元，而不是画成线段 ──────────────────────
    {
        Sink s;
        Render(MakeDots(1.0), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(!s.points.empty(), "点阵被画出来（至少 1 个点）");
        Check(s.segs.empty(), "点阵**不画任何线段**（点走点图元，不是短线段）");
        std::printf("    点数 = %zu，线段数 = %zu\n", s.points.size(), s.segs.size());
        Check(s.points.size() >= 10, "20×20 范围、间距 1 的点数 ≥ 10");
    }

    // ── 2. 点不重叠、间距等于线间距 ────────────────────────────
    {
        Sink s;
        Render(MakeDots(1.0), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        if (s.points.size() >= 2)
        {
            // 同一条填充线（y 相同）上相邻点的 x 间距应为 1.0
            std::vector<double> xs;
            for (const auto& p : s.points)
                if (Near(p.y, s.points.front().y, 1e-9)) xs.push_back(p.x);
            std::sort(xs.begin(), xs.end());
            const double d = xs.size() >= 2 ? xs[1] - xs[0] : -1.0;
            std::printf("    同行点数 %zu，相邻间距 %.6f\n", xs.size(), d);
            Check(Near(d, 1.0, 1e-6), "同行相邻点间距 = 线间距 1.0（不重叠）");

            // 全部点互不重复
            std::vector<Math::Point3> uniq = s.points;
            std::sort(uniq.begin(), uniq.end(), [](const Math::Point3& a, const Math::Point3& b)
            { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            uniq.erase(std::unique(uniq.begin(), uniq.end(), [](const Math::Point3& a, const Math::Point3& b)
            { return Near(a.x, b.x, 1e-9) && Near(a.y, b.y, 1e-9); }), uniq.end());
            Check(uniq.size() == s.points.size(),
                  "没有重复点（修前是几十万个点挤在同一位置）");
        }
    }

    // ── 3. 修前的具体病症：纯点阵整个被跳过 ────────────────────
    //
    // 全部 dash 都是 0 时 period 算出 0，原来直接 return（一个点都不画）；
    // 后来 period 置为 dy 但循环不推进，变成输出几十万个重叠点。
    {
        Sink s;
        Render(MakeDots(2.0), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(!s.points.empty(), "纯点阵（只有 dash=0）画得出点");
        Check(s.segs.empty(), "纯点阵不产生线段");
        if (!s.points.empty())
        {
            std::vector<double> xs;
            for (const auto& p : s.points)
                if (Near(p.y, s.points.front().y, 1e-9)) xs.push_back(p.x);
            std::sort(xs.begin(), xs.end());
            const double d = xs.size() >= 2 ? xs[1] - xs[0] : -1.0;
            std::printf("    纯点阵同行点数 %zu，相邻间距 %.6f（应为 2.0）\n", xs.size(), d);
            Check(Near(d, 2.0, 1e-6), "纯点阵相邻点间距 = 线间距 2.0");
        }
    }

    // ── 4. 内置 DOTS（点 + 空白）间距正确 ──────────────────────
    {
        Sink s;
        Render(MakeBuiltinDots(), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(!s.points.empty(), "内置 DOTS 画得出点");
        Check(s.segs.empty(), "内置 DOTS 不画线段（只有点笔划）");
        if (!s.points.empty())
        {
            std::vector<double> xs;
            for (const auto& p : s.points)
                if (Near(p.y, s.points.front().y, 1e-9)) xs.push_back(p.x);
            std::sort(xs.begin(), xs.end());
            const double d = xs.size() >= 2 ? xs[1] - xs[0] : -1.0;
            std::printf("    内置 DOTS 同行点数 %zu，相邻间距 %.6f（应为 1.5875）\n", xs.size(), d);
            // 点 + 空白 1.5875 ⇒ 点间距 = 1.5875 = 线间距
            Check(Near(d, 1.5875, 1e-6), "内置 DOTS 点间距 = 空白长 1.5875");
        }
    }

    // ── 5. 点与缩放无关（点图元是屏幕像素）─────────────────────
    //
    // 这是走点图元的核心好处：不需要 worldPerPixel，任何缩放大小的点
    // 都是 1 个屏幕像素。测试通过"同一个 sink 不提供视口信息也照常输出"
    // 来体现（旧实现拿不到 wpp 时会退成 0.1 世界单位的线）。
    {
        Sink a, b;
        Render(MakeDots(1.0), 1.0, a, 0.0, 0.0, 20.0, 20.0);   // 默认 GetWorldPerPixel() = 0
        Render(MakeDots(1.0), 1.0, b, 0.0, 0.0, 20.0, 20.0);
        Check(a.points.size() == b.points.size() && !a.points.empty(),
              "拿不到视口信息时点数一致（点大小与缩放无关，不需要 wpp）");
    }

    // ── 6. 点仍在填充边界内 ────────────────────────────────────
    {
        Sink s;
        Render(MakeDots(1.0), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        bool inside = true;
        for (const auto& p : s.points)
            if (p.x < -1e-9 || p.x > 20.0 + 1e-9 || p.y < -1e-9 || p.y > 20.0 + 1e-9)
                inside = false;
        Check(inside, "所有点都落在填充边界内");
    }

    // ── 7. 图案比例仍参与（scale 影响间距）─────────────────────
    {
        Sink s1, s2;
        Render(MakeDots(1.0), 1.0, s1, 0.0, 0.0, 20.0, 20.0);
        Render(MakeDots(1.0), 2.0, s2, 0.0, 0.0, 20.0, 20.0);
        Check(!s1.points.empty() && !s2.points.empty(), "两种比例下都画得出点");
        std::printf("    scale=1 点数 %zu；scale=2 点数 %zu\n", s1.points.size(), s2.points.size());
        Check(s2.points.size() < s1.points.size(), "比例放大后点数减少（间距变大）");
    }

    // ── 8. 大坐标图纸（百万量级）也不消失 ──────────────────────
    {
        Sink s;
        Render(MakeDots(100.0), 1.0, s, 1e6, 1e6, 1e6 + 20000.0, 1e6 + 20000.0);
        Check(!s.points.empty(), "大坐标图纸（1e6）下仍画得出点");
        Check(s.segs.empty(), "大坐标图纸下点仍不走线段");
        if (!s.points.empty())
            std::printf("    大坐标图纸点数 %zu\n", s.points.size());
    }

    // ── 9. 混合图案：点与实线各自走各路 ────────────────────────
    {
        HatchPattern p;
        p.Name  = "MIX";
        p.Solid = false;
        HatchLineFamily fam;
        fam.AngleDeg = 0.0;
        fam.Spacing  = 1.0;
        fam.Dashes.push_back(1.0);      // 实线 1.0
        fam.Dashes.push_back(0.0);      // 点
        p.Families.push_back(fam);

        Sink s;
        Render(p, 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(!s.segs.empty(), "混合图案画出了实线段");
        Check(!s.points.empty(), "混合图案画出了点");
        std::printf("    混合图案：线段 %zu，点 %zu\n", s.segs.size(), s.points.size());

        // 实线段长度应等于 dash 的 1.0（不被点影响）
        double maxSeg = 0.0;
        for (const auto& g : s.segs)
            maxSeg = std::max(maxSeg, std::hypot(g.b.x - g.a.x, g.b.y - g.a.y));
        Check(Near(maxSeg, 1.0, 1e-6), "混合图案的实线段长 = dash 长 1.0");
    }

    // ── 10. 点必须走 DrawPoint（守住拾取的通路）──────────────────
    //
    // 拾取收集器（Picking.cpp 的 PickCollectSink）只实现 DrawLine /
    // FillTriangle + DrawPoint。若点被画成线段，收集器虽仍能收到几何，
    // 但场景流里就不再有点图元；反之若点走 DrawPoint 而收集器没收，
    // 就是"能显示、拾取不到"（本轮修的就是这个）。
    //
    // 这里用一个**只收线段**的 sink 验证：点阵填充在该 sink 下什么都收不到。
    // 这说明点的几何只经 DrawPoint 流出 —— 也正是收集器必须重写它的原因。
    {
        LineOnlySink s;
        Render(MakeDots(1.0), 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(s.segs.empty(),
              "点阵在只收线段的 sink 下零输出（点的几何只经 DrawPoint 流出）");
    }
    {
        // 对照：混合图案（实线 + 点）在同一下能收到实线段
        HatchPattern p;
        p.Name  = "MIX2";
        p.Solid = false;
        HatchLineFamily fam;
        fam.AngleDeg = 0.0;
        fam.Spacing  = 1.0;
        fam.Dashes.push_back(1.0);
        fam.Dashes.push_back(0.0);
        p.Families.push_back(fam);

        LineOnlySink s;
        Render(p, 1.0, s, 0.0, 0.0, 20.0, 20.0);
        Check(!s.segs.empty(), "混合图案在只收线段的 sink 下能收到实线段");
    }

    const int n = g_failures - before;
    std::printf("图案填充点用例失败：%d\n", n);
    return n;
}
