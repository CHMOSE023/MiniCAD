// NodeSolidTests：空间曲线与曲面（M7 / 曲面节点、N8）。
// 空间曲线（插值、拟合、螺旋线）；放样、加厚、旋转曲面、点阵曲面、管道、预览烘焙、读写往返与导出；
// N8：曲面的创建（平面带洞、挤出、直纹放样、单轨 / 双轨扫描、边界、网络、拟合、参数曲面）、
// 修改（修剪、分割、炸开 → 拼接成实体、封口、翻转、延伸、重建）、分析（分解、等参线、最近点、求交、曲率）
// 实现：src/NodeGraph/BuiltinSurfaceNodes.cpp、BuiltinSurfaceOps.cpp（surface.*）、src/Solid/SMLib/SMLibKernel.cpp
// 计划：docs/NodeGraph/PLAN.md 第 6 节；docs/Solid/PLAN.md 第 11、13 节
#include "NodeSolidCommon.h"

namespace NodeSolidTest
{
    namespace
    {
        std::vector<uint8_t> ReadAll(const std::filesystem::path& p)
        {
            std::ifstream f(p, std::ios::binary);
            return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
        }

        // 两个圆（半径 5，z = 0 与 z = 10）放样：圆是曲线实体，连到空间曲线端口时自动转换
        struct LoftFixture : Fixture
        {
            NodeId c0, c1, loft, info;
            LoftFixture()
            {
                c0 = Add(g, "curve.circle"), c1 = Add(g, "curve.circle"), loft = Add(g, "surface.loft"), info = Add(g, "surface.info");
                g.SetInputValue(c1, 0, Value(Math::Vec3{ 0, 0, 10 }));
                g.Connect({ c0, 0, loft, 0 });
                g.Connect({ c1, 0, loft, 0 }, /*replace=*/false);
                g.Connect({ loft, 0, info, 0 });
            }
        };
    }

    void TestCurve3D()
    {
        Fixture f;
        // 插值：5 个点；曲线取点
        const NodeId pts = Add(f.g, "vector.grid"), interp = Add(f.g, "nurbs.interpolate"), eval = Add(f.g, "nurbs.evaluate");
        f.g.SetInputValue(pts, 1, int64_t(5));
        f.g.SetInputValue(pts, 2, int64_t(1));
        f.g.Connect({ pts, 0, interp, 0 });
        f.g.Connect({ interp, 0, eval, 0 });
        f.g.SetInputValue(eval, 1, 1.0);
        f.Eval();
        const auto& end = f.g.GetNode(eval)->Outputs;
        Check(f.Ok(interp) && f.g.GetNode(interp)->Outputs[0][0].Type() == DataType::Curve3D, "interpolate: outputs a space curve");
        Check(f.Ok(eval) && Near(end[0][0].AsVector().x, 40.0) && Near(end[2][0].AsNumber(), 40.0) && Near(end[1][0].AsVector().x, 1.0),
              "curve evaluate: t = 1 is the last point, tangent +X, length 40 (collinear points)");

        // 螺旋线 → 管道（弹簧）：体积 ≈ π·r²·L
        const NodeId helix = Add(f.g, "nurbs.helix"), pipe = Add(f.g, "surface.pipe"), len = Add(f.g, "nurbs.evaluate");
        f.g.SetInputValue(helix, 1, 10.0);
        f.g.SetInputValue(helix, 2, 10.0);
        f.g.SetInputValue(helix, 3, 20.0);
        f.g.SetInputValue(helix, 4, 2.0);
        f.g.Connect({ helix, 0, pipe, 0 });
        f.g.Connect({ helix, 0, len, 0 });
        f.Eval();
        const double coilLength = f.Number(len, 2);
        Check(f.Ok(pipe) && f.kernel->IsSolid(f.Body(pipe)) && Near(f.Volume(pipe), kPi * coilLength, 1e-2), "pipe along a helix: solid spring, volume π·r²·L");
        Check(Near(coilLength, std::hypot(40 * kPi, 20), 1e-3), "helix: length √((2π·10·2)² + 20²)");

        // 曲线实体 → 空间曲线：圆转成有理二次曲线，长度不变
        const NodeId circle = Add(f.g, "curve.circle"), ce = Add(f.g, "nurbs.evaluate");
        f.g.Connect({ circle, 0, ce, 0 });
        f.Eval();
        Check(f.Ok(ce) && Near(f.Number(ce, 2), 10 * kPi, 1e-4), "conversion: circle → NURBS keeps the length 10π");
        const NodeId line = Add(f.g, "curve.line");
        const NodeId revolve = Add(f.g, "solid.revolve");   // 拉伸的轮廓也收空间曲线，用旋转的轮廓端口
        Check(f.g.CanConnect({ line, 0, ce, 0 }) && !f.g.CanConnect({ interp, 0, revolve, 0 }),
              "types: curve → space curve connects, space curve → planar curve port does not");

        // 没有内核：控制点曲线照常求值，放样报错
        Graph g2;
        const NodeId grid = Add(g2, "vector.grid"), ctrl = Add(g2, "nurbs.control"), loft = Add(g2, "surface.loft");
        g2.Connect({ grid, 0, ctrl, 0 });
        g2.Connect({ ctrl, 0, loft, 0 });
        g2.Evaluate(SolveContext{});
        Check(g2.GetNode(ctrl)->State == NodeState::Ok && g2.GetNode(ctrl)->Outputs[0][0].AsCurve3D()->Degree == 3, "control-point curve works without a kernel");
        Check(g2.GetNode(loft)->State == NodeState::Error, "loft without a kernel reports an error");
    }

