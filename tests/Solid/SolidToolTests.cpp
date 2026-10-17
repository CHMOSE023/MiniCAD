// 三维建模命令测试（M3）：经 Editor 的公开入口驱动，等同于命令行 / 点取操作：
//   ActivateToolById 启动命令，SubmitPoint 点取，SubmitCoordinateText 键入数值，SetSelection 预选
// 覆盖长方体、圆柱体、球体、拉伸、实体并 / 差 / 交、撤销，没有内核时的提示，以及面域布尔不受影响
#include "Editor/Editor.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Solid/SMLib/SMLibKernel.h"
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
        // 闭合多段线（跑道形：两段直线 + 两个半圆）、圆、一条不闭合的直线
        Polyline slot({ { 0, 0, 0 }, { 6, 0, 0 }, { 6, 2, 0 }, { 0, 2, 0 }, { 0, 0, 0 } }, { 0.0, 1.0, 0.0, 1.0 });
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

    void TestNoKernel()
    {
        Fixture f(nullptr);
        for (const char* command : { "Box", "Cylinder", "Sphere", "Extrude" })
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
                                    { "EXT", "Extrude" }, { "EXTRUDE", "Extrude" } })
            ok = ok && f.editor.ResolveCommandAlias(alias) == tool;
        Check(ok, "command-line aliases BOX / CYL / SPHERE / EXT");
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
    TestNoKernel();
    TestAliases(*kernel);
    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
