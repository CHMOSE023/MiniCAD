// ── 分帧细分测试（Editor::BeginProgressiveBuild）──────────────────────────────
// 打开大图纸后不在一帧里细分整张图：每帧按绘制序细分一批、到时限就停，大块参照按块内子实体分段
//（DrawContext::BeginChunk，接着画时按路径跳过已画的子实体，不进已画完的嵌套块参照），细分出来的接到场景流尾部。
// 这里把时限设成 0：每帧只细分一个实体或块内的一个子实体，检查
//   · 第一帧就有内容、进度在走，画全后与一次细分完（同步）的顶点完全相同（含块内后面的段里的区域覆盖裁剪前几段、
//     裁剪块的边框只画一次、阵列插入）
//   · 后端能接追加段时新细分的接到缓冲末尾，不每帧整体上传；接不了时每帧全量拼接也能画全
//   · 细分到一半时删掉正在分段细分的块参照、隐藏图层：画全后与同样改动后同步细分的结果相同
#include "Document/DocumentManager.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

using namespace MiniCAD;

namespace
{
    // Viewport.cpp 里的槽位号
    constexpr uint32_t kSlotSceneFill = 0;
    constexpr uint32_t kSlotScene     = 1;

    class NullTarget final : public IRenderTarget
    {
        void  Create(int, int) override {}
        void  Resize(int, int) override {}
        void  Release() override {}
        int   GetWidth()  const override { return 800; }
        int   GetHeight() const override { return 600; }
        void* GetNativeHandle() const override { return nullptr; }
        void* GetNativeShaderResource() const override { return nullptr; }
    };

    // 记录每个槽位缓冲的内容、整体上传与追加的次数
    class BufferRenderer final : public IRenderer
    {
    public:
        struct Slot { uint64_t version = 0; };
        std::map<uint32_t, Slot> slots;
        std::map<uint32_t, int>  uploads;
        std::map<uint32_t, std::vector<Vertex_P3_C4>> data;
        int  appends     = 0;
        bool allowAppend = true;

        void BeginFrame(IRenderTarget&, const ViewportDesc&) override {}
        void EndFrame() override {}
        void Submit(std::span<const Vertex_P3_C4>, const Math::Mat4&, PrimitiveType, bool, bool) override {}
        void SubmitTextured(std::span<const Vertex_P3_C4_UV>, const Math::Mat4&, void*, bool, bool) override {}
        void* GetNativeDevice() override { return nullptr; }