    void TestSurfaceNodes()
    {
        // 放样：圆柱侧面，面积 2π·5·10；开放曲面
        LoftFixture f;
        const NodeId sinfo = Add(f.g, "solid.info"), thick = Add(f.g, "surface.thicken"), tinfo = Add(f.g, "surface.info");
        f.g.Connect({ f.loft, 0, sinfo, 0 });
        f.g.Connect({ f.loft, 0, thick, 0 });
        f.g.Connect({ thick, 0, tinfo, 0 });
        f.Eval();
        Check(f.Ok(f.loft) && Near(f.Number(f.info), 100 * kPi) && !f.g.GetNode(f.info)->Outputs[2][0].AsBool(), "loft: area 100π, not a closed solid");
        Check(f.State(sinfo) == NodeState::Warning && Near(f.Number(sinfo), 0.0), "solid info on an open surface: warning, volume 0");

        // 加厚 1：管壁，体积 π(6² − 5²)·10 或 π(5² − 4²)·10（沿法线方向）
        const double wall = f.Volume(thick);
        Check(f.Ok(thick) && f.g.GetNode(tinfo)->Outputs[2][0].AsBool() && (Near(wall, 110 * kPi) || Near(wall, 90 * kPi)), "thicken: tube wall is a solid");
        f.g.SetInputValue(thick, 1, -1.0);
        f.Eval();
        Check(Near(f.Volume(thick) + wall, 200 * kPi), "thicken: negative thickness goes to the other side");

        // 开放曲面不能做布尔运算；实体不能再加厚
        const NodeId box = Add(f.g, "solid.box"), uni = Add(f.g, "solid.union"), again = Add(f.g, "surface.thicken");
        f.g.Connect({ box, 0, uni, 0 });
        f.g.Connect({ f.loft, 0, uni, 0 }, false);
        f.g.Connect({ box, 0, again, 0 });
        f.Eval();
        Check(f.State(uni) == NodeState::Error && f.g.GetNode(uni)->Message.find("开放曲面") != std::string::npos, "union with an open surface: clear error");
        Check(f.State(again) == NodeState::Error, "thicken a closed solid: error");

        // 旋转曲面：斜线 (3,0,0)–(0,0,4) 绕 Z 轴 → 圆锥面，面积 π·3·5；180° 一半
        Fixture r;
        const NodeId slant = Add(r.g, "curve.line"), rev = Add(r.g, "surface.revolve"), rinfo = Add(r.g, "surface.info");
        r.g.SetInputValue(slant, 0, Value(Math::Vec3{ 3, 0, 0 }));
        r.g.SetInputValue(slant, 1, Value(Math::Vec3{ 0, 0, 4 }));
        r.g.Connect({ slant, 0, rev, 0 });
        r.g.Connect({ rev, 0, rinfo, 0 });
        r.Eval();
        Check(r.Ok(rev) && Near(r.Number(rinfo), 15 * kPi), "revolve surface: cone area 15π");
        r.g.SetInputValue(rev, 3, 180.0);
        r.Eval();
        Check(Near(r.Number(rinfo), 7.5 * kPi), "revolve surface 180°: half the area");
        r.g.SetInputValue(rev, 3, 0.0);
        r.Eval();
        Check(r.State(rev) == NodeState::Error, "revolve surface: zero angle fails");

        // 点阵曲面：4 × 3 个点、间距 10 → 30 × 20 的平面片；加厚 2 → 体积 1200；曲面取点在中间
        Fixture p;
        const NodeId grid = Add(p.g, "vector.grid"), surf = Add(p.g, "surface.points"), pinfo = Add(p.g, "surface.info");
        const NodeId slab = Add(p.g, "surface.thicken"), ev = Add(p.g, "surface.evaluate");
        p.g.SetInputValue(grid, 1, int64_t(4));
        p.g.SetInputValue(grid, 2, int64_t(3));
        p.g.Connect({ grid, 0, surf, 0 });
        p.g.Connect({ surf, 0, pinfo, 0 });
        p.g.Connect({ surf, 0, slab, 0 });
        p.g.Connect({ surf, 0, ev, 0 });
        p.g.SetInputValue(slab, 1, 2.0);
        p.Eval();
        Check(p.Ok(surf) && Near(p.Number(pinfo), 600.0), "surface from points: 30 × 20 sheet");
        Check(p.Ok(slab) && Near(p.Volume(slab), 1200.0), "thicken the sheet by 2: volume 1200");
        const auto& evOut = p.g.GetNode(ev)->Outputs;
        Check(p.Ok(ev) && Near(evOut[0][0].AsVector().x, 15.0) && Near(evOut[0][0].AsVector().y, 10.0) && Near(std::abs(evOut[1][0].AsVector().z), 1.0),
              "surface evaluate: (0.5, 0.5) is the middle, normal ±Z");
        p.g.SetInputValue(surf, 1, int64_t(5));
        p.Eval();
        Check(p.State(surf) == NodeState::Error, "surface from points: count not a multiple of the columns fails");

        // 直纹面与偏移曲面
        Fixture d;
        const NodeId la = Add(d.g, "curve.line"), lb = Add(d.g, "curve.line"), ruled = Add(d.g, "surface.ruled"), off = Add(d.g, "surface.offset");
        const NodeId oinfo = Add(d.g, "surface.info");
        d.g.SetInputValue(lb, 0, Value(Math::Vec3{ 0, 5, 0 }));
        d.g.SetInputValue(lb, 1, Value(Math::Vec3{ 10, 5, 0 }));
        d.g.Connect({ la, 0, ruled, 0 });
        d.g.Connect({ lb, 0, ruled, 1 });
        d.g.Connect({ ruled, 0, off, 0 });
        d.g.Connect({ off, 0, oinfo, 0 });
        d.g.SetInputValue(off, 1, 3.0);
        d.Eval();
        const AABB ob = d.Ok(off) ? d.kernel->Bounds(d.Body(off)) : AABB::Empty();
        Check(d.Ok(ruled) && Near(d.Number(oinfo), 50.0) && Near(std::abs(ob.Min.z), 3.0, 1e-2), "ruled surface 10 × 5, offset by 3 along its normal");
    }

