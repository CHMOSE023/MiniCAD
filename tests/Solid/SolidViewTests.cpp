// 三维视图测试（M6）：相机的视图方向与坐标换算、标准视图与撤销、视图方向的读写、
// 着色面（法线、明暗、随视图 / 样式 / 图层重建）、视口在俯视与三维视图下的深度设置、三维视图里的拾取
#include "Editor/Editor.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Solid/SMLib/SMLibKernel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <numbers>

using namespace MiniCAD;
using Math::Point3;
using Math::Vec3;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    bool Near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
    bool NearVec(const Vec3& a, const Vec3& b, double tol = 1e-9) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }
    bool NearPoint(const Point3& a, const Point3& b, double tol = 1e-6) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }
    bool NearScreen(const Math::Point2& a, const Math::Point2& b, double tol = 1e-6) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol); }

    struct Fixture
    {
        Document doc;
        Viewport viewport;
        Editor   editor;

        explicit Fixture(Solid::ISolidKernel* kernel) : viewport(1000.0f, 800.0f)
        {
            editor.Bind(doc, viewport);
            editor.SetSolidKernel(kernel);
        }

        Scene&  scene()  { return doc.GetScene(); }
        Camera& camera() { return viewport.GetCamera(); }

        Object::ObjectID Add(std::unique_ptr<Entity> e)
        {
            const auto id = e->GetID();
            scene().AddEntity(std::move(e));
            return id;
        }

        void Click(const Math::Point2& p)
        {
            for (auto type : { InputEventType::MouseButtonDown, InputEventType::MouseButtonUp })
            {
                InputEvent e;
                e.Type   = type;
                e.Button = MouseButton::Left;
                e.MouseX = e.LastMouseX = e.PressMouseX = static_cast<int>(std::lround(p.x));
                e.MouseY = e.LastMouseY = e.PressMouseY = static_cast<int>(std::lround(p.y));
                editor.OnInput(e);
            }
        }
    };

    // 记录视口提交的渲染器
    class RecordingRenderer final : public IRenderer
    {
    public:
        struct Call { uint32_t Slot; size_t Count; Math::Mat4 ViewProj; PrimitiveType Type; bool Depth, Blend; };
        std::map<uint32_t, Call> Cached;

        void BeginFrame(IRenderTarget&, const ViewportDesc&) override { Cached.clear(); }
        void EndFrame() override {}
        void Submit(std::span<const Vertex_P3_C4>, const Math::Mat4&, PrimitiveType, bool, bool) override {}
        void SubmitTextured(std::span<const Vertex_P3_C4_UV>, const Math::Mat4&, void*, bool, bool) override {}
        void SubmitCached(uint32_t slot, uint64_t, std::span<const Vertex_P3_C4> verts, const Math::Mat4& vp, PrimitiveType type, bool depth, bool blend) override
        {
            Cached[slot] = { slot, verts.size(), vp, type, depth, blend };
        }
        void* GetNativeDevice() override { return nullptr; }
    };

    class NullTarget final : public IRenderTarget
    {
    public:
        void  Create(int, int) override {}
        void  Resize(int, int) override {}
        void  Release() override {}
        int   GetWidth()  const override { return 1000; }
        int   GetHeight() const override { return 800; }
        void* GetNativeHandle() const override { return nullptr; }
        void* GetNativeShaderResource() const override { return nullptr; }
    };

    constexpr uint32_t kSlotScene  = 1;    // 与 Viewport.cpp 的槽位一致
    constexpr uint32_t kSlotGrid   = 6;
    constexpr uint32_t kSlotAxis   = 7;
    constexpr uint32_t kSlotShaded = 12;

    double ClipZ(const Math::Mat4& vp, const Point3& p) { return vp.TransformPoint(p).z; }

    void TestCameraBasics()
    {
        Camera cam(1000, 800);
        cam.ZoomToBounds(0, 0, 100, 100);
        Check(cam.IsPlanView() && NearVec(cam.GetViewDir(), { 0, 0, -1 }) && NearVec(cam.GetRight(), { 1, 0, 0 }) && NearVec(cam.GetUp(), { 0, 1, 0 }),
              "camera: default is the plan view (looking down, +X right, +Y up)");
        const Point3 w = cam.ScreenToWorld(123.0, 456.0);
        Check(Near(w.z, 0) && NearScreen(cam.WorldToScreen(w), { 123, 456 }, 1e-6), "camera: plan view screen ↔ world round trip on z = 0");

        cam.SetStandardView(StandardView::Front);
        Check(!cam.IsPlanView() && NearVec(cam.GetViewDir(), { 0, 1, 0 }) && NearVec(cam.GetRight(), { 1, 0, 0 }) && NearVec(cam.GetUp(), { 0, 0, 1 }),
              "camera: front view looks along +Y with +Z up");
        cam.SetStandardView(StandardView::Right);
        Check(NearVec(cam.GetViewDir(), { -1, 0, 0 }) && NearVec(cam.GetRight(), { 0, 1, 0 }) && NearVec(cam.GetUp(), { 0, 0, 1 }),
              "camera: right view looks along −X with +Y to the right");
        cam.SetStandardView(StandardView::SWIso);
        const double k = 1 / std::sqrt(3.0);
        Check(NearVec(cam.GetViewDir(), { k, k, -k }, 1e-12) && Near(cam.GetUp().z, std::sqrt(2.0 / 3.0), 1e-12),
              "camera: SW isometric looks along (1, 1, −1)");
        cam.SetStandardView(StandardView::Top);
        Check(cam.IsPlanView(), "camera: the top view is the plan view again");

        cam.SetView(-90, 60);
        cam.Orbit(0, 1000);
        Check(Near(cam.GetElevation(), 90) && cam.IsPlanView(), "camera: orbit clamps elevation at 90° (back to plan)");
        cam.SetView(-90, 90);
        cam.Orbit(100, 0);
        Check(Near(cam.GetAzimuth(), -130) && !cam.IsPlanView(), "camera: dragging right turns the azimuth (twisted plan is not the plan view)");

        cam.SetStandardView(StandardView::NEIso);
        const CameraState s = cam.GetState();
        Camera other(1000, 800);
        other.SetState(s);
        Check(Near(other.GetAzimuth(), 45) && NearVec(other.GetViewDir(), cam.GetViewDir()), "camera: state carries the view direction");
    }

    void TestCameraConversions()
    {
        Camera cam(1000, 800);
        cam.ZoomToBounds(-50, -50, 50, 50);
        cam.SetStandardView(StandardView::SWIso);

        // 屏幕点落在 z = 0 平面上，投影回去是同一个屏幕点
        const Point3 w = cam.ScreenToWorld(300.0, 200.0);
        Check(Near(w.z, 0, 1e-9) && NearScreen(cam.WorldToScreen(w), { 300, 200 }, 1e-6), "iso view: screen point → point on z = 0 → same screen point");
        // 同一视线上的点投影到同一屏幕点
        const Vec3 f = cam.GetViewDir();
        const Point3 w2{ w.x - f.x * 7, w.y - f.y * 7, w.z - f.z * 7 };
        Check(NearScreen(cam.WorldToScreen(w2), { 300, 200 }, 1e-6), "iso view: points along the view direction share a screen point");

        // 平移：世界点在屏幕上跟着移动相同的像素
        const Point3 p{ 10, 20, 30 };
        const auto before = cam.WorldToScreen(p);
        cam.Pan(15, -25);
        const auto after = cam.WorldToScreen(p);
        Check(NearScreen(after, { before.x + 15, before.y - 25 }, 1e-6), "iso view: pan moves the picture by the mouse delta");

        // 缩放：鼠标下的 z = 0 平面上的点不动
        const Point3 under = cam.ScreenToWorld(700.0, 500.0);
        cam.Zoom(3, 700, 500);
        Check(NearScreen(cam.WorldToScreen(under), { 700, 500 }, 1e-6), "iso view: wheel zoom keeps the point under the mouse");

        // 缩放到三维范围：8 个角点都在屏幕内
        const AABB box({ 100, 200, 0 }, { 140, 260, 90 });
        cam.ZoomToBounds(box);
        bool inside = true;
        for (int i = 0; i < 8; ++i)
        {
            const auto sp = cam.WorldToScreen({ (i & 1) ? box.Max.x : box.Min.x, (i & 2) ? box.Max.y : box.Min.y, (i & 4) ? box.Max.z : box.Min.z });
            inside = inside && sp.x >= 0 && sp.x <= 1000 && sp.y >= 0 && sp.y <= 800;
        }
        Check(inside, "iso view: zoom to a 3D box keeps all its corners on screen");

        // 前视：视线与 XY 平面平行，屏幕点取在过目标点、垂直于视线的平面上
        cam.SetStandardView(StandardView::Front);
        const Point3 fw = cam.ScreenToWorld(250.0, 600.0);
        Check(Near(fw.y, cam.GetTarget().y, 1e-9) && NearScreen(cam.WorldToScreen(fw), { 250, 600 }, 1e-6),
              "front view: screen points land on the plane through the target");
    }

    void TestStandardViewCommands(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 20, 30)));
        f.editor.RunCommand("ViewSWIso");
        Check(!f.camera().IsPlanView() && Near(f.camera().GetAzimuth(), -135), "ViewSWIso: switches to the SW isometric view");
        const auto top = f.camera().WorldToScreen({ 10, 20, 30 }), bottom = f.camera().WorldToScreen({ 0, 0, 0 });
        Check(top.x >= 0 && top.x <= 1000 && top.y >= 0 && bottom.y <= 800, "ViewSWIso: zooms to show the whole drawing");
        f.doc.GetCommandStack().Undo(f.scene());
        Check(f.camera().IsPlanView(), "ViewSWIso: undo returns to the previous (plan) view");
        f.editor.RunCommand("PLAN");
        Check(f.camera().IsPlanView(), "PLAN: plan view");
        Check(f.editor.ResolveCommandAlias("SHADE") == "Shade" && f.editor.ResolveCommandAlias("WIREFRAME") == "Wireframe",
              "aliases SHADE / WIREFRAME");
    }

    void TestDocumentViewRoundTrip()
    {
        const auto dir = std::filesystem::temp_directory_path() / ("minicad-view-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        for (const char* ext : { ".mcad", ".json" })
        {
            const std::string path = (dir / (std::string("view") + ext)).string();
            Document a;
            CameraState s;
            s.Target    = { 1, 2, 3 };
            s.Zoom      = 42;
            s.Azimuth   = -45;
            s.Elevation = 20;
            a.SetCameraState(s);
            const bool saved = a.SaveAs(path);
            Document b;
            const bool loaded = saved && b.LoadFromFile(path);
            const CameraState r = b.GetCameraState();
            Check(loaded && Near(r.Azimuth, -45) && Near(r.Elevation, 20) && Near(r.Zoom, 42) && NearPoint(r.Target, { 1, 2, 3 }),
                  (std::string("document ") + ext + ": the view direction is saved and loaded").c_str());
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void TestCornerNormals(Solid::ISolidKernel& k)
    {
        auto box = Solid3DEntity::Create(1, k, k.Box({ 0, 0, 0 }, 1, 2, 3));
        const auto& mesh = box->GetLocalMesh();
        const auto& n    = box->GetLocalCornerNormals();
        bool sharp = n.size() == mesh.Indices.size() && !n.empty();
        for (size_t t = 0; sharp && t < mesh.TriangleCount(); ++t)
        {
            const Vec3& a = n[t * 3];
            sharp = NearVec(a, n[t * 3 + 1], 1e-9) && NearVec(a, n[t * 3 + 2], 1e-9)
                 && Near(std::abs(a.x) + std::abs(a.y) + std::abs(a.z), 1.0, 1e-9);   // 沿坐标轴
        }
        Check(sharp, "corner normals: a box keeps flat, axis-aligned normals per face (sharp edges)");

        auto cyl = Solid3DEntity::Create(2, k, k.Cylinder({ 0, 0, 0 }, 2, 5));
        const auto& cm = cyl->GetLocalMesh();
        const auto& cn = cyl->GetLocalCornerNormals();
        bool smoothSide = false, capsFlat = true;
        for (size_t t = 0; t < cm.TriangleCount(); ++t)
        {
            bool side = true;
            for (int i = 0; i < 3; ++i)
            {
                const auto& p = cm.Positions[cm.Indices[t * 3 + i]];
                side = side && Near(std::hypot(p.x, p.y), 2, 1e-6);
            }
            const Vec3 &a = cn[t * 3], &b = cn[t * 3 + 1], &c = cn[t * 3 + 2];
            if (side && !(NearVec(a, b, 1e-6) && NearVec(b, c, 1e-6)) && Near(a.z, 0, 1e-6))
                smoothSide = true;
            if (!side && (std::abs(a.z) < 1 - 1e-9 || std::abs(b.z) < 1 - 1e-9 || std::abs(c.z) < 1 - 1e-9))
                capsFlat = false;
        }
        Check(smoothSide, "corner normals: the cylinder wall is smooth (normals differ across a triangle, all horizontal)");
        Check(capsFlat, "corner normals: the cylinder caps stay flat (±Z)");
    }

    void TestShading(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.camera().ZoomToBounds(-20, -20, 20, 20);
        auto box = Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10));
        const size_t corners = box->GetLocalMesh().Indices.size();
        const auto id = f.Add(std::move(box));
        f.editor.Render();
        Check(f.editor.GetVisualStyle() == Editor::VisualStyle::Shaded && f.editor.GetShadedVertices().size() == corners,
              "shading: shaded by default, one vertex per triangle corner");
        bool notWhite = true;
        for (const auto& v : f.editor.GetShadedVertices())
            notWhite = notWhite && !(v.color.r == 255 && v.color.g == 255 && v.color.b == 255);
        Check(notWhite, "shading: no vertex is pure white (would turn black on a light background)");

        // 俯视时顶面最亮；西南等轴测时三个可见面亮度各不相同
        auto brightness = [&](auto pick) {
            int best = -1;
            const auto& vs = f.editor.GetShadedVertices();
            for (size_t i = 0; i + 2 < vs.size(); i += 3)
                if (pick(vs[i].pos, vs[i + 1].pos, vs[i + 2].pos))
                    best = std::max(best, int(vs[i].color.r));
            return best;
        };
        auto onPlane = [](int axis, float value) {
            return [=](const Math::Float3& a, const Math::Float3& b, const Math::Float3& c) {
                auto get = [axis](const Math::Float3& p) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
                return get(a) == value && get(b) == value && get(c) == value;
            };
        };
        const int topPlan = brightness(onPlane(2, 10)), sidePlan = brightness(onPlane(0, 10));
        Check(topPlan > sidePlan, "shading: in the plan view the top face is the brightest");
        {
            const auto& vs = f.editor.GetShadedVertices();
            const size_t n = vs.size();
            Check(n >= 3 && vs[n - 1].pos.z == 10 && vs[n - 2].pos.z == 10 && vs[n - 3].pos.z == 10 && vs[0].pos.z < 10,
                  "shading: triangles sorted far to near (plan view draws without depth, the top face comes last)");
        }

        const uint64_t before = f.editor.BuildViewState().ShadedVersion;
        f.camera().Pan(10, 10);
        f.editor.Render();
        Check(f.editor.BuildViewState().ShadedVersion == before, "shading: panning does not rebuild the shaded faces");
        f.editor.RunCommand("ViewSWIso");
        f.editor.Render();
        Check(f.editor.BuildViewState().ShadedVersion != before, "shading: changing the view direction rebuilds them");
        const int top = brightness(onPlane(2, 10)), front = brightness(onPlane(1, 0)), left = brightness(onPlane(0, 0));
        Check(top > 0 && front > 0 && left > 0 && top != front && front != left, "shading: the three visible faces of the iso view get different shades");

        f.editor.RunCommand("WIREFRAME");
        f.editor.Render();
        Check(f.editor.GetShadedVertices().empty() && f.editor.GetVisualStyle() == Editor::VisualStyle::Wireframe, "WIREFRAME: no shaded faces");
        f.editor.RunCommand("SHADE");
        f.editor.Render();
        Check(f.editor.GetShadedVertices().size() == corners, "SHADE: shaded faces come back");

        // 关闭图层后不画；移动后跟着走
        auto* solid = static_cast<Solid3DEntity*>(f.scene().GetEntity(id));
        solid->ApplyTransform(Solid3DEntity::MoveMatrix({ 100, 0, 0 }));
        f.scene().MarkEntityDirty(id);
        f.editor.Render();
        float minX = 1e30f;
        for (const auto& v : f.editor.GetShadedVertices())
            minX = std::min(minX, v.pos.x);
        Check(Near(minX, 100, 1e-4), "shading: faces follow the solid's transform");
        f.scene().GetLayerManager().GetLayer(solid->GetAttr().LayerId)->SetVisible(false);
        f.scene().MarkDisplayDirty();
        f.editor.Render();
        Check(f.editor.GetShadedVertices().empty(), "shading: solids on a hidden layer are not shaded");
    }

    void TestSilhouettes(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 20, 0, 0 }, 5, 5, 5)));
        f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Cylinder({ 0, 0, 0 }, 2, 6)));
        f.editor.RunCommand("ViewFront");
        f.editor.Render();
        // 前视：圆柱的外轮廓是 x = ±2 的两条竖线；长方体没有曲面，没有轮廓线
        const auto& sil = f.editor.GetSilhouetteVertices();
        double left = 0, right = 0;
        bool onCylinder = !sil.empty();
        for (size_t i = 0; i + 1 < sil.size(); i += 2)
        {
            const auto& a = sil[i].pos;
            const auto& b = sil[i + 1].pos;
            onCylinder = onCylinder && a.x < 10 && b.x < 10 && Near(std::hypot(a.x, a.y), 2, 1e-4);
            const double len = std::abs(b.z - a.z);
            (a.x < 0 ? left : right) += len;
        }
        Check(onCylinder && Near(left, 6, 1e-3) && Near(right, 6, 1e-3), "silhouettes: front view of a cylinder gives its two side outlines; a box gives none");
        const uint64_t v = f.editor.BuildViewState().ShadedVersion;
        f.editor.RunCommand("WIREFRAME");
        f.editor.Render();
        Check(!f.editor.GetSilhouetteVertices().empty() && f.editor.BuildViewState().ShadedVersion != v, "silhouettes: also drawn in the wireframe style");
        f.editor.RunCommand("ViewTop");
        f.editor.Render();
        double maxLen = 0;
        const auto& top = f.editor.GetSilhouetteVertices();
        for (size_t i = 0; i + 1 < top.size(); i += 2)
            maxLen = std::max(maxLen, std::abs(double(top[i + 1].pos.z) - top[i].pos.z));
        Check(maxLen < 1e-6, "silhouettes: none along the cylinder wall when looking straight down");
    }

    void TestViewportDepth(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10)));
        f.camera().ZoomToBounds(-20, -20, 20, 20);
        RecordingRenderer r;
        NullTarget target;

        f.editor.Render();
        f.viewport.Render(r, target, f.editor.BuildViewState());
        const auto& shadedPlan = r.Cached[kSlotShaded];
        Check(shadedPlan.Count > 0 && !shadedPlan.Depth && shadedPlan.Type == PrimitiveType::Triangle,
              "viewport, plan view: shaded faces drawn by order only (no depth)");
        Check(Near(ClipZ(r.Cached[kSlotScene].ViewProj, { 3, 4, 10 }), 0.5) && Near(ClipZ(r.Cached[kSlotScene].ViewProj, { 3, 4, -7 }), 0.5),
              "viewport, plan view: depth is flattened to 0.5 as before");

        f.editor.RunCommand("ViewSWIso");
        f.editor.Render();
        f.viewport.Render(r, target, f.editor.BuildViewState());
        const auto shaded = r.Cached[kSlotShaded];
        const auto lines  = r.Cached[kSlotScene];
        Check(shaded.Depth && !shaded.Blend, "viewport, 3D view: shaded faces write depth");
        Check(lines.Depth && lines.Blend, "viewport, 3D view: lines are depth-tested (read only)");
        // 西南等轴测的眼睛在 (−1, −1, 1) 方向：角点 (0, 0, 10) 最近，(10, 10, 0) 最远
        const double zEye = ClipZ(shaded.ViewProj, { 0, 0, 10 }), zFar = ClipZ(shaded.ViewProj, { 10, 10, 0 });
        Check(zEye > 0 && zEye < 1 && zFar > 0 && zFar < 1 && zEye < zFar, "viewport, 3D view: depth in (0, 1), the corner facing the eye is nearer");
        Check(ClipZ(lines.ViewProj, { 5, 5, 5 }) < ClipZ(shaded.ViewProj, { 5, 5, 5 }), "viewport, 3D view: lines are pulled slightly toward the eye");

        // 网格与坐标轴：俯视时按屏幕坐标，三维视图里画在 z = 0 平面上（世界坐标、按深度测试）
        f.viewport.ShowGrid(true);
        f.viewport.ShowAxis(true);
        f.viewport.Render(r, target, f.editor.BuildViewState());
        const auto grid3D = r.Cached[kSlotGrid], axis3D = r.Cached[kSlotAxis];
        Check(grid3D.Count > 0 && axis3D.Count == 6 && grid3D.Depth && Near(grid3D.ViewProj.m[0], lines.ViewProj.m[0], 1e-12),
              "viewport, 3D view: grid and axes (X / Y / Z) are drawn on the z = 0 plane with the scene matrix");
        f.editor.RunCommand("PLAN");
        f.editor.Render();
        f.viewport.Render(r, target, f.editor.BuildViewState());
        Check(r.Cached[kSlotGrid].Count > 0 && Near(r.Cached[kSlotGrid].ViewProj.m[0], 2.0 / 1000.0, 1e-9),
              "viewport, plan view: the grid stays in screen coordinates");
    }

    void TestPickingIn3D(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        const auto id = f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10)));
        f.editor.RunCommand("ViewSWIso");
        f.editor.Render();
        // 竖直边 (10, 0, 0)–(10, 0, 10) 的中点：俯视时投影成一点，三维视图里是一条可点选的线
        f.Click(f.camera().WorldToScreen({ 10, 0, 5 }));
        Check(f.editor.GetSelection().count(id) == 1, "3D view: clicking a vertical edge selects the solid");
        f.editor.SetSelection({});
        const auto far = f.camera().WorldToScreen({ 5, 5, 40 });
        f.Click(far);
        Check(f.editor.GetSelection().empty(), "3D view: clicking empty space above the solid selects nothing");

        // 窗选（从左往右）：框住整个长方体的投影
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (int i = 0; i < 8; ++i)
        {
            const auto sp = f.camera().WorldToScreen({ (i & 1) ? 10.0 : 0.0, (i & 2) ? 10.0 : 0.0, (i & 4) ? 10.0 : 0.0 });
            x0 = std::min(x0, sp.x); y0 = std::min(y0, sp.y); x1 = std::max(x1, sp.x); y1 = std::max(y1, sp.y);
        }
        auto mouse = [&](InputEventType type, double x, double y, uint8_t buttons) {
            InputEvent e;
            e.Type = type;
            e.Button = MouseButton::Left;
            e.MouseButtons = buttons;
            e.MouseX = e.LastMouseX = static_cast<int>(x);
            e.MouseY = e.LastMouseY = static_cast<int>(y);
            e.PressMouseX = static_cast<int>(x0 - 5);
            e.PressMouseY = static_cast<int>(y0 - 5);
            f.editor.OnInput(e);
        };
        const uint8_t left = static_cast<uint8_t>(MouseButtonState::Left);
        mouse(InputEventType::MouseButtonDown, x0 - 5, y0 - 5, left);
        mouse(InputEventType::MouseMove, (x0 + x1) / 2, (y0 + y1) / 2, left);
        mouse(InputEventType::MouseMove, x1 + 5, y1 + 5, left);
        mouse(InputEventType::MouseButtonUp, x1 + 5, y1 + 5, 0);
        Check(f.editor.GetSelection().count(id) == 1, "3D view: a window around the projected solid selects it");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto kernel = Solid::CreateSMLibKernel();
    TestCameraBasics();
    TestCameraConversions();
    TestStandardViewCommands(*kernel);
    TestDocumentViewRoundTrip();
    TestCornerNormals(*kernel);
    TestShading(*kernel);
    TestSilhouettes(*kernel);
    TestViewportDepth(*kernel);
    TestPickingIn3D(*kernel);
    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
