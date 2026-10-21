// ── 块内图元定位 / 块内线型 / 裁剪块显示编辑 / 文字强行换行 ──────────────────
//
// 四项都对照 V0.1.0 的修复记录移植，每条都先量出"修前"的实测值再动手：
//
//  ① 块内图元定位（V0.1.0 修复记录 V17）
//     标注的匿名块（*D…）几何建在**世界坐标**上，AutoCAD 靠组码 12 把它平移
//     `local = worldGeom + code12`。此前插入点写死 (0,0,0) ⇒ 整条标注错位。
//     实测《总说明.dwg》：实绘宽度 910056 而非 7525（错位 92 万单位）。
//
//  ② 块内线型（用户报「块内线型显示不对」）
//     块参照展开时整块只在外层设了一次绘制上下文，块内成员**自己的**线型被
//     INSERT 的覆盖 ⇒ 块内 DASHED 一律画成实线。
//     实测（本文件第 2 组）：修前 1 段直线，修后 1334 段虚线。
//
//  ③ 裁剪块的显示与编辑（V0.1.0 V18/V19/V22/V23）
//     · 边框开关（$XCLIPFRAME）：只有 None 才不显示；DisplayNotPlot=2 是"显示但不打印"。
//     · 边框平时就要画，不能只在选中时才出现。
//     · 悬停高亮也要按裁剪后绘制，否则悬停会把整块亮出来。
//     · 夹点：顶点夹点只动被拖的那个角；边中点夹点整条边平移。
//     · 坐标系：`m_clip` 是**块坐标**，夹点/鼠标是**世界坐标**，必须互相换算。
//     · 撤销：拖裁剪夹点时块本身没动，消费端不得把块移到 (0,0,0)。
//     · 持久化：边框开关随文档保存。
//
//  ④ 文字强行换行（V0.1.0 V28 / V12）
//     MTEXT 只在**词边界**折行；不含空格的超长 token 不拆开，而是溢出栏宽。
//     实测（铂尔曼核位红线图改.dwg 的 MTEXT 2992）：41=53.926、字高 15、
//     文本 "420114001037GB10066" 18 字符无空格 ⇒ 逐字符折行 4 行（错）、
//     词边界折行 1 行（与 CAD 一致）。
#include "Scene/Scene.h"
#include "Scene/BlockTable.h"
#include "Scene/LayerManager.h"
#include "Scene/LineTypeTable.h"
#include "Document/DrawContext.hpp"
#include "Document/Document.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Text/Layout/TextLayoutEngine.h"
#include "Text/FontSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

    // 记录线段与颜色（只关心几何）
    struct RecSink : IDrawSink
    {
        std::vector<std::pair<Math::Point3, Math::Point3>> lines;
        std::vector<Math::Color4> colors;

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4& c, bool) override
        { lines.emplace_back(a, b); colors.push_back(c); }
        Math::Color4 GetLayerColor(LayerID) const override { return { 0.0, 1.0, 0.0, 1.0 }; }
        bool IsLayerVisible(LayerID) const override { return true; }
    };

    size_t LineCount(TextLayoutEngine& eng, IFont* f, const std::string& t, double box)
    {
        auto res = eng.Layout(t, f, 15.0, 1.0, 0.0, box);
        std::vector<double> ys;
        for (const auto& g : res.m_glyphs)
        {
            bool seen = false;
            for (double y : ys)
                if (std::abs(y - g.m_position.y) < 1e-6) { seen = true; break; }
            if (!seen) ys.push_back(g.m_position.y);
        }
        return ys.size();
    }
}

