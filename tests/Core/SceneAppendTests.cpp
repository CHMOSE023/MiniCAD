// ── 场景流追加段测试 ───────────────────────────────────────────────────────
// 大图纸的拼接流上传 GPU 后即释放。之后新画一个图形时，后端能接追加段（IRenderer::SupportsCachedAppend）
// 的话，只把新实体的顶点经 AppendCached 接到场景缓冲末尾，不从逐实体缓存重新拼接、不整体重新上传
//（几千万顶点的图纸每画一个图形要几百毫秒）；后端接不上时按旧版本画一帧，再全量重新拼接；
// 新实体带区域覆盖（要裁剪此前的顶点）时直接全量拼接。
// 修改 / 删除 / 撤销：实体在静态流里的那一段经 WriteCached 原地改写（删除写成透明顶点），放不下才追加；
// 写不进时按旧版本画一帧再全量拼接；碰到区域覆盖的改动全量拼接（要重新裁剪）。
#include "Document/DocumentManager.h"
#include "Document/Command/AddEntityCommand.h"
#include "Document/Command/MoveCommand.h"
#include "Document/Command/CopyCommand.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include <algorithm>
#include <cstdio>
#include <map>

using namespace MiniCAD;

namespace
{
    // Viewport.cpp 里的槽位号
    constexpr uint32_t kSlotSceneFill  = 0;
    constexpr uint32_t kSlotScene      = 1;
    constexpr uint32_t kSlotScenePoint = 14;

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

    // 记录每个槽位持有的版本与顶点数；能接追加段，allowAppend 为假时模拟接不上
    class AppendRenderer final : public IRenderer
    {
    public:
        struct Slot { uint64_t version = 0; size_t count = 0; };
        std::map<uint32_t, Slot> slots;
        std::map<uint32_t, int>  uploads;   // 每个槽位整体上传的次数
        std::map<uint32_t, std::vector<Vertex_P3_C4>> data;   // 每个槽位缓冲的内容（核对改写段写到了哪里）
        int  appends     = 0;
        int  writes      = 0;
        bool allowAppend = true;   // 为假时模拟接不上 / 写不进

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
            slots[slot] = { version, verts.size() };
            data[slot].assign(verts.begin(), verts.end());
            ++uploads[slot];
        }
        bool IsCached(uint32_t slot, uint64_t version, bool textured) const override
        {
            const auto it = slots.find(slot);
            return !textured && it != slots.end() && it->second.version == version;
        }
        bool SupportsCachedAppend() const override { return true; }
        bool AppendCached(uint32_t slot, uint64_t baseVersion, uint64_t version, std::span<const Vertex_P3_C4> verts) override
        {
            if (!allowAppend) return false;
            auto it = slots.find(slot);
            if (it == slots.end()) return false;
            if (it->second.version == version) return true;
            if (it->second.version != baseVersion) return false;
            it->second.count += verts.size();
            it->second.version = version;
            data[slot].insert(data[slot].end(), verts.begin(), verts.end());
            ++appends;
            return true;
        }
        bool WriteCached(uint32_t slot, uint64_t baseVersion, size_t offset, std::span<const Vertex_P3_C4> verts) override
        {
            if (!allowAppend) return false;
            auto it = slots.find(slot);
            if (it == slots.end() || it->second.version != baseVersion || offset + verts.size() > it->second.count) return false;
            std::copy(verts.begin(), verts.end(), data[slot].begin() + static_cast<std::ptrdiff_t>(offset));
            ++writes;
            return true;
        }
    };

    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }
}

