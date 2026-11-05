// ── 全局线型比例（$LTSCALE）测试 ────────────────────────────────────────────
// 回归点：用户报「新建图纸能看出线型，test0.dwg 看不出」。
//
// 根因：虚线实际周期 = LTYPE图案周期 × 实体线型比例(组码48) × 全局$LTSCALE。
// test0.dwg 的 $LTSCALE = 1000（大尺寸图纸让虚线物理长度合理），而 LTYPE 表里
// DASHED 的图案周期只有 0.35。旧实现**从不读 $LTSCALE**（MiniCAD 侧零处使用），
// 周期因此小 1000 倍 = 0.35；再叠加 DrawContext::EmitStyled 里
// 「一个周期不足 4 像素就退化为实线」的护栏，任何正常缩放下虚线都整条消失。
//
// 实测（真实 test0.dxf，ODA 转换的权威版本）：
//   修复前 周期 0.35 → 只画 1 段（实线）
//   修复后 周期 350.0 = 0.35 × 1000，虚线正常
#include "Scene/Scene.h"
#include "Document/Document.h"
#include "Document/Command/DrawingVarsCommand.h"
#include <filesystem>
#include "Document/DrawContext.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>
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

    // 铺一条直线，返回相邻实线段的间距（= 实际虚线周期）；不足 2 段返回 -1。
    // lineTypeScale 经 ctx.SetLineTypeScale 传（与 Editor 给场景流传值同一条路）。
    double MeasureDashCycle(const Scene& scene, double lineTypeScale, double wpp,
                            double length, double* outCount = nullptr)
    {
        std::vector<Vertex_P3_C4>    verts;
        std::vector<Vertex_P3_C4>    fills;
        std::vector<Vertex_P3_C4_UV> texts;
        Overlay                       overlay;

        DrawContext ctx(verts, fills, texts, overlay,
                        scene.GetLayerManager(), scene.GetLineTypeTable(),
                        wpp, nullptr, nullptr);
        ctx.SetLineTypeScale(lineTypeScale);

        EntityAttr a;
        a.LayerId  = 0;
        a.LineType = scene.GetLineTypeTable().FindByName("DASHED");
        ctx.BeginEntity(&a);
        ctx.DrawLine({ 0, 0, 0 }, { length, 0, 0 }, Math::Color4::White(), false);

        if (outCount)
            *outCount = static_cast<double>(verts.size() / 2);

        std::vector<double> starts;
        for (size_t i = 0; i + 1 < verts.size(); i += 2)
            starts.push_back(verts[i].pos.x);
        return starts.size() >= 2 ? starts[1] - starts[0] : -1.0;
    }
}