    void TestSurfacePreviewBakeAndFiles()
    {
        Solid::ISolidKernel* kernel = g_kernel;
        Document doc;
        Viewport viewport(800.0f, 600.0f);
        Editor   editor;
        editor.Bind(doc, viewport);
        editor.SetSolidKernel(kernel);

        Graph& g = doc.GetNodeGraph();
        const NodeId c0 = Add(g, "curve.circle"), c1 = Add(g, "curve.circle"), loft = Add(g, "surface.loft"), helix = Add(g, "nurbs.helix");
        g.SetInputValue(c1, 0, Value(Math::Vec3{ 0, 0, 10 }));
        g.Connect({ c0, 0, loft, 0 });
        g.Connect({ c1, 0, loft, 0 }, false);
        (void)helix;
        SolveContext ctx;
        ctx.Kernel = kernel;
        g.Evaluate(ctx);

        // 预览：曲面 → Solid3DEntity（着色），空间曲线 → 样条
        PreviewBuilder builder;
        PreviewSet set = builder.Build(g, {}, kernel);
        int surfaces = 0, splines = 0;
        for (const auto& e : set.Entities)
        {
            surfaces += e->IsKindOf<Solid3DEntity>() ? 1 : 0;
            splines  += e->IsKindOf<SplineEntity>() && static_cast<const SplineEntity&>(*e).HasControlData() ? 1 : 0;
        }
        Check(surfaces == 1 && splines == 1, "preview: the loft is a shaded surface, the helix a spline");
        editor.SetPreviewGeometry(set.Entities, set.Points, set.HighlightPoints);
        editor.Render();
        Check(!editor.GetShadedVertices().empty(), "editor: the open surface is shaded");

        // 烘焙 → 图纸：曲面实体 + 样条，一步撤销
        const int n = Bake(g, {}, doc.GetScene(), doc.GetCommandStack(), kernel);
        const Solid3DEntity* baked = nullptr;
        const SplineEntity*  spline = nullptr;
        doc.GetScene().ForEachObject([&](const Object& o) {
            if (o.IsKindOf<Solid3DEntity>()) baked = static_cast<const Solid3DEntity*>(&o);
            if (o.IsKindOf<SplineEntity>())  spline = static_cast<const SplineEntity*>(&o);
        });
        Check(n == 2 && baked && spline && !baked->GetLocalMesh().Empty(), "bake: surface and helix become document entities");
        const size_t tris = baked ? baked->GetLocalMesh().TriangleCount() : 0;
        const int    ctrl = spline ? spline->ControlPointCount() : 0;

        // .mcad / .json：曲面的内核数据、网格与样条的控制点往返；节点图随文档保存，重新求值面积不变
        const auto dir = std::filesystem::temp_directory_path() / "minicad_node_surface";
        std::filesystem::create_directories(dir);
        for (const char* name : { "surface.mcad", "surface.json" })
        {
            const std::string path = (dir / name).string();
            Check(doc.SaveAs(path), name);
            Document back;
            Check(back.LoadFromFile(path), "load back");
            const Solid3DEntity* s = nullptr;
            const SplineEntity*  sp = nullptr;
            back.GetScene().ForEachObject([&](const Object& o) {
                if (o.IsKindOf<Solid3DEntity>()) s = static_cast<const Solid3DEntity*>(&o);
                if (o.IsKindOf<SplineEntity>())  sp = static_cast<const SplineEntity*>(&o);
            });
            const Solid::BodyPtr body = s ? s->GetBody(*kernel) : nullptr;
            Check(s && s->GetLocalMesh().TriangleCount() == tris && body && !kernel->IsSolid(body) && Near(kernel->Area(body), 100 * kPi),
                  "round trip: the baked surface keeps its mesh, stays open, area 100π");
            Check(sp && sp->HasControlData() && sp->ControlPointCount() == ctrl, "round trip: the baked helix keeps its control points");

            Graph& bg = back.GetNodeGraph();
            const NodeId info = Add(bg, "surface.info");
            NodeId backLoft = kInvalidNode;
            for (const auto& node : bg.GetNodes())
                if (node->Type && node->Type->Id == "surface.loft")
                    backLoft = node->Id;
            bg.Connect({ backLoft, 0, info, 0 });
            bg.Evaluate(ctx);
            const auto& out = bg.GetNode(info)->Outputs[0];
            Check(!out.empty() && Near(out[0].AsNumber(), 100 * kPi), "round trip: the node graph re-evaluates the loft");
        }

        // DWG / DXF：开放曲面写成多面网格
        for (const char* name : { "surface.dwg", "surface.dxf" })
        {
            const auto path = dir / name;
            Check(doc.SaveToFile(path.string()), name);
            const auto bytes = ReadAll(path);
            auto db = std::string(name).ends_with(".dwg") ? MiniDWG::ReadDwg(bytes) : MiniDWG::ReadDxf(bytes);
            size_t meshes = 0, faces = 0;
            if (db)
                for (const auto& [h, o] : db->Objects())
                    if (auto* pm = dynamic_cast<const MiniDWG::PolyfaceMesh*>(o.get()))
                    {
                        ++meshes;
                        faces += pm->Faces.size();
                    }
            Check(db && meshes == 1 && faces == tris, "export: the open surface becomes one polyface mesh");
        }

        doc.Undo();
        Check(doc.GetScene().EntityCount() == 0, "bake: one undo step");
    }

