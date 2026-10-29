// ── 渲染原点测试 ───────────────────────────────────────────────────────────
// 测绘坐标的图纸（x≈53 万、y≈368 万）：顶点直接转 float 只剩 0.25 的分辨率，视图投影矩阵里几百万的平移转成 float
// 后再与顶点相减又丢一截，放大后线画偏几十个像素、画线的橡皮筋一格一格地跳；夹点与捕捉标记按双精度换算到屏幕，
// 于是看起来"不在线上"。顶点改为相对渲染原点（ToRenderFloat3），原点在双精度里合进矩阵（Viewport::Render）后，
// 按 GPU 的 float 运算换算到屏幕，与双精度的位置（Camera::WorldToScreen）应相差远小于 1 像素。
#include "Document/DocumentManager.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace MiniCAD;

namespace
{
    // Viewport.cpp 里的槽位号
    constexpr uint32_t kSlotScene = 1;
    constexpr uint32_t kSlotSel   = 4;

    class NullTarget final : public IRenderTarget
    {
        void  Create(int, int) override {}
        void  Resize(int, int) override {}
        void  Release() override {}
        int   GetWidth()  const override { return 1200; }
        int   GetHeight() const override { return 800; }
        void* GetNativeHandle() const override { return nullptr; }
        void* GetNativeShaderResource() const override { return nullptr; }
    };

    // 记下每次提交的顶点与矩阵（缓存槽位按槽位号，非缓存提交按顺序）
    class CaptureRenderer final : public IRenderer
    {
    public:
        struct Batch { std::vector<Vertex_P3_C4> verts; Math::Mat4 vp; };
        std::map<uint32_t, Batch> slots;
        std::vector<Batch>        submits;

        void BeginFrame(IRenderTarget&, const ViewportDesc&) override { submits.clear(); }
        void EndFrame() override {}
        void Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& vp, PrimitiveType, bool, bool) override
        {
            submits.push_back({ { verts.begin(), verts.end() }, vp });
        }
        void SubmitTextured(std::span<const Vertex_P3_C4_UV>, const Math::Mat4&, void*, bool, bool) override {}
        void* GetNativeDevice() override { return nullptr; }
        void SubmitCached(uint32_t slot, uint64_t, std::span<const Vertex_P3_C4> verts, const Math::Mat4& vp, PrimitiveType, bool, bool) override
        {
            slots[slot] = { { verts.begin(), verts.end() }, vp };
        }
        bool IsCached(uint32_t, uint64_t, bool) const override { return false; }   // 拼接流不释放，每帧都交过来
    };

    // 与顶点着色器相同的运算：矩阵与顶点都是 float（行向量 P·M），再按视口换成屏幕像素
    Math::Point2 GpuToScreen(const Math::Float3& p, const Math::Mat4& vp, double w, double h)
    {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = static_cast<float>(vp.m[i]);
        const float cx = p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12];
        const float cy = p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13];
        const float cw = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
        return { (cx / cw * 0.5 + 0.5) * w, (-cy / cw * 0.5 + 0.5) * h };
    }

    double Dist(const Math::Point2& a, const Math::Point2& b) { return std::hypot(a.x - b.x, a.y - b.y); }

    // 每个顶点到最近的期望位置（像素）的距离，取最大
    double MaxError(const std::vector<Vertex_P3_C4>& verts, const Math::Mat4& vp, const Camera& cam,
                    const std::vector<Math::Point3>& expected)
    {
        double worst = 0.0;
        for (const auto& v : verts)
        {
            const Math::Point2 s = GpuToScreen(v.pos, vp, cam.GetWidth(), cam.GetHeight());
            double best = 1e300;
            for (const auto& e : expected)
                best = std::min(best, Dist(s, cam.WorldToScreen(e)));
            worst = std::max(worst, best);
        }
        return verts.empty() ? 1e300 : worst;
    }

    // float 本身的舍入：顶点离原点至多约 2000（原点取 4096 的整数倍），那里 float 的间隔约 1.2e-4，
    // 这个缩放下约 0.05 像素；改之前偏几十像素
    constexpr double kTolPx = 0.2;

    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }
}

