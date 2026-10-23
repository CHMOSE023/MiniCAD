// ── 图纸布局（Paper Space / LAYOUT / VIEWPORT）─────────────────────────────
//
// 对照 V0.0.1 的布局功能移植，核心是让它「切布局 = 换一张图」。
//
// 语义要点（都以 DXF 组码为准）：
//   · 模型空间与每张布局各对应一个 BLOCK_RECORD：*Model_Space / *Paper_Space /
//     *Paper_Space0 / *Paper_Space1 …
//   · LAYOUT 对象（OBJECTS 段）把「布局名」与「图纸空间块记录」关联：
//       AssociatedBlockHandle(330) → 该布局的 BLOCK_RECORD
//       TabOrder(71)               → 标签顺序
//   · 图纸空间实体带组码 67（in paper space），归属由 owner(330) 表达；
//     导出时按**所有者**自动写出，所以只要挂对块记录就正确。
//   · 布局页的图形内容**全部靠视口从模型空间取** ——
//     图纸空间本身通常只有视口框（实测 test0.dwg 的 *Paper_Space 完全为空）。
//     视口变换：modelPt → 平移(-viewCenter) → 缩放(height/viewHeight) → 旋转(-twist) → 平移(+center)。
//
// 性能：视口合成前按**包围盒**预剔除。没有这一步，每个视口都要遍历全部模型实体
// （实测 20 万实体 × 13 视口 = 260 万次无用构建）。
//
// 两个必须守住的去重（都是实测踩出来的）：
//   ① DXF 读取时 `CreateDefaults()` 会先补 "Model" + "Layout1" + 一个整页视口，
//      随后文件里的 LAYOUT 再追加 ⇒ 同名布局出现两次（导出 3 个、读回 4 个）。
//   ② 那个自动补的视口只设了 Width/Height/Center，视图中心与视图高都是 0 ——
//      不是文件内容，必须跳过，否则往返一轮视口多 1。
#include "Document/Document.h"
#include "Document/TextStyleResolver.h"
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Text/FontSystem.h"
#include "Viewport/Viewport.h"
#include "Viewport/ViewState.h"
#include "Scene/Scene.h"
#include "Core/Entity/ViewportEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Render/ImageDraw.hpp"
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

    struct LayoutStats
    {
        size_t Layouts          = 0;
        int    PaperEntities    = 0;
        int    Viewports        = 0;
        int    ViewportsWithModel = 0;
    };

    LayoutStats Scan(Scene& s)
    {
        LayoutStats st;
        st.Layouts = s.Layouts().size();
        s.ForEachObject([&](const Object& o)
        {
            if (!o.IsKindOf<Entity>()) return;
            const auto& e = static_cast<const Entity&>(o);
            if (!e.GetAttr().InPaperSpace) return;
            ++st.PaperEntities;
            if (const auto* vp = dynamic_cast<const ViewportEntity*>(&e))
            {
                ++st.Viewports;
                if (vp->ShowsModel()) ++st.ViewportsWithModel;
            }
        });
        return st;
    }

    std::filesystem::path WritableDir()
    {
        std::error_code ec;
        for (const std::filesystem::path& cand :
             { std::filesystem::temp_directory_path(ec), std::filesystem::current_path(ec) })
        {
            if (cand.empty()) continue;
            const std::filesystem::path probe = cand / "minicad_layout_probe.tmp";
            {
                std::ofstream out(probe, std::ios::binary | std::ios::trunc);
                if (!out) continue;
                out << "x";
                out.close();
            }
            ec.clear();
            std::filesystem::remove(probe, ec);
            if (!ec) return cand;
        }
        return {};
    }
}