int RunSceneAppendTests()
{
    g_failures = 0;
    Editor::SetSceneAppendOnGpu(true);

    AppendRenderer renderer;
    NullTarget     target;
    DocumentManager manager;
    manager.InitViewport(renderer, 800, 600);
    Document& doc   = manager.Create();
    Scene&    scene = doc.GetScene();
    Editor&   editor = manager.GetEditor();
    Viewport& viewport = manager.GetViewport();

    auto addLine = [&](double y)
    {
        doc.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(
            std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, y, 0 }, Math::Point3{ 100, y, 0 })), scene);
    };
    // 与 MainFrame::RenderFrame 相同的一帧：编辑器 → 视口 → 上传成功就释放拼接流，追加段没接上就请求重新拼接
    auto frameOf = [&](Editor& ed) -> ViewState
    {
        ed.Render();
        const ViewState vs = ed.BuildViewState();
        viewport.Render(renderer, target, vs);
        if (Viewport::SceneUploaded(renderer, vs))
            ed.ReleaseUploadedSceneStreams();
        else if (vs.SceneAppendActive)
            ed.RequestSceneRestitch();
        return vs;
    };
    auto frame = [&]() -> ViewState { return frameOf(editor); };

    for (int i = 0; i < 3; ++i) addLine(10.0 * i);
    viewport.GetCamera().ZoomToBounds(-10, -10, 110, 40);
    frame();
    Check(renderer.slots[kSlotScene].count == 6 && renderer.uploads[kSlotScene] == 1, "首帧整体上传 3 条直线（6 个顶点）");
    Check(editor.BuildViewState().Scene.empty(), "上传后 CPU 端的拼接流已释放");

    // 拼接流释放后新画一条：只经 AppendCached 接到末尾
    addLine(30.0);
    ViewState vs = frame();
    Check(vs.SceneAppendActive && vs.SceneAppend.size() == 2 && vs.Scene.empty(), "新增的直线进追加段（2 个顶点），拼接流没有重建");
    Check(renderer.appends == 3 && renderer.slots[kSlotScene].count == 8 && renderer.uploads[kSlotScene] == 1,
          "三个场景槽位都接上了追加段，线缓冲 8 个顶点，没有整体重新上传");
    Check(renderer.slots[kSlotScene].version == vs.SceneVersion, "缓冲升级到新的场景版本");
    Check(!editor.BuildViewState().SceneAppendActive, "接上之后追加段清空");
    Check(editor.GetSceneStreamStats().lines == 8, "统计里的线顶点数跟着累加");

    // 连续再画一条：接在上一次升级后的版本之后
    addLine(40.0);
    vs = frame();
    Check(renderer.appends == 6 && renderer.slots[kSlotScene].count == 10 && renderer.uploads[kSlotScene] == 1,
          "再画一条仍是追加，不整体上传");

    // 后端接不上：这一帧按旧版本画，下一帧全量重新拼接并整体上传
    renderer.allowAppend = false;
    const uint64_t held = renderer.slots[kSlotScene].version;
    addLine(50.0);
    vs = frame();
    Check(vs.SceneAppendActive && renderer.slots[kSlotScene].version == held && renderer.slots[kSlotScene].count == 10,
          "接不上时这一帧按旧版本画（缓冲不变）");
    vs = frame();
    Check(!vs.SceneAppendActive && vs.Scene.size() == 12 && renderer.uploads[kSlotScene] == 2 && renderer.slots[kSlotScene].count == 12,
          "下一帧全量重新拼接（6 条直线 12 个顶点）并整体上传");
    Check(editor.BuildViewState().Scene.empty(), "整体上传后拼接流再次释放");
    renderer.allowAppend = true;

    // 新实体带区域覆盖：要裁剪此前的顶点，直接全量拼接
    doc.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<WipeoutEntity>(scene.NextObjectID(),
        std::vector<Math::Point3>{ { 20, -5, 0 }, { 40, -5, 0 }, { 40, 55, 0 }, { 20, 55, 0 } })), scene);
    vs = frame();
    Check(!vs.SceneAppendActive && renderer.uploads[kSlotScene] == 3 && renderer.appends == 6,
          "新增区域覆盖走全量拼接（裁剪此前的线），不走追加段");

    // 后端不支持追加时（如网页版）：拼接流释放后新画一条仍是全量拼接（原有行为）
    Editor::SetSceneAppendOnGpu(false);
    addLine(60.0);
    vs = frame();
    Check(!vs.SceneAppendActive && renderer.uploads[kSlotScene] == 4 && renderer.appends == 6, "关闭追加能力后新画一条走全量拼接");

    // ── 修改 / 删除 / 撤销：原地改写段 ────────────────────────────────────────
    // 拼接流释放后改一个实体：它在静态流里的那一段经 WriteCached 原地改写（删除写成透明顶点），放不下才追加
    Editor::SetSceneAppendOnGpu(true);
    Document& doc2    = manager.Create();
    Scene&    scene2  = doc2.GetScene();
    Editor&   editor2 = manager.GetEditor();
    std::vector<Object::ObjectID> ids2;
    auto addLine2 = [&](double x0, double x1, double y)
    {
        ids2.push_back(scene2.NextObjectID());
        doc2.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(
            std::make_unique<LineEntity>(ids2.back(), Math::Point3{ x0, y, 0 }, Math::Point3{ x1, y, 0 })), scene2);
    };
    auto move2 = [&](Object::ObjectID id, double dy)
    {
        doc2.GetCommandStack().Execute(std::make_unique<MoveCommand>(std::vector<Object::ObjectID>{ id }, Math::Vec3{ 0, dy, 0 }, scene2), scene2);
    };
    for (int i = 0; i < 3; ++i) addLine2(0, 100, 10.0 * i);
    viewport.GetCamera().ZoomToBounds(-10, -10, 110, 40);
    renderer.uploads[kSlotScene] = 0;
    renderer.appends = renderer.writes = 0;
    frameOf(editor2);
    Check(renderer.uploads[kSlotScene] == 1 && renderer.slots[kSlotScene].count == 6 && editor2.BuildViewState().Scene.empty(),
          "新文档首帧整体上传 3 条直线并释放拼接流");
    // 全量拼接按场景容器的遍历序（各标准库不同）：按 y 找出每条直线在缓冲里的那一段
    const auto& lineData = renderer.data[kSlotScene];
    auto segmentAt = [&](float y)
    {
        for (size_t i = 0; i + 1 < lineData.size(); i += 2)
            if (lineData[i].pos.y == y)
                return i;
        return size_t(0);
    };
    const size_t at0 = segmentAt(0.f), at1 = segmentAt(10.f), at2 = segmentAt(20.f);
    Check(at0 != at1 && at1 != at2 && at0 != at2, "三条直线各占缓冲里的一段");

    // 移动第 2 条：先把旧段清成透明顶点，再把新顶点写回原位
    move2(ids2[1], 5.0);
    vs = frameOf(editor2);
    Check(vs.SceneAppendActive && vs.ScenePatches.size() == 2 && vs.SceneAppend.empty(), "移动一条直线：两条改写段（清旧段、写回原位），没有追加");
    Check(renderer.writes == 2 && renderer.uploads[kSlotScene] == 1 && renderer.slots[kSlotScene].count == 6,
          "只写了那一段，没有整体上传，缓冲长度不变");
    Check(lineData.size() == 6 && lineData[at1].pos.y == 15.f && lineData[at1 + 1].pos.y == 15.f && lineData[at0].pos.y == 0.f && lineData[at2].pos.y == 20.f,
          "缓冲里第 2 条直线的顶点已是新位置，其余不动");
    Check(!editor2.BuildViewState().SceneAppendActive, "写入之后改写段清空");

    // 删除第 3 条：那段写成透明的退化顶点，缓冲长度不变
    editor2.SetSelection({ ids2[2] });
    editor2.DeleteSelected();
    vs = frameOf(editor2);
    Check(vs.ScenePatches.size() == 1 && renderer.slots[kSlotScene].count == 6 && lineData[at2].color.a == 0 && lineData[at2 + 1].color.a == 0,
          "删除：那段写成透明顶点，缓冲长度不变");
    Check(renderer.uploads[kSlotScene] == 1, "删除没有整体上传");

    // 撤销删除：直线回来，接到流尾（旧段仍是透明的）
    manager.Undo();
    vs = frameOf(editor2);
    Check(renderer.slots[kSlotScene].count == 8 && lineData.size() == 8 && lineData[6].pos.y == 20.f && lineData[at2].color.a == 0,
          "撤销删除：直线接到流尾，旧段仍是透明的");
    Check(editor2.GetSceneStreamStats().lines == 8 && renderer.uploads[kSlotScene] == 1, "统计跟着累加，仍没有整体上传");

    // 再移动它（现在在追加过的区域里）：仍原地改写
    move2(ids2[2], 5.0);
    vs = frameOf(editor2);
    Check(renderer.slots[kSlotScene].count == 8 && lineData[6].pos.y == 25.f && lineData[7].pos.y == 25.f && renderer.uploads[kSlotScene] == 1,
          "再移动追加过的直线：原地改写");

    // 撤销移动：同一条路
    manager.Undo();
    vs = frameOf(editor2);
    Check(lineData[6].pos.y == 20.f && renderer.uploads[kSlotScene] == 1, "撤销移动：原地改写回去");

    // 修剪 / 延伸 / 偏移 / 圆角 / 拉伸用 GeometryEditCommand（同一 ID 换几何）：也是原地改写
    {
        auto* cur = static_cast<Entity*>(scene2.GetEntity(ids2[0]));
        std::vector<GeometryEditCommand::Item> items;
        items.push_back({ ids2[0], cur->Clone(ids2[0]), std::make_unique<LineEntity>(ids2[0], Math::Point3{ 0, 0, 0 }, Math::Point3{ 60, 0, 0 }) });
        doc2.GetCommandStack().Execute(std::make_unique<GeometryEditCommand>("修剪", std::move(items)), scene2);
        vs = frameOf(editor2);
        Check(vs.ScenePatches.size() == 2 && vs.SceneAppend.empty() && renderer.uploads[kSlotScene] == 1 && lineData[at0 + 1].pos.x == 60.f,
              "修剪（同一 ID 换几何）：原地改写，没有整体上传");
    }
    // 复制：新实体接到流尾
    doc2.GetCommandStack().Execute(std::make_unique<CopyCommand>(std::vector<Object::ObjectID>{ ids2[0] }, Math::Vec3{ 0, 30, 0 }), scene2);
    vs = frameOf(editor2);
    Check(vs.SceneAppend.size() == 2 && vs.ScenePatches.empty() && renderer.slots[kSlotScene].count == 10 && renderer.uploads[kSlotScene] == 1,
          "复制：副本接到流尾，没有整体上传");
    manager.Undo();
    vs = frameOf(editor2);
    Check(vs.ScenePatches.size() == 1 && renderer.slots[kSlotScene].count == 10 && lineData[8].color.a == 0 && renderer.uploads[kSlotScene] == 1,
          "撤销复制：副本那段写成透明顶点");

    // 后端写不进：这一帧按旧版本画，下一帧全量重新拼接（透明的洞随之清掉：3 条直线 6 个顶点）
    renderer.allowAppend = false;
    const uint64_t held2 = renderer.slots[kSlotScene].version;
    move2(ids2[0], 5.0);
    vs = frameOf(editor2);
    Check(vs.SceneAppendActive && renderer.slots[kSlotScene].version == held2, "写不进时这一帧按旧版本画");
    vs = frameOf(editor2);
    // 全量拼接按场景容器的遍历序，直线 0 不一定在最前：只查有没有它移动后的顶点
    const bool hasMoved0 = std::any_of(lineData.begin(), lineData.end(), [](const Vertex_P3_C4& v) { return v.pos.y == 5.f && v.pos.x == 60.f; });
    Check(!vs.SceneAppendActive && renderer.uploads[kSlotScene] == 2 && renderer.slots[kSlotScene].count == 6 && hasMoved0,
          "下一帧全量重新拼接（洞清掉，6 个顶点）并整体上传");
    renderer.allowAppend = true;

    // 区域覆盖：拼进场景后，碰到它的实体改动要重新裁剪（全量拼接），没碰到的仍原地改写
    doc2.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<WipeoutEntity>(scene2.NextObjectID(),
        std::vector<Math::Point3>{ { 20, -5, 0 }, { 40, -5, 0 }, { 40, 55, 0 }, { 20, 55, 0 } })), scene2);
    vs = frameOf(editor2);
    Check(renderer.uploads[kSlotScene] == 3, "新增区域覆盖：全量拼接");
    addLine2(50, 100, 40.0);   // 在区域覆盖右边，碰不到
    vs = frameOf(editor2);
    Check(vs.SceneAppendActive && vs.SceneAppend.size() == 2 && renderer.uploads[kSlotScene] == 3, "区域覆盖右边新画一条：追加");
    const size_t nBefore = renderer.slots[kSlotScene].count;
    move2(ids2.back(), 2.0);
    vs = frameOf(editor2);
    Check(vs.ScenePatches.size() == 2 && renderer.uploads[kSlotScene] == 3 && renderer.slots[kSlotScene].count == nBefore,
          "移动它：原地改写（没碰到区域覆盖）");
    move2(ids2[1], 2.0);   // 横穿区域覆盖的直线：要重新裁剪
    vs = frameOf(editor2);
    Check(!vs.SceneAppendActive && renderer.uploads[kSlotScene] == 4, "移动被区域覆盖裁过的直线：全量拼接");

    std::printf("场景流追加段测试：%d 项失败\n", g_failures);
    return g_failures;
}