int RunBlockFixTests()
{
    g_failures = 0;

    // ══ ① 块内图元定位：块基点 + 缩放 + 旋转 都参与变换 ══════════════════
    {
        Scene s;
        BlockTable& tbl = s.GetBlockTable();
        // 块基点故意非零：块内线从 (10,20) 到 (20,20)
        const BlockID bid = tbl.Create("*BF_BASE", Math::Point3{ 10, 20, 0 });
        BlockEntity* blk = tbl.Find(bid);
        if (blk)
            blk->AddEntity(std::make_unique<LineEntity>(
                1, Math::Point3{ 10, 20, 0 }, Math::Point3{ 20, 20, 0 }));

        auto ins = std::make_unique<InsertEntity>(7001, bid, "*BF_BASE", Math::Point3{ 100, 200, 0 });
        ins->SetBlock(blk);
        ins->SetUniformScale(2.0);
        ins->SetRotation(3.14159265358979323846 / 2.0);

        RecSink sink;
        ins->Draw(sink, false, false);

        // 期望：(10,20) 减基点→(0,0)，×2→(0,0)，转90°→(0,0)，+插入点→(100,200)
        //       (20,20) 减基点→(10,0)，×2→(20,0)，转90°→(0,20)，+插入点→(100,220)
        Check(sink.lines.size() == 1, "块内图元定位：块内一条线只产出一段");
        if (!sink.lines.empty())
        {
            const auto& a = sink.lines[0].first;
            const auto& b = sink.lines[0].second;
            Check(std::abs(a.x - 100) < 1e-6 && std::abs(a.y - 200) < 1e-6 &&
                  std::abs(b.x - 100) < 1e-6 && std::abs(b.y - 220) < 1e-6,
                  "块内图元定位：基点 / 缩放 / 旋转叠加后落点正确");
        }
    }

    // ══ ② 块内线型：成员自己的 DASHED 必须生效 ═══════════════════════════
    {
        Scene s;
        const LineTypeID dashId = s.GetLineTypeTable().FindByName("DASHED");
        Check(dashId != LineTypeTable::ContinuousID,
              "块内线型：内置线型表里有 DASHED（否则本组是假通过）");

        BlockTable& tbl = s.GetBlockTable();
        const BlockID bid = tbl.Create("*BF_LT", Math::Point3{ 0, 0, 0 });
        BlockEntity* blk = tbl.Find(bid);
        if (blk)
        {
            auto ln = std::make_unique<LineEntity>(
                1, Math::Point3{ 0, 0, 0 }, Math::Point3{ 1000, 0, 0 });
            EntityAttr a; a.LayerId = 0; a.LineType = dashId;
            ln->SetAttr(a);
            blk->AddEntity(std::move(ln));
        }

        auto ins = std::make_unique<InsertEntity>(7002, bid, "*BF_LT", Math::Point3{ 0, 0, 0 });
        ins->SetBlock(blk);
        // 插入自己是实线 —— 块内成员的线型不能被它覆盖
        EntityAttr host; host.LayerId = 0; host.LineType = LineTypeTable::ContinuousID;
        ins->SetAttr(host);

        std::vector<Vertex_P3_C4>     verts, fills;
        std::vector<Vertex_P3_C4_UV>  texts;
        Overlay                       ov;
        // wpp=0：不触发"一个周期不足 4px 退化为实线"的像素护栏
        DrawContext ctx(verts, fills, texts, ov, s.GetLayerManager(), s.GetLineTypeTable(),
                        0.0, nullptr, nullptr);
        ctx.SetLineTypeScale(s.GetLineTypeScale());
        ctx.BeginEntity(&ins->GetAttr());
        ins->Draw(ctx, false, false);
        ctx.BeginEntity(nullptr);

        std::printf("    块内 DASHED → %zu 段\n", verts.size() / 2);
        Check(verts.size() / 2 > 10,
              "块内线型：成员自己的 DASHED 生效（修前只有 1 段实线）");
    }

    // ══ ③ 裁剪块：边框开关 + 夹点编辑 + 拖基点显示整块 ═══════════════════
    {
        Scene s;
        BlockTable& tbl = s.GetBlockTable();
        const BlockID bid = tbl.Create("*BF_CLIP", Math::Point3{ 0, 0, 0 });
        if (BlockEntity* b = tbl.Find(bid))
            b->AddEntity(std::make_unique<LineEntity>(
                1, Math::Point3{ -500, 0, 0 }, Math::Point3{ 500, 0, 0 }));

        auto ins = std::make_unique<InsertEntity>(7003, bid, "*BF_CLIP", Math::Point3{ 0, 0, 0 });
        ins->SetBlock(tbl.Find(bid));
        ins->SetClipBoundary({ { -50, -50, 0 }, { 50, -50, 0 },
                               {  50,  50, 0 }, { -50, 50, 0 } });

        // 边框开关（$XCLIPFRAME）
        Check(!ins->IsClipFrameVisible(), "裁剪块：默认不显示边框（与 $XCLIPFRAME=None 一致）");
        ins->SetClipFrameVisible(true);
        Check(ins->IsClipFrameVisible(), "裁剪块：边框开关可置位");

        // 顶点夹点：只动被拖的那一个角
        ins->MoveClipBoundaryPoint(2, Math::Point3{ 80, 70, 0 });
        const auto& poly = ins->GetClipBoundary();
        Check(std::abs(poly[2].x - 80) < 1e-9 && std::abs(poly[2].y - 70) < 1e-9,
              "裁剪块：顶点夹点把被拖的角移到目标位置");
        Check(std::abs(poly[0].x + 50) < 1e-9 && std::abs(poly[0].y + 50) < 1e-9 &&
              std::abs(poly[1].x - 50) < 1e-9 && std::abs(poly[3].y - 50) < 1e-9,
              "裁剪块：拖一个角时**其余顶点不动**（不是矩形重排）");

        // 边夹点：整条边平移（边 0 = 顶点 0 → 顶点 1，水平边）
        const Math::Point3 beforeP0 = poly[0], beforeP1 = poly[1];
        const double midY = (beforeP0.y + beforeP1.y) * 0.5;
        ins->MoveClipBoundaryEdge(0, Math::Point3{ (beforeP0.x + beforeP1.x) * 0.5, midY - 20, 0 });
        const auto& p2 = ins->GetClipBoundary();
        Check(std::abs(p2[0].y - (beforeP0.y - 20)) < 1e-9 &&
              std::abs(p2[1].y - (beforeP1.y - 20)) < 1e-9,
              "裁剪块：边夹点整条边平移（两端点一起移动）");

        // 拖基点期间按**整块**绘制（临时忽略裁剪）
        RecSink clipped, whole;
        ins->Draw(clipped, false, false);
        ins->SetShowWholeWhileDraggingBase(true);
        ins->Draw(whole, false, false);
        ins->SetShowWholeWhileDraggingBase(false);

        double cx0 = 1e300, cx1 = -1e300, wx0 = 1e300, wx1 = -1e300;
        for (const auto& l : clipped.lines)
        { cx0 = std::min({ cx0, l.first.x, l.second.x }); cx1 = std::max({ cx1, l.first.x, l.second.x }); }
        for (const auto& l : whole.lines)
        { wx0 = std::min({ wx0, l.first.x, l.second.x }); wx1 = std::max({ wx1, l.first.x, l.second.x }); }

        std::printf("    裁剪后 X=[%.1f, %.1f]，整块 X=[%.1f, %.1f]\n", cx0, cx1, wx0, wx1);
        Check(cx1 - cx0 < 200.0, "裁剪块：平时按裁剪后绘制（X 跨度收敛到边界内）");
        Check(wx1 - wx0 > 900.0, "裁剪块：拖基点期间按整块绘制（临时忽略裁剪）");
    }

    // ══ ④ 文字强行换行：词边界折行 ═══════════════════════════════════════
    {
        FontSystem fonts;
        fonts.Initialize();
        fonts.PreloadDefaultFonts();
        IFont* f = fonts.GetTssdSHXCompositeFont();

        Check(f != nullptr, "文字折行：内置组合字体可用（否则本组是假通过）");
        if (!f)
        {
            std::printf("块/线型/裁剪/折行 用例失败：%d\n", g_failures);
            return g_failures;
        }

        TextLayoutEngine eng;
        const double BOX = 53.92573839662449;   // 用户那条的栏宽

        // 无空格长编号不拆分（本次修复的核心）
        const size_t n1 = LineCount(eng, f, "420114001037GB10066", BOX);
        std::printf("    无空格 18 字符 boxWidth=%.3f → %zu 行\n", BOX, n1);
        Check(n1 == 1, "文字折行：无空格的长编号**不拆分**（AutoCAD：溢出栏宽，不拆词）");

        // 不限宽仍是单行
        Check(LineCount(eng, f, "420114001037GB10066", 0.0) == 1,
              "文字折行：栏宽 0（不限宽）→ 单行");

        // 含空格的英文仍要在词边界折行（别把功能关掉）
        const size_t n2 = LineCount(eng, f, "AAA BBB CCC DDD EEE", BOX);
        std::printf("    含空格 \"AAA BBB CCC DDD EEE\" → %zu 行\n", n2);
        Check(n2 > 1, "文字折行：含空格的文本仍在**词边界**折行（功能未被关闭）");

        // 中文无空格：可在汉字之间折行
        const size_t n3 = LineCount(eng, f, "这是一个很长的中文说明文字需要折行显示", BOX);
        std::printf("    中文无空格 → %zu 行\n", n3);
        Check(n3 > 1, "文字折行：中文（无空格）可在**汉字之间**折行");

        // 显式 \n 必须仍然生效
        Check(LineCount(eng, f, "第一行\n第二行\n第三行", 0.0) == 3,
              "文字折行：显式 \\n 换行仍然生效");

        // 长 token 夹在可折文本中 → 在它两侧的边界折
        const size_t n4 = LineCount(eng, f, "AB 420114001037GB10066 CD", BOX);
        std::printf("    \"AB <长token> CD\" → %zu 行\n", n4);
        Check(n4 >= 2, "文字折行：长 token 夹在可折文本中，在它两侧的**边界**折行");

        // 空串 / 纯空白不崩
        Check(LineCount(eng, f, "", BOX) <= 1, "文字折行：空串安全（不产生多余行）");
        Check(LineCount(eng, f, "   ", BOX) <= 1, "文字折行：纯空白安全");
    }

    // ══ ⑤ 标注匿名块的组码 12（插入点）必须参与平移 ══════════════════════
    //
    // 标注的匿名块（*D…）几何建在**世界坐标**上，AutoCAD 用组码 12 把它平移：
    //     local = worldGeom + code12
    // 导入时若把插入点写死 (0,0,0)，整个标注会被画在匿名块自身的坐标上。
    // 实测《总说明.dwg》：实绘宽度 910056 而非 7525（错位 92 万单位）。
    {
        Scene s;
        BlockTable& tbl = s.GetBlockTable();
        const BlockID bid = tbl.Create("*D_BF", Math::Point3{ 0, 0, 0 });
        BlockEntity* blk = tbl.Find(bid);
        Check(blk != nullptr, "标注插入点：建出测试匿名块");
        if (blk)
        {
            // 匿名块几何在世界坐标 922000 附近（这是 DIMENSION 匿名块的真实形态）
            blk->AddEntity(std::make_unique<LineEntity>(
                1, Math::Point3{ 922000.0, -63000.0, 0 }, Math::Point3{ 923000.0, -63000.0, 0 }));

            const Math::Point3 inset{ -924643.954, 61440.065, 0.0 };   // 组码 12

            auto ins = std::make_unique<InsertEntity>(
                7101, bid, "*D_BF", inset);          // ← 修复点：用组码 12，不是 (0,0,0)
            ins->SetBlock(blk);

            RecSink sink;
            ins->Draw(sink, false, false);
            Check(sink.lines.size() >= 1, "标注插入点：匿名块内容被画出");
            if (!sink.lines.empty())
            {
                const double x0 = sink.lines[0].first.x;
                const double expect = 922000.0 + inset.x;     // = -2643.954
                std::printf("    匿名块 922000 → 世界 %.3f（期望 %.3f）\n", x0, expect);
                // 容差 1e-2：顶点以 float32 存储，2643 量级 ulp 约 2.4e-4
                Check(std::abs(x0 - expect) < 1e-2,
                      "标注插入点：组码 12 的平移生效（写死 0 会错位 92 万）");
            }
        }
    }

    // ══ ⑥ 裁剪边界的坐标系：块坐标 ↔ 世界坐标 ═══════════════════════════
    //
    // 本项目的 `m_clip` 存在**块坐标系**（导入时 SPATIAL_FILTER 的两个矩阵已把点
    // 算到该系；绘制时 ClipDrawSink 位于 TransformDrawSink 之**内**）。
    // 而夹点的 WorldPos、鼠标事件给的都是**世界坐标**。不换算就会出现：
    //   · 块有平移/缩放时，夹点画在错误位置、拖不动或拖飞；
    //   · 拖基点带动边界，夹点却停在旧位置。
    {
        Scene s;
        BlockTable& tbl = s.GetBlockTable();
        const BlockID bid = tbl.Create("*BF_CLIPCS", Math::Point3{ 0, 0, 0 });
        if (BlockEntity* b = tbl.Find(bid))
            b->AddEntity(std::make_unique<LineEntity>(
                1, Math::Point3{ -500, 0, 0 }, Math::Point3{ 500, 0, 0 }));

        // 块平移到 (1000, 2000) 且放大 2 倍 —— 让块坐标与世界坐标**明显不同**
        auto ins = std::make_unique<InsertEntity>(
            7201, bid, "*BF_CLIPCS", Math::Point3{ 1000, 2000, 0 });
        ins->SetBlock(tbl.Find(bid));
        ins->SetUniformScale(2.0);
        ins->SetClipBoundary({ { -50, -50, 0 }, { 50, -50, 0 },
                               {  50,  50, 0 }, { -50, 50, 0 } });
        ins->SetClipFrameVisible(true);
        InsertEntity* raw = ins.get();
        s.AddEntity(std::move(ins));

        // 裁剪生效：块坐标 [-50,50] 经插入变换 → 世界 [1000±100, 2000±100]
        RecSink sink;
        raw->Draw(sink, false, false);
        double x0 = 1e300, x1 = -1e300;
        for (const auto& l : sink.lines)
        { x0 = std::min({ x0, l.first.x, l.second.x }); x1 = std::max({ x1, l.first.x, l.second.x }); }
        std::printf("    块(1000,2000) 缩放2，裁剪后世界 X=[%.1f, %.1f]\n", x0, x1);
        Check(std::abs(x0 - 900.0) < 1.0 && std::abs(x1 - 1100.0) < 1.0,
              "裁剪坐标系：块坐标边界经插入变换后裁到正确世界范围");

        // 夹点靠 CellMatrix 定位，必须是世界坐标：块坐标 (-50,-50) → (900,1900)
        const Math::Mat4 m = raw->CellMatrix(0, 0);
        const Math::Point3 corner = m * Math::Point3{ -50, -50, 0 };
        std::printf("    块坐标(-50,-50) → 世界(%.1f, %.1f)\n", corner.x, corner.y);
        Check(std::abs(corner.x - 900.0) < 1e-9 && std::abs(corner.y - 1900.0) < 1e-9,
              "裁剪坐标系：边界顶点经 CellMatrix 得到正确世界坐标（夹点靠它定位）");

        // 边界本身仍按块坐标写入，不会混入世界坐标
        raw->MoveClipBoundaryPoint(0, Math::Point3{ -60, -60, 0 });
        const auto& poly = raw->GetClipBoundary();
        Check(std::abs(poly[0].x + 60) < 1e-9 && std::abs(poly[0].y + 60) < 1e-9,
              "裁剪坐标系：MoveClipBoundaryPoint 按块坐标写入");

        // 拖基点后，块坐标边界不变（边界天然随块走），但世界位置随之平移
        raw->SetPosition(Math::Point3{ 1500, 2500, 0 });
        const auto& poly2 = raw->GetClipBoundary();
        Check(std::abs(poly2[0].x + 60) < 1e-9,
              "裁剪坐标系：拖基点不改边界（边界是块坐标，天然随块走）");
        const Math::Mat4 m2 = raw->CellMatrix(0, 0);
        const Math::Point3 corner2 = m2 * Math::Point3{ -60, -60, 0 };
        Check(std::abs(corner2.x - 1380.0) < 1e-6 && std::abs(corner2.y - 2380.0) < 1e-6,
              "裁剪坐标系：块移动后边界的世界位置同步平移（夹点不会停在旧位置）");
    }

    // ══ ⑦ 裁剪边框开关随文档保存 ═════════════════════════════════════════
    //
    // `$XCLIPFRAME` 是块的状态，不持久化就会"裁剪还在、框不见了"。
    {
        // 临时目录用「先 %TEMP%、失败退当前目录」的探测式选法（与 LineTypeScaleTests 同一套）：
        // 只判断目录存在发现不了"建不出文件"的情况，必须真的写一个试。
        std::error_code ec;
        std::filesystem::path dir;
        for (const std::filesystem::path& cand :
             { std::filesystem::temp_directory_path(ec), std::filesystem::current_path(ec) })
        {
            if (cand.empty()) continue;
            const std::filesystem::path probe = cand / "minicad_clipframe_probe.json";
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
        Check(!dir.empty(), "裁剪边框持久化：找到可写的临时目录");

        if (!dir.empty())
        {
            const std::filesystem::path path = dir / "minicad_blockfix_clipframe.json";
            const auto u8 = path.u8string();
            const std::string file(u8.begin(), u8.end());

            {
                Document doc;
                Scene& s = doc.GetScene();
                // 块定义**必须**进 BlockTable：否则保存时解析不到块，
                // 块内容与裁剪状态都会丢
                BlockTable& tbl = s.GetBlockTable();
                const BlockID bid = tbl.Create("*BF_SAVE", Math::Point3{ 0, 0, 0 });
                if (BlockEntity* b = tbl.Find(bid))
                    b->AddEntity(std::make_unique<LineEntity>(
                        1, Math::Point3{ 0, 0, 0 }, Math::Point3{ 10, 0, 0 }));

                auto ins = std::make_unique<InsertEntity>(
                    7301, bid, "*BF_SAVE", Math::Point3{ 0, 0, 0 });
                ins->SetBlock(tbl.Find(bid));
                ins->SetClipBoundary({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } });
                ins->SetClipFrameVisible(true);
                s.AddEntity(std::move(ins));

                Check(doc.SaveAs(file), "裁剪边框持久化：文档能保存");
            }

            Document doc2;
            const bool loaded = doc2.LoadFromFile(file);
            std::filesystem::remove(path, ec);
            Check(loaded, "裁剪边框持久化：文档能读回");
            if (loaded)
            {
                bool found = false;
                doc2.GetScene().ForEachObject([&](const Object& obj)
                {
                    if (const auto* ri = dynamic_cast<const InsertEntity*>(&obj))
                    {
                        found = true;
                        Check(ri->HasClipBoundary(), "裁剪边框持久化：裁剪边界仍在");
                        Check(ri->IsClipFrameVisible(), "裁剪边框持久化：边框开关没丢");
                    }
                });
                Check(found, "裁剪边框持久化：读回了块参照");
            }
        }
    }

    // ══ ⑧ 裁剪边界写回 DWG / DXF ═════════════════════════════════════════
    //
    // 只读不写的后果：另存为后裁剪状态整个丢失，被裁过的块变回整块显示。
    // 写回要重建 OBJECTS 段的字典链：
    //   INSERT ─ {ACAD_XDICTIONARY} → D1 ─ "ACAD_FILTER" → D2 ─ "SPATIAL" → SPATIAL_FILTER
    // 且 SPATIAL_FILTER 的数据是「逆块变换(12) + 边界变换(12)」两个 3×4 矩阵，
    // 边界点存的是**世界坐标**（与场景里的块坐标相反）。
    //
    // ⚠ 必须强制走「重新生成整张图纸」：格式与版本不变时 SaveAs 走「以原图为底」，
    //   那是把旧文件的原始字节写回，根本不经过新代码（也就测不出缺陷）。
    {
        std::error_code ec;
        std::filesystem::path dir;
        for (const std::filesystem::path& cand :
             { std::filesystem::temp_directory_path(ec), std::filesystem::current_path(ec) })
        {
            if (cand.empty()) continue;
            const std::filesystem::path probe = cand / "minicad_clipwb_probe.tmp";
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
        Check(!dir.empty(), "裁剪写回：找到可写的临时目录");

        if (!dir.empty())
        {
            for (const char* ext : { ".dxf", ".dwg" })
            {
                Document doc;
                Scene& s = doc.GetScene();
                BlockTable& tbl = s.GetBlockTable();
                const BlockID bid = tbl.Create("*WB_BLK", Math::Point3{ 0, 0, 0 });
                if (BlockEntity* b = tbl.Find(bid))
                    b->AddEntity(std::make_unique<LineEntity>(
                        1, Math::Point3{ -500, 0, 0 }, Math::Point3{ 500, 0, 0 }));

                // 块平移到 (1000,2000)：块坐标 -50..50 ↔ 世界坐标 950..1050
                // 故意让两者不同，坐标换算错了就会暴露
                auto ins = std::make_unique<InsertEntity>(
                    7401, bid, "*WB_BLK", Math::Point3{ 1000, 2000, 0 });
                ins->SetBlock(tbl.Find(bid));
                ins->SetClipBoundary({ { -50, -50, 0 }, { 50, -50, 0 },
                                       {  50,  50, 0 }, { -50, 50, 0 } });
                ins->SetClipFrameVisible(true);
                s.AddEntity(std::move(ins));

                const std::filesystem::path path =
                    dir / (std::string("minicad_clipwb") + ext);
                const auto u8 = path.u8string();
                const std::string file(u8.begin(), u8.end());
                const char* fmt = path.extension() == ".dxf" ? "DXF" : "DWG";

                const bool saved = doc.SaveAs(file);
                {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "裁剪写回 %s：保存成功", fmt);
                    Check(saved, buf);
                }
                if (saved)
                {
                    Document doc2;
                    const bool loaded = doc2.LoadFromFile(file);
                    {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "裁剪写回 %s：能读回", fmt);
                        Check(loaded, buf);
                    }
                    if (loaded)
                    {
                        const InsertEntity* back = nullptr;
                        doc2.GetScene().ForEachObject([&](const Object& obj)
                        {
                            if (const auto* ri = dynamic_cast<const InsertEntity*>(&obj))
                                back = ri;
                        });
                        Check(back != nullptr, "裁剪写回：往返后仍有块参照");
                        if (back != nullptr)
                        {
                            const bool has = back->HasClipBoundary() &&
                                             back->GetClipBoundary().size() == 4;
                            {
                                char buf[160];
                                std::snprintf(buf, sizeof(buf),
                                              "裁剪写回 %s：边界经往返仍在（4 点）", fmt);
                                Check(has, buf);
                            }
                            {
                                char buf[160];
                                std::snprintf(buf, sizeof(buf),
                                              "裁剪写回 %s：边框开关经往返仍在", fmt);
                                Check(back->IsClipFrameVisible(), buf);
                            }
                            if (has)
                            {
                                // 往返后是**块坐标**：应回到 -50..50（若坐标换算错，
                                // 会把世界坐标 950..1050 当块坐标写回，这里就会失败）
                                double x0 = 1e300, x1 = -1e300;
                                for (const auto& p : back->GetClipBoundary())
                                { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); }
                                std::printf("    %s 往返后边界块坐标 X=[%.3f, %.3f]\n",
                                            ext, x0, x1);
                                Check(std::abs(x0 + 50.0) < 1e-3 &&
                                      std::abs(x1 - 50.0) < 1e-3,
                                      "裁剪写回：边界坐标经往返仍为块坐标（不是世界坐标）");
                            }
                        }
                    }
                    std::filesystem::remove(path, ec);
                }
            }
        }
    }

    std::printf("块/线型/裁剪/折行 用例失败：%d\n", g_failures);
    return g_failures;
}