int RunLayoutTests()
{
    g_failures = 0;

    // ══ ① 视口几何：模型 → 图纸的变换 ═══════════════════════════════════
    //
    // 这是布局页能显示图形的根本。纸张上的框 100×50、看向视图中心 (1000,2000)、
    // 视图高 100 ⇒ 缩放 0.5。模型点 (1000,2000) 应落在框中心。
    {
        ViewportEntity vp(1, Math::Point3{ 50, 25, 0 }, /*w*/100.0, /*h*/50.0);
        vp.SetViewCenter(Math::Point3{ 1000, 2000, 0 });
        vp.SetViewHeight(100.0);

        Check(std::abs(vp.ModelToPaperScale() - 0.5) < 1e-12,
              "视口：缩放 = 框高 / 视图高（50/100 = 0.5）");

        const Math::Point3 c = vp.ModelToPaper(Math::Point3{ 1000, 2000, 0 });
        Check(std::abs(c.x - 50.0) < 1e-9 && std::abs(c.y - 25.0) < 1e-9,
              "视口：视图中心映射到框中心");

        // 模型右移 200 单位 → 图纸右移 100（缩放 0.5）
        const Math::Point3 r = vp.ModelToPaper(Math::Point3{ 1200, 2000, 0 });
        Check(std::abs(r.x - 150.0) < 1e-9,
              "视口：模型偏移按缩放映射到图纸");

        // 模型可见范围：高 100，宽 = 100 × (100/50) = 200
        const AABB box = vp.ModelVisibleBox();
        Check(std::abs(box.Min.x - 900.0) < 1e-9 && std::abs(box.Max.x - 1100.0) < 1e-9 &&
              std::abs(box.Min.y - 1950.0) < 1e-9 && std::abs(box.Max.y - 2050.0) < 1e-9,
              "视口：模型可见矩形（按框宽高比换算，供包围盒预剔除使用）");
    }

    // ══ ② ShowsModel：区分「显示模型的浮动视口」与「代表纸张的主视口」══════
    //
    // 判据必须用**几何特征**而不是视口 id：实测某图 45 个视口里 44 个 id=0，
    // 但都是真正显示模型的浮动视口；按 id>1 判断会把它们全排除、布局仍空白。
    {
        ViewportEntity floating(2, Math::Point3{ 50, 25, 0 }, 100.0, 50.0);
        floating.SetViewCenter(Math::Point3{ 5000, 6000, 0 });   // 看向模型
        floating.SetViewHeight(400.0);
        Check(floating.ShowsModel(), "ShowsModel：浮动视口（视图中心 ≠ 框中心）显示模型");

        ViewportEntity paper(3, Math::Point3{ 50, 25, 0 }, 100.0, 50.0);
        paper.SetViewCenter(Math::Point3{ 50, 25, 0 });          // 与框中心重合
        paper.SetViewHeight(50.0);                               // 与框高相等 = 1:1
        Check(!paper.ShowsModel(), "ShowsModel：纸张主视口不显示模型（否则模型 1:1 糊在纸上）");

        // id 完全不参与判断：两个视口 id 都给 0，结论仍不同
        ViewportEntity a(4, Math::Point3{ 0, 0, 0 }, 10.0, 10.0);
        a.SetViewCenter(Math::Point3{ 999, 999, 0 });
        a.SetViewHeight(10.0);
        Check(a.ShowsModel(), "ShowsModel：id=0 的浮动视口照样显示模型（不看 id）");
    }

    // ══ ③ 布局切换与实体隔离 ═════════════════════════════════════════════
    //
    // 「切布局 = 换一张图」：模型页只显示模型空间实体，布局页只显示
    // 属于**该布局**的图纸空间实体。
    {
        Document doc;
        Scene& scene = doc.GetScene();

        // 建两张布局
        std::vector<DrawingLayout> layouts;
        layouts.push_back(DrawingLayout{});                 // 模型页
        {
            DrawingLayout l1; l1.Name = "布局1"; l1.Model = false;
            l1.OwnerKey = 0x100; l1.Order = 1;
            layouts.push_back(l1);
            DrawingLayout l2; l2.Name = "布局2"; l2.Model = false;
            l2.OwnerKey = 0x200; l2.Order = 2;
            layouts.push_back(l2);
        }
        scene.SetLayouts(std::move(layouts));

        Check(scene.Layouts().size() == 3, "布局：SetLayouts 保存了 3 项（模型 + 2 张）");
        Check(scene.Layouts()[0].Model, "布局：第 0 项是模型页（约定）");
        Check(scene.ModelLayoutIndex() == 0, "布局：ModelLayoutIndex 返回 0");

        // 模型空间实体
        EntityAttr modelAttr;
        EntityAttr paper1; paper1.InPaperSpace = true; paper1.LayoutKey = 0x100;
        EntityAttr paper2; paper2.InPaperSpace = true; paper2.LayoutKey = 0x200;

        // 模型页：只看到模型空间实体
        CameraState cam;
        doc.SetActiveLayout(0, cam);
        Check(!doc.ActiveLayoutIsPaper(), "布局：模型页 ActiveLayoutIsPaper = false");
        Check(doc.IsVisibleInActiveLayout(modelAttr), "布局：模型页显示模型空间实体");
        Check(!doc.IsVisibleInActiveLayout(paper1), "布局：模型页**不**显示图纸空间实体");

        // 切到布局1：只看到布局1的实体
        doc.SetActiveLayout(1, cam);
        Check(doc.ActiveLayoutIsPaper(), "布局：布局1 ActiveLayoutIsPaper = true");
        Check(!doc.IsVisibleInActiveLayout(modelAttr), "布局：布局1 不显示模型空间实体");
        Check(doc.IsVisibleInActiveLayout(paper1), "布局：布局1 显示属于它的图纸空间实体");
        Check(!doc.IsVisibleInActiveLayout(paper2),
              "布局：布局1 **不**显示布局2的实体（按 OwnerKey 隔离）");
        Check(doc.ActiveLayoutOwnerKey() == 0x100, "布局：ActiveLayoutOwnerKey 取当前布局的归属键");

        // 切到布局2
        doc.SetActiveLayout(2, cam);
        Check(doc.ActiveLayoutOwnerKey() == 0x200, "布局：切到布局2 后归属键随之改变");
        Check(doc.IsVisibleInActiveLayout(paper2), "布局：布局2 显示属于它的实体");
        Check(!doc.IsVisibleInActiveLayout(paper1), "布局：布局2 不显示布局1的实体");

        // 未提供归属键（OwnerKey=0）时退化为不过滤，而不是整页空白
        std::vector<DrawingLayout> legacy;
        legacy.push_back(DrawingLayout{});
        {
            DrawingLayout l; l.Name = "旧布局"; l.Model = false; l.OwnerKey = 0; l.Order = 1;
            legacy.push_back(l);
        }
        scene.SetLayouts(std::move(legacy));
        doc.SetActiveLayout(1, cam);
        EntityAttr unknownPaper; unknownPaper.InPaperSpace = true; unknownPaper.LayoutKey = 0;
        Check(doc.IsVisibleInActiveLayout(unknownPaper),
              "布局：布局未给出归属键时不过滤（老数据仍能显示，不退化为空白）");
    }

    // ══ ④ 真实图纸：布局导入 + 往返一致 ══════════════════════════════════
    //
    // 用仓库自带的样例（含真实 LAYOUT 与 VIEWPORT）。判据是**数量与名称一致**，
    // 而不是"函数被调了"。
    {
        // 样例路径：用测试目标里已定义的 MINIDWG_SAMPLE_DIR。
        // ⚠ 不要用 std::filesystem::path 构造它 —— 该路径含中文，
        //   path 从窄字符串构造会走 MBSC→wide 转换，C locale 下抛 filesystem_error
        //   （未捕获即 __fastfail，退出码 0xC0000409）。Document::LoadFromFile 收 std::string，
        //   内部自己处理编码，直接传字符串即可。
        const std::string sample = std::string(MINIDWG_SAMPLE_DIR) + "/sample_AC1015_ascii.dxf";
        // ⚠ 不要用 std::ifstream 探测这个路径：MSVC 的窄字符流打不开含中文的路径
        //   （GBK 字节被当成 ANSI 代码页），会误报"样例不存在"。
        //   Document::LoadFromFile 内部会把 std::string 转成宽字符处理，直接交给它就对了。
        Document doc;
        std::string err;
        const bool loaded = doc.LoadFromFile(sample, &err);
        Check(loaded, "布局：样例图纸能导入（缺失时本组无法验证）");
        if (!loaded)
            std::printf("    样例路径: %s（%s）\n", sample.c_str(), err.c_str());
        if (loaded)
        {
            const LayoutStats before = Scan(doc.GetScene());
                std::printf("    导入后：布局 %zu，图纸实体 %d，视口 %d（显示模型 %d）\n",
                            before.Layouts, before.PaperEntities,
                            before.Viewports, before.ViewportsWithModel);

                Check(before.Layouts > 1, "布局：样例里读到多张布局（否则本组是假通过）");
                Check(before.Viewports > 0, "布局：样例里读到 VIEWPORT 实体");
                Check(before.ViewportsWithModel > 0,
                      "布局：至少有一个视口真正显示模型内容（布局页不会空白）");

                // 布局名不能有重复（CreateDefaults 预置的同名布局要去掉）
                {
                    bool dup = false;
                    const auto& ls = doc.GetScene().Layouts();
                    for (size_t i = 0; i < ls.size() && !dup; ++i)
                        for (size_t j = i + 1; j < ls.size(); ++j)
                            if (ls[i].Name == ls[j].Name) { dup = true; break; }
                    Check(!dup, "布局：导入后没有重名布局（默认布局与文件布局已去重）");
                }

                // 往返：强制走「重新生成整张图纸」，否则 SaveAs 走「以原图为底」，
                // 那是把旧文件字节写回，根本不经过新代码
                const std::filesystem::path dir = WritableDir();
                Check(!dir.empty(), "布局：找到可写的临时目录");
                if (!dir.empty())
                {
                    doc.SetCadSaveVersion(CadSaveVersion::R2018);
                    const std::filesystem::path out = dir / "minicad_layout_roundtrip.dxf";
                    const auto u8 = out.u8string();
                    const std::string file(u8.begin(), u8.end());

                    const bool saved = doc.SaveAs(file);
                    Check(saved, "布局：能另存为 DXF");
                    if (saved)
                    {
                        Document doc2;
                        const bool loaded2 = doc2.LoadFromFile(file);
                        Check(loaded2, "布局：往返文件能读回");
                        if (loaded2)
                        {
                            const LayoutStats after = Scan(doc2.GetScene());
                            std::printf("    往返后：布局 %zu，图纸实体 %d，视口 %d（显示模型 %d）\n",
                                        after.Layouts, after.PaperEntities,
                                        after.Viewports, after.ViewportsWithModel);

                            Check(after.Layouts == before.Layouts,
                                  "布局：往返后布局数一致（不重复、不丢失）");
                            Check(after.Viewports == before.Viewports,
                                  "布局：往返后视口数一致（默认补的整页视口已排除）");
                            Check(after.ViewportsWithModel == before.ViewportsWithModel,
                                  "布局：往返后「显示模型的视口」数一致");
                        }
                        std::error_code ec;
                        std::filesystem::remove(out, ec);
                    }
                }
        }
    }

    // ══ 布局页必须把视口里的模型内容真正画出来 ═══════════════════════════
    //
    // 用户报「布局页里很多图元能选中但看不到，模型页正常」。
    //
    // 根因：`Editor::TessellateEntity` 里 `c.lines = m_scratchLines` 等**拷贝
    // 发生在视口合成之前**。合成是往 m_scratch* 里**追加**顶点，所以追加进去的
    // 模型内容永远进不了该实体的缓存块 —— 布局页只剩视口边框（合成前已拷），
    // 模型内容一个都不显示；而拾取走场景数据，仍然能选中。
    //
    // 本组守住：带「显示模型」视口的布局页，渲染出的场景顶点必须包含
    // 模型空间的内容（数量远大于仅有视口边框的 4 条线）。
    {
        Document doc;
        Scene& scene = doc.GetScene();

        // 模型空间放一条明显的线
        auto line = std::make_unique<LineEntity>(scene.NextObjectID(),
                                                 Math::Point3{ 0, 0, 0 }, Math::Point3{ 1000, 500, 0 });
        scene.AddEntity(std::move(line));

        // 加一张布局 + 一个显示该模型的视口
        std::vector<DrawingLayout> layouts = scene.Layouts();
        DrawingLayout sheet = layouts[0];
        sheet.Name = "布局1";
        sheet.Model = false;
        sheet.OwnerKey = 12345;
        layouts.push_back(sheet);
        scene.SetLayouts(std::move(layouts));

        auto vp = std::make_unique<ViewportEntity>(scene.NextObjectID(),
                                                   Math::Point3{ 500, 300, 0 }, 1000.0, 600.0);
        vp->SetViewCenter(Math::Point3{ 500, 250, 0 });
        vp->SetViewHeight(600.0);
        vp->SetAttr(EntityAttr{});
        EntityAttr& va = vp->GetAttr();
        va.InPaperSpace = true;
        va.LayoutKey = 12345;
        scene.AddEntity(std::move(vp));

        CameraState cs;
        const bool switched = doc.SetActiveLayout(1, cs);
        Check(switched, "布局渲染：能切到布局页");

        // 渲染并统计
        Viewport viewport(1200.f, 900.f);
        Editor   editor;
        editor.Bind(doc, viewport);
        if (switched)
        {
            const auto& L = doc.GetScene().Layouts()[1];
            viewport.GetCamera().ZoomToBounds(L.MinX, L.MinY, L.MaxX, L.MaxY);
            editor.Render();
            const ViewState vs = editor.BuildViewState();
            const size_t total = vs.Scene.size() + vs.SceneDyn.size();
            std::printf("    布局页渲染：静态 %zu + 动态 %zu = %zu\n",
                        vs.Scene.size(), vs.SceneDyn.size(), total);
            Check(total > 8,
                  "布局渲染：视口里的模型内容已进入渲染流（不止视口边框的 4 条线）");
            // 视口边框 4 条线 = 8 顶点；模型线另加 2 顶点 ⇒ 应 >= 10
            Check(vs.Scene.size() + vs.SceneDyn.size() >= 10,
                  "布局渲染：既有视口边框（8 顶点）也有模型内容（另加 2 顶点）");
        }
    }

    // ══ 布局页：视口外的图元必须被裁掉（用户报「视口外图元依然显示」）═══════
    //
    // AutoCAD 里视口是一扇「开向模型空间的窗」：模型实体的几何被视口边界硬裁掉，
    // 框外一根线、一个字都不露。
    //
    // 本工程把模型的五路几何**分开提交**，因此每一路都要单独裁：
    //   ① 线段 ClipLines   ② 填充 ClipFills   ③ 点 ClipPoints
    //   ④ 纹理文字 ClipTexts   ⑤ 字形实例 ClipGlyphInstances
    //   ⑥ 光栅图像 ClipImages
    //
    // 曾经漏掉的是 ⑤（和 ⑥）：桌面 / 网页两版的文字都走字形实例（GlyphInstance），
    // ComposeModelIntoViewport 只裁了前四路，模型空间的文字整行溢出到视口外的纸面上。
    // 表现就是「模型页正常，布局页里图元跑到视口外面」。
    //
    // 本组守住：带「显示模型」视口的布局页，渲染出的每一路几何都不能落在
    // 视口矩形之外。
    {
        Document doc;
        Scene& scene = doc.GetScene();

        // 文字要走**字形实例**这一路，必须有字体系统：
        // 不注入时 MakeTextStyleResolver 返回 nullptr，文字一个字都不产出，
        // 后面的字形断言就是假通过（这正是本用例要守住的漏洞）。
#ifdef MINICAD_ASSET_DIR
        auto fontSystem = std::make_unique<FontSystem>();
        fontSystem->Initialize();
        fontSystem->GetFontEngine().SetFontDir(std::string(MINICAD_ASSET_DIR) + "/fonts/");
        doc.SetFontSystem(fontSystem.get());
#endif

        // 视口：纸上中心 (50,25)、框 100×50；看向模型 (1000,2000)、视图高 100 ⇒ 缩放 0.5
        // 模型可见框 = x∈[900,1100], y∈[1950,2050]；纸面框 = x∈[0,100], y∈[0,50]
        auto vp = std::make_unique<ViewportEntity>(scene.NextObjectID(),
                                                   Math::Point3{ 50, 25, 0 }, 100.0, 50.0);
        vp->SetViewCenter(Math::Point3{ 1000, 2000, 0 });
        vp->SetViewHeight(100.0);
        EntityAttr va{};
        va.InPaperSpace = true;
        va.LayoutKey    = 0x100;
        vp->SetAttr(va);
        scene.AddEntity(std::move(vp));

        // 横穿视口左右边界的线：模型 500..1500 ⇒ 纸面 -225..275，应被切成框内的一段
        scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                                                     Math::Point3{ 500, 2000, 0 }, Math::Point3{ 1500, 2000, 0 }));
        // 完全在视口外的线：模型 100..300 ⇒ 纸面 -400..-350，应整条消失
        scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                                                     Math::Point3{ 100, 2000, 0 }, Math::Point3{ 300, 2000, 0 }));
        // 插入点在视口内的文字：模型 950 ⇒ 纸面 25，字形实例应保留
        scene.AddEntity(std::make_unique<TextEntity>(scene.NextObjectID(),
                                                     Math::Point3{ 950, 2000, 0 }, std::string("AB"), 20.0, 0.0));
        // 插入点在视口外的文字：模型 100 ⇒ 纸面 -400，字形实例应整批裁掉
        // （这正是漏裁字形实例时溢出的那一行）
        scene.AddEntity(std::make_unique<TextEntity>(scene.NextObjectID(),
                                                     Math::Point3{ 100, 2050, 0 }, std::string("CD"), 20.0, 0.0));

        std::vector<DrawingLayout> layouts = scene.Layouts();
        DrawingLayout sheet = layouts[0];
        sheet.Name    = "布局1";
        sheet.Model   = false;
        sheet.OwnerKey = 0x100;
        sheet.MinX = 0; sheet.MinY = 0; sheet.MaxX = 100; sheet.MaxY = 50;
        layouts.push_back(sheet);
        scene.SetLayouts(std::move(layouts));

        CameraState cs;
        const bool switched = doc.SetActiveLayout(1, cs);
        Check(switched, "视口裁剪：能切到布局页");

        if (switched)
        {
            Viewport viewport(1200.f, 900.f);
            viewport.GetCamera().ZoomToBounds(0, 0, 100, 50);
            Editor editor;
            editor.Bind(doc, viewport);
            editor.Render();
            const ViewState vs = editor.BuildViewState();

            // ── ① 线段：只允许落在纸面框 [0,100] 内 ──
            int lineOut = 0;
            for (size_t k = 0; k + 1 < vs.Scene.size(); k += 2)
                if (vs.Scene[k].pos.x < -0.5f || vs.Scene[k].pos.x > 100.5f ||
                    vs.Scene[k + 1].pos.x < -0.5f || vs.Scene[k + 1].pos.x > 100.5f ||
                    vs.Scene[k].pos.y < -0.5f || vs.Scene[k].pos.y > 50.5f ||
                    vs.Scene[k + 1].pos.y < -0.5f || vs.Scene[k + 1].pos.y > 50.5f)
                    ++lineOut;
            Check(lineOut == 0, "视口裁剪：线段不溢出到视口矩形外");

            // 横穿的那条必须留下（被切成框内段），不能整条消失
            bool crossingKept = false;
            for (size_t k = 0; k + 1 < vs.Scene.size(); k += 2)
                if (std::abs(vs.Scene[k].pos.y - 25.0f) < 0.5f && vs.Scene[k + 1].pos.x > vs.Scene[k].pos.x)
                    crossingKept = true;
            Check(crossingKept, "视口裁剪：横穿视口的线被裁成框内段，内容不丢");

            // ── ⑤ 字形实例：只允许落在纸面框内 ──
            // 这是本次修复的核心：文字走的是单独提交的字形实例流，
            // 漏裁它时「视口外那行文字」整行溢出到纸面上。
            int glyphOut = 0;
            for (const GlyphInstance& g : vs.Glyphs)
                if (g.tx < 0.0f || g.tx > 100.0f || g.ty < 0.0f || g.ty > 50.0f)
                    ++glyphOut;
            Check(glyphOut == 0, "视口裁剪：字形实例不溢出到视口矩形外（文字不再铺到视口外）");

            // 视口内那行文字必须还在（不能把该留的也裁掉）
            bool insideTextKept = false;
            for (const GlyphInstance& g : vs.Glyphs)
                if (g.tx >= 20.0f && g.tx <= 40.0f && std::abs(g.ty - 25.0f) < 1.0f)
                    insideTextKept = true;
            Check(insideTextKept, "视口裁剪：视口内的文字照常显示（没有把该留的裁掉）");
            // ── ⑥ 光栅图像：中心点只允许落在纸面框内 ──
            int imageOut = 0;
            for (const ImageDraw& im : vs.Images)
            {
                const double cx = (static_cast<double>(im.Verts[0].pos.x) + im.Verts[2].pos.x) * 0.5;
                const double cy = (static_cast<double>(im.Verts[0].pos.y) + im.Verts[2].pos.y) * 0.5;
                if (cx < 0.0 || cx > 100.0 || cy < 0.0 || cy > 50.0) ++imageOut;
            }
            Check(imageOut == 0, "视口裁剪：光栅图像不溢出到视口矩形外");

            std::printf("    视口裁剪：线段 %zu 对 / 字形 %zu / 图像 %zu，溢出：线 %d、字形 %d、图像 %d\n",
                        vs.Scene.size() / 2, vs.Glyphs.size(), vs.Images.size(), lineOut, glyphOut, imageOut);
        }
    }

    // ══ 布局页：渲染看不见的图元，拾取也不能命中 ══════════════════════════
    //
    // 用户报「B地块地下室平面【20210618】结构 - 发概算.dwg，布局视口外，
    // 为什么还可以选中隐藏的图元」。
    //
    // 根因：渲染与拾取是**两套独立过滤**。
    //   渲染侧 Editor::UpdateSceneVertices 的 isShown() 调
    //         Document::IsVisibleInActiveLayout ⇒ 布局页只画本布局的图纸空间实体
    //   拾取侧 Picking::IsPickable() 此前只看 attr.Visible 与图层可见性
    //         ⇒ 既不按布局过滤，也不按视口矩形过滤
    // 于是图纸空间里属于**别的**布局的实体（LayoutKey 不匹配）画不出来，
    // 但在纸面上的位置照旧，点选 / 框选 / 悬停全能命中 —— 「能选中但看不到」。
    //
    // 本组守住：渲染侧排除的实体，拾取侧必须一并排除；反过来的方向也不能误伤。
    {
        Document doc;
        Scene& scene = doc.GetScene();

        auto vp = std::make_unique<ViewportEntity>(scene.NextObjectID(),
                                                   Math::Point3{ 50, 25, 0 }, 100.0, 50.0);
        vp->SetViewCenter(Math::Point3{ 1000, 2000, 0 });
        vp->SetViewHeight(100.0);
        EntityAttr va{};
        va.InPaperSpace = true; va.LayoutKey = 0x100;
        vp->SetAttr(va);
        scene.AddEntity(std::move(vp));

        // 图纸空间实体：属于**另一张**布局（LayoutKey 不匹配）⇒ 本布局既不画也不该选中
        auto other = std::make_unique<LineEntity>(scene.NextObjectID(),
                                                  Math::Point3{ 10, 40, 0 }, Math::Point3{ 20, 40, 0 });
        const Object::ObjectID idOther = other->GetID();
        EntityAttr oa{};
        oa.InPaperSpace = true; oa.LayoutKey = 0x999;
        other->SetAttr(oa);
        scene.AddEntity(std::move(other));

        // 模型空间实体：模型页可见可选中，布局页不画也不该选中
        auto model = std::make_unique<LineEntity>(scene.NextObjectID(),
                                                  Math::Point3{ 15, 40, 0 }, Math::Point3{ 25, 40, 0 });
        const Object::ObjectID idModel = model->GetID();   // 默认 InPaperSpace = false
        scene.AddEntity(std::move(model));

        std::vector<DrawingLayout> layouts = scene.Layouts();
        DrawingLayout sheet = layouts[0];
        sheet.Name = "L1"; sheet.Model = false; sheet.OwnerKey = 0x100;
        sheet.MinX = 0; sheet.MinY = 0; sheet.MaxX = 100; sheet.MaxY = 50;
        layouts.push_back(sheet);
        scene.SetLayouts(std::move(layouts));

        Viewport viewport(1200.f, 900.f);

        // ── 模型页：模型空间实体点得中（确认过滤没有把该留的也砍掉）──
        {
            CameraState cs;
            doc.SetActiveLayout(0, cs);
            viewport.GetCamera().ZoomToBounds(0, 0, 50, 50);
            Editor editor;
            editor.Bind(doc, viewport);
            editor.Render();

            const Math::Point2 sp = viewport.GetCamera().WorldToScreen(Math::Point3{ 20, 40, 0 });
            InputEvent m; m.Type = InputEventType::MouseMove; m.MouseX = (int)sp.x; m.MouseY = (int)sp.y;
            editor.OnInput(m);
            InputEvent d; d.Type = InputEventType::MouseButtonDown; d.Button = MouseButton::Left; d.MouseX = (int)sp.x; d.MouseY = (int)sp.y;
            editor.OnInput(d);
            InputEvent u; u.Type = InputEventType::MouseButtonUp; u.Button = MouseButton::Left; u.MouseX = (int)sp.x; u.MouseY = (int)sp.y;
            editor.OnInput(u);
            Check(editor.GetSelection().contains(idModel),
                  "拾取过滤：模型页上模型空间实体点得中（过滤没有误伤）");
        }

        // ── 布局页 ──
        CameraState cs1;
        const bool switched = doc.SetActiveLayout(1, cs1);
        Check(switched, "拾取过滤：能切到布局页");
        viewport.GetCamera().ZoomToBounds(0, 0, 100, 50);

        if (switched)
        {
            Editor editor;
            editor.Bind(doc, viewport);
            editor.Render();
            const ViewState vs = editor.BuildViewState();

            // 渲染侧：别布局的图纸空间实体确实不画（只剩视口边框）
            Check(vs.Scene.size() / 2 == 4,
                  "拾取过滤：渲染侧排除别布局的图纸空间实体（只剩视口边框）");

            // 拾取侧：在别布局那条线的纸面位置上点击，选择集必须为空
            const Math::Point2 screenPt =
                viewport.GetCamera().WorldToScreen(Math::Point3{ 15, 40, 0 });

            InputEvent move;
            move.Type = InputEventType::MouseMove;
            move.MouseX = static_cast<int>(screenPt.x);
            move.MouseY = static_cast<int>(screenPt.y);
            editor.OnInput(move);

            InputEvent down;
            down.Type = InputEventType::MouseButtonDown;
            down.Button = MouseButton::Left;
            down.MouseX = static_cast<int>(screenPt.x);
            down.MouseY = static_cast<int>(screenPt.y);
            editor.OnInput(down);
            InputEvent up;
            up.Type = InputEventType::MouseButtonUp;
            up.Button = MouseButton::Left;
            up.MouseX = static_cast<int>(screenPt.x);
            up.MouseY = static_cast<int>(screenPt.y);
            editor.OnInput(up);

            Check(!editor.GetSelection().contains(idOther),
                  "拾取过滤：别布局的图纸空间实体点选选不中（渲染与拾取口径一致）");
            Check(editor.GetSelection().empty(),
                  "拾取过滤：该位置点选后选择集为空（没有看不见却能选中的对象）");

            // 布局页上模型空间实体同样不该被拾取（渲染侧已排除，见 IsVisibleInActiveLayout）
            Check(!editor.GetSelection().contains(idModel),
                  "拾取过滤：布局页上模型空间实体也点选选不中（与渲染口径一致）");
        }
    }

    // ══ ⑪ 视口 ViewCenter 的 DCS → WCS 换算（用户报「布局里有几个视口不显示」）══
    //
    // DWG 规范：VIEWPORT 的组码 12/22（ViewCenter）是 **DCS（显示坐标系）** 坐标。
    //   DCS = 把 WCS 绕视线方向旋转 +Twist 得到的坐标系
    //   ⇒ DCS → WCS 必须反向旋转 -Twist
    //
    // 实测 `01-曲陵路.dwg`（12 个视口）：
    //   · #1..#5  Twist = 1.6701 rad(95.7°)，ViewCenter 原始值
    //     (-457894..-457114, 725685.4) —— 离模型包围盒 45 万单位
    //   · 按 R(-Twist) 转过之后 (767492..767415, 383720..382944)，
    //     正好落在模型包围盒内（与 #6..#11 同一片区域）
    //
    // 之前把组码 12/22 的原值直接当 WCS 存进 ViewportEntity：
    //   ① ModelVisibleBox() 偏离模型 ⇒ 包围盒预剔除把全部实体挡掉
    //   ② 合成矩阵的平移量用错 ⇒ 即使漏几个实体进来也画到几十万单位外
    //   ⇒ 表现就是「布局页里有 5 个视口只剩空框」。
    //
    // 本组守住：SetViewCenterDcs 的入出是互逆的，且 Twist≠0 时
    // 存进来的 WCS 与"手动旋转 -Twist"的结果一致。
    {
        const double twist = 1.6701;                 // 95.69°
        const Math::Point3 dcs{ -457894.1, 725685.4, 0.0 };

        ViewportEntity vp(1, Math::Point3{ 231.5, 145.9, 0 }, 390.0, 245.0);
        vp.SetTwist(twist);
        vp.SetViewCenterDcs(dcs, twist);

        const Math::Point3& wcs = vp.GetViewCenter();
        const double c = std::cos(-twist), s = std::sin(-twist);
        const double ex = dcs.x * c - dcs.y * s;
        const double ey = dcs.x * s + dcs.y * c;
        Check(std::abs(wcs.x - ex) < 1e-6 && std::abs(wcs.y - ey) < 1e-6,
              "DCS→WCS：SetViewCenterDcs 按 R(-Twist) 换算");
        std::printf("    DCS(%.1f, %.1f) → WCS(%.1f, %.1f)\n", dcs.x, dcs.y, wcs.x, wcs.y);

        // 换算结果必须落在该图模型的包围盒内（这是本 bug 的判定依据）
        constexpr double kMinX = 766296.6, kMaxX = 770401.3;
        constexpr double kMinY = 380997.1, kMaxY = 384959.3;
        Check(wcs.x >= kMinX && wcs.x <= kMaxX && wcs.y >= kMinY && wcs.y <= kMaxY,
              "DCS→WCS：换算后的视图中心落在模型包围盒内（原来在 45 万单位外）");

        // 换算后视口可见矩形与模型包围盒相交 —— 包围盒预剔除不会再把它整体挡掉
        const AABB vis = vp.ModelVisibleBox();
        Check(vis.Max.x >= kMinX && vis.Min.x <= kMaxX && vis.Max.y >= kMinY && vis.Min.y <= kMaxY,
              "DCS→WCS：视口可见矩形与模型包围盒相交（预剔除不再误杀）");

        // 往返：GetViewCenterDcs 必须还原原始 DCS 值（导出另存为要写回）
        const Math::Point3 back = vp.GetViewCenterDcs(vp.GetTwist());
        Check(std::abs(back.x - dcs.x) < 1e-6 && std::abs(back.y - dcs.y) < 1e-6,
              "WCS→DCS：GetViewCenterDcs 还原组码 12/22 的原始值（往返无损）");

        // Twist = 0 时换算必须是恒等（不能把正常视口也带偏）
        ViewportEntity straight(2, Math::Point3{ 0, 0, 0 }, 100.0, 100.0);
        straight.SetTwist(0.0);
        straight.SetViewCenterDcs(Math::Point3{ 768101.2, 384788.3, 0.0 }, 0.0);
        Check(std::abs(straight.GetViewCenter().x - 768101.2) < 1e-9 &&
              std::abs(straight.GetViewCenter().y - 384788.3) < 1e-9,
              "DCS→WCS：Twist=0 时是恒等变换（正常视口不受影响）");
    }

    // ══ ⑫ 视口内容旋转必须是 R(+Twist)（用户报「图元与视口有角度」）═══════
    //
    // 背景：上一轮修好了「视口空白」（ViewCenter 的 DCS→WCS 换算），
    // 但内容仍相对视口边框倾斜 —— 因为合成矩阵里的旋转方向也反了。
    //
    // 判据（与任何假设无关的硬事实，实测 `01-曲陵路.dwg`）：
    //   5 个 Twist=1.6701 rad(95.69°) 的浮动视口在纸面上并排一行
    //   （Center.x = 231.5/651.5/1071.5/1491.5/1911.5，步长 +420，y 同为 145.9），
    //   各看模型里 195 宽的一条带（ViewHeight=122.5、框高 245 ⇒ scale=2，
    //   195×2 = 390 = 框宽）⇒ 相邻视口无缝并排。
    //   把 ViewCenter 按 DCS→WCS 的 R(-Twist) 转过之后，相邻 WCS 位移的
    //   单位向量恒等于该视口的 UcsXAxis —— 即「纸面 +X 对应的模型方向」。
    //
    //   内容变换必须把 UcsXAxis 映射到纸面 +X：
    //       R(+Twist)·UcsXAxis = (+1.0000, -0.0000)  ✔
    //       R(-Twist)·UcsXAxis = (-0.9804, +0.1972)  ✘（偏 95.7°）
    //   ⇒ 旋转角必须是 **+Twist**。用 -Twist 时内容整体转错约 90°。
    {
        // 该图 5 个 Twist 视口的 UCS 基向量（DWG 组码 111/112）
        const double tw = 1.6701;
        const double ux = -0.0991, uy = -0.9951;      // UcsXAxis

        // R(+tw)·UcsXAxis 必须正好是纸面 +X
        const double cp = std::cos(tw), sp = std::sin(tw);
        const double px = ux * cp + uy * (-sp);       // 行向量约定 Mat4::RotationZ(+tw)
        const double py = ux * sp + uy * ( cp);
        Check(std::abs(px - 1.0) < 1e-3 && std::abs(py) < 1e-3,
              "视口旋转：R(+Twist)·UcsXAxis = 纸面 +X（这是正确方向）");

        // R(-tw)·UcsXAxis 必须明显偏离 —— 证明旧代码的方向是错的
        const double cn = std::cos(-tw), sn = std::sin(-tw);
        const double nx = ux * cn + uy * (-sn);
        const double ny = ux * sn + uy * ( cn);
        Check(std::abs(nx - 1.0) > 1e-3 || std::abs(ny) > 1e-3,
              "视口旋转：R(-Twist)·UcsXAxis 偏离纸面 +X（旧代码就错在这）");

        // ── 端到端：ModelToPaper 必须把 UCS 横向映射到纸张横向 ──
        // 视口框 390×245、scale=2、viewCenter 取 WCS 值（已按 DCS 转过）
        ViewportEntity vp(1, Math::Point3{ 231.5, 145.9, 0 }, 390.0, 245.0);
        vp.SetTwist(tw);
        vp.SetViewCenter(Math::Point3{ 767492.7, 383720.3, 0 });   // WCS
        vp.SetViewHeight(122.5);

        // 视图中心必须落在框中心
        const Math::Point3 p0 = vp.ModelToPaper(vp.GetViewCenter());
        Check(std::abs(p0.x - 231.5) < 1e-9 && std::abs(p0.y - 145.9) < 1e-9,
              "视口旋转：视图中心映射到框中心（Twist 不改变中心对应）");

        // 沿 UcsXAxis 走 195 个单位（= 半个框宽对应的模型距离）→ 图纸横向 +390
        // 容差取 5e-2：用例里的 tw 与 UCS 基向量都是四舍五入值
        // （真值 tw=1.6701…、UcsXAxis=(-0.09913…,-0.99507…)），
        // 走 195 后残差被放大到 ~2e-2 量级
        const Math::Point3 p1 = vp.ModelToPaper(
            Math::Point3{ vp.GetViewCenter().x + ux * 195.0, vp.GetViewCenter().y + uy * 195.0, 0.0 });
        const double ddx = p1.x - p0.x, ddy = p1.y - p0.y;
        Check(std::abs(ddx - 390.0) < 5e-2 && std::abs(ddy) < 5e-2,
              "视口旋转：沿 UCS 横向走 195 → 图纸横向 +390（横向对上，无角度偏差）");
        std::printf("    UCS 横向 195 → 图纸 (%+.4f, %+.4f)\n", ddx, ddy);

        // 沿 UcsYAxis 走 122.5（= ViewHeight）→ 图纸竖向 +245
        const double vx = 0.9951, vy = -0.0991;       // UcsYAxis
        const Math::Point3 p2 = vp.ModelToPaper(
            Math::Point3{ vp.GetViewCenter().x + vx * 122.5, vp.GetViewCenter().y + vy * 122.5, 0.0 });
        Check(std::abs(p2.x - p0.x) < 5e-2 && std::abs(p2.y - p0.y - 245.0) < 5e-2,
              "视口旋转：沿 UCS 竖向走 122.5 → 图纸竖向 +245");

        // 旋转后的可见矩形外接盒：扁框旋转约 90° 后外接盒变「瘦高」，
        // 宽/高都可能小于框的宽/高 —— 这不是bug。要守的性质是：
        //   ① 含视图中心（否则把内容全剔掉）
        //   ② 面积不小于**模型可见矩形**面积 195 × 122.5 = 23869
        //      （外接盒 ⊇ 旋转后的可见矩形）
        //   ③ Twist=0 时与未旋转版完全一致（不改变既有行为）
        const AABB vis = vp.ModelVisibleBox();
        const double visW = vis.Max.x - vis.Min.x, visH = vis.Max.y - vis.Min.y;
        Check(vp.GetViewCenter().x >= vis.Min.x && vp.GetViewCenter().x <= vis.Max.x &&
              vp.GetViewCenter().y >= vis.Min.y && vp.GetViewCenter().y <= vis.Max.y,
              "视口旋转：ModelVisibleBox 含视图中心（不会把内容整体剔掉）");
        Check(visW * visH >= 195.0 * 122.5,
              "视口旋转：外接盒面积不小于模型可见矩形面积（外接盒 ⊇ 可见矩形）");
        std::printf("    外接盒 %.1f × %.1f（面积 %.0f ≥ 可见矩形面积 %.0f）\n",
                    visW, visH, visW * visH, 195.0 * 122.5);

        // Twist=0 时必须与旋转无关的原始公式一致（既有行为不变）
        ViewportEntity noTwist(2, Math::Point3{ 231.5, 145.9, 0 }, 390.0, 245.0);
        noTwist.SetTwist(0.0);
        noTwist.SetViewCenter(Math::Point3{ 767492.7, 383720.3, 0 });
        noTwist.SetViewHeight(122.5);
        const AABB plain = noTwist.ModelVisibleBox();
        Check(std::abs(plain.Max.x - plain.Min.x - 195.0) < 1e-9 &&
              std::abs(plain.Max.y - plain.Min.y - 122.5) < 1e-9,
              "视口旋转：Twist=0 时外接盒就是未旋转矩形（195 × 122.5，行为不变）");
    }

    std::printf("图纸布局用例失败：%d\n", g_failures);
    return g_failures;
}
