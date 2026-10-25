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
//
// 注：那条「一个周期不足 4 像素就退化成实线」的护栏后来去掉了，改为
// 「周期过密时按比例放大周期」的节流（见第 5 节）—— 保持 dash/gap 结构，
// 绝不跳变成一条实线。
#include "Scene/Scene.h"
#include "Document/Document.h"
#include "Document/Command/DrawingVarsCommand.h"
#include <filesystem>
#include "Document/DrawContext.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
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

    // ── 5. 周期过密时节流但不退化 ──────────────────────────────────
    // 旧行为：一个周期不足 4px 就压成一条实线（线型会突然跳变成光杆线）。
    // 现在改为只按比例放大铺设周期：一个周期至少 4px、整条线最多 4000 个
    // 周期，dash/gap 比例不变，绝不跳变成实线。
    // 这里 wpp = 1.0（DASHED 周期 0.75 远小于 4）触发节流，断言仍然铺出
    // 多段；同时断言段数被压到远小于「不节流时的 100/0.75 ≈ 133 段」。
    {
        Scene s;
        const LineTypeRecord* dash = s.GetLineTypeTable().Find(s.GetLineTypeTable().FindByName("DASHED"));
        if (dash && !dash->IsContinuous())
        {
            double count = 0;
            MeasureDashCycle(s, 1.0, 1.0, 100.0, &count);
            std::printf("    周期过密：段数 %.0f（不节流时约 133）\n", count);
            Check(count > 1.0, "周期不足 4px 时仍按周期铺（不退化成实线）");
            Check(count < 60.0, "周期过密时按比例放大周期（节流生效）");
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

    const int n = g_failures - before;
    std::printf("全局线型比例用例失败：%d\n", n);
    return n;
}