        void SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                          const Math::Mat4&, PrimitiveType, bool, bool) override
        {
            auto it = slots.find(slot);
            if (it != slots.end() && it->second.version == version) return;
            slots[slot] = { version };
            data[slot].assign(verts.begin(), verts.end());
            ++uploads[slot];
        }
        bool IsCached(uint32_t slot, uint64_t version, bool textured) const override
        {
            const auto it = slots.find(slot);
            return !textured && it != slots.end() && it->second.version == version;
        }
        bool SupportsCachedAppend() const override { return allowAppend; }
        bool AppendCached(uint32_t slot, uint64_t baseVersion, uint64_t version, std::span<const Vertex_P3_C4> verts) override
        {
            if (!allowAppend) return false;
            auto it = slots.find(slot);
            if (it == slots.end()) return false;
            if (it->second.version == version) return true;
            if (it->second.version != baseVersion) return false;
            it->second.version = version;
            data[slot].insert(data[slot].end(), verts.begin(), verts.end());
            ++appends;
            return true;
        }
        bool WriteCached(uint32_t slot, uint64_t baseVersion, size_t offset, std::span<const Vertex_P3_C4> verts) override
        {
            if (!allowAppend) return false;
            auto it = slots.find(slot);
            if (it == slots.end() || it->second.version != baseVersion || offset + verts.size() > data[slot].size()) return false;
            std::copy(verts.begin(), verts.end(), data[slot].begin() + static_cast<std::ptrdiff_t>(offset));
            return true;
        }
    };

    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    struct Ids { Object::ObjectID insertB = 0; LayerID arrayLayer = 0; };

    // 模型空间：30 条线、块 B 的插入、块 C 的 2×3 阵列插入（在单独的图层上）、带边框的裁剪插入 B、再 10 条线。
    // 块 B：60 条横线、两个嵌套的 C、一个横穿前面横线的区域覆盖（分段细分时在后面的段里）、再 60 条横线
    Ids BuildScene(Document& doc)
    {
        Scene& scene = doc.GetScene();
        Ids ids;
        BlockTable& blocks = scene.GetBlockTable();
        const BlockID cid = blocks.Create("PB_C", Math::Point3{ 0, 0, 0 });
        BlockEntity* c = blocks.Find(cid);
        for (int i = 0; i < 20; ++i)
            c->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, i * 0.5, 0 }, Math::Point3{ 8, i * 0.5, 0 }));
        const BlockID bid = blocks.Create("PB_B", Math::Point3{ 0, 0, 0 });
        BlockEntity* b = blocks.Find(bid);
        for (int i = 0; i < 60; ++i)
            b->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, double(i), 0 }, Math::Point3{ 100, double(i), 0 }));
        for (int k = 0; k < 2; ++k)
        {
            auto nested = std::make_unique<InsertEntity>(scene.NextObjectID(), cid, "PB_C", Math::Point3{ 20.0 + 50.0 * k, 62, 0 });
            nested->SetBlock(c);
            b->AddEntity(std::move(nested));
        }
        b->AddEntity(std::make_unique<WipeoutEntity>(scene.NextObjectID(),
            std::vector<Math::Point3>{ { 40, -5, 0 }, { 60, -5, 0 }, { 60, 140, 0 }, { 40, 140, 0 } }));
        for (int i = 0; i < 60; ++i)
            b->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 75.0 + i, 0 }, Math::Point3{ 100, 75.0 + i, 0 }));

        for (int i = 0; i < 30; ++i)
            scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ -50, -10.0 - i, 0 }, Math::Point3{ 450, -10.0 - i, 0 }));
        auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), bid, "PB_B", Math::Point3{ 0, 0, 0 });
        ins->SetBlock(b);
        ids.insertB = ins->GetID();
        scene.AddEntity(std::move(ins));
        auto arr = std::make_unique<InsertEntity>(scene.NextObjectID(), cid, "PB_C", Math::Point3{ 150, 0, 0 });
        arr->SetBlock(c);
        arr->SetArray(2, 3, 20, 20);
        ids.arrayLayer = scene.GetLayerManager().AddLayer("PB_ARRAY");
        EntityAttr attr = arr->GetAttr();
        attr.LayerId = ids.arrayLayer;
        arr->SetAttr(attr);
        scene.AddEntity(std::move(arr));
        auto clipped = std::make_unique<InsertEntity>(scene.NextObjectID(), bid, "PB_B", Math::Point3{ 250, 0, 0 });
        clipped->SetBlock(b);
        clipped->SetClipBoundary({ { 10, 10, 0 }, { 90, 10, 0 }, { 90, 120, 0 }, { 10, 120, 0 } });
        clipped->SetClipFrameVisible(true);
        scene.AddEntity(std::move(clipped));
        for (int i = 0; i < 10; ++i)
            scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ -50, 150.0 + i, 0 }, Math::Point3{ 450, 150.0 + i, 0 }));
        return ids;
    }

    // 绘制序在追加与全量拼接之间可能不同：按坐标与颜色排序后比较
    std::vector<Vertex_P3_C4> Sorted(std::vector<Vertex_P3_C4> v)
    {
        auto key = [](const Vertex_P3_C4& a) { return std::tie(a.pos.x, a.pos.y, a.pos.z, a.color.r, a.color.g, a.color.b, a.color.a); };
        std::sort(v.begin(), v.end(), [&](const Vertex_P3_C4& a, const Vertex_P3_C4& b) { return key(a) < key(b); });
        return v;
    }
    bool SameVertices(const std::vector<Vertex_P3_C4>& a, const std::vector<Vertex_P3_C4>& b)
    {
        if (a.size() != b.size()) return false;
        auto key = [](const Vertex_P3_C4& v) { return std::tie(v.pos.x, v.pos.y, v.pos.z, v.color.r, v.color.g, v.color.b, v.color.a); };
        for (size_t i = 0; i < a.size(); ++i)
            if (key(a[i]) != key(b[i])) return false;
        return true;
    }
}

