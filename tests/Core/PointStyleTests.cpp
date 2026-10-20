// ── 点样式（POINT / $PDMODE / $PDSIZE）测试 ──────────────────────────────
//
// 用户报：「sample_AC1032.dwg 图纸中，这个点的显示与 CAD 中不一样」
//    ((-1 . <图元名>) (0 . "POINT") ... (5 . "29C") (8 . "0") (62 . 7)
//     (10 1.4944 5.1215 0.0) (210 0.0 0.0 1.0) (50 . 0.0))
//
// 根因：POINT 实体**自身不带显示信息**，画成什么样完全由图纸级 HEADER 的
// $PDMODE / $PDSIZE 决定。该图 $PDMODE=34（= 32 外接圆 | 2 十字 ⇒ ⊕）、
// $PDSIZE=0.1（世界单位）。我们两处都没读，还把图形写死成一个半臂 0.2 的十字
//（既不是 ⊕，尺寸也不对：CAD 里十字臂长 0.1、外接圆半径 0.05）。
//
// 本文件用**合成数据**守住这些不变量（不依赖外部图纸）。
#include "Core/Entity/PointEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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

    // 记录点画了什么
    struct PtsSink : IDrawSink
    {
        std::vector<std::pair<Math::Point3, Math::Point3>> lines;
        int    dots = 0;
        double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

        int    mode = 0;
        double size = 0.0;
        bool   haveStyle = true;
        std::string layerName = "0";
        double wpp = 0.0, vh = 0.0;

        Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
        std::string  GetLayerName(LayerID) const override { return layerName; }
        bool GetPointStyle(int& m, double& s) const override
        { if (!haveStyle) return false; m = mode; s = size; return true; }
        double GetWorldPerPixel() const override    { return wpp; }
        double GetViewportHeightPx() const override { return vh; }

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override
        {
            lines.push_back({ a, b });
            for (const auto& p : { a, b })
            {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
        }
        void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override
        { ++dots; minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    };
}

int RunPointStyleTests()
{
    std::printf("\n=== 点样式（$PDMODE/$PDSIZE）===\n");
    const int before = g_failures;

    // ── 1. $PDMODE 位语义 ────────────────────────────────────────────
    Check(PdmodeShape(34) == 2,         "34 的基本形 = 2（十字）");
    Check(PdmodeHasCircle(34),          "34 含外接圆（bit5=32）");
    Check(!PdmodeHasSquare(34),         "34 不含外接方");
    Check(PdmodeHasCircle(98) && PdmodeHasSquare(98), "98 = 32|64|2：圆和方都有");
    Check(PdmodeShape(0) == 0,          "0 的基本形 = 实心点");

    // ── 2. $PDSIZE 尺寸换算 ──────────────────────────────────────────
    Check(std::abs(PointEntity::ResolveHalfSize(0.1, 0.0, 0.0) - 0.05) < 1e-12,
          "$PDSIZE=0.1（世界单位）→ 半尺寸 0.05");
    // 相对：0 = 视口 5%，wpp=0.01、视口高 1000px ⇒ 5/100*1000*0.01 = 0.5 世界单位
    Check(std::abs(PointEntity::ResolveHalfSize(0.0, 0.01, 1000.0) - 0.25) < 1e-12,
          "$PDSIZE=0（相对）＝视口 5% 高 ⇒ 半尺寸 = 5/100×1000×0.01/2 = 0.25");
    Check(std::abs(PointEntity::ResolveHalfSize(-3.0, 0.01, 1000.0) - 0.15) < 1e-12,
          "$PDSIZE=-3（相对）＝视口 3% 高（取绝对值）");
    Check(PointEntity::ResolveHalfSize(0.1, 0.0, 0.0) > 0.0,
          "世界单位尺寸不需要视口信息");
    Check(PointEntity::ResolveHalfSize(0.0, 0.0, 0.0) <= 0.0,
          "相对尺寸但拿不到视口 ⇒ 取不到（调用方退化成 1 像素点）");

    // ── 3. 用户那个点：$PDMODE=34 / $PDSIZE=0.1 ⇒ ⊕ ──────────────────
    {
        PointEntity p(1, { 1.494404150136852, 5.121502684938733, 0.0 });
        PtsSink s; s.mode = 34; s.size = 0.1;
        p.Draw(s, false, false);

        const double w = s.maxX - s.minX, h = s.maxY - s.minY;
        std::printf("    ⊕：线=%zu 点=%d  宽=%.6f 高=%.6f\n",
                    s.lines.size(), s.dots, w, h);
        Check(s.dots == 0, "有图形时不走点图元");

        // 十字臂长 = $PDSIZE = 0.1，所以总宽 = 2×0.1 = 0.2（十字比圆半径 0.05 长）
        Check(std::abs(w - 0.2) < 1e-9, "⊕ 总宽 = 0.200000（= 2×$PDSIZE）");
        Check(std::abs(h - 0.2) < 1e-9, "⊕ 总高 = 0.200000");

        // 必须含一条整根横线（十字）——从 -0.1 到 +0.1（相对中心）
        bool hasHoriz = false, hasVert = false;
        const double cx = 1.494404150136852, cy = 5.121502684938733;
        for (const auto& [a, b] : s.lines)
        {
            if (std::abs(a.y - cy) < 1e-12 && std::abs(b.y - cy) < 1e-12 &&
                std::abs(std::abs(a.x - cx) - 0.1) < 1e-9 &&
                std::abs(std::abs(b.x - cx) - 0.1) < 1e-9)
                hasHoriz = true;
            if (std::abs(a.x - cx) < 1e-12 && std::abs(b.x - cx) < 1e-12 &&
                std::abs(std::abs(a.y - cy) - 0.1) < 1e-9 &&
                std::abs(std::abs(b.y - cy) - 0.1) < 1e-9)
                hasVert = true;
        }
        Check(hasHoriz, "⊕ 含整根横线（半臂 0.1 = 整个 $PDSIZE）");
        Check(hasVert,  "⊕ 含整根竖线");

        // 外接圆：所有圆弧段的点到中心距离 ≈ 0.05
        int arcSegs = 0; double maxErr = 0.0;
        for (const auto& [a, b] : s.lines)
        {
            const double ra = std::hypot(a.x - cx, a.y - cy);
            const double rb = std::hypot(b.x - cx, b.y - cy);
            if (ra > 1e-9 && std::abs(ra - 0.05) < 1e-9 && std::abs(rb - 0.05) < 1e-9)
            { ++arcSegs; maxErr = std::max(maxErr, std::abs(rb - 0.05)); }
        }
        Check(arcSegs >= 16, "⊕ 含外接圆（半径 0.05，≥16 段拟合）");
        Check(maxErr < 1e-9, "外接圆各段端点都严格落在半径 0.05 上");
    }

    // ── 4. 其它常见 $PDMODE ──────────────────────────────────────────
    {
        // 十字（2）：无圆，宽 = 2×size
        PointEntity p(2, { 0, 0, 0 });
        PtsSink s; s.mode = 2; s.size = 2.0;
        p.Draw(s, false, false);
        Check(s.lines.size() == 2 && std::abs((s.maxX - s.minX) - 4.0) < 1e-9,
              "$PDMODE=2：只有 2 条十字线，宽 = 2×$PDSIZE = 4");
        Check(s.dots == 0, "$PDMODE=2 不走点图元");

        // 叉（3）
        PtsSink s3; s3.mode = 3; s3.size = 2.0;
        p.Draw(s3, false, false);
        Check(s3.lines.size() == 2, "$PDMODE=3：2 条斜线（叉）");

        // 方+十字（66 = 64|2）：十字 4 条 + 方 4 条
        PtsSink s66; s66.mode = 66; s66.size = 2.0;
        p.Draw(s66, false, false);
        Check(s66.lines.size() == 6, "$PDMODE=66（方+十字）：2 条十字 + 4 条方 = 6");
        Check(std::abs((s66.maxX - s66.minX) - 4.0) < 1e-9, "66 的外接方边长 = 2×半径 = 2");

        // 圆+方+十字（98）
        PtsSink s98; s98.mode = 98; s98.size = 2.0;
        p.Draw(s98, false, false);
        Check(s98.lines.size() >= 16 + 4 + 2, "$PDMODE=98：圆+方+十字都画");
    }

    // ── 5. $PDMODE=0 / 1 与 DEFPOINTS ────────────────────────────────
    {
        PointEntity p(3, { 0, 0, 0 });

        PtsSink s0; s0.mode = 0; s0.size = 5.0;
        p.Draw(s0, false, false);
        Check(s0.dots == 1 && s0.lines.empty(),
              "$PDMODE=0：1 个点图元、不画线（AutoCAD 默认实心小点）");

        PtsSink s1; s1.mode = 1; s1.size = 5.0;
        p.Draw(s1, false, false);
        Check(s1.dots == 0 && s1.lines.empty(), "$PDMODE=1：什么都不画");

        // DEFPOINTS 层：即使 $PDMODE=34 也按小点
        PtsSink sd; sd.mode = 34; sd.size = 0.1; sd.layerName = "Defpoints";
        p.Draw(sd, false, false);
        Check(sd.dots == 1 && sd.lines.empty(), "DEFPOINTS 层忽略 $PDMODE，仍画实心小点");

        PtsSink sd2; sd2.mode = 34; sd2.size = 0.1; sd2.layerName = "块A|DEFPOINTS";
        p.Draw(sd2, false, false);
        Check(sd2.dots == 1, "块内图层「块名|DEFPOINTS」也识别（取 | 之后那段）");

        PtsSink sn; sn.mode = 34; sn.size = 0.1; sn.layerName = "DEFPOINTSX";
        p.Draw(sn, false, false);
        Check(sn.lines.size() > 0, "「DEFPOINTSX」不是 DEFPOINTS，照常按 $PDMODE 画");
    }

    // ── 6. sink 不知道点样式时（选中流/探针）退回实心小点 ────────────
    {
        PointEntity p(4, { 0, 0, 0 });
        PtsSink s; s.haveStyle = false;
        p.Draw(s, false, false);
        Check(s.dots == 1 && s.lines.empty(),
              "sink 不提供点样式时退回实心小点（不崩、不空画）");
    }

    // ── 7. 相对尺寸（$PDSIZE<=0）在视口信息可得时能算出来 ───────────
    {
        PointEntity p(5, { 0, 0, 0 });
        PtsSink s; s.mode = 34; s.size = -10.0; s.wpp = 1.0; s.vh = 100.0;
        // pct=10 → 10/100×100×1 = 10 世界单位 ⇒ 半径 5 ⇒ 十字臂 10 ⇒ 总宽 20
        p.Draw(s, false, false);
        Check(s.dots == 0 && std::abs((s.maxX - s.minX) - 20.0) < 1e-9,
              "$PDSIZE=-10（视口 10%）+ 视口信息 ⇒ 总宽 20 世界单位");

        // 同样的点，拿不到视口信息 ⇒ 退化成 1 像素点（不能画成世界尺寸的小十字）
        PtsSink s2; s2.mode = 34; s2.size = -10.0;
        p.Draw(s2, false, false);
        Check(s2.dots == 1 && s2.lines.empty(),
              "相对尺寸 + 无视口信息 ⇒ 退化成 1 像素点（不画成世界尺寸小十字）");
    }

    // ── 8. Scene 级存取（对话框改的就是它）──────────────────────────
    {
        Scene scene;
        Check(scene.GetPointDisplayMode() == 0 && scene.GetPointDisplaySize() == 0.0,
              "Scene 默认 $PDMODE=0/$PDSIZE=0（= AutoCAD 默认实心小点）");
        scene.SetPointDisplayMode(34);
        scene.SetPointDisplaySize(0.1);
        Check(scene.GetPointDisplayMode() == 34 &&
              std::abs(scene.GetPointDisplaySize() - 0.1) < 1e-12,
              "Scene 能存取 $PDMODE/$PDSIZE（点样式对话框改的就是它）");
    }

    // ── 9. AutoCAD DDPTYPE 的 20 个图标（对话框按它排，别少排/错排）────
    //
    // 用户报「点样式没实现」时指的是：DDPTYPE 对话框有 20 个图标（4 行 × 5 列），
    // 我们只给了 5 个基本形的单选 + 两个"外加圆/方"复选框 —— 功能上等价，
    // 但**少了那一整片图标视图**，且 96（方+圆）这档根本点不出来。
    {
        // 4 行 = 外框，5 列 = 基本形；按行优先展开必须正好是这 20 个值
        const int want[20] = {
             0,  1,  2,  3,  4,      // 无外框
            32, 33, 34, 35, 36,      // 外接圆
            64, 65, 66, 67, 68,      // 外接方
            96, 97, 98, 99, 100,     // 方 + 圆
        };
        int got[20]; int k = 0;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 5; ++c)
                got[k++] = PdmodeAt(r, c);

        bool same = true;
        for (int i = 0; i < 20; ++i) if (got[i] != want[i]) same = false;
        std::printf("    20 图标 $PDMODE =");
        for (int i = 0; i < 20; ++i) std::printf(" %d", got[i]);
        std::printf("\n");
        Check(same, "DDPTYPE 20 个图标 = 4 外框 × 5 基本形，值为 0..4/32..36/64..68/96..100");

        Check(kPdmodeOuter[0] == 0 && kPdmodeOuter[1] == 32 &&
              kPdmodeOuter[2] == 64 && kPdmodeOuter[3] == 96,
              "4 行外框依次是 无 / 圆(32) / 方(64) / 方+圆(96)");

        // 每一档都必须真的画得出东西（96 那档此前完全无法选择）
        for (int i = 0; i < 20; ++i)
        {
            PointEntity p(100 + i, { 0, 0, 0 });
            PtsSink s; s.mode = got[i]; s.size = 2.0;
            p.Draw(s, false, false);
            const bool drawsNothing = (got[i] % 16 == 1) && !(got[i] & 32) && !(got[i] & 64);
            const bool ok = drawsNothing ? (s.lines.empty() && s.dots == 0)
                                         : (s.lines.size() > 0 || s.dots > 0);
            if (!ok) std::printf("    **图标 %d（$PDMODE=%d）画不出东西**\n", i, got[i]);
            Check(ok, "20 个图标逐个都能画出图形（或按 $PDMODE=1 正确地什么都不画）");
        }

        // 外框与基本形的组合关系：96 = 32|64，圆和方必须同时出现
        Check(PdmodeHasCircle(96) && PdmodeHasSquare(96) && PdmodeShape(96) == 0,
              "$PDMODE=96 = 圆+方+实心点（圆方都置位）");
    }

    const int n = g_failures - before;
    std::printf("点样式用例失败：%d\n", n);
    return n;
}