    // N8：曲面的创建（平面带洞、挤出、直纹放样、单轨 / 双轨扫描、边界、网络、拟合、参数曲面）、
    // 修改（修剪、分割、炸开 → 拼接成实体、封口、翻转、延伸、重建）、分析（分解、等参线、最近点、求交、曲率）
    void TestN8Surfaces()
    {
        Fixture f;
        Graph& g = f.g;
        auto area = [&](NodeId id, int port = 0, size_t i = 0)
        {
            const auto& o = g.GetNode(id)->Outputs[port];
            return i < o.size() ? f.kernel->Area(o[i].AsBody()) : -1.0;
        };
        auto line = [&](Math::Vec3 a, Math::Vec3 b)
        {
            const NodeId l = Add(g, "curve.line");
            g.SetInputValue(l, 0, Value(a));
            g.SetInputValue(l, 1, Value(b));
            return l;
        };
        auto circle = [&](Math::Vec3 c, double r)
        {
            const NodeId n = Add(g, "curve.circle");
            g.SetInputValue(n, 0, Value(c));
            g.SetInputValue(n, 1, r);
            return n;
        };
        auto evalAt = [&](NodeId surface, double u, double v)
        {
            Math::Point3 p;
            Math::Vec3 n;
            f.kernel->EvaluateSurface(f.Body(surface), 0, u, v, p, n);
            return std::pair{ p, n };
        };

        // 平面曲面：同心圆 r 5、r 2 → 圆环面 21π
        const NodeId planar = Add(g, "surface.planar");
        g.Connect({ circle({}, 5), 0, planar, 0 });
        g.Connect({ circle({}, 2), 0, planar, 0 }, false);
        f.Eval();
        Check(f.Ok(planar) && Near(area(planar), 21 * kPi), "N8 planar: a disc with a hole");

        // 挤出曲面：圆 r 5 沿 Z 挤出 10 → 侧面 100π，开放
        const NodeId ext = Add(g, "surface.extrude");
        g.Connect({ circle({}, 5), 0, ext, 0 });
        f.Eval();
        Check(f.Ok(ext) && Near(area(ext), 100 * kPi) && !f.kernel->IsSolid(f.Body(ext)), "N8 extrude: an open tube");

        // 直纹放样：三条直线成两片直纹面
        const NodeId loft = Add(g, "surface.loft");
        g.Connect({ line({ 0, 0, 0 }, { 10, 0, 0 }), 0, loft, 0 });
        g.Connect({ line({ 0, 5, 5 }, { 10, 5, 5 }), 0, loft, 0 }, false);
        g.Connect({ line({ 0, 10, 0 }, { 10, 10, 0 }), 0, loft, 0 }, false);
        g.SetParam(loft, 0, true);
        f.Eval();
        Check(f.Ok(loft) && Near(area(loft), 2 * 10 * std::sqrt(50.0)), "N8 loft: ruled through three lines");

        // 单轨扫描：长 5 的线段沿长 10 的竖直线扫过 → 50
        const NodeId sw1 = Add(g, "surface.sweep1");
        g.Connect({ line({ 0, 0, 0 }, { 0, 0, 10 }), 0, sw1, 0 });
        g.Connect({ line({ 0, 0, 0 }, { 5, 0, 0 }), 0, sw1, 1 });
        f.Eval();
        Check(f.Ok(sw1) && Near(area(sw1), 50.0), "N8 sweep1: a segment along a line");

        // 双轨扫描：半圆截面（r 5）沿两条平行直线 → 半圆柱面 50π；第二条轨道张开时截面随间距放大
        const NodeId arc = Add(g, "curve.arc"), sw2 = Add(g, "surface.sweep2");
        g.SetInputValue(arc, 0, Value(Math::Vec3{ 5, 0, 0 }));
        g.SetInputValue(arc, 2, 0.0);
        g.SetInputValue(arc, 3, 180.0);   // (10, 0, 0) → (0, 0, 0)
        const NodeId rail1 = line({ 10, 0, 0 }, { 10, 0, 10 }), rail2 = line({ 0, 0, 0 }, { 0, 0, 10 });
        g.Connect({ rail1, 0, sw2, 0 });
        g.Connect({ rail2, 0, sw2, 1 });
        g.Connect({ arc, 0, sw2, 2 });
        f.Eval();
        Check(f.Ok(sw2) && Near(area(sw2), 50 * kPi), "N8 sweep2: half cylinder between parallel rails");
        g.SetInputValue(rail2, 1, Value(Math::Vec3{ -10, 0, 10 }));
        f.Eval();
        const AABB sb = f.Count(sw2) ? f.kernel->Bounds(f.Body(sw2)) : AABB::Empty();
        Check(f.Ok(sw2) && Near(sb.Min.x, -10.0, 1e-2) && Near(sb.Max.y, 10.0, 1e-2), "N8 sweep2: the section scales with the rail distance");

        // 边界曲面与网络曲面：马鞍面 z = 0.1·x·y，中点 (5, 5, 2.5)
        const NodeId edge = Add(g, "surface.edge");
        g.Connect({ line({ 0, 0, 0 }, { 10, 0, 0 }), 0, edge, 0 });
        g.Connect({ line({ 10, 10, 10 }, { 10, 0, 0 }), 0, edge, 0 }, false);   // 方向随意
        g.Connect({ line({ 10, 10, 10 }, { 0, 10, 0 }), 0, edge, 0 }, false);
        g.Connect({ line({ 0, 10, 0 }, { 0, 0, 0 }), 0, edge, 0 }, false);
        const NodeId net = Add(g, "surface.network");
        for (int i = 0; i < 3; ++i)
        {
            g.Connect({ line({ 0, i * 5.0, 0 }, { 10, i * 5.0, i * 5.0 }), 0, net, 0 }, i > 0 ? false : true);
            g.Connect({ line({ i * 5.0, 0, 0 }, { i * 5.0, 10, i * 5.0 }), 0, net, 1 }, i > 0 ? false : true);
        }
        f.Eval();
        const auto [ep, en] = f.Ok(edge) ? evalAt(edge, 0.5, 0.5) : std::pair{ Math::Point3{}, Math::Vec3{} };
        const auto [np, nn] = f.Ok(net) ? evalAt(net, 0.5, 0.5) : std::pair{ Math::Point3{}, Math::Vec3{} };
        Check(f.Ok(edge) && Near(ep.x, 5) && Near(ep.y, 5) && Near(ep.z, 2.5), "N8 edge surface: Coons saddle");
        Check(f.Ok(net) && Near(np.x, 5) && Near(np.y, 5) && Near(np.z, 2.5), "N8 network surface: Gordon saddle");

        // 拟合曲面：打乱顺序的 25 个马鞍面上的点，最近点距离接近 0
        const NodeId fit = Add(g, "surface.fit"), probe = Add(g, "surface.closest");
        for (int i = 0; i < 25; ++i)
        {
            const int j = (i * 7) % 25;
            const double x = (j % 5) * 2.5, y = (j / 5) * 2.5;
            const NodeId pt = Add(g, "vector.point");
            g.SetInputValue(pt, 0, x);
            g.SetInputValue(pt, 1, y);
            g.SetInputValue(pt, 2, 0.1 * x * y);
            g.Connect({ pt, 0, fit, 0 }, i == 0);
        }
        g.Connect({ fit, 0, probe, 0 });
        g.SetInputValue(probe, 1, Value(Math::Vec3{ 5, 5, 2.5 }));
        f.Eval();
        Check(f.Ok(fit) && f.Ok(probe) && f.Number(probe, 1) < 1e-3, "N8 fit surface: passes near the points");

        // 参数曲面：圆柱 x = 5cos(u)、y = 5sin(u)、z = v → 侧面 100π
        const NodeId param = Add(g, "surface.param");
        f.Eval();
        Check(f.Ok(param) && Near(area(param), 100 * kPi, 1e-2) && f.Count(param, 1) == 24 * 8, "N8 parametric surface: a cylinder");

        // 修剪与分割：10 × 10 的平面被上方 r 2 的圆投影切开
        const NodeId sq = Add(g, "curve.rectangle"), plate = Add(g, "surface.planar"), trim = Add(g, "surface.trim"), split = Add(g, "surface.split");
        g.SetInputValue(sq, 1, 10.0);
        g.SetInputValue(sq, 2, 10.0);
        g.Connect({ sq, 0, plate, 0 });
        const NodeId cutter = circle({ 0, 0, 1 }, 2);
        g.Connect({ plate, 0, trim, 0 });
        g.Connect({ cutter, 0, trim, 1 });
        g.Connect({ plate, 0, split, 0 });
        g.Connect({ cutter, 0, split, 1 });
        f.Eval();
        Check(f.Ok(trim) && Near(area(trim), 4 * kPi, 1e-2), "N8 trim: keep the side with the point");
        // 带洞的修剪面的面积 SmApiBrepComputeArea 算不准（差约 3%），这里按离散网格的面积检查
        auto meshArea = [&](const Solid::BodyPtr& b)
        {
            const Solid::Mesh m = f.kernel->Tessellate(b);
            double s = 0.0;
            for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
            {
                const Math::Vec3 u = m.Positions[m.Indices[i + 1]] - m.Positions[m.Indices[i]], w = m.Positions[m.Indices[i + 2]] - m.Positions[m.Indices[i]];
                s += 0.5 * Math::Vec3{ u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x }.Length();
            }
            return s;
        };
        const auto& pieces = g.GetNode(split)->Outputs[0];
        Check(f.Ok(split) && pieces.size() == 2 && Near(meshArea(pieces[0].AsBody()) + meshArea(pieces[1].AsBody()), 100.0, 1e-2),
              "N8 split: two pieces");

        // 炸开长方体 → 6 个面 → 拼接回实体；管状曲面封口成实体
        const NodeId box = Add(g, "solid.box"), explode = Add(g, "surface.explode"), join = Add(g, "surface.join");
        g.Connect({ box, 0, explode, 0 });
        g.Connect({ explode, 0, join, 0 });
        const NodeId tube = Add(g, "surface.extrude"), cap = Add(g, "surface.cap");
        g.Connect({ circle({}, 5), 0, tube, 0 });
        g.Connect({ tube, 0, cap, 0 });
        f.Eval();
        Check(f.Ok(explode) && f.Count(explode) == 6 && !f.kernel->IsSolid(f.Body(explode)), "N8 explode: a box gives six faces");
        Check(f.Ok(join) && f.kernel->IsSolid(f.Body(join)) && Near(f.Volume(join), 1000.0), "N8 join: six faces stitch into a solid");
        Check(f.Ok(cap) && f.kernel->IsSolid(f.Body(cap)) && Near(f.Volume(cap), 250 * kPi, 1e-2), "N8 cap: an open tube becomes a cylinder");

        // 翻转、延伸、重建：用点阵曲面（没有修剪的单个面）
        // 用 4 个角点构造 10 × 10 的平面点阵
        const NodeId flat = Add(g, "surface.points");
        for (int i = 0; i < 4; ++i)
        {
            const NodeId pt = Add(g, "vector.point");
            g.SetInputValue(pt, 0, (i % 2) * 10.0);
            g.SetInputValue(pt, 1, (i / 2) * 10.0);
            g.Connect({ pt, 0, flat, 0 }, i == 0);
        }
        g.SetInputValue(flat, 1, int64_t(2));
        const NodeId flip = Add(g, "surface.flip"), extend = Add(g, "surface.extend"), rebuild = Add(g, "surface.rebuild");
        g.Connect({ flat, 0, flip, 0 });
        g.Connect({ flat, 0, extend, 0 });
        g.Connect({ flat, 0, rebuild, 0 });
        f.Eval();
        const auto [fp0, fn0] = evalAt(flat, 0.5, 0.5);
        const auto [fp1, fn1] = f.Ok(flip) ? evalAt(flip, 0.5, 0.5) : std::pair{ Math::Point3{}, Math::Vec3{} };
        Check(f.Ok(flip) && std::abs(fn0.z) > 0.99 && fn0.z * fn1.z < 0, "N8 flip: the normal is reversed");
        Check(f.Ok(extend) && Near(area(extend), 144.0, 1e-2), "N8 extend: 10 × 10 grows to 12 × 12");
        Check(f.Ok(rebuild) && Near(area(rebuild), 100.0, 1e-2), "N8 rebuild: same shape");
        const NodeId flipTrimmed = Add(g, "surface.flip");
        g.Connect({ trim, 0, flipTrimmed, 0 });
        f.Eval();
        Check(f.State(flipTrimmed) == NodeState::Error, "N8 flip: trimmed faces are rejected");

        // 分解：长方体 6 面、12 条边、8 个顶点；边是精确曲线（总长 120）
        const NodeId dec = Add(g, "surface.deconstruct");
        g.Connect({ box, 0, dec, 0 });
        f.Eval();
        double edgeLength = 0.0;
        for (const auto& e : g.GetNode(dec)->Outputs[1])
            edgeLength += e.AsCurve3D()->Length();
        Check(f.Ok(dec) && f.Count(dec, 0) == 6 && f.Count(dec, 1) == 12 && f.Count(dec, 2) == 8 && Near(edgeLength, 120.0, 1e-3),
              "N8 deconstruct: faces, edges and vertices of a box");

        // 等参线：10 × 10 平面中间的 U 向线长 10
        const NodeId iso = Add(g, "surface.isocurve");
        g.Connect({ flat, 0, iso, 0 });
        f.Eval();
        Check(f.Ok(iso) && f.Count(iso) == 1 && Near(g.GetNode(iso)->Outputs[0][0].AsCurve3D()->Length(), 10.0, 1e-3), "N8 isocurve: across a plane");

        // 求交：竖直线穿过平面 → 1 个交点；球与平面 → 交线（圆，周长 2π·4）
        const NodeId hit = Add(g, "surface.intersect");
        g.Connect({ flat, 0, hit, 0 });
        g.Connect({ line({ 3, 4, -5 }, { 3, 4, 5 }), 0, hit, 1 });
        const NodeId sph = Add(g, "solid.sphere"), cut = Add(g, "surface.intersect"), big = Add(g, "surface.points");
        for (int i = 0; i < 4; ++i)
        {
            const NodeId pt = Add(g, "vector.point");
            g.SetInputValue(pt, 0, (i % 2) * 20.0 - 10.0);
            g.SetInputValue(pt, 1, (i / 2) * 20.0 - 10.0);
            g.SetInputValue(pt, 2, 3.0);
            g.Connect({ pt, 0, big, 0 }, i == 0);
        }
        g.SetInputValue(big, 1, int64_t(2));
        g.Connect({ sph, 0, cut, 0 });
        g.Connect({ big, 0, cut, 1 });
        f.Eval();
        const auto& hp = g.GetNode(hit)->Outputs[0];
        Check(f.Ok(hit) && hp.size() == 1 && Near(hp[0].AsVector().x, 3) && Near(hp[0].AsVector().z, 0), "N8 intersect: curve × surface");
        double loop = 0.0;
        for (const auto& c : g.GetNode(cut)->Outputs[1])
            loop += c.AsCurve3D()->Length();
        Check(f.Ok(cut) && Near(loop, 2 * kPi * 4, 1e-2), "N8 intersect: sphere × plane is a circle");

        // 曲率：半径 5 的球 |K| = 1/25、|H| = 1/5
        const NodeId curv = Add(g, "surface.curvature");
        g.Connect({ sph, 0, curv, 0 });
        g.SetInputValue(curv, 2, 0.3);
        f.Eval();
        Check(f.Ok(curv) && Near(std::abs(f.Number(curv, 0)), 0.04, 1e-3) && Near(std::abs(f.Number(curv, 1)), 0.2, 1e-3) &&
              Near(f.Number(curv, 2), f.Number(curv, 3), 1e-3), "N8 curvature: a sphere is umbilic with K = 1/r²");
    }
}
