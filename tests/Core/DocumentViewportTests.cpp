// ── 多视口测试 ─────────────────────────────────────────────────────────────
// 文档拖出成独立窗口时有自己的视口（DocumentManager::DetachDocument），主视口继续显示其余文档：
// - 当前文档（GetActive）与主视口里的文档（GetMainDocument）分开：拖出的文档成为当前文档时主视口不动
// - 两个视口各自的相机、尺寸互不影响；拖回（AttachDocument）时相机回写到文档并恢复到主视口
// - 渲染器按槽位只留一份顶点缓冲，两个视口轮流渲染时互相顶掉：下一次渲染前 Editor::EnsureSceneCached 重新拼接，
//   不会按空流上传；视口的网格 / 坐标轴 / Gizmo 的版本号进程内全局分配，两个视口不会被当成同一份缓冲
#include "Document/DocumentManager.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include <cstdio>
#include <map>

using namespace MiniCAD;

namespace
{
    // Viewport.cpp 里的槽位号
    constexpr uint32_t kSlotScene = 1;
    constexpr uint32_t kSlotGizmo = 8;

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

    // 记录每个槽位持有的版本与顶点数（每个槽位只留一份，同 D3D11 / D3D12 渲染器）
    class SlotRenderer final : public IRenderer
    {
    public:
        struct Slot { uint64_t version = 0; size_t count = 0; };
        std::map<uint32_t, Slot> slots;
        std::map<uint32_t, int>  uploads;

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
    };

    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }
}

