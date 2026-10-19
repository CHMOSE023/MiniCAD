// 三维出二维图测试（M4）：
//   - 曲线拟合（CurveFit.h）：直线、整圆、带圆角的多段线、单段圆弧、不该拟合成圆弧的多边形、首尾相接的折线连接
//   - Solid3DEntity 的网格：合并顶点、面内的边、.mcad / JSON 往返
//   - DWG / DXF 导出：三维实体写成多面网格，读回检查顶点、面、不可见边、变换
//   - 截面（SOLSECTION）：水平截面原位输出、铅垂剖面展开放置、结果可再拉伸、撤销
//   - 平面投影（FLATSHOT）：视图切换、隐藏线线型、放置点、撤销
#include "Editor/Editor.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Solid/CurveFit.h"
#include "Solid/SMLib/SMLibKernel.h"
#include "Dwg/Read/DwgReader.h"
#include "Dxf/Read/DxfReader.h"
#include "Database/CadDatabase.h"
#include "Database/Generated/Model.g.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <unordered_set>

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

    std::vector<Point3> ArcPoints(const Point3& c, double r, double a0, double a1, int n)
    {
        std::vector<Point3> pts;
        for (int i = 0; i <= n; ++i)
        {
            const double a = a0 + (a1 - a0) * i / n;
            pts.push_back({ c.x + r * std::cos(a), c.y + r * std::sin(a), 0 });
        }
        return pts;
    }

    // 多段线第 i 段圆弧的圆心与半径（按 MiniCAD 的凸度约定）
    ArcGeom SegArc(const Polyline& pl, int i) { return Polyline::ComputeArc(pl.Points[i], pl.Points[i + 1], pl.SegBulge(i)); }

    // ── 曲线拟合 ────────────────────────────────────────────────────
    void TestCurveFit()
    {
        using Kind = Solid::FittedCurve::Kind;
        const double tol = 1e-5;

        auto line = Solid::FitCurve({ { 0, 0, 5 }, { 1, 1, 5 }, { 2, 2, 5 }, { 5, 5, 5 } }, tol);
        Check(line.Type == Kind::Line && NearPoint(line.Poly.Points[0], { 0, 0, 0 }) && NearPoint(line.Poly.Points[1], { 5, 5, 0 }),
              "fit: collinear points → one line, z flattened to 0");

        auto circlePts = ArcPoints({ 3, 4, 0 }, 2, 0, 2 * kPi, 36);
        auto circle = Solid::FitCurve(circlePts, tol);
        Check(circle.Type == Kind::Circle && NearPoint(circle.Center, { 3, 4, 0 }) && Near(circle.Radius, 2), "fit: sampled closed circle → circle");

        // 圆角矩形：4 条直线 + 4 段 90° 圆弧（逆时针）
        std::vector<Point3> rr;
        auto append = [&](const std::vector<Point3>& pts) { for (const auto& p : pts) if (rr.empty() || !NearPoint(rr.back(), p, 1e-12)) rr.push_back(p); };
        append({ { 1, 0, 0 }, { 9, 0, 0 } });
        append(ArcPoints({ 9, 1, 0 }, 1, -kPi / 2, 0, 9));
        append({ { 10, 1, 0 }, { 10, 5, 0 } });
        append(ArcPoints({ 9, 5, 0 }, 1, 0, kPi / 2, 9));
        append({ { 9, 6, 0 }, { 1, 6, 0 } });
        append(ArcPoints({ 1, 5, 0 }, 1, kPi / 2, kPi, 9));
        append({ { 0, 5, 0 }, { 0, 1, 0 } });
        append(ArcPoints({ 1, 1, 0 }, 1, kPi, 1.5 * kPi, 9));
        auto rounded = Solid::FitCurve(rr, tol);
        Check(rounded.Type == Kind::Polyline && rounded.Poly.SegCount() == 8, "fit: rounded rectangle → 8 segments");
        if (rounded.Poly.SegCount() == 8)
        {
            Check(NearPoint(rounded.Poly.Points.front(), rounded.Poly.Points.back()), "fit: rounded rectangle stays closed (last == first)");
            int arcs = 0;
            bool centersOk = true;
            for (int i = 0; i < 8; ++i)
                if (rounded.Poly.SegIsArc(i))
                {
                    ++arcs;
                    const ArcGeom g = SegArc(rounded.Poly, i);
                    centersOk = centersOk && Near(g.Radius, 1) && (g.SweepAngle > 0);   // 逆时针
                }
            Check(arcs == 4 && centersOk, "fit: 4 arc segments, radius 1, counter-clockwise like the input");
            // 圆弧中点在原来的圆弧上（凸度方向正确）
            bool midsOk = true;
            for (int i = 0; i < 8; ++i)
                if (rounded.Poly.SegIsArc(i))
                {
                    const ArcGeom g = SegArc(rounded.Poly, i);
                    const double m = g.StartAngle + g.SweepAngle / 2;
                    const Point3 mid{ g.Center.x + std::cos(m), g.Center.y + std::sin(m), 0 };
                    const bool inside = mid.x > -1e-9 && mid.x < 10 + 1e-9 && mid.y > -1e-9 && mid.y < 6 + 1e-9;
                    const bool corner = (mid.x < 1 || mid.x > 9) && (mid.y < 1 || mid.y > 5);
                    midsOk = midsOk && inside && corner;
                }
            Check(midsOk, "fit: arc bulges bow outward at the corners");
        }

        auto arc = Solid::FitCurve(ArcPoints({ 0, 0, 0 }, 5, 0.2, 1.5, 12), tol);
        Check(arc.Type == Kind::Arc && Near(arc.Radius, 5) && Near(arc.StartAngle, 0.2) && Near(arc.EndAngle, 1.5), "fit: open sampled arc → arc");
        auto cwArc = Solid::FitCurve(ArcPoints({ 0, 0, 0 }, 5, 1.5, 0.2, 12), tol);
        Check(cwArc.Type == Kind::Arc && Near(cwArc.StartAngle, 0.2) && Near(cwArc.EndAngle, 1.5), "fit: clockwise sampled arc → same CCW arc");

        // 正六边形的顶点都在一个圆上，但只有 7 个点、每段是直线：不应拟合成圆
        auto hex = Solid::FitCurve(ArcPoints({ 0, 0, 0 }, 4, 0, 2 * kPi, 6), tol);
        Check(hex.Type == Kind::Polyline && hex.Poly.SegCount() == 6, "fit: hexagon stays 6 straight segments");
        bool straight = true;
        for (int i = 0; i < hex.Poly.SegCount(); ++i)
            straight = straight && !hex.Poly.SegIsArc(i);
        Check(straight, "fit: hexagon has no arc segments");

        // 首尾相接：顺序打乱、方向相反也能连成闭合轮廓
        std::vector<std::vector<Point3>> pieces{ { { 10, 0, 0 }, { 10, 5, 0 } }, { { 0, 0, 0 }, { 10, 0, 0 } },
                                                 { { 0, 5, 0 }, { 10, 5, 0 } },  { { 0, 5, 0 }, { 0, 0, 0 } } };
        auto joined = Solid::JoinPolylines(pieces, 1e-6);
        Check(joined.size() == 1 && joined[0].size() == 5 && NearPoint(joined[0].front(), joined[0].back()), "join: 4 shuffled edges → one closed loop");
        auto closedRect = Solid::FitCurve(joined[0], tol);
        Check(closedRect.Type == Kind::Polyline && closedRect.Poly.SegCount() == 4, "join + fit: closed rectangle with 4 segments");
    }

    // ── 网格与读写 ──────────────────────────────────────────────────
    std::vector<uint8_t> ReadAll(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }

    int InvisibleEdgeCount(const Solid3DEntity::SurfaceMesh& m)
    {
        int n = 0;
        for (uint8_t f : m.EdgeFlags)
            for (int i = 0; i < 3; ++i)
                n += (f >> i) & 1;
        return n;
    }

    void TestMeshAndExport(Solid::ISolidKernel& k)
    {
        Document doc;
        auto box = Solid3DEntity::Create(doc.GetScene().NextObjectID(), k, k.Box({ 0, 0, 0 }, 10, 20, 30));
        if (!box)
        {
            Check(false, "mesh: create box");
            return;
        }
        const auto& m = box->GetLocalMesh();
        Check(m.Positions.size() == 8 && m.TriangleCount() == 12, "mesh: box → 8 welded vertices, 12 triangles");
        Check(m.EdgeFlags.size() == 12 && InvisibleEdgeCount(m) == 12, "mesh: 6 face diagonals marked inside a face (12 triangle edges)");

        box->ApplyTransform(Solid3DEntity::MoveMatrix({ 100, 0, 0 }));
        auto cyl = Solid3DEntity::Create(doc.GetScene().NextObjectID(), k, k.Cylinder({ 0, 0, 0 }, 5, 10));
        Check(cyl && !cyl->GetLocalMesh().Empty() && InvisibleEdgeCount(cyl->GetLocalMesh()) > 0, "mesh: cylinder has a mesh");
        const size_t cylTris = cyl ? cyl->GetLocalMesh().TriangleCount() : 0;
        doc.GetScene().AddEntity(std::move(box));
        if (cyl) doc.GetScene().AddEntity(std::move(cyl));

        const auto dir = std::filesystem::temp_directory_path() / "minicad_solid_drawing";
        std::filesystem::create_directories(dir);

        // .mcad / .json 往返保留网格
        for (const char* name : { "mesh.mcad", "mesh.json" })
        {
            const std::string path = (dir / name).string();
            Check(doc.SaveAs(path), name);
            Document back;
            Check(back.LoadFromFile(path), "load back");
            size_t meshes = 0, tris = 0;
            back.GetScene().ForEachObject([&](const Object& o) {
                if (o.IsKindOf<Solid3DEntity>())
                {
                    const auto& sm = static_cast<const Solid3DEntity&>(o).GetLocalMesh();
                    meshes += sm.Empty() ? 0 : 1;
                    tris += sm.TriangleCount();
                }
            });
            Check(meshes == 2 && tris == 12 + cylTris, "round trip keeps both meshes");
        }

        // DWG / DXF：多面网格
        for (const char* name : { "mesh.dwg", "mesh.dxf" })
        {
            const auto path = dir / name;
            Check(doc.SaveToFile(path.string()), name);
            const auto bytes = ReadAll(path);
            auto db = std::string(name).ends_with(".dwg") ? MiniDWG::ReadDwg(bytes) : MiniDWG::ReadDxf(bytes);
            Check(db != nullptr, "read back exported file");
            if (!db)
                continue;
            std::vector<const MiniDWG::PolyfaceMesh*> meshes;
            for (const auto& [h, o] : db->Objects())
                if (auto* pm = dynamic_cast<const MiniDWG::PolyfaceMesh*>(o.get()))
                    meshes.push_back(pm);
            Check(meshes.size() == 2, "two solids → two polyface meshes");
            if (meshes.empty())
                continue;
            const MiniDWG::PolyfaceMesh* boxMesh = nullptr;
            for (auto* pm : meshes)
                if (pm->Vertices.size() == 8)
                    boxMesh = pm;
            Check(boxMesh && boxMesh->Faces.size() == 12, "box mesh: 8 vertices, 12 faces");
            if (!boxMesh)
                continue;
            double minX = 1e9, maxX = -1e9;
            for (auto h : boxMesh->Vertices)
                if (auto* v = db->FindAs<MiniDWG::Vertex>(h))
                {
                    minX = std::min(minX, v->Location.X);
                    maxX = std::max(maxX, v->Location.X);
                }
            Check(Near(minX, 100) && Near(maxX, 110), "box mesh: transform applied (moved +100 in X)");
            int negative = 0;
            bool inRange = true;
            for (auto h : boxMesh->Faces)
                if (auto* f = db->FindAs<MiniDWG::VertexFaceRecord>(h))
                    for (int idx : { int(f->Index1), int(f->Index2), int(f->Index3) })
                    {
                        negative += idx < 0;
                        inRange = inRange && idx != 0 && std::abs(idx) <= 8;
                    }
            Check(inRange && negative == 12, "box mesh: 1-based indices, face diagonals invisible (negative)");
        }
    }

    // ── 截面与投影（经 Editor 的公开入口） ────────────────────────────
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

        Object::ObjectID AddSolid(Solid::ISolidKernel& k, const Solid::BodyPtr& body)
        {
            auto e = Solid3DEntity::Create(scene().NextObjectID(), k, body);
            const auto id = e->GetID();
            scene().AddEntity(std::move(e));
            return id;
        }

        // 不是三维实体的对象（本次命令生成的二维曲线）
        std::vector<const Entity*> Curves()
        {
            std::vector<const Entity*> out;
            scene().ForEachObject([&](const Object& o) {
                if (o.IsKindOf<Entity>() && !o.IsKindOf<Solid3DEntity>())
                    out.push_back(static_cast<const Entity*>(&o));
            });
            return out;
        }

        template<class T>
        int Count() { int n = 0; for (auto* e : Curves()) n += e->IsKindOf<T>() ? 1 : 0; return n; }

        // 多段线的包围盒带线宽余量，这里用顶点（本测试里的多段线没有圆弧段）
        AABB CurveBounds()
        {
            AABB b = AABB::Empty();
            for (auto* e : Curves())
            {
                if (e->IsKindOf<PolylineEntity>())
                    for (const auto& p : static_cast<const PolylineEntity*>(e)->GetPolyline().Points)
                        b.Expand(p);
                else
                    b.Expand(e->GetBoundingBox());
            }
            return b;
        }

        void Key(KeyCode key)
        {
            InputEvent e;
            e.Type = InputEventType::KeyDown;
            e.Key  = key;
            editor.OnInput(e);
        }

        bool LastMessageHas(const char* text) const
        {
            const auto& lines = editor.GetCmdLine().Lines();
            for (size_t i = 0; i < 3 && i < lines.size(); ++i)
                if (lines[lines.size() - 1 - i].find(text) != std::string::npos)
                    return true;
            return false;
        }
    };

    // 10 × 20 × 30 的长方体，中间一个半径 3、沿 Z 的通孔
    Solid::BodyPtr BoxWithHole(Solid::ISolidKernel& k)
    {
        return k.Boolean(Solid::BooleanOp::Difference, k.Box({ 0, 0, 0 }, 10, 20, 30), k.Cylinder({ 5, 10, -1 }, 3, 32));
    }

    void TestSection(Solid::ISolidKernel& k)
    {
        {
            Fixture f(&k);
            const auto id = f.AddSolid(k, BoxWithHole(k));
            f.editor.SetSelection({ id });
            f.editor.ActivateToolById("SolidSection");
            Check(f.editor.IsActiveTool(), "SECTION starts with a selected solid");
            f.editor.SubmitCoordinateText("5");
            Check(f.Count<PolylineEntity>() == 1 && f.Count<CircleEntity>() == 1 && f.Curves().size() == 2,
                  "SECTION z=5: one outline polyline + one circle");
            for (auto* e : f.Curves())
            {
                if (e->IsKindOf<CircleEntity>())
                {
                    const auto& c = static_cast<const CircleEntity*>(e)->GetCircle();
                    Check(NearPoint(c.Center, { 5, 10, 0 }) && Near(c.Radius, 3), "SECTION: hole → circle r=3 at (5,10)");
                }
                if (e->IsKindOf<PolylineEntity>())
                {
                    const auto& pl = static_cast<const PolylineEntity*>(e)->GetPolyline();
                    Check(pl.SegCount() == 4 && NearPoint(pl.Points.front(), pl.Points.back()), "SECTION: outline is a closed 4-segment polyline");
                    AABB b = AABB::Empty();
                    for (const auto& p : pl.Points)
                        b.Expand(p);
                    Check(NearPoint(b.Min, { 0, 0, 0 }) && NearPoint(b.Max, { 10, 20, 0 }), "SECTION: outline in place, flattened to z=0");
                }
            }
            Check(f.LastMessageHas("2 条曲线"), "SECTION: reports the curve count");

            // 截面结果可再拉伸成实体
            std::unordered_set<Object::ObjectID> outline;
            for (auto* e : f.Curves())
                outline.insert(e->GetID());
            f.editor.SetSelection(outline);
            f.editor.ActivateToolById("Extrude");
            f.editor.SubmitCoordinateText("4");
            int solids = 0;
            f.scene().ForEachObject([&](const Object& o) { solids += o.IsKindOf<Solid3DEntity>() ? 1 : 0; });
            Check(solids == 3, "SECTION result can be extruded (outline + circle → 2 solids)");

            f.doc.GetCommandStack().Undo(f.scene());
            f.doc.GetCommandStack().Undo(f.scene());
            Check(f.Curves().empty(), "SECTION: one undo removes all section curves");
        }
        {
            // 铅垂剖面：沿 y = 10 从 x=0 到 x=10，放在 (100, 0)
            Fixture f(&k);
            const auto id = f.AddSolid(k, BoxWithHole(k));
            f.editor.SetSelection({ id });
            f.editor.ActivateToolById("SolidSection");
            f.editor.SubmitPoint({ 0, 10, 0 });
            f.editor.SubmitPoint({ 10, 10, 0 });
            f.editor.SubmitPoint({ 100, 0, 0 });
            const AABB b = f.CurveBounds();
            Check(f.Curves().size() == 2 && NearPoint(b.Min, { 100, 0, 0 }) && NearPoint(b.Max, { 110, 30, 0 }),
                  "SECTION vertical: hole splits the cut into two 2×30 rectangles placed at (100,0)");
        }
        {
            Fixture f(&k);
            const auto id = f.AddSolid(k, BoxWithHole(k));
            f.editor.SetSelection({ id });
            f.editor.ActivateToolById("SolidSection");
            f.editor.SubmitCoordinateText("50");
            Check(f.Curves().empty() && f.LastMessageHas("不相交"), "SECTION above the solid: nothing, reported");
        }
    }

    void TestFlatshot(Solid::ISolidKernel& k)
    {
        {
            Fixture f(&k);
            const auto id = f.AddSolid(k, BoxWithHole(k));
            f.editor.SetSelection({ id });
            f.editor.ActivateToolById("Flatshot");
            Check(f.editor.IsActiveTool(), "FLATSHOT starts with a selected solid");
            f.editor.SubmitPoint({ 200, 0, 0 });   // 默认前视
            const AABB b = f.CurveBounds();
            Check(!f.Curves().empty() && NearPoint(b.Min, { 200, 0, 0 }) && NearPoint(b.Max, { 210, 30, 0 }),
                  "FLATSHOT front: 10 × 30 outline, lower-left at the picked point");
            const LineTypeID hiddenType = f.scene().GetLineTypeTable().FindByName("HIDDEN");
            int hidden = 0;
            for (auto* e : f.Curves())
                hidden += e->GetAttr().LineType == hiddenType ? 1 : 0;
            Check(hiddenType != LineTypeTable::ContinuousID && hidden == 2, "FLATSHOT front: the hole's two hidden silhouette lines use HIDDEN");
            f.doc.GetCommandStack().Undo(f.scene());
            Check(f.Curves().empty(), "FLATSHOT: one undo removes the projection");
        }
        {
            Fixture f(&k);
            const auto id = f.AddSolid(k, BoxWithHole(k));
            f.editor.SetSelection({ id });
            f.editor.ActivateToolById("Flatshot");
            f.Key(KeyCode::T);
            f.Key(KeyCode::H);   // 不要隐藏线
            Check(f.LastMessageHas("不生成隐藏线"), "FLATSHOT: H toggles hidden lines");
            f.editor.SubmitPoint({ 0, 100, 0 });
            const AABB b = f.CurveBounds();
            Check(NearPoint(b.Min, { 0, 100, 0 }) && NearPoint(b.Max, { 10, 120, 0 }), "FLATSHOT top: 10 × 20 at the picked point");
            bool circleOk = false;
            for (auto* e : f.Curves())
                if (e->IsKindOf<CircleEntity>())
                {
                    const auto& c = static_cast<const CircleEntity*>(e)->GetCircle();
                    circleOk = NearPoint(c.Center, { 5, 110, 0 }) && Near(c.Radius, 3);
                }
            Check(circleOk, "FLATSHOT top: hole → circle r=3, dimensionable");
            Check(f.LastMessageHas("俯视"), "FLATSHOT: reports the view");
        }
        {
            // 两个实体：前面的挡住后面的（同一 x 范围），合并后后面的边不可见
            Fixture f(&k);
            const auto a = f.AddSolid(k, k.Box({ 0, 0, 0 }, 10, 10, 10));
            const auto b = f.AddSolid(k, k.Box({ 2, 20, 2 }, 6, 10, 6));
            f.editor.SetSelection({ a, b });
            f.editor.ActivateToolById("Flatshot");
            f.editor.SubmitPoint({ 0, 0, 0 });
            const LineTypeID hiddenType = f.scene().GetLineTypeTable().FindByName("HIDDEN");
            int hidden = 0;
            for (auto* e : f.Curves())
                hidden += e->GetAttr().LineType == hiddenType ? 1 : 0;
            Check(hidden > 0, "FLATSHOT front: a solid behind another is hidden");
        }
        {
            Fixture f(nullptr);
            f.editor.ActivateToolById("Flatshot");
            Check(!f.editor.IsActiveTool() && f.LastMessageHas("不可用"), "FLATSHOT without a kernel: reported, not started");
        }
    }
}

int main()
{
    auto kernel = Solid::CreateSMLibKernel();
    TestCurveFit();
    TestMeshAndExport(*kernel);
    TestSection(*kernel);
    TestFlatshot(*kernel);
    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