int RunLineTypeScaleTests()
{
    g_failures = 0;
    std::printf("\n=== 全局线型比例（$LTSCALE）===\n");
    const int before = g_failures;

    // ── 1. Scene 承载与取值 ──────────────────────────────────────
    {
        Scene s;
        Check(Near(s.GetLineTypeScale(), 1.0), "Scene 默认全局线型比例 = 1.0");

        s.SetLineTypeScale(1000.0);
        Check(Near(s.GetLineTypeScale(), 1000.0), "SetLineTypeScale(1000) 生效");

        // 非法值（0 / 负数）退回 1，避免把周期乘成 0 或负数
        s.SetLineTypeScale(0.0);
        Check(Near(s.GetLineTypeScale(), 1.0), "SetLineTypeScale(0) 退回 1.0");
        s.SetLineTypeScale(-5.0);
        Check(Near(s.GetLineTypeScale(), 1.0), "SetLineTypeScale(负数) 退回 1.0");
    }

    // ── 2. 可跨 Scene 传递（另存为/读取后不丢）──────────────────
    {
        Scene s;
        s.SetLineTypeScale(250.0);
        Scene t;
        t.SetLineTypeScale(s.GetLineTypeScale());
        Check(Near(t.GetLineTypeScale(), 250.0), "全局线型比例可跨 Scene 传递");
    }

    // ── 3. 渲染时三层相乘（这是本修复的核心）─────────────────────
    {
        Scene s;
        // 内置线型表里的虚线叫 DASHED（不是 DASH）—— 踩过一次：
        // 旧版用例查 "DASH" 查不到，整段渲染断言被静默跳过 ⇒ **假通过**。
        // 所以这里查不到直接判失败，不允许"没有就跳过"。
        const LineTypeID dashId = s.GetLineTypeTable().FindByName("DASHED");
        const LineTypeRecord* dash = s.GetLineTypeTable().Find(dashId);
        Check(dash != nullptr && !dash->IsContinuous(),
              "内置线型表里有虚线线型 DASHED（查不到就不能继续，否则是假通过）");
        if (dash && !dash->IsContinuous())
        {
            const double pat = dash->PatternLength;
            std::printf("    DASHED 图案周期 = %.4f\n", pat);

            // 全局 = 1：周期就是图案周期
            const double c1 = MeasureDashCycle(s, 1.0, 0.001, 100.0);
            Check(Near(c1, pat, 1e-6), "全局=1：周期 == 图案周期");

            // 全局 = 1000：周期放大 1000 倍（test0 的情形）
            s.SetLineTypeScale(1000.0);
            const double c1000 = MeasureDashCycle(s, 1000.0, 0.001, 100000.0);
            Check(Near(c1000, pat * 1000.0, 1e-3), "全局=1000：周期 == 图案周期 × 1000");

            // 反向验证：全局=1 时周期不该被放大（防止把乘数写成加法或写反）
            const double cBack = MeasureDashCycle(s, 1.0, 0.001, 100.0);
            Check(Near(cBack, pat, 1e-6), "全局回到 1：周期回到图案周期");
        }
    }

    // ── 4. 实体级比例(组码48) × 全局 仍是两层相乘 ────────────────
    {
        Scene s;
        const LineTypeRecord* dash = s.GetLineTypeTable().Find(s.GetLineTypeTable().FindByName("DASHED"));
        Check(dash != nullptr && !dash->IsContinuous(), "DASHED 可用于验证两层相乘");
        if (dash && !dash->IsContinuous())
        {
            s.SetLineTypeScale(100.0);
            std::vector<Vertex_P3_C4>    verts;
            std::vector<Vertex_P3_C4>    fills;
            std::vector<Vertex_P3_C4_UV> texts;
            Overlay                       overlay;
            DrawContext ctx(verts, fills, texts, overlay,
                            s.GetLayerManager(), s.GetLineTypeTable(),
                            0.001, nullptr, nullptr);
            ctx.SetLineTypeScale(s.GetLineTypeScale());

            EntityAttr a;
            a.LayerId        = 0;
            a.LineType       = s.GetLineTypeTable().FindByName("DASHED");
            a.LinetypeScale  = 2.0;                 // 实体级组码 48
            ctx.BeginEntity(&a);
            ctx.DrawLine({ 0, 0, 0 }, { 100000, 0, 0 }, Math::Color4::White(), false);

            std::vector<double> starts;
            for (size_t i = 0; i + 1 < verts.size(); i += 2)
                starts.push_back(verts[i].pos.x);
            Check(starts.size() >= 2, "虚线被铺出来（段数 ≥ 2）");
            if (starts.size() >= 2)
            {
                const double measured = starts[1] - starts[0];
                const double expected = dash->PatternLength * 2.0 * 100.0;
                std::printf("    组码48=2 × 全局=100：实测 %.4f，期望 %.4f\n", measured, expected);
                Check(Near(measured, expected, 1e-3), "实体比例 × 全局比例 两层都乘进去了");
            }
        }
    }

    // ── 5. 虚线几何与缩放无关：周期过密（一个周期不足 1px）也照样铺出来 ─────
    // 以前按「不足 4px 退化为实线」取舍，代价是每格滚轮都要把整张图的虚线实体重新细分；
    // 现在虚线进静态流，缩放不重建，段数与 wpp 无关。dash 数量过大（> 4000 段）的护栏仍在
    {
        Scene s;
        const LineTypeRecord* dash = s.GetLineTypeTable().Find(s.GetLineTypeTable().FindByName("DASHED"));
        if (dash && !dash->IsContinuous())
        {
            double countFine = 0, countCoarse = 0;
            MeasureDashCycle(s, 1.0, 0.001, 100.0, &countFine);
            MeasureDashCycle(s, 1.0, 1.0, 100.0, &countCoarse);
            std::printf("    wpp=0.001 段数 %.0f，wpp=1 段数 %.0f\n", countFine, countCoarse);
            Check(countCoarse > 1.0 && countCoarse == countFine, "虚线段数不随 wpp 变化（缩放不重新细分）");

            double countHuge = 0;
            MeasureDashCycle(s, 1.0, 0.001, dash->PatternLength * 5000.0, &countHuge);
            Check(countHuge <= 1.0, "dash 数量过大（> 4000 段）时退化为实线（护栏仍在）");

            // 一段超过 200 个周期的长虚线仍按像素取舍：缩小（周期不足 4px）时退化为实线，放大时铺满
            double countLongCoarse = 0, countLongFine = 0;
            MeasureDashCycle(s, 1.0, 1.0, dash->PatternLength * 1000.0, &countLongCoarse);
            MeasureDashCycle(s, 1.0, 0.001, dash->PatternLength * 1000.0, &countLongFine);
            std::printf("    1000 个周期的长线：wpp=1 段数 %.0f，wpp=0.001 段数 %.0f\n", countLongCoarse, countLongFine);
            Check(countLongCoarse <= 1.0 && countLongFine >= 999.0, "超过 200 个周期的长虚线仍按像素取舍（缩小为实线、放大铺满）");

            // 场景流开启延迟：长虚线段只记录不铺设，实体不随视图变化；Editor 再按缩放铺进动态流
            {
                std::vector<Vertex_P3_C4> verts, fills;
                std::vector<Vertex_P3_C4_UV> texts;
                Overlay ov;
                DrawContext ctx(verts, fills, texts, ov, s.GetLayerManager(), s.GetLineTypeTable(), 1.0, nullptr, nullptr);
                ctx.SetDeferLongDashes(true);
                EntityAttr a;
                a.LayerId  = 0;
                a.LineType = s.GetLineTypeTable().FindByName("DASHED");
                ctx.BeginEntity(&a);
                ctx.DrawLine({ 0, 0, 0 }, { dash->PatternLength * 1000.0, 0, 0 }, Math::Color4::White(), false);
                ctx.DrawLine({ 0, 10, 0 }, { dash->PatternLength * 10.0, 10, 0 }, Math::Color4::White(), false);   // 短的照常铺
                ctx.BeginEntity(nullptr);
                const auto deferred = ctx.TakeDeferredDashes();
                std::printf("    延迟：长段 %zu 条，直接铺出的线段 %zu\n", deferred.size(), verts.size() / 2);
                Check(deferred.size() == 1 && verts.size() / 2 >= 10 && verts.size() / 2 <= 11 && !ctx.HasViewDependentGeometry(),
                      "开启延迟：长虚线段只记录，短虚线照常铺，实体不随视图变化");

                std::vector<Vertex_P3_C4> coarse, fine;
                if (deferred.size() == 1)
                {
                    DrawContext::EmitDeferredDash(deferred[0], s.GetLineTypeTable(), 1.0, coarse);
                    DrawContext::EmitDeferredDash(deferred[0], s.GetLineTypeTable(), 0.001, fine);
                }
                std::printf("    延迟段：wpp=1 段数 %zu，wpp=0.001 段数 %zu\n", coarse.size() / 2, fine.size() / 2);
                Check(coarse.size() == 2 && fine.size() / 2 >= 999, "延迟段按缩放铺设：缩小为一段实线、放大铺满");
            }

            // 虚线不随视图变化：细分后 HasViewDependentGeometry 为 false
            std::vector<Vertex_P3_C4> verts, fills;
            std::vector<Vertex_P3_C4_UV> texts;
            Overlay ov;
            DrawContext ctx(verts, fills, texts, ov, s.GetLayerManager(), s.GetLineTypeTable(), 0.01, nullptr, nullptr);
            EntityAttr a;
            a.LayerId  = 0;
            a.LineType = s.GetLineTypeTable().FindByName("DASHED");
            ctx.BeginEntity(&a);
            ctx.DrawLine({ 0, 0, 0 }, { 100, 0, 0 }, Math::Color4::White(), false);
            ctx.BeginEntity(nullptr);
            Check(verts.size() >= 4 && !ctx.HasViewDependentGeometry(), "虚线实体不标记为随视图变化（进静态流）");
        }
    }

    // ── 6. 线型里的“点”笔划走点流（恒定 1 像素，与缩放无关）────────────────
    {
        Scene s;
        auto& lt = s.GetLineTypeTable();
        const auto dotId = lt.FindByName("DASHDOT");
        const LineTypeRecord* rec = lt.Find(dotId);
        if (rec && !rec->IsContinuous())
        {
            std::vector<Vertex_P3_C4> verts, fills;
            std::vector<Vertex_P3_C4_UV> texts;
            Overlay ov;
            DrawContext ctx(verts, fills, texts, ov, s.GetLayerManager(), lt, 0.01, nullptr, nullptr);
            EntityAttr a;
            a.LayerId  = 0;
            a.LineType = dotId;
            ctx.BeginEntity(&a);
            ctx.DrawLine({ 0, 0, 0 }, { rec->PatternLength * 10.0, 0, 0 }, Math::Color4::White(), false);
            ctx.BeginEntity(nullptr);
            const auto points = ctx.TakePoints();
            std::printf("    DASHDOT 10 个周期：线段 %zu，点 %zu\n", verts.size() / 2, points.size());
            Check(points.size() >= 9 && !ctx.HasViewDependentGeometry(), "点笔划进点流，且不随视图变化");

            // 选中流：点笔划画成小段（没有点流），随缩放重建
            verts.clear();
            DrawContext sel(verts, fills, texts, ov, s.GetLayerManager(), lt, 0.01, nullptr, nullptr);
            sel.SetLineTypeDotsAsSegments(true);
            sel.BeginEntity(&a);
            sel.DrawLine({ 0, 0, 0 }, { rec->PatternLength * 10.0, 0, 0 }, Math::Color4::White(), false);
            sel.BeginEntity(nullptr);
            Check(sel.TakePoints().empty() && sel.HasViewDependentGeometry(), "选中流：点笔划画成小段并标记随视图变化");
        }
    }

    // ── .mcad / JSON 往返：$PDMODE / $PDSIZE / $LTSCALE 随文档保存 ─────────
    //
    // 临时目录用「先 %TEMP%、失败退当前目录」的探测式选法（与 RecentFilesTests 的
    // TempDir() 同一套）：实测在有些进程里 %TEMP% 建不出文件（errno 13 / EACCES），
    // 而同一个路径在别的进程里可写 —— 只看目录存在或只看 temp_directory_path()
    // 都发现不了，必须真的写一个文件试。探测失败就静默降级，否则整条往返用例
    // 会因为环境而不是代码失败。
    for (const char* ext : { ".mcad", ".json" })
    {
        std::error_code ec;
        std::filesystem::path dir;
        for (const std::filesystem::path& cand :
             { std::filesystem::temp_directory_path(ec),
               std::filesystem::current_path(ec) })
        {
            if (cand.empty()) continue;
            const std::filesystem::path probe = cand / (std::string("minicad_dvars_probe") + ext);
            {
                std::ofstream out(probe, std::ios::binary | std::ios::trunc);
                if (!out) continue;
                out << "x";
                out.close();
            }
            ec.clear();
            std::filesystem::remove(probe, ec);
            if (!ec) { dir = cand; break; }
        }
        if (dir.empty())
        {
            Check(false, "找不到可写的临时目录（%TEMP% 与当前目录都不可写）");
            continue;
        }

        const auto path = dir / (std::string("MiniCADDrawingVars") + ext);
        const auto u8 = path.u8string();
        const std::string file(u8.begin(), u8.end());
        {
            Document doc;
            doc.GetScene().SetPointDisplayMode(34);
            doc.GetScene().SetPointDisplaySize(-7.5);
            doc.GetScene().SetLineTypeScale(1000.0);
            Check(doc.SaveAs(file), "保存带图纸变量的文档");
        }
        Document doc;
        const bool ok = doc.LoadFromFile(file);
        std::filesystem::remove(path);
        const Scene& s = doc.GetScene();
        Check(ok && s.GetPointDisplayMode() == 34 && Near(s.GetPointDisplaySize(), -7.5) && Near(s.GetLineTypeScale(), 1000.0),
              ext[1] == 'm' ? ".mcad 往返保留点样式与线型比例" : "JSON 往返保留点样式与线型比例");
    }

    // ── 修改图纸变量走命令：可撤销、标记文档已修改 ─────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const DrawingVars orig = DrawingVars::From(s);
        DrawingVars changed = orig;
        changed.PointMode     = 3;
        changed.LineTypeScale = 50.0;
        doc.GetCommandStack().Execute(std::make_unique<SetDrawingVarsCommand>(orig, changed, "测试"), s);
        Check(s.GetPointDisplayMode() == 3 && Near(s.GetLineTypeScale(), 50.0), "SetDrawingVarsCommand 执行后生效");
        Check(doc.IsDirty(), "SetDrawingVarsCommand 执行后文档标记为已修改");
        doc.GetCommandStack().Undo(s);
        Check(DrawingVars::From(s) == orig, "撤销后图纸变量还原");
    }

    // ── 多段线的圆弧段：虚线相位跨段连续（r=1350 的 90° 弧，细分小段远短于周期 750）──
    {
        const Math::Point3 A{ 0.0, 0.0, 0.0 };
        const Math::Point3 B{ 1350.0, 1350.0, 0.0 };
        Polyline pl({ A, B }, { 0.414214 });

        Scene s;
        s.SetLineTypeScale(1000.0);
        auto& lt = s.GetLineTypeTable();
        const auto dashId = lt.FindByName("DASHED");
        const LineTypeRecord* rec = lt.Find(dashId);
        if (rec && !rec->IsContinuous())
        {
            std::vector<Vertex_P3_C4>    verts;
            std::vector<Vertex_P3_C4>    fills;
            std::vector<Vertex_P3_C4_UV> texts;
            Overlay                       overlay;
            DrawContext ctx(verts, fills, texts, overlay,
                            s.GetLayerManager(), lt, 0.01, nullptr, nullptr);
            ctx.SetLineTypeScale(s.GetLineTypeScale());
            EntityAttr a;
            a.LayerId  = 0;
            a.LineType = dashId;
            ctx.BeginEntity(&a);
            PolylineEntity ent(1, std::move(pl));
            // 零宽度走细线路径（默认 m_width = 1.0 走填充）
            ent.SetWidth(0.0);
            ent.SetSegmentWidths({ 0.0 }, { 0.0 });
            ent.Draw(ctx, false, false);
            ctx.BeginEntity(nullptr);

            // 逐段累加输出的实段长度（顶点流是两两成段的 TRIANGLELIST 风格点对）
            const double arcLen = 1350.0 * Math::PI * 0.5;
            auto measure = [&](double wpp)
            {
                std::vector<Vertex_P3_C4>    v2;
                std::vector<Vertex_P3_C4>    f2;
                std::vector<Vertex_P3_C4_UV> t2;
                Overlay                       ov2;
                DrawContext c2(v2, f2, t2, ov2, s.GetLayerManager(), lt, wpp, nullptr, nullptr);
                c2.SetLineTypeScale(s.GetLineTypeScale());
                c2.BeginEntity(&a);
                ent.Draw(c2, false, false);
                c2.BeginEntity(nullptr);
                double solid = 0.0;
                for (size_t i = 0; i + 1 < v2.size(); i += 2)
                {
                    const auto& p0 = v2[i].pos;
                    const auto& p1 = v2[i + 1].pos;
                    solid += std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
                }
                return std::make_pair(solid, v2.size());
            };

            // ── 周期数不超过 kStaticMaxDashes 时与缩放无关，同直线 ──
            {
                const double wpps[] = { 2222.0, 555.0, 111.1, 22.2, 5.56, 1.0 };   // 整图一屏 → 放大看清
                int dashedAt = 0;
                for (double wpp : wpps)
                {
                    const auto [solid, count] = measure(wpp);
                    const double r = solid / arcLen;
                    std::printf("    wpp=%-8.1f 周期 %.2fpx 实线 %.1f%%（%zu 段）\n", wpp, 0.75 * 1000.0 / wpp, r * 100.0, count / 2);
                    if (!count || r > 0.92) continue;
                    ++dashedAt;
                }
                Check(dashedAt == static_cast<int>(sizeof(wpps) / sizeof(wpps[0])), "多段线圆弧段的虚线不随缩放退化（各缩放级别都是虚线）");
            }

            const auto [solidLen, segCount] = measure(0.01);
            (void)segCount;
            const double ratio = solidLen / arcLen;
            std::printf("    多段线圆弧段：实线 %.1f / 弧长 %.1f = %.1f%%\n", solidLen, arcLen, ratio * 100.0);
            // 逐段 DrawLine 时为 100%（整条实线）；跨段累计后必须留出 gap
            Check(ratio > 0.0 && ratio < 0.92, "多段线圆弧段：虚线相位跨段累计，不是整条实线");

            // ── 带宽度的多段线：虚线同样铺开（逐点宽度的 DrawPath）──
            {
                auto coverage = [&](double width)
                {
                    Polyline seg({ { 0.0, 0.0, 0.0 }, { 4000.0, 0.0, 0.0 } }, { 0.0 });
                    PolylineEntity e2(1, std::move(seg));
                    e2.SetWidth(width);
                    std::vector<Vertex_P3_C4>    v2;
                    std::vector<Vertex_P3_C4>    f2;
                    std::vector<Vertex_P3_C4_UV> t2;
                    Overlay                       ov2;
                    DrawContext c2(v2, f2, t2, ov2, s.GetLayerManager(), lt, 0.01, nullptr, nullptr);
                    c2.SetLineTypeScale(s.GetLineTypeScale());
                    c2.BeginEntity(&a);
                    e2.Draw(c2, false, false);
                    c2.BeginEntity(nullptr);
                    // 每个三角形在中心线(x 轴)上的跨度，合并后即覆盖长度
                    std::vector<std::pair<double, double>> tris;
                    for (size_t i = 0; i + 2 < f2.size(); i += 3)
                    {
                        const double lo = std::min({ f2[i].pos.x, f2[i + 1].pos.x, f2[i + 2].pos.x });
                        const double hi = std::max({ f2[i].pos.x, f2[i + 1].pos.x, f2[i + 2].pos.x });
                        tris.push_back({ lo, hi });
                    }
                    if (tris.empty()) return 0.0;
                    std::sort(tris.begin(), tris.end());
                    double covered = 0.0, curL = 0.0, curR = -1.0;
                    for (const auto& tr : tris)
                    {
                        if (tr.first > curR) { if (curR >= curL) covered += curR - curL; curL = tr.first; curR = tr.second; }
                        else curR = std::max(curR, tr.second);
                    }
                    if (curR >= curL) covered += curR - curL;
                    return covered / 4000.0;
                };
                const double covWide  = coverage(20.0);
                const double covThin  = coverage(5.0);
                std::printf("    粗多段线（宽 20）沿中心线覆盖 %.1f%%，（宽 5）%.1f%%\n", covWide * 100.0, covThin * 100.0);
                Check(covWide > 0.0 && covWide < 0.92, "带宽度的多段线：虚线铺开（不是整条实线）");
                Check(covThin > 0.0 && covThin < 0.92, "窄带宽度的多段线：虚线铺开（不是整条实线）");
            }

            // ── 长虚线多段线（整条超过 kStaticMaxDashes 个周期、每段不足）：4px 按整条判断 ──
            {
                const Math::Point3 rp[] = {
                    { 0.0, 0.0, 0.0 }, { 18000.0, 0.0, 0.0 }, { 30000.0, -5000.0, 0.0 },
                    { 22000.0, -12000.0, 0.0 }, { 6000.0, -13000.0, 0.0 }, { -13000.0, -13000.0, 0.0 },
                    { 0.0, 0.0, 0.0 },
                };
                const double rb[] = { 0.0, -0.2, -0.96, 0.42, 0.0, 0.0 };

                Scene s2;
                s2.SetLineTypeScale(100.0);            // 周期 350
                auto& lt2 = s2.GetLineTypeTable();
                LineTypeRecord dr;
                dr.Name = "DASH"; dr.Description = "Dashed";
                dr.Pattern = { 0.25, -0.1 }; dr.PatternLength = 0.35;
                const LineTypeID did = lt2.Add(dr);

                PolylineEntity e(1, std::vector<Math::Point3>(rp, rp + 7), std::vector<double>(rb, rb + 6));
                e.SetWidth(100.0);
                EntityAttr ea;
                ea.LayerId = 0;
                ea.LineType = did;
                ea.LinetypeScale = 10.0;           // (48 . 10.0)
                e.SetAttr(ea);

                // 同一缩放下输出几何量应当是「整条一种状态」，不能在 4px 边界处忽多忽少
                auto fillCount = [&](double wpp)
                {
                    std::vector<Vertex_P3_C4>    v3;
                    std::vector<Vertex_P3_C4>    f3;
                    std::vector<Vertex_P3_C4_UV> t3;
                    Overlay                       o3;
                    DrawContext c3(v3, f3, t3, o3, s2.GetLayerManager(), lt2, wpp, nullptr, nullptr);
                    c3.SetLineTypeScale(s2.GetLineTypeScale());
                    c3.BeginEntity(&ea);
                    e.Draw(c3, false, false);
                    c3.BeginEntity(nullptr);
                    return f3.size();
                };
                // wpp=100 ⇒ 周期 3.5px（不足 4px，整条应退化为实线）；wpp=50 ⇒ 7px（正常铺 dash）
                const size_t coarse = fillCount(100.0);
                const size_t fine   = fillCount(50.0);
                std::printf("    长虚线多段线：周期3.5px 顶点 %zu，周期7px 顶点 %zu\n", coarse, fine);
                Check(fine > coarse, "长虚线多段线：周期不足 4px 时整条退化为实线（不是逐段密集铺）");
                Check(coarse > 0, "长虚线多段线：缩小时仍有几何输出（不是整条消失）");
            }
        }
    }

    const int n = g_failures - before;
    std::printf("全局线型比例用例失败：%d\n", n);
    return n;
}
