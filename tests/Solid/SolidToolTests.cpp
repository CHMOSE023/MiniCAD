// 三维建模命令测试（M3、M5）：经 Editor 的公开入口驱动，等同于命令行 / 点取操作：
//   ActivateToolById 启动命令，SubmitPoint 点取，SubmitCoordinateText 键入数值，SetSelection 预选
// 覆盖长方体、圆柱体、球体、拉伸、实体并 / 差 / 交、撤销，没有内核时的提示，以及面域布尔不受影响；
// M5：旋转、扫掠（直线 / 折线 / 圆弧 / 圆路径）、圆角边与倒角边（选边、重叠边的选择与 Tab 切换、撤销、失败）
#include "Editor/Editor.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Solid/SMLib/SMLibKernel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace MiniCAD;
using Math::Point3;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = std::numbers::pi;
    bool Near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol; }
    bool NearPoint(const Point3& a, const Point3& b, double tol = 1e-4) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

    // 一个文档 + 视口 + 编辑器，与桌面版的绑定方式相同
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

        Scene& scene() { return doc.GetScene(); }

        template<class T>
        std::vector<const T*> All()
        {
            std::vector<const T*> out;
            scene().ForEachObject([&](const Object& o) { if (o.IsKindOf<T>()) out.push_back(static_cast<const T*>(&o)); });
            return out;
        }

        Object::ObjectID Add(std::unique_ptr<Entity> e)
        {
            const auto id = e->GetID();
            scene().AddEntity(std::move(e));
            return id;
        }

        bool LastMessageHas(const char* text) const
        {
            const auto& lines = editor.GetCmdLine().Lines();
            for (size_t i = 0; i < 3 && i < lines.size(); ++i)
                if (lines[lines.size() - 1 - i].find(text) != std::string::npos)
                    return true;
            return false;
        }

        void Key(KeyCode key)
        {
            InputEvent e;
            e.Type = InputEventType::KeyDown;
            e.Key  = key;
            editor.OnInput(e);
        }

        void Move(const Point3& p)
        {
            const auto sp = viewport.GetCamera().WorldToScreen(p);
            InputEvent e;
            e.Type   = InputEventType::MouseMove;
            e.MouseX = e.LastMouseX = static_cast<int>(std::lround(sp.x));
            e.MouseY = e.LastMouseY = static_cast<int>(std::lround(sp.y));
            editor.OnInput(e);
        }

        double SolidVolume(Solid::ISolidKernel& k, const Solid3DEntity* s) { auto b = s->GetWorldBody(k); return b ? k.Volume(b) : -1.0; }
    };

    void TestBox(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.editor.ActivateToolById("Box");
        Check(f.editor.IsActiveTool(), "BOX starts");
        f.editor.SubmitPoint({ 0, 0, 0 });
        f.editor.SubmitPoint({ 10, 20, 0 });
        f.editor.SubmitCoordinateText("30");
        auto solids = f.All<Solid3DEntity>();
        Check(solids.size() == 1, "BOX: two corners + typed height create one solid");
        if (solids.size() != 1) return;
        const AABB bb = solids[0]->GetBoundingBox();
        Check(NearPoint(bb.Min, { 0, 0, 0 }) && NearPoint(bb.Max, { 10, 20, 30 }), "BOX: bounds 10 × 20 × 30");
        Check(Near(f.SolidVolume(k, solids[0]), 6000), "BOX: volume 6000");
        Check(solids[0]->GetAttr().LayerId == f.scene().GetLayerManager().GetActiveLayerID(), "BOX: on the current layer");

        // 工具继续：再画一个，高度为负（向下）
        f.editor.SubmitPoint({ 100, 0, 0 });
        f.editor.SubmitPoint({ 90, 5, 0 });
        f.editor.SubmitCoordinateText("-4");
        solids = f.All<Solid3DEntity>();
        const Solid3DEntity* down = solids.size() == 2 ? (solids[0]->GetBoundingBox().Min.x > 50 ? solids[0] : solids[1]) : nullptr;
        Check(down && NearPoint(down->GetBoundingBox().Min, { 90, 0, -4 }) && NearPoint(down->GetBoundingBox().Max, { 100, 5, 0 }),
              "BOX: corners in any order, negative height goes down");

        f.doc.GetCommandStack().Undo(f.scene());
        Check(f.All<Solid3DEntity>().size() == 1, "BOX: undo removes the last box");
    }

    void TestCylinderAndSphere(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        f.editor.ActivateToolById("Cylinder");
        f.editor.SubmitPoint({ 50, 50, 0 });
        f.editor.SubmitCoordinateText("5");     // 半径
        f.editor.SubmitCoordinateText("10");    // 高度
        auto solids = f.All<Solid3DEntity>();
        Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 250 * kPi, 1e-3), "CYLINDER: typed radius 5 and height 10, volume 250π");
        Check(solids.size() == 1 && NearPoint(solids[0]->GetBoundingBox().Min, { 45, 45, 0 }, 1e-3), "CYLINDER: base centered on the picked point");

        f.editor.ActivateToolById("Sphere");
        f.editor.SubmitPoint({ 0, 0, 0 });
        f.editor.SubmitPoint({ 3, 0, 0 });      // 点取半径
        solids = f.All<Solid3DEntity>();
        const Solid3DEntity* sphere = nullptr;
        for (auto* s : solids)
            if (s->GetBoundingBox().Max.x < 10)
                sphere = s;
        Check(solids.size() == 2 && sphere && Near(f.SolidVolume(k, sphere), 36 * kPi, 1e-3), "SPHERE: picked radius 3, volume 36π");
        // 球的线框只有接缝与赤道，包围盒要用内核的精确值才完整
        Check(sphere && NearPoint(sphere->GetBoundingBox().Min, { -3, -3, -3 }, 1e-3) && NearPoint(sphere->GetBoundingBox().Max, { 3, 3, 3 }, 1e-3),
              "SPHERE: bounding box covers the whole sphere");
    }

    void TestExtrude(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        // 闭合多段线（跑道形：两段直线 + 两个向外凸的半圆；MiniCAD 的凸度正值为顺时针，这里逆时针绕行、向外凸为负）、
        // 圆、一条不闭合的直线
        Polyline slot({ { 0, 0, 0 }, { 6, 0, 0 }, { 6, 2, 0 }, { 0, 2, 0 }, { 0, 0, 0 } }, { 0.0, -1.0, 0.0, -1.0 });
        const auto pl   = f.Add(std::make_unique<PolylineEntity>(f.scene().NextObjectID(), slot));
        const auto c    = f.Add(std::make_unique<CircleEntity>(f.scene().NextObjectID(), Point3{ 20, 0, 0 }, 1.0));
        const auto line = f.Add(std::make_unique<LineEntity>(f.scene().NextObjectID(), Point3{ 0, 10, 0 }, Point3{ 5, 10, 0 }));

        f.editor.ActivateToolById("Extrude");
        Check(!f.editor.IsActiveTool() && f.LastMessageHas("需要先选择"), "EXTRUDE without a selection asks for one");

        f.editor.SetSelection({ pl, c, line });
        f.editor.ActivateToolById("Extrude");
        Check(f.editor.IsActiveTool(), "EXTRUDE starts with a selection");
        f.editor.SubmitCoordinateText("3");

        auto solids = f.All<Solid3DEntity>();
        double total = 0;
        for (auto* s : solids) total += f.SolidVolume(k, s);
        Check(solids.size() == 2 && Near(total, (12 + kPi) * 3 + kPi * 3, 1e-3), "EXTRUDE: slot polyline and circle become solids");
        bool outward = false;   // 半圆向外凸：x 从 −1 到 7（凸度方向弄反则是向内凹，x 从 0 到 6）
        for (auto* s : solids)
            if (const AABB b = s->GetBoundingBox(); Near(b.Min.x, -1, 1e-3) && Near(b.Max.x, 7, 1e-3) && Near(b.Min.y, 0, 1e-3) && Near(b.Max.y, 2, 1e-3))
                outward = true;
        Check(outward, "EXTRUDE: arc segments bulge the same way as the drawn polyline");
        Check(!f.scene().GetEntity(pl) && !f.scene().GetEntity(c) && f.scene().GetEntity(line), "EXTRUDE: profiles replaced, the open line kept");
        Check(f.LastMessageHas("1 个对象不是闭合"), "EXTRUDE reports the ignored open line");

        f.doc.GetCommandStack().Undo(f.scene());
        Check(f.All<Solid3DEntity>().empty() && f.scene().GetEntity(pl) && f.scene().GetEntity(c), "EXTRUDE: one undo restores the profiles");
    }

    void TestBooleans(Solid::ISolidKernel& k)
    {
        auto run = [&](const char* command, double expected, const char* label)
        {
            Fixture f(&k);
            auto a = Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10));
            auto b = Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10));
            b->ApplyTransform(Solid3DEntity::MoveMatrix({ 5, 0, 0 }));   // 第二个经变换平移，确认运算用的是世界坐标
            const auto ida = f.Add(std::move(a)), idb = f.Add(std::move(b));
            f.editor.SetSelection({ ida, idb });
            f.editor.ActivateToolById(command);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), expected, 1e-3), label);
            f.doc.GetCommandStack().Undo(f.scene());
            Check(f.All<Solid3DEntity>().size() == 2 && f.scene().GetEntity(ida) && f.scene().GetEntity(idb), "  … and one undo restores both solids");
        };
        run("Union",     1500, "UNION of two overlapping boxes: volume 1500");
        run("Subtract",  500,  "SUBTRACT: the first-created box minus the second, volume 500");
        run("Intersect", 500,  "INTERSECT: the common part, volume 500");

        Fixture f(&k);
        const auto id = f.Add(Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 1, 1, 1)));
        f.editor.SetSelection({ id });
        f.editor.ActivateToolById("Union");
        Check(f.All<Solid3DEntity>().size() == 1 && f.LastMessageHas("至少选择两个三维实体"), "UNION with one solid reports and changes nothing");

        // 只有圆（没有实体）时仍是面域布尔
        Fixture g(&k);
        const auto c1 = g.Add(std::make_unique<CircleEntity>(g.scene().NextObjectID(), Point3{ 0, 0, 0 }, 2.0));
        const auto c2 = g.Add(std::make_unique<CircleEntity>(g.scene().NextObjectID(), Point3{ 1, 0, 0 }, 2.0));
        g.editor.SetSelection({ c1, c2 });
        g.editor.ActivateToolById("Union");
        Check(g.All<RegionEntity>().size() == 1 && g.All<Solid3DEntity>().empty(), "UNION of circles is still a region boolean");
    }

    // ── M5：旋转、扫掠、圆角边、倒角边 ─────────────────────────────

    Polyline ClosedRect(double x0, double y0, double x1, double y1)
    {
        return Polyline({ { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } });
    }

    void TestRevolve(Solid::ISolidKernel& k)
    {
        // 2 ≤ x ≤ 4、0 ≤ y ≤ 3 的矩形绕 Y 轴：整圈体积 π(4² − 2²)·3 = 36π
        {
            Fixture f(&k);
            const auto pl = f.Add(std::make_unique<PolylineEntity>(f.scene().NextObjectID(), ClosedRect(2, 0, 4, 3)));
            f.editor.SetSelection({ pl });
            f.editor.ActivateToolById("Revolve");
            Check(f.editor.IsActiveTool(), "REVOLVE starts with a selection");
            f.editor.SubmitPoint({ 0, 0, 0 });
            f.editor.SubmitPoint({ 0, 5, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 36 * kPi, 1e-3), "REVOLVE: two axis points + Enter = full turn, volume 36π");
            Check(!f.scene().GetEntity(pl), "REVOLVE: the profile is replaced");
            Check(solids.size() == 1 && NearPoint(solids[0]->GetBoundingBox().Min, { -4, 0, -4 }, 1e-3), "REVOLVE: solid around the Y axis");
            f.doc.GetCommandStack().Undo(f.scene());
            Check(f.All<Solid3DEntity>().empty() && f.scene().GetEntity(pl), "REVOLVE: one undo restores the profile");
        }
        {
            Fixture f(&k);
            const auto pl = f.Add(std::make_unique<PolylineEntity>(f.scene().NextObjectID(), ClosedRect(2, 0, 4, 3)));
            f.editor.SetSelection({ pl });
            f.editor.ActivateToolById("Revolve");
            f.Key(KeyCode::Y);
            f.editor.SubmitCoordinateText("90");
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 9 * kPi, 1e-3), "REVOLVE: Y key for the axis, typed 90°, volume 9π");
        }
        {
            // 轴穿过轮廓：失败，原对象保留
            Fixture f(&k);
            const auto pl = f.Add(std::make_unique<PolylineEntity>(f.scene().NextObjectID(), ClosedRect(2, 0, 4, 3)));
            f.editor.SetSelection({ pl });
            f.editor.ActivateToolById("Revolve");
            f.editor.SubmitPoint({ 3, -1, 0 });
            f.editor.SubmitPoint({ 3, 5, 0 });
            f.Key(KeyCode::Enter);
            Check(f.All<Solid3DEntity>().empty() && f.scene().GetEntity(pl) && f.LastMessageHas("失败"),
                  "REVOLVE: an axis through the profile fails and keeps the profile");
        }
    }

    void TestSweep(Solid::ISolidKernel& k)
    {
        auto run = [&](auto makePath, const Point3& pickAt, double expected, const char* label)
        {
            Fixture f(&k);
            const auto c  = f.Add(std::make_unique<CircleEntity>(f.scene().NextObjectID(), Point3{ 50, 50, 0 }, 1.0));
            const auto pa = f.Add(makePath(f.scene().NextObjectID()));
            f.editor.SetSelection({ c });
            f.editor.ActivateToolById("Sweep");
            f.editor.SubmitPoint(pickAt);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), expected, expected * 1e-3), label);
            Check(!f.scene().GetEntity(c) && f.scene().GetEntity(pa), "  … profile replaced, path kept");
        };
        using Id = Object::ObjectID;
        run([](Id id) { return std::make_unique<LineEntity>(id, Point3{ 0, 0, 0 }, Point3{ 10, 0, 0 }); }, { 5, 0, 0 }, 10 * kPi, "SWEEP circle r = 1 along a line of 10: volume 10π");
        run([](Id id) { return std::make_unique<PolylineEntity>(id, Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } })); }, { 10, 5, 0 }, 20 * kPi,
            "SWEEP along an open L polyline: volume 20π (mitered)");
        run([](Id id) { return std::make_unique<ArcEntity>(id, Point3{ 0, 0, 0 }, 10.0, 0.0, kPi / 2); }, { 10 * std::cos(kPi / 4), 10 * std::sin(kPi / 4), 0 },
            kPi * 5 * kPi, "SWEEP along an arc entity R = 10, 90°: volume 5π²");
        // 直线接逆时针相切圆弧（MiniCAD 的凸度：逆时针为负）
        run([](Id id) { return std::make_unique<PolylineEntity>(id, Polyline({ { -10, 0, 0 }, { 0, 0, 0 }, { 10, 10, 0 } }, { 0.0, -std::tan(kPi / 8) })); },
            { -5, 0, 0 }, kPi * (10 + 5 * kPi), "SWEEP along a polyline with a CCW arc segment: volume π(10 + 5π)");
        run([](Id id) { return std::make_unique<CircleEntity>(id, Point3{ 0, 0, 0 }, 10.0); }, { 10, 0, 0 }, kPi * 2 * kPi * 10,
            "SWEEP along a circle R = 10: torus volume 20π²");

        {
            Fixture f(&k);
            const auto c = f.Add(std::make_unique<CircleEntity>(f.scene().NextObjectID(), Point3{ 0, 0, 0 }, 1.0));
            f.editor.SetSelection({ c });
            f.editor.ActivateToolById("Sweep");
            f.editor.SubmitPoint({ 1, 0, 0 });
            Check(f.All<Solid3DEntity>().empty() && f.editor.IsActiveTool() && f.LastMessageHas("不能是要扫掠的轮廓"),
                  "SWEEP: picking the profile itself as the path is refused, the command waits");
        }
    }

    void TestEdgeTools(Solid::ISolidKernel& k)
    {
        const double filletOne = 1000 - (1 - kPi / 4) * 10;   // 10 × 10 × 10 的长方体一条边 r = 1
        auto boxFixture = [&](Fixture& f, const Math::Vec3& move = {}) {
            auto box = Solid3DEntity::Create(f.scene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 10, 10));
            box->ApplyTransform(Solid3DEntity::MoveMatrix(move));
            return f.Add(std::move(box));
        };
        // 线框里有一条边：所有点满足 pred
        auto hasEdge = [](const Solid3DEntity* s, auto pred) {
            for (const auto& line : s->GetWorldWire())
                if (std::all_of(line.begin(), line.end(), pred))
                    return true;
            return false;
        };

        {
            // 点在顶边中点：顶边与底边重合，取高的（顶边）
            Fixture f(&k);
            boxFixture(f);
            f.editor.ActivateToolById("FilletEdge");
            Check(f.editor.IsActiveTool(), "FILLETEDGE starts without a selection");
            f.editor.SubmitPoint({ 5, 0, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), filletOne, 1e-2), "FILLETEDGE: one edge, default r = 1");
            Check(solids.size() == 1 && hasEdge(solids[0], [](const Point3& p) { return Near(p.y, 0, 1e-6) && Near(p.z, 9, 1e-6); }),
                  "FILLETEDGE: overlapping edges in plan view pick the top one");
            Check(f.LastMessageHas("圆角：1 条边"), "FILLETEDGE reports the result");
            f.doc.GetCommandStack().Undo(f.scene());
            solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 1000, 1e-6), "FILLETEDGE: one undo restores the box");
        }
        {
            // 角点处：投影成一点的竖直边优先
            Fixture f(&k);
            boxFixture(f);
            f.editor.ActivateToolById("FilletEdge");
            f.editor.SubmitCoordinateText("2");
            f.editor.SubmitPoint({ 0, 0, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 1000 - (4 - kPi) * 10, 1e-2), "FILLETEDGE: typed r = 2");
            Check(solids.size() == 1 && hasEdge(solids[0], [](const Point3& p) { return Near(p.z, 10, 1e-6) && p.x < 2 + 1e-6 && p.y < 2 + 1e-6; }),
                  "FILLETEDGE: at a corner the vertical edge is picked (arc on the top face)");
        }
        {
            // Tab 切换到下面的边；倒角距离 2
            Fixture f(&k);
            boxFixture(f);
            f.editor.ActivateToolById("ChamferEdge");
            f.editor.SubmitCoordinateText("2");
            f.Move({ 5, 0, 0 });
            f.Key(KeyCode::Tab);
            f.editor.SubmitPoint({ 5, 0, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 1000 - 0.5 * 4 * 10, 1e-2), "CHAMFEREDGE: d = 2 removes 20");
            Check(solids.size() == 1 && hasEdge(solids[0], [](const Point3& p) { return Near(p.y, 0, 1e-6) && Near(p.z, 2, 1e-6); }),
                  "CHAMFEREDGE: Tab cycles to the bottom edge under the top one");
        }
        {
            // 点两次取消；回车不改动
            Fixture f(&k);
            boxFixture(f);
            f.editor.ActivateToolById("FilletEdge");
            f.editor.SubmitPoint({ 5, 0, 0 });
            f.editor.SubmitPoint({ 5, 0, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 1000, 1e-6) && !f.doc.GetCommandStack().CanUndo(),
                  "FILLETEDGE: clicking an edge twice deselects it; Enter with nothing selected ends without changes");
        }
        {
            // 两条边；经变换移动过的实体按世界坐标选边，结果留在原位
            Fixture f(&k);
            boxFixture(f, { 100, 0, 0 });
            f.editor.ActivateToolById("FilletEdge");
            f.editor.SubmitPoint({ 105, 0, 0 });
            f.editor.SubmitPoint({ 110, 5, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            // 两条相邻的边在角上重叠，去掉的比两倍单边略少
            const double v = solids.size() == 1 ? f.SolidVolume(k, solids[0]) : -1;
            Check(v > 1000 - 2 * (1 - kPi / 4) * 10 - 1e-2 && v < filletOne - 1 && f.LastMessageHas("2 条边"),
                  "FILLETEDGE: two edges of a moved solid");
            Check(solids.size() == 1 && NearPoint(solids[0]->GetBoundingBox().Min, { 100, 0, 0 }, 1e-3), "FILLETEDGE: the result stays where the solid was");
        }
        {
            // 半径过大：内核失败，提示且不改动
            Fixture f(&k);
            boxFixture(f);
            f.editor.ActivateToolById("FilletEdge");
            f.editor.SubmitCoordinateText("20");
            f.editor.SubmitPoint({ 5, 0, 0 });
            f.Key(KeyCode::Enter);
            auto solids = f.All<Solid3DEntity>();
            Check(solids.size() == 1 && Near(f.SolidVolume(k, solids[0]), 1000, 1e-6) && f.LastMessageHas("失败，未做修改"),
                  "FILLETEDGE: a radius too large fails and leaves the solid unchanged");
        }
    }

    void TestNoKernel()
    {
        Fixture f(nullptr);
        for (const char* command : { "Box", "Cylinder", "Sphere", "Extrude", "Revolve", "Sweep", "FilletEdge", "ChamferEdge" })
        {
            f.editor.ActivateToolById(command);
            Check(!f.editor.IsActiveTool() && f.LastMessageHas("不可用"), (std::string(command) + " without a kernel reports it is unavailable").c_str());
        }
    }

    void TestAliases(Solid::ISolidKernel& k)
    {
        Fixture f(&k);
        bool ok = true;
        for (auto [alias, tool] : { std::pair{ "BOX", "Box" }, { "CYL", "Cylinder" }, { "CYLINDER", "Cylinder" }, { "SPHERE", "Sphere" },
                                    { "EXT", "Extrude" }, { "EXTRUDE", "Extrude" }, { "REV", "Revolve" }, { "REVOLVE", "Revolve" },
                                    { "SWEEP", "Sweep" }, { "FILLETEDGE", "FilletEdge" }, { "CHAMFEREDGE", "ChamferEdge" } })
            ok = ok && f.editor.ResolveCommandAlias(alias) == tool;
        Check(ok, "command-line aliases BOX / CYL / SPHERE / EXT / REV / SWEEP / FILLETEDGE / CHAMFEREDGE");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto kernel = Solid::CreateSMLibKernel();
    TestBox(*kernel);
    TestCylinderAndSphere(*kernel);
    TestExtrude(*kernel);
    TestBooleans(*kernel);
    TestRevolve(*kernel);
    TestSweep(*kernel);
    TestEdgeTools(*kernel);
    TestNoKernel();
    TestAliases(*kernel);
    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