int RunRenderOriginTests()
{
    g_failures = 0;

    CaptureRenderer renderer;
    NullTarget      target;
    DocumentManager manager;
    manager.InitViewport(renderer, 1200, 800);
    Viewport& viewport = manager.GetViewport();
    Camera&   cam      = viewport.GetCamera();

    auto frameOf = [&](Editor& ed) -> ViewState
    {
        ed.Render();
        const ViewState vs = ed.BuildViewState();
        viewport.Render(renderer, target, vs);
        return vs;
    };

    // 地形测试.dwg 里一条等高线的三个顶点（小数部分各不相同）
    const Math::Point3 a{ 538845.301021282, 3686327.671080875, 0 };
    const Math::Point3 b{ 538846.509608437, 3686329.080180231, 0 };
    const Math::Point3 c{ 538842.059569058, 3686324.380563197, 0 };
    const std::vector<Math::Point3> ends{ a, b, c };

    Document& doc    = manager.Create();
    Editor&   editor = manager.GetEditor();
    std::vector<Object::ObjectID> ids;
    auto addLine = [&](Document& d, const Math::Point3& p, const Math::Point3& q)
    {
        ids.push_back(d.GetScene().NextObjectID());
        d.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<LineEntity>(ids.back(), p, q)), d.GetScene());
    };
    addLine(doc, a, b);
    addLine(doc, a, c);

    // 放大到约 2 个单位宽（1 像素约 0.002）：float 的世界坐标在这里差几十个像素
    cam.ZoomToBounds(a.x - 1.0, a.y - 1.0, a.x + 1.0, a.y + 1.0, 1.0);
    ViewState vs = frameOf(editor);
    const Math::Point3 o = vs.RenderOrigin;
    Check(std::fmod(o.x, Editor::kRenderOriginGrid) == 0.0 && std::fmod(o.y, Editor::kRenderOriginGrid) == 0.0 &&
          std::abs(o.x - a.x) <= Editor::kRenderOriginGrid / 2 && std::abs(o.y - a.y) <= Editor::kRenderOriginGrid / 2,
          "渲染原点取在图形附近（4096 的整数倍）");

    const CaptureRenderer::Batch& sceneBatch = renderer.slots[kSlotScene];
    const double sceneErr = MaxError(sceneBatch.verts, sceneBatch.vp, cam, ends);
    std::printf("  场景线顶点经 float 换算到屏幕，与双精度位置最多差 %.4f 像素\n", sceneErr);
    Check(sceneBatch.verts.size() == 4 && sceneErr < kTolPx, "场景线：按 GPU 的 float 运算画在双精度的位置上（< 0.2 像素）");

    // 对照：顶点与矩阵都按世界坐标转 float（改之前的做法）在这里偏几十个像素，说明上面的场景确实落在 float 不够用的范围
    {
        Math::Mat4 world = cam.GetViewProj();
        world.m[2] = world.m[6] = world.m[10] = 0.0;
        world.m[14] = 0.5;
        double worst = 0.0;
        for (const auto& p : ends)
        {
            const Math::Float3 f{ static_cast<float>(p.x), static_cast<float>(p.y), 0.0f };
            worst = std::max(worst, Dist(GpuToScreen(f, world, cam.GetWidth(), cam.GetHeight()), cam.WorldToScreen(p)));
        }
        std::printf("  对照：世界坐标直接转 float 最多差 %.1f 像素\n", worst);
        Check(worst > 5.0, "对照：世界坐标直接转 float 在这个缩放下偏出 5 像素以上");
    }

    // 选中：选中流与夹点。夹点按双精度换算到屏幕（ViewState::Grips），应落在画出来的线的端点或中点上
    editor.SetSelection({ ids[0], ids[1] });
    vs = frameOf(editor);
    const CaptureRenderer::Batch& selBatch = renderer.slots[kSlotSel];
    Check(MaxError(selBatch.verts, selBatch.vp, cam, ends) < kTolPx, "选中流：画在双精度的位置上");
    {
        // 画出来的每条线段（两个顶点一段）的端点与中点
        std::vector<Math::Point2> drawn;
        for (size_t i = 0; i + 1 < sceneBatch.verts.size(); i += 2)
        {
            const Math::Point2 p = GpuToScreen(sceneBatch.verts[i].pos, sceneBatch.vp, cam.GetWidth(), cam.GetHeight());
            const Math::Point2 q = GpuToScreen(sceneBatch.verts[i + 1].pos, sceneBatch.vp, cam.GetWidth(), cam.GetHeight());
            drawn.insert(drawn.end(), { p, q, { (p.x + q.x) * 0.5, (p.y + q.y) * 0.5 } });
        }
        double worst = 0.0;
        for (const GripDraw& g : vs.Grips)
        {
            double best = 1e300;
            for (const auto& d : drawn)
                best = std::min(best, Dist(g.Pos, d));
            worst = std::max(worst, best);
        }
        std::printf("  夹点 %zu 个，与画出来的线端点 / 中点最多差 %.4f 像素\n", vs.Grips.size(), worst);
        Check(vs.Grips.size() == 6 && worst < kTolPx, "夹点落在画出来的线的端点、中点上（< 0.2 像素）");
    }
    editor.SetSelection({});

    // 画线：起点键入 a，鼠标逐像素右移，橡皮筋的起点钉在 a 上、终点跟着鼠标走，不再一格一格地跳
    editor.SetSnapEnabled(false);
    editor.SetOrthoEnabled(false);
    editor.SetPolarEnabled(false);
    editor.StartLineTool();
    editor.SubmitPoint(a);
    const Math::Point2 sa = cam.WorldToScreen(a);
    double startErr = 0.0, endErr = 0.0;
    int    frames = 0;
    for (int dx = 40; dx < 60; ++dx)
    {
        InputEvent move;
        move.Type   = InputEventType::MouseMove;
        move.MouseX = static_cast<int>(std::lround(sa.x)) + dx;
        move.MouseY = static_cast<int>(std::lround(sa.y)) - 30;
        editor.OnInput(move);
        vs = frameOf(editor);
        // 橡皮筋在 Overlay 那次提交里（顶点数与 vs.Overlay 相同、矩阵是世界的）
        const CaptureRenderer::Batch* band = nullptr;
        for (const auto& s : renderer.submits)
            if (s.verts.size() == vs.Overlay.size() && s.verts.size() >= 2 &&
                std::equal(s.verts.begin(), s.verts.end(), vs.Overlay.begin(),
                           [](const Vertex_P3_C4& p, const Vertex_P3_C4& q) { return p.pos.x == q.pos.x && p.pos.y == q.pos.y; }))
                band = &s;
        if (!band) continue;
        ++frames;
        const Math::Point2 s0 = GpuToScreen(band->verts[0].pos, band->vp, cam.GetWidth(), cam.GetHeight());
        const Math::Point2 s1 = GpuToScreen(band->verts[1].pos, band->vp, cam.GetWidth(), cam.GetHeight());
        startErr = std::max(startErr, Dist(s0, sa));
        endErr   = std::max(endErr, Dist(s1, { static_cast<double>(move.MouseX), static_cast<double>(move.MouseY) }));
    }
    std::printf("  橡皮筋 %d 帧：起点最多差 %.4f 像素，终点与鼠标最多差 %.4f 像素\n", frames, startErr, endErr);
    Check(frames == 20 && startErr < kTolPx && endErr < kTolPx, "画线的橡皮筋：起点钉在键入的点上，终点逐像素跟着鼠标");

    // 空图纸：原点是 0；画到测绘坐标上之后换原点（整图重新细分），新画的线同样画准
    Document& doc2    = manager.Create();
    Editor&   editor2 = manager.GetEditor();
    addLine(doc2, { 0, 0, 0 }, { 100, 50, 0 });
    cam.ZoomToBounds(-10, -10, 110, 60, 1.0);
    vs = frameOf(editor2);
    Check(vs.RenderOrigin.x == 0.0 && vs.RenderOrigin.y == 0.0 && vs.RenderOrigin.z == 0.0, "原点附近的图纸：渲染原点是 0（顶点就是世界坐标）");
    addLine(doc2, a, b);
    cam.ZoomToBounds(a.x - 1.0, a.y - 1.0, a.x + 1.0, a.y + 1.0, 1.0);
    vs = frameOf(editor2);
    Check(std::abs(vs.RenderOrigin.x - a.x) <= Editor::kRenderOriginGrid && std::abs(vs.RenderOrigin.y - a.y) <= Editor::kRenderOriginGrid,
          "空图纸里画到测绘坐标上：换到新图形附近的原点");
    {
        std::vector<Vertex_P3_C4> near;
        for (const auto& v : renderer.slots[kSlotScene].verts)
            if (Dist(GpuToScreen(v.pos, renderer.slots[kSlotScene].vp, cam.GetWidth(), cam.GetHeight()), cam.WorldToScreen(a)) < 100.0 ||
                Dist(GpuToScreen(v.pos, renderer.slots[kSlotScene].vp, cam.GetWidth(), cam.GetHeight()), cam.WorldToScreen(b)) < 100.0)
                near.push_back(v);
        Check(near.size() == 2 && MaxError(near, renderer.slots[kSlotScene].vp, cam, { a, b }) < kTolPx, "换原点后新画的线画在双精度的位置上");
    }

    std::printf("渲染原点测试：%d 项失败\n", g_failures);
    return g_failures;
}
