// ── 块定义级细分缓存测试（BlockTessCache、DrawContext::BeginBlockInstance）──────────────────
// 同一块定义插入多次时只细分一次，之后的插入把记下的结果经平移、旋转接上。这里用 DrawContext 逐个实体细分
//（同 Editor::TessellateEntity），开缓存与不开缓存的输出逐项比较（顺序相同，坐标按 float 舍入的误差比较）：
//   · 平移、旋转、镜像、放大的插入，阵列插入，块内 ByBlock 颜色与 0 层随插入，嵌套块、虚线、圆弧、文字、区域覆盖
//   · 在裁剪框里的块参照：整个在框内的接上缓存，整个在框外的跳过，跨边的照常逐个画；裁剪框改动过内容的不记
//   · 分段细分（DrawContext::BeginChunk）里命中缓存：阵列的前一个单元接上缓存、后一个单元分几段画完，与一次画完相同
#include "Document/DrawContext.hpp"
#include "Document/BlockTessCache.hpp"
#include "Document/TextStyleResolver.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Scene/Scene.h"
#include "Text/FontSystem.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    // 一次细分的全部输出
    struct Output
    {
        std::vector<Vertex_P3_C4>    lines, fills, points;
        std::vector<Vertex_P3_C4_UV> texts;
        std::vector<GlyphInstance>   glyphs;
        std::vector<std::vector<Math::Point3>> wipes;
        std::vector<DeferredDash>    dashes;
    };

    bool Near(float a, float b) { return std::abs(a - b) <= 1e-4f * (1.0f + std::abs(a)); }
    bool Same(const std::vector<Vertex_P3_C4>& a, const std::vector<Vertex_P3_C4>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!Near(a[i].pos.x, b[i].pos.x) || !Near(a[i].pos.y, b[i].pos.y) || !Near(a[i].pos.z, b[i].pos.z) || !(a[i].color == b[i].color))
                return false;
        return true;
    }
    bool Same(const std::vector<GlyphInstance>& a, const std::vector<GlyphInstance>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!Near(a[i].m00, b[i].m00) || !Near(a[i].m01, b[i].m01) || !Near(a[i].m10, b[i].m10) || !Near(a[i].m11, b[i].m11) ||
                !Near(a[i].tx, b[i].tx) || !Near(a[i].ty, b[i].ty) || a[i].mesh != b[i].mesh || !(a[i].color == b[i].color))
                return false;
        return true;
    }
    bool Same(const Output& a, const Output& b)
    {
        bool wipes = a.wipes.size() == b.wipes.size();
        for (size_t i = 0; wipes && i < a.wipes.size(); ++i)
        {
            wipes = a.wipes[i].size() == b.wipes[i].size();
            for (size_t k = 0; wipes && k < a.wipes[i].size(); ++k)
                wipes = std::abs(a.wipes[i][k].x - b.wipes[i][k].x) < 1e-6 && std::abs(a.wipes[i][k].y - b.wipes[i][k].y) < 1e-6;
        }
        return Same(a.lines, b.lines) && Same(a.fills, b.fills) && Same(a.points, b.points) && Same(a.glyphs, b.glyphs) &&
               a.texts.size() == b.texts.size() && a.dashes.size() == b.dashes.size() && wipes;
    }

    // 细分用的环境：场景的图层 / 线型表、字体、字形网格库
    struct Tessellator
    {
        Scene&           scene;
        FontSystem&      fonts;
        GlyphMeshLibrary library;
        Overlay          overlay;

        // 逐个实体细分（同 Editor::TessellateEntity：每个实体前后 BeginEntity），cache 为空时不缓存
        Output Run(const std::vector<const Entity*>& entities, BlockTessCache* cache)
        {
            Output out;
            DrawContext ctx(out.lines, out.fills, out.texts, overlay, scene.GetLayerManager(), scene.GetLineTypeTable(), 0.5,
                            nullptr, MakeTextStyleResolver(&fonts, &scene.GetTextStyleTable()));
            ctx.SetGlyphInstanceSink(&out.glyphs, &library);
            ctx.SetDeferLongDashes(true);
            ctx.SetBlockCache(cache);
            for (const Entity* e : entities)
            {
                ctx.BeginEntity(&e->GetAttr());
                e->Draw(ctx, false, false);
                ctx.BeginEntity(nullptr);
                for (auto& w : ctx.TakeWipes())   // 每个实体自己的区域覆盖（下一个实体开始时清空）
                    out.wipes.push_back(std::move(w));
            }
            out.points = ctx.TakePoints();
            out.dashes = ctx.TakeDeferredDashes();
            return out;
        }
    };

    std::unique_ptr<InsertEntity> MakeInsert(Scene& scene, BlockEntity* block, const Math::Point3& at)
    {
        auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), block->GetID(), block->GetName(), at);
        ins->SetBlock(block);
        return ins;
    }
}

