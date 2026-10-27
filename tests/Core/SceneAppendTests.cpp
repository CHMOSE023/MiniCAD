// ── 场景流追加段测试 ───────────────────────────────────────────────────────
// 大图纸的拼接流上传 GPU 后即释放。之后新画一个图形时，后端能接追加段（IRenderer::SupportsCachedAppend）
// 的话，只把新实体的顶点经 AppendCached 接到场景缓冲末尾，不从逐实体缓存重新拼接、不整体重新上传
//（几千万顶点的图纸每画一个图形要几百毫秒）；后端接不上时按旧版本画一帧，再全量重新拼接；
// 新实体带区域覆盖（要裁剪此前的顶点）时直接全量拼接。
#include "Document/DocumentManager.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
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
            slots[slot] = { version, verts.size() };
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
            ++appends;
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
    auto frame = [&]() -> ViewState
    {
        editor.Render();
        const ViewState vs = editor.BuildViewState();
        viewport.Render(renderer, target, vs);
        if (Viewport::SceneUploaded(renderer, vs))
            editor.ReleaseUploadedSceneStreams();
        else if (vs.SceneAppendActive)
            editor.RequestSceneRestitch();
        return vs;
    };

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

    std::printf("场景流追加段测试：%d 项失败\n", g_failures);
    return g_failures;
}