int RunDocumentViewportTests()
{
    g_failures = 0;
    SlotRenderer    renderer;
    NullTarget      target;
    DocumentManager manager;
    manager.InitViewport(renderer, 800, 600);
    Viewport& mainVp = manager.GetMainViewport();

    auto addLine = [](Document& doc, double y)
    {
        Scene& scene = doc.GetScene();
        doc.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(
            std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, y, 0 }, Math::Point3{ 100, y, 0 })), scene);
    };
    // 与 MainFrame::RenderViewport / RenderDocWindow 相同的一帧
    auto frame = [&](Document& doc) -> ViewState
    {
        Editor&   ed = manager.GetEditor(doc);
        Viewport* vp = manager.ViewportOf(&doc);
        ed.EnsureSceneCached(renderer);
        ed.Render();
        const ViewState vs = ed.BuildViewState();
        vp->Render(renderer, target, vs);
        if (Viewport::SceneUploaded(renderer, vs))
            ed.ReleaseUploadedSceneStreams();
        return vs;
    };

    Document& a = manager.Create();
    Document& b = manager.Create();
    addLine(a, 0.0);
    addLine(b, 10.0);
    addLine(b, 20.0);
    mainVp.GetCamera().ZoomToBounds(-10, -10, 110, 40);
    Check(manager.GetActive() == &b && manager.GetMainDocument() == &b, "新建的文档既是当前文档也是主视口里的文档");
    Check(manager.ViewportOf(&a) == &mainVp && manager.ViewportOf(&b) == &mainVp && manager.ViewportOf(nullptr) == nullptr,
          "停靠的文档都在主视口里");

    // ── 拖出 ────────────────────────────────────────────────────────────────
    const CameraState bCam = mainVp.GetCamera().GetState();
    Viewport* vpB = manager.DetachDocument(&b, 400, 300);
    Check(vpB != nullptr && vpB != &mainVp && manager.IsDetached(&b), "拖出：文档有了自己的视口");
    Check(vpB && vpB->GetWidth() == 400 && vpB->GetHeight() == 300, "独立视口按给定尺寸创建");
    Check(manager.GetMainDocument() == &a && manager.GetActive() == &b, "它原本在主视口里：主视口换成别的停靠文档，当前文档不变");
    Check(&manager.GetViewport() == vpB && manager.ViewportOf(&b) == vpB && manager.ViewportOf(&a) == &mainVp,
          "GetViewport 是当前文档的视口，ViewportOf 按文档区分");
    Check(manager.GetEditor(b).GetViewport() == vpB && manager.GetEditor(a).GetViewport() == &mainVp, "编辑器绑到各自的视口");
    Check(vpB && vpB->GetCamera().GetState().Zoom == bCam.Zoom && vpB->GetCamera().GetState().Target.x == bCam.Target.x,
          "独立视口的相机从文档拖出前的视图恢复");
    Check(manager.DetachDocument(&b, 10, 10) == nullptr, "已拖出的文档不能再拖出");

    // 相机互不影响
    const double mainZoom = mainVp.GetCamera().GetState().Zoom;
    vpB->Zoom(3.0f, 200.0f, 150.0f);
    Check(mainVp.GetCamera().GetState().Zoom == mainZoom && vpB->GetCamera().GetState().Zoom != bCam.Zoom, "缩放独立视口不动主视口的相机");

    // 当前文档切换：停靠的文档成为当前文档也就进主视口；拖出的文档成为当前文档时主视口不动
    manager.SetActive(&a);
    Check(manager.GetActive() == &a && manager.GetMainDocument() == &a && &manager.GetViewport() == &mainVp, "切到停靠文档：当前文档与主视口都是它");
    manager.SetActive(&b);
    Check(manager.GetActive() == &b && manager.GetMainDocument() == &a, "切到拖出的文档：主视口仍显示原来的停靠文档");

    // ── 轮流渲染：两个视口各自上传自己的场景，被顶掉后重新拼接而不是按空流上传 ──
    ViewState vsA = frame(a);
    Check(renderer.slots[kSlotScene].version == vsA.SceneVersion && renderer.slots[kSlotScene].count == 2, "主视口上传 A 的场景（1 条直线）");
    Check(manager.GetEditor(a).BuildViewState().Scene.empty(), "上传后 A 的拼接流已释放");
    ViewState vsB = frame(b);
    Check(renderer.slots[kSlotScene].version == vsB.SceneVersion && renderer.slots[kSlotScene].count == 4, "独立视口上传 B 的场景（2 条直线），顶掉 A 的");
    Check(renderer.uploads[kSlotGizmo] == 2, "两个视口的 Gizmo 版本号不同，各上传一次");
    vsA = frame(a);
    Check(renderer.slots[kSlotScene].version == vsA.SceneVersion && renderer.slots[kSlotScene].count == 2,
          "再渲染主视口：A 的场景被顶掉了，重新拼接后上传（不是空流）");
    // 场景没变、缓冲还在：不重复上传
    const int uploadsBefore = renderer.uploads[kSlotScene];
    frame(a);
    Check(renderer.uploads[kSlotScene] == uploadsBefore, "缓冲还在时不重复上传");

    // ── 拖回 ────────────────────────────────────────────────────────────────
    const CameraState floatCam = vpB->GetCamera().GetState();
    manager.AttachDocument(&b);
    Check(!manager.IsDetached(&b) && manager.GetMainDocument() == &b && manager.GetActive() == &b, "拖回：它成为主视口里的文档与当前文档");
    Check(manager.GetEditor(b).GetViewport() == &mainVp && &manager.GetViewport() == &mainVp, "编辑器换回主视口");
    Check(mainVp.GetCamera().GetState().Zoom == floatCam.Zoom && b.GetCameraState().Zoom == floatCam.Zoom, "独立视口里的相机回写到文档并恢复到主视口");
    Check(a.GetCameraState().Zoom == mainZoom, "换下去的文档保存了它在主视口里的相机");
    vsB = frame(b);
    Check(renderer.slots[kSlotScene].version == vsB.SceneVersion && renderer.slots[kSlotScene].count == 4, "拖回后在主视口里照常渲染");

    // ── 关闭拖出的文档 ───────────────────────────────────────────────────────
    Check(manager.DetachDocument(&b, 300, 200) != nullptr && manager.GetMainDocument() == &a, "再次拖出");
    manager.Close(&b);
    Check(manager.GetAll().size() == 1 && manager.GetActive() == &a && manager.GetMainDocument() == &a, "关闭拖出的文档：当前文档回到主视口里的文档");
    // 主视口里的文档关掉、只剩拖出的：主视口空着，当前文档是拖出的那个
    Document& c = manager.Create();
    addLine(c, 30.0);
    manager.DetachDocument(&c, 300, 200);
    Check(manager.GetMainDocument() == &a, "新文档拖出后主视口仍是 A");
    manager.Close(&a);
    Check(manager.GetMainDocument() == nullptr && manager.GetActive() == &c && manager.IsDetached(&c), "主视口的文档关掉后主视口空着，当前文档是拖出的那个");
    manager.AttachDocument(&c);
    Check(manager.GetMainDocument() == &c && !manager.IsDetached(&c), "拖回空着的主视口");

    return g_failures;
}