int RunProgressiveBuildTests()
{
    g_failures = 0;
    Editor::SetSceneAppendOnGpu(true);

    BufferRenderer renderer;
    NullTarget     target;
    DocumentManager manager;
    manager.InitViewport(renderer, 800, 600);
    Viewport& viewport = manager.GetViewport();

    // 与 MainFrame::RenderViewport 相同的一帧
    auto frameOf = [&](Editor& ed)
    {
        ed.EnsureSceneCached(renderer);
        ed.Render();
        const ViewState vs = ed.BuildViewState();
        viewport.Render(renderer, target, vs);
        if (Viewport::SceneUploaded(renderer, vs))
            ed.ReleaseUploadedSceneStreams();
        else if (vs.SceneAppendActive)
            ed.RequestSceneRestitch();
    };
    // 一次细分完的结果（不分帧）：线、填充两个槽位排序后的顶点
    auto reference = [&](const std::function<void(Document&, const Ids&)>& edit)
    {
        Document& doc = manager.Create();
        const Ids ids = BuildScene(doc);
        if (edit) edit(doc, ids);
        viewport.GetCamera().ZoomToBounds(-60, -50, 460, 170);
        frameOf(manager.GetEditor());
        frameOf(manager.GetEditor());
        return std::pair{ Sorted(renderer.data[kSlotScene]), Sorted(renderer.data[kSlotSceneFill]) };
    };
    // 分帧细分到画全（最多 5000 帧），返回帧数
    auto buildUntilDone = [&](Editor& ed, int maxFrames = 5000)
    {
        int frames = 0;
        while (ed.IsBuildingDisplay() && frames < maxFrames)
        {
            frameOf(ed);
            ++frames;
        }
        return frames;
    };

    // ── 后端能接追加段 ────────────────────────────────────────────────────
    const auto [refLines, refFills] = reference(nullptr);
    Check(refLines.size() > 1000, "同步细分出参照顶点");
    {
        Document& doc = manager.Create();
        BuildScene(doc);
        Editor& editor = manager.GetEditor();
        viewport.GetCamera().ZoomToBounds(-60, -50, 460, 170);
        editor.BeginProgressiveBuild(1e-6);
        renderer.uploads.clear();
        renderer.appends = 0;
        frameOf(editor);
        const float p1 = editor.DisplayBuildProgress();
        Check(editor.IsBuildingDisplay() && !renderer.data[kSlotScene].empty() && renderer.data[kSlotScene].size() < refLines.size(),
              "第一帧就画出一部分，还在细分");
        Check(p1 > 0.0f && p1 < 0.1f, "第一帧之后进度大于 0、远不到一半");
        bool monotonic = true;
        float last = p1;
        int frames = 1;
        while (editor.IsBuildingDisplay() && frames < 5000)
        {
            frameOf(editor);
            ++frames;
            const float p = editor.DisplayBuildProgress();
            monotonic &= p >= last;
            last = p;
        }
        std::printf("    分帧细分 %d 帧，整体上传 %d 次，追加 %d 次\n", frames, renderer.uploads[kSlotScene], renderer.appends);
        Check(!editor.IsBuildingDisplay() && frames > 300, "每帧只细分一个（子）实体，几百帧后画全");
        Check(monotonic, "进度一路不减");
        Check(renderer.uploads[kSlotScene] <= 3 && renderer.appends > 100, "新细分的接到缓冲末尾，不每帧整体上传");
        Check(SameVertices(Sorted(renderer.data[kSlotScene]), refLines), "画全后线顶点与同步细分完全相同（块内区域覆盖、裁剪边框、阵列）");
        Check(SameVertices(Sorted(renderer.data[kSlotSceneFill]), refFills), "画全后填充顶点与同步细分相同");
        // 画全后恢复平常的增量更新：再画一条线只追加
        const int uploads = renderer.uploads[kSlotScene];
        doc.GetScene().AddEntity(std::make_unique<LineEntity>(doc.GetScene().NextObjectID(), Math::Point3{ 0, 300, 0 }, Math::Point3{ 10, 300, 0 }));
        frameOf(editor);
        Check(renderer.uploads[kSlotScene] == uploads && renderer.data[kSlotScene].size() == refLines.size() + 2,
              "画全后新画一条线：追加，不整体上传");
    }

    // ── 后端接不了追加段：每帧从缓存全量拼接，在 CPU 端的流上接着细分 ───────────────
    {
        Editor::SetSceneAppendOnGpu(false);
        renderer.allowAppend = false;
        Document& doc = manager.Create();
        BuildScene(doc);
        Editor& editor = manager.GetEditor();
        viewport.GetCamera().ZoomToBounds(-60, -50, 460, 170);
        editor.BeginProgressiveBuild(1e-6);
        // 每帧都全量拼接，细分时限至少是拼接用时的两倍：每帧不止细分一个，但仍分好几帧
        const int frames = buildUntilDone(editor);
        std::printf("    接不了追加段：分帧细分 %d 帧\n", frames);
        Check(!editor.IsBuildingDisplay() && frames > 1, "接不了追加段时也能分帧画全");
        Check(SameVertices(Sorted(renderer.data[kSlotScene]), refLines), "接不了追加段：画全后与同步细分相同");
        Editor::SetSceneAppendOnGpu(true);
        renderer.allowAppend = true;
    }

    // ── 细分到一半时删掉正在分段细分的块参照 ──────────────────────────────
    {
        const auto [delLines, delFills] = reference([](Document& d, const Ids& ids) { d.GetScene().RemoveEntity(ids.insertB); });
        Document& doc = manager.Create();
        const Ids ids = BuildScene(doc);
        Editor& editor = manager.GetEditor();
        viewport.GetCamera().ZoomToBounds(-60, -50, 460, 170);
        editor.BeginProgressiveBuild(1e-6);
        for (int i = 0; i < 80; ++i)   // 30 条线之后进到块 B 里面
            frameOf(editor);
        Check(editor.IsBuildingDisplay() && renderer.data[kSlotScene].size() > 70 * 2, "删除前已细分到块 B 里面");
        editor.SetSelection({ ids.insertB });
        editor.DeleteSelected();
        buildUntilDone(editor);
        Check(!editor.IsBuildingDisplay() && SameVertices(Sorted(renderer.data[kSlotScene]), delLines),
              "删掉细分到一半的块参照：已接上的几段清掉，画全后与删除后同步细分相同");
    }

    // ── 细分到一半时隐藏图层（全局显示失效：从头分帧细分）─────────────────────────
    {
        const auto [hidLines, hidFills] = reference([](Document& d, const Ids& ids)
        {
            d.GetScene().GetLayerManager().GetLayer(ids.arrayLayer)->SetVisible(false);
        });
        Document& doc = manager.Create();
        const Ids ids = BuildScene(doc);
        Editor& editor = manager.GetEditor();
        viewport.GetCamera().ZoomToBounds(-60, -50, 460, 170);
        editor.BeginProgressiveBuild(1e-6);
        for (int i = 0; i < 250; ++i)   // 进到阵列插入里面
            frameOf(editor);
        doc.GetScene().GetLayerManager().GetLayer(ids.arrayLayer)->SetVisible(false);
        doc.GetScene().MarkDisplayDirty();
        frameOf(editor);
        Check(editor.IsBuildingDisplay(), "隐藏图层后仍在分帧细分（从头再来，不当帧细分完）");
        buildUntilDone(editor);
        Check(!editor.IsBuildingDisplay() && SameVertices(Sorted(renderer.data[kSlotScene]), hidLines),
              "细分到一半隐藏图层：画全后与隐藏后同步细分相同");
    }

    std::printf("分帧细分测试：%d 项失败\n", g_failures);
    return g_failures;
}