int RunBlockTessCacheTests()
{
    g_failures = 0;
    // 文字要有字体才产出字形实例（否则文字这一项是假通过）
    FontSystem fonts;
    fonts.Initialize();
#ifdef MINICAD_ASSET_DIR
    fonts.GetFontEngine().SetFontDir(std::string(MINICAD_ASSET_DIR) + "/fonts/");
#endif

    Scene scene;
    LayerManager& layers = scene.GetLayerManager();
    const LayerID red   = layers.AddLayer("BT_RED");
    const LayerID green = layers.AddLayer("BT_GREEN");
    layers.GetLayer(red)->SetColor({ 1, 0, 0, 1 });
    layers.GetLayer(green)->SetColor({ 0, 1, 0, 1 });
    LineTypeRecord dashed;
    dashed.Name          = "BT_DASHED";
    dashed.Pattern       = { 2.0, -1.0 };
    dashed.PatternLength = 3.0;
    const LineTypeID dashedId = scene.GetLineTypeTable().Add(dashed);

    BlockTable& blocks = scene.GetBlockTable();
    // 块 E：嵌套在 D 里的小块
    BlockEntity* e = blocks.Find(blocks.Create("BT_E", Math::Point3{ 0, 0, 0 }));
    for (int i = 0; i < 3; ++i)
        e->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, i * 2.0, 0 }, Math::Point3{ 10, i * 2.0 + 1, 0 }));
    e->AddEntity(std::make_unique<CircleEntity>(scene.NextObjectID(), Math::Point3{ 5, 3, 0 }, 4.0));

    // 块 D：0 层的线（随插入的图层）、ByBlock 颜色的线、虚线、圆弧、文字、两个嵌套的 E（一个转 90°）、区域覆盖、之后再几条线
    BlockEntity* d = blocks.Find(blocks.Create("BT_D", Math::Point3{ 0, 0, 0 }));
    d->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 100, 0, 0 }));
    {
        auto byBlock = std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 10, 0 }, Math::Point3{ 100, 10, 0 });
        EntityAttr a = byBlock->GetAttr();
        a.LayerId = red;
        a.Color   = EntityColor::ByBlock();
        byBlock->SetAttr(a);
        d->AddEntity(std::move(byBlock));
        auto dash = std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 20, 0 }, Math::Point3{ 97.3, 31.7, 0 });
        a          = dash->GetAttr();
        a.LayerId  = green;
        a.LineType = dashedId;
        dash->SetAttr(a);
        d->AddEntity(std::move(dash));
    }
    d->AddEntity(std::make_unique<ArcEntity>(scene.NextObjectID(), Math::Point3{ 50, 50, 0 }, 20.0, 0.3, 2.5));
    d->AddEntity(std::make_unique<TextEntity>(scene.NextObjectID(), Math::Point3{ 5, 60, 0 }, "AB12", 5.0f));
    for (int k = 0; k < 2; ++k)
    {
        auto nested = MakeInsert(scene, e, Math::Point3{ 20.0 + 40.0 * k, 35, 0 });
        nested->SetRotation(k * std::numbers::pi / 2);
        d->AddEntity(std::move(nested));
    }
    d->AddEntity(std::make_unique<WipeoutEntity>(scene.NextObjectID(),
        std::vector<Math::Point3>{ { 40, -5, 0 }, { 60, -5, 0 }, { 60, 45, 0 }, { 40, 45, 0 } }));
    for (int i = 0; i < 4; ++i)
        d->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 70.0 + i, 0 }, Math::Point3{ 100, 70.0 + i, 0 }));

    // 模型空间：D 的各种插入
    std::vector<std::unique_ptr<Entity>> owned;
    auto add = [&](std::unique_ptr<Entity> ent) { owned.push_back(std::move(ent)); return owned.back().get(); };
    {
        auto i1 = MakeInsert(scene, d, { 0, 0, 0 });
        EntityAttr a = i1->GetAttr();
        a.LayerId = red;
        i1->SetAttr(a);
        add(std::move(i1));
        auto i2 = MakeInsert(scene, d, { 200, 0, 0 });
        a         = i2->GetAttr();
        a.LayerId = green;
        a.Color   = EntityColor::FromAci(5);
        i2->SetAttr(a);
        add(std::move(i2));
        auto i3 = MakeInsert(scene, d, { 400, 0, 0 });
        i3->SetRotation(std::numbers::pi / 6);
        add(std::move(i3));
        auto i4 = MakeInsert(scene, d, { 600, 0, 0 });
        i4->SetScale({ -1, 1, 1 });
        add(std::move(i4));
        auto i5 = MakeInsert(scene, d, { 800, 0, 0 });
        i5->SetScale({ 2, 2, 2 });
        add(std::move(i5));
        auto i6 = MakeInsert(scene, d, { 0, 300, 0 });
        i6->SetArray(3, 2, 150, 120);
        add(std::move(i6));
        add(MakeInsert(scene, d, { 1100, 0, 0 }));
        add(MakeInsert(scene, d, { 1250, 37.5, 0 }));
    }

    Tessellator tess{ scene, fonts };
    std::vector<const Entity*> model;
    for (const auto& ent : owned)
        model.push_back(ent.get());
    auto countUses = [&](std::unordered_map<const void*, int>& uses) { uses[d] = 2; uses[e] = 2; };

    // ── 不在裁剪框里 ─────────────────────────────────────────────────────────
    {
        const Output plain = tess.Run(model, nullptr);
        BlockTessCache cache;
        cache.SetUseCounter(countUses);
        const Output cached = tess.Run(model, &cache);
        std::printf("    线 %zu、填充 %zu、字形 %zu、区域覆盖 %zu；记下 %zu 个块，接上 %zu 次\n", plain.lines.size(), plain.fills.size(),
                    plain.glyphs.size(), plain.wipes.size(), cache.EntryCount(), cache.Replays());
        Check(plain.lines.size() > 1000 && plain.wipes.size() == 13 && !plain.glyphs.empty(),
              "不缓存时细分出参照（13 个插入各一个区域覆盖，有文字的字形实例）");
        Check(cache.EntryCount() >= 4 && cache.Replays() >= 8, "按块定义记下，之后的插入接上");
        Check(Same(plain, cached), "缓存接上的与逐个细分的相同（平移、旋转、镜像、放大、阵列、ByBlock、0 层、嵌套、虚线、文字、区域覆盖）");
    }

    // ── 在裁剪框里：块 F 里三个 D（整个在框内、跨边、整个在框外），F 用矩形与 L 形两种裁剪框各插入一次 ──────
    BlockEntity* f = blocks.Find(blocks.Create("BT_F", Math::Point3{ 0, 0, 0 }));
    for (const double x : { 0.0, 250.0, 500.0 })
        f->AddEntity(MakeInsert(scene, d, { x, 0, 0 }));
    f->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ -20, -20, 0 }, Math::Point3{ 700, -20, 0 }));
    std::vector<const Entity*> clippedModel;
    {
        auto f1 = MakeInsert(scene, f, { 0, 1000, 0 });
        f1->SetClipBoundary({ { -10, -30, 0 }, { 300, -30, 0 }, { 300, 150, 0 }, { -10, 150, 0 } });
        clippedModel.push_back(add(std::move(f1)));
        auto f2 = MakeInsert(scene, f, { 2000, 1000, 0 });
        f2->SetClipBoundary({ { -10, -30, 0 }, { 120, -30, 0 }, { 120, 60, 0 }, { 300, 60, 0 }, { 300, 150, 0 }, { -10, 150, 0 } });
        clippedModel.push_back(add(std::move(f2)));
    }
    {
        const Output plain = tess.Run(clippedModel, nullptr);
        // 先在框外把 D、E 记下来（同样在 0 层、不缩放），再画裁剪框里的。D 有区域覆盖（裁剪框里画法不同），
        // 只有整个在框外时能直接用；E 在框内时接上
        BlockTessCache cache;
        cache.SetUseCounter(countUses);
        tess.Run({ model[6] }, &cache);
        const size_t warmReplays = cache.Replays();
        const Output cached = tess.Run(clippedModel, &cache);
        std::printf("    裁剪框里：线 %zu；接上 %zu 次，在框外跳过 %zu 次\n", plain.lines.size(), cache.Replays() - warmReplays, cache.Skips());
        Check(cache.Skips() >= 2 && cache.Replays() > warmReplays, "整个在裁剪框外的块跳过，框内的接上");
        Check(Same(plain, cached), "裁剪框里命中缓存（框内接上、框外跳过、跨边逐个画）与逐个细分相同");

        // 第一次就在裁剪框里遇到：整个在框内的 D 能记下来（第二个裁剪插入里接上），跨边的、在框外的不记
        BlockTessCache cold;
        cold.SetUseCounter(countUses);
        const Output fresh = tess.Run(clippedModel, &cold);
        Check(cold.EntryCount() > 0 && Same(plain, fresh), "在裁剪框里也能记：裁剪框改动过内容的块不记，结果与逐个细分相同");
    }

    // ── 分段细分里命中缓存：D 的 2 列阵列放进裁剪块 G，第一个单元在框内接上缓存，第二个跨边、逐个子实体分段画 ─────
    {
        BlockEntity* plainD = blocks.Find(blocks.Create("BT_PLAIN", Math::Point3{ 0, 0, 0 }));   // 不带区域覆盖（分段之间的裁剪由 Editor 做）
        for (int i = 0; i < 12; ++i)
            plainD->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, i * 3.0, 0 }, Math::Point3{ 80, i * 3.0, 0 }));
        plainD->AddEntity(MakeInsert(scene, e, { 30, 10, 0 }));
        BlockEntity* g = blocks.Find(blocks.Create("BT_G", Math::Point3{ 0, 0, 0 }));
        auto arr = MakeInsert(scene, plainD, { 0, 0, 0 });
        arr->SetArray(2, 1, 100, 0);
        g->AddEntity(std::move(arr));
        for (int i = 0; i < 5; ++i)
            g->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 50.0 + i, 0 }, Math::Point3{ 200, 50.0 + i, 0 }));
        auto gi = MakeInsert(scene, g, { 0, 3000, 0 });
        gi->SetClipBoundary({ { -10, -10, 0 }, { 140, -10, 0 }, { 140, 60, 0 }, { -10, 60, 0 } });
        const Entity* gEntity = add(std::move(gi));
        auto uses = [&](std::unordered_map<const void*, int>& u) { u[plainD] = 2; u[e] = 2; };

        const Output whole = tess.Run({ gEntity }, nullptr);
        BlockTessCache cache;
        cache.SetUseCounter(uses);
        auto warmInsert = MakeInsert(scene, plainD, { 5000, 0, 0 });
        tess.Run({ warmInsert.get() }, &cache);

        // 分段：时限已过，每段画一个叶子子实体（或接上一个缓存的块）就停
        Output parts;
        DrawContext ctx(parts.lines, parts.fills, parts.texts, tess.overlay, scene.GetLayerManager(), scene.GetLineTypeTable(), 0.5,
                        nullptr, MakeTextStyleResolver(&fonts, &scene.GetTextStyleTable()));
        ctx.SetGlyphInstanceSink(&parts.glyphs, &tess.library);
        ctx.SetDeferLongDashes(true);
        ctx.SetBlockCache(&cache);
        std::vector<size_t> resume;
        int chunks = 0;
        size_t drawn = 0;
        do
        {
            ctx.BeginChunk(std::move(resume), std::chrono::steady_clock::now() - std::chrono::seconds(1));
            ctx.BeginEntity(&gEntity->GetAttr());
            gEntity->Draw(ctx, false, false);
            ctx.BeginEntity(nullptr);
            drawn += ctx.EndChunk(resume);
            ++chunks;
        } while (!resume.empty() && chunks < 1000);
        std::printf("    分段细分 %d 段，画了 %zu 个子实体；接上 %zu 次\n", chunks, drawn, cache.Replays());
        Check(cache.Replays() >= 1 && chunks > 5, "阵列的第一个单元接上缓存，之后分段");
        Check(Same(whole.lines, parts.lines), "分段时命中缓存：序号跳过接上的子实体，各段拼起来与一次画完相同");
    }

    std::printf("块定义级细分缓存测试：%d 项失败\n", g_failures);
    return g_failures;
}
