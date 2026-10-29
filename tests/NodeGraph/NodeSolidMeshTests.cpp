// NodeSolidTests：网格（N9）与几何运算、转换（N10）。
// N9：网格的创建 / 修改 / 分析（不需要内核）、网格化与网格转实体（经内核）、预览、烘焙、读写、DWG / DXF 多面网格；
// N10：网格 → 曲线（截面、网格边、轮廓线）、网格求交、求交的退路与性能警告、第 7.2 节转换矩阵逐格、示例图
// 实现：src/NodeGraph/BuiltinMeshNodes.cpp（mesh.*）、MeshOps.cpp、MeshIntersect.cpp、Examples.cpp
// 计划：docs/NodeGraph/PLAN.md 第 7、8 节
#include "NodeSolidCommon.h"

namespace NodeSolidTest
{
    namespace
    {
        // 折线 / 空间曲线的总长
        double TotalLength(const DataList& list)
        {
            double len = 0.0;
            for (const auto& v : list)
                if (v.Type() == DataType::Curve3D && v.AsCurve3D())
                    len += v.AsCurve3D()->Length();
            return len;
        }

        const Node* FirstOfType(const Graph& g, const char* type)
        {
            for (const auto& n : g.GetNodes())
                if (n->TypeId == type)
                    return n.get();
            return nullptr;
        }
    }

    // N9：网格——创建 / 修改 / 分析在没有内核时也能用（网页版）；网格化、网格转实体经内核；预览、烘焙、读写、DWG / DXF 多面网格
    void TestN9Meshes()
    {
        Fixture f;
        f.ctx.Kernel = nullptr;   // 网格运算不依赖内核
        Graph& g = f.g;
        auto out = [&](NodeId id, int port = 0) -> const DataList& { return g.GetNode(id)->Outputs[port]; };
        auto mesh = [&](NodeId id, size_t i = 0) -> const Solid::Mesh* { return i < out(id).size() ? out(id)[i].AsMesh().get() : nullptr; };
        auto faces = [&](NodeId id, size_t i = 0) { return mesh(id, i) ? mesh(id, i)->TriangleCount() : size_t(0); };
        auto op = [&](const char* type, NodeId src, int port = 0)
        {
            const NodeId n = Add(g, type);
            g.Connect({ src, 0, n, port });
            return n;
        };
        auto pt = [&](double x, double y, double z = 0)
        {
            const NodeId n = Add(g, "vector.point");
            g.SetInputValue(n, 0, x);
            g.SetInputValue(n, 1, y);
            g.SetInputValue(n, 2, z);
            return n;
        };

        // ── 创建 ──
        const NodeId box = Add(g, "mesh.box"), box2 = Add(g, "mesh.box"), sphere = Add(g, "mesh.sphere"), plane = Add(g, "mesh.plane");
        g.SetInputValue(box2, 2, Value(int64_t(2)));
        const NodeId boxInfo = op("mesh.info", box), box2Info = op("mesh.info", box2), sphInfo = op("mesh.info", sphere), planeInfo = op("mesh.info", plane);
        f.Eval();
        Check(f.Ok(boxInfo) && out(boxInfo, 0)[0].AsInteger() == 8 && out(boxInfo, 1)[0].AsInteger() == 12 && out(boxInfo, 2)[0].AsBool() &&
                  Near(out(boxInfo, 4)[0].AsNumber(), 1000.0),
              "N9 box: 8 vertices, 12 faces, closed, volume 1000 (no kernel)");
        Check(out(box2Info, 0)[0].AsInteger() == 26 && out(box2Info, 1)[0].AsInteger() == 48 && Near(out(box2Info, 4)[0].AsNumber(), 1000.0),
              "N9 box: 2 segments per edge → 26 vertices, 48 faces");
        const double ball = 4.0 / 3.0 * kPi * 125;
        Check(out(sphInfo, 0)[0].AsInteger() == 24 * 11 + 2 && out(sphInfo, 1)[0].AsInteger() == 2 * 24 * 11 && out(sphInfo, 2)[0].AsBool() &&
                  out(sphInfo, 4)[0].AsNumber() < ball && out(sphInfo, 4)[0].AsNumber() > ball * 0.95,
              "N9 sphere: 266 vertices, 528 faces, closed, volume just under 4/3πr³");
        Check(out(planeInfo, 1)[0].AsInteger() == 200 && !out(planeInfo, 2)[0].AsBool() && Near(out(planeInfo, 3)[0].AsNumber(), 100.0),
              "N9 plane: 10 × 10 cells, open, area 100");

        // 构造网格：一个四边形 → 两个三角形，面积 6
        const NodeId cons = Add(g, "mesh.construct");
        const double quad[4][2] = { { 0, 0 }, { 3, 0 }, { 3, 2 }, { 0, 2 } };
        for (int i = 0; i < 4; ++i)
            g.Connect({ pt(quad[i][0], quad[i][1]), 0, cons, 0 }, i == 0);
        for (int i = 0; i < 4; ++i)
        {
            const NodeId k = Add(g, "input.integer");
            g.SetParam(k, 0, int64_t(i));
            g.Connect({ k, 0, cons, 1 }, i == 0);
        }
        g.SetParam(cons, 0, int64_t(1));
        const NodeId consInfo = op("mesh.info", cons);
        f.Eval();
        Check(f.Ok(cons) && faces(cons) == 2 && Near(out(consInfo, 3)[0].AsNumber(), 6.0), "N9 construct: a quad becomes two triangles");

        // Delaunay：伪随机 40 个点，三角形数 = 2n − 2 − h（h 为凸包顶点数）
        const NodeId del = Add(g, "mesh.delaunay");
        std::vector<std::pair<double, double>> pts;
        uint32_t seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / double(1 << 24); };
        for (int i = 0; i < 40; ++i)
        {
            pts.push_back({ rnd() * 100, rnd() * 100 });
            g.Connect({ pt(pts.back().first, pts.back().second), 0, del, 0 }, i == 0);
        }
        // 凸包（单调链）
        auto sorted = pts;
        std::sort(sorted.begin(), sorted.end());
        auto cross = [](auto o, auto a, auto b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
        std::vector<std::pair<double, double>> hull;
        for (int pass = 0; pass < 2; ++pass)
        {
            const size_t start = hull.size();
            for (const auto& p : sorted)
            {
                while (hull.size() >= start + 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
                    hull.pop_back();
                hull.push_back(p);
            }
            hull.pop_back();
            std::reverse(sorted.begin(), sorted.end());
        }
        f.Eval();
        Check(f.Ok(del) && faces(del) == 2 * 40 - 2 - hull.size(), "N9 delaunay: 2n − 2 − h triangles");
        bool up = f.Ok(del);
        for (size_t t = 0; up && t < faces(del); ++t)
            up = MeshOps::FaceNormal(*mesh(del), t).z > 0.99;
        Check(up, "N9 delaunay: triangles face the plane normal");

        // 闭合曲线网格：20 × 10 的矩形带 r 2 的圆洞 → 面积 200 − 4π（圆按折线采样，略大）
        const NodeId fill = Add(g, "mesh.fromcurve");
        g.Connect({ Add(g, "curve.rectangle"), 0, fill, 0 });
        const NodeId hole = Add(g, "curve.circle");
        g.SetInputValue(hole, 1, 2.0);
        g.Connect({ hole, 0, fill, 1 });
        const NodeId fillInfo = op("mesh.info", fill);
        f.Eval();
        Check(f.Ok(fill) && Near(out(fillInfo, 3)[0].AsNumber(), 200 - 4 * kPi, 5e-3), "N9 fromcurve: rectangle with a round hole");

        // ── 修改 ──
        // 两个相同的长方体合并后 16 个顶点，焊接回 8 个；平移后的长方体合并再拆分得到 2 块
        const NodeId join = Add(g, "mesh.join");
        g.Connect({ box, 0, join, 0 });
        g.Connect({ Add(g, "mesh.box"), 0, join, 0 }, false);
        const NodeId weld = op("mesh.weld", join);
        const NodeId moved = op("xform.move", box);
        g.SetInputValue(moved, 1, Value(Math::Vec3{ 20, 0, 0 }));
        const NodeId join2 = Add(g, "mesh.join");
        g.Connect({ box, 0, join2, 0 });
        g.Connect({ moved, 0, join2, 0 }, false);
        const NodeId split = op("mesh.split", join2);
        f.Eval();
        Check(mesh(join)->Positions.size() == 16 && f.Ok(weld) && mesh(weld)->Positions.size() == 8, "N9 weld: duplicate vertices merged");
        Check(f.Ok(split) && out(split).size() == 2 && faces(split, 0) == 12 && faces(split, 1) == 12, "N9 split: two disjoint boxes");

        // 翻转：体积变负；镜像变换后体积仍为正
        const NodeId flipInfo = op("mesh.info", op("mesh.flip", box));
        const NodeId mirror = op("xform.mirror", box);
        const NodeId mirrorInfo = op("mesh.info", mirror);
        f.Eval();
        Check(Near(out(flipInfo, 4)[0].AsNumber(), -1000.0) && Near(out(mirrorInfo, 4)[0].AsNumber(), 1000.0), "N9 flip and mirror orientation");

        // 细分：面数 ×4、仍封闭、体积变小；平滑：锁定边界的平面不变，球体积变小；简化到 100 面以内仍封闭
        const NodeId sub = op("mesh.subdivide", box);
        const NodeId subInfo = op("mesh.info", sub);
        const NodeId smoothPlane = op("mesh.smooth", plane), smoothBall = op("mesh.smooth", sphere);
        const NodeId spInfo = op("mesh.info", smoothPlane), sbInfo = op("mesh.info", smoothBall);
        const NodeId reduce = op("mesh.reduce", sphere);
        const NodeId redInfo = op("mesh.info", reduce);
        f.Eval();
        Check(f.Ok(sub) && faces(sub) == 48 && out(subInfo, 2)[0].AsBool() && out(subInfo, 4)[0].AsNumber() < 1000.0, "N9 subdivide: ×4 faces, closed, shrinks");
        Check(Near(out(spInfo, 3)[0].AsNumber(), 100.0) && out(sbInfo, 4)[0].AsNumber() < out(sphInfo, 4)[0].AsNumber(), "N9 smooth: locked plane unchanged, sphere shrinks");
        Check(faces(reduce) <= 100 && faces(reduce) > 50 && out(redInfo, 2)[0].AsBool() && Near(out(redInfo, 4)[0].AsNumber(), ball, 0.1),
              "N9 reduce: sphere to at most 100 faces, still closed");

        // ── 分析 ──
        const NodeId cl = op("mesh.closest", box);
        g.SetInputValue(cl, 1, Value(Math::Vec3{ 20, 0, 0 }));
        const NodeId dec = op("mesh.deconstruct", box);
        f.Eval();
        Check(f.Ok(cl) && Near(out(cl, 0)[0].AsVector().x, 5.0) && Near(out(cl, 1)[0].AsNumber(), 15.0), "N9 closest: point on the box face");
        Check(out(dec, 0).size() == 8 && out(dec, 1).size() == 36 && out(dec, 2).size() == 12, "N9 deconstruct: vertices, indices, normals");

        // 没有内核时：实体节点没有输出，网格化也就没有结果（不会崩溃）
        const NodeId noKernel = op("mesh.from", Add(g, "solid.box"));
        f.Eval();
        Check(f.State(noKernel) != NodeState::Ok && out(noKernel).empty(), "N9 mesh.from without the kernel gives nothing");

        // ── 内核：网格化、实体隐式转网格、网格转实体 ──
        f.ctx.Kernel = f.kernel;
        const NodeId sbox = Add(g, "solid.box");
        const NodeId tess = op("mesh.from", sbox);
        const NodeId tessInfo = op("mesh.info", tess);
        const NodeId implicitInfo = op("mesh.info", Add(g, "solid.sphere"));   // 实体直接连到网格端口
        const NodeId toBody = op("mesh.tobody", box2);
        const NodeId openBody = op("mesh.tobody", plane);
        f.Eval();
        Check(f.Ok(tess) && out(tessInfo, 2)[0].AsBool() && Near(out(tessInfo, 4)[0].AsNumber(), 1000.0), "N9 mesh.from: box tessellates to a closed mesh");
        Check(f.Ok(implicitInfo) && Near(out(implicitInfo, 4)[0].AsNumber(), ball, 0.03), "N9 body to mesh port: implicit tessellation");
        Check(f.Ok(toBody) && f.kernel->IsSolid(f.Body(toBody)) && Near(f.Volume(toBody), 1000.0), "N9 mesh.tobody: closed mesh becomes a solid");
        Check(f.Ok(openBody) && !f.kernel->IsSolid(f.Body(openBody)) && Near(f.kernel->Area(f.Body(openBody)), 100.0), "N9 mesh.tobody: open mesh becomes a surface");

        // ── 预览、烘焙、读写、DWG / DXF ──
        Document doc;
        Viewport viewport(800.0f, 600.0f);
        Editor   editor;
        editor.Bind(doc, viewport);
        Graph& dg = doc.GetNodeGraph();
        const NodeId dsph = Add(dg, "mesh.sphere");
        (void)dsph;
        SolveContext ctx;   // 没有内核
        dg.Evaluate(ctx);
        PreviewBuilder builder;
        PreviewSet set = builder.Build(dg, {}, nullptr);
        Check(set.Entities.size() == 1 && set.Entities[0]->IsKindOf<MeshEntity>(), "N9 preview: a mesh entity without the kernel");
        editor.SetPreviewGeometry(set.Entities, set.Points, set.HighlightPoints);
        editor.Render();
        Check(!editor.GetShadedVertices().empty(), "N9 preview: the mesh is shaded");

        const int baked = Bake(dg, {}, doc.GetScene(), doc.GetCommandStack(), nullptr);
        const MeshEntity* me = nullptr;
        doc.GetScene().ForEachObject([&](const Object& o) { if (o.IsKindOf<MeshEntity>()) me = static_cast<const MeshEntity*>(&o); });
        Check(baked == 1 && me && me->GetLocalMesh().TriangleCount() == 528 && me->GetLocalWire().size() == 528 * 3 / 2, "N9 bake: a mesh entity with every edge in its wireframe");

        const auto dir = std::filesystem::temp_directory_path() / "minicad_node_mesh";
        std::filesystem::create_directories(dir);
        for (const char* name : { "mesh.mcad", "mesh.json", "mesh.dxf", "mesh.dwg" })
        {
            const std::string path = (dir / name).string();
            const bool cad = std::string(name).ends_with(".dxf") || std::string(name).ends_with(".dwg");
            Check(cad ? doc.SaveToFile(path) : doc.SaveAs(path), name);
            Document back;
            Check(back.LoadFromFile(path), "load back");
            const MeshEntity* bm = nullptr;
            back.GetScene().ForEachObject([&](const Object& o) { if (o.IsKindOf<MeshEntity>()) bm = static_cast<const MeshEntity*>(&o); });
            const double vol = bm ? MeshOps::Volume(bm->ToWorldMesh()) : 0.0;
            Check(bm && bm->GetLocalMesh().TriangleCount() == 528 && Near(vol, out(sphInfo, 4)[0].AsNumber()),
                  cad ? "N9 DWG / DXF: the mesh round-trips as a polyface mesh" : "N9 round trip: the baked mesh keeps its triangles");
        }
        doc.Undo();
        Check(doc.GetScene().EntityCount() == 0, "N9 bake: one undo step");
    }

    // N10：网格 → 曲线（截面、网格边、轮廓线）、网格求交、求交的退路与性能警告、第 7.2 节转换矩阵逐格、示例图
    void TestN10()
    {
        Fixture f;
        Graph& g = f.g;
        auto out = [&](NodeId id, int port = 0) -> const DataList& { return g.GetNode(id)->Outputs[port]; };
        auto op = [&](const char* type, NodeId src, int port = 0, int fromPort = 0)
        {
            const NodeId n = Add(g, type);
            g.Connect({ src, fromPort, n, port });
            return n;
        };
        auto closedCount = [&](NodeId id, int port)
        {
            size_t n = 0;
            for (const auto& v : out(id, port))
                n += v.AsCurve3D() && v.AsCurve3D()->IsClosed() ? 1 : 0;
            return n;
        };

        // ── 网格 → 曲线（不需要内核）──
        f.ctx.Kernel = nullptr;
        const NodeId box = Add(g, "mesh.box"), sphere = Add(g, "mesh.sphere"), plane = Add(g, "mesh.plane");
        const NodeId secBox = op("mesh.section", box), secTilt = op("mesh.section", sphere);
        g.SetInputValue(secBox, 1, 0.0);
        g.SetInputValue(secTilt, 1, 0.0);
        g.SetInputValue(secTilt, 2, Value(*Plane::FromNormal({ 0, 0, 0 }, { 1, 1, 1 })));
        const NodeId boundary = op("mesh.edges", plane), crease = op("mesh.edges", box), all = op("mesh.edges", box);
        g.SetParam(crease, 0, int64_t(1));
        g.SetParam(all, 0, int64_t(2));
        const NodeId outline = op("mesh.outline", sphere);
        f.Eval();
        Check(f.Ok(secBox) && out(secBox, 1).size() == 1 && closedCount(secBox, 1) == 1 && Near(TotalLength(out(secBox, 1)), 40.0) &&
                  out(secBox, 0).size() == 1,
              "N10 mesh.section: a box cut at mid height is one closed square (also as a polyline)");
        Check(f.Ok(secTilt) && closedCount(secTilt, 1) == 1 && out(secTilt, 0).empty() && Near(TotalLength(out(secTilt, 1)), 2 * kPi * 5, 0.02),
              "N10 mesh.section: a tilted plane through the sphere centre is a great circle (no polyline output)");
        Check(f.Ok(boundary) && out(boundary).size() == 1 && closedCount(boundary, 0) == 1 && Near(TotalLength(out(boundary)), 40.0),
              "N10 mesh.edges: the boundary of the 10 × 10 plane mesh");
        Check(f.Ok(crease) && out(crease).size() == 12 && Near(TotalLength(out(crease)), 120.0), "N10 mesh.edges: 12 crease edges of the box");
        Check(f.Ok(all) && Near(TotalLength(out(all)), 120.0 + 6 * 10 * std::sqrt(2.0)), "N10 mesh.edges: every edge incl. face diagonals");
        Check(f.Ok(outline) && out(outline).size() == 1 && closedCount(outline, 0) == 1 && Near(TotalLength(out(outline)), 24 * 10 * std::sin(kPi / 24)),
              "N10 mesh.outline: the sphere seen from above outlines at the equator");

        // 网格求交：盒子与平移 (3, 4, 2.5) 的盒子 → 一圈交线，长 2 × (30 − 3 − 4 − 2.5)；直线 → 两个交点；平面 → 截面；实体 → 没有内核时报错
        const NodeId moved = op("xform.move", box);
        g.SetInputValue(moved, 1, Value(Math::Vec3{ 3, 4, 2.5 }));
        const NodeId mm = op("mesh.intersect", box);
        g.Connect({ moved, 0, mm, 1 });
        const NodeId line = Add(g, "curve.line");
        g.SetInputValue(line, 0, Value(Math::Vec3{ -20, 0.3, 0.2 }));
        g.SetInputValue(line, 1, Value(Math::Vec3{ 20, 0.3, 0.2 }));
        const NodeId ml = op("mesh.intersect", box);
        g.Connect({ line, 0, ml, 1 });
        const NodeId mp = op("mesh.intersect", box);
        g.SetInputValue(mp, 1, Value(Plane{}));
        f.Eval();
        Check(f.Ok(mm) && out(mm, 1).size() == 1 && closedCount(mm, 1) == 1 && Near(TotalLength(out(mm, 1)), 41.0),
              "N10 mesh.intersect: two overlapping boxes meet in one closed loop");
        Check(f.Ok(ml) && out(ml, 0).size() == 2 && Near(std::abs(out(ml, 0)[0].AsVector().x), 5.0) && out(ml, 0)[0].AsVector().x < 0,
              "N10 mesh.intersect: a line pierces the box twice, in order along the line");
        Check(f.Ok(mp) && closedCount(mp, 1) == 1 && Near(TotalLength(out(mp, 1)), 40.0), "N10 mesh.intersect: a plane gives the section");

        // ── 有内核 ──
        f.ctx.Kernel = f.kernel;
        // 曲面求交：球与长方体（顶面在球心高度）交于赤道；“网格近似”与精确结果一致
        const NodeId ball = Add(g, "solid.sphere"), slab = Add(g, "solid.box");
        g.SetInputValue(slab, 0, Value(Math::Vec3{ -10, -10, -10 }));
        g.SetInputValue(slab, 1, 20.0);
        g.SetInputValue(slab, 2, 20.0);
        g.SetInputValue(slab, 3, 10.0);
        const NodeId exact = op("surface.intersect", ball), approx = op("surface.intersect", ball);
        g.Connect({ slab, 0, exact, 1 });
        g.Connect({ slab, 0, approx, 1 });
        g.SetParam(approx, 0, int64_t(1));
        const NodeId pierceExact = op("surface.intersect", ball), pierceApprox = op("surface.intersect", ball);
        for (NodeId n : { pierceExact, pierceApprox })
            g.Connect({ line, 0, n, 1 });
        g.SetParam(pierceApprox, 0, int64_t(1));
        f.Eval();
        Check(f.Ok(exact) && Near(TotalLength(out(exact, 1)), 2 * kPi * 5, 1e-3), "N10 surface.intersect exact: the equator");
        Check(f.Ok(approx) && closedCount(approx, 1) == 1 && Near(TotalLength(out(approx, 1)), 2 * kPi * 5, 5e-3),
              "N10 surface.intersect mesh approximation: one closed loop close to the equator");
        Check(f.Ok(pierceExact) && f.Ok(pierceApprox) && out(pierceExact, 0).size() == 2 && out(pierceApprox, 0).size() == 2 &&
                  Near(std::abs(out(pierceApprox, 0)[0].AsVector().x), std::sqrt(25 - 0.09 - 0.04), 1e-2),
              "N10 surface.intersect: a line pierces the sphere twice (exact and mesh)");

        // 隐式转换：实体连到网格节点（截面、轮廓线）
        const NodeId bodySec = op("mesh.section", ball);
        g.SetInputValue(bodySec, 1, 0.0);
        f.Eval();
        Check(f.Ok(bodySec) && closedCount(bodySec, 1) == 1 && Near(TotalLength(out(bodySec, 1)), 2 * kPi * 5, 0.02),
              "N10 a solid on a mesh port is tessellated implicitly (section)");

        // 性能警告：面数超过 2000 的多面实体参与布尔
        const NodeId fine = Add(g, "mesh.sphere");
        g.SetInputValue(fine, 2, Value(int64_t(48)));
        g.SetInputValue(fine, 3, Value(int64_t(24)));
        const NodeId poly = op("mesh.tobody", fine);
        const NodeId uni = Add(g, "solid.union");
        g.Connect({ poly, 0, uni, 0 });
        const NodeId small = Add(g, "solid.box");
        g.SetInputValue(small, 0, Value(Math::Vec3{ 2, 2, 2 }));
        g.Connect({ small, 0, uni, 0 }, false);
        f.Eval();
        const Node* un = g.GetNode(uni);
        Check(f.Ok(poly) && un->State == NodeState::Warning && un->Message.find("个面") != std::string::npos,
              "N10 boolean on a 2208-face polyhedral solid warns about speed");
        std::printf("  (mesh.tobody %.0f ms, union %.0f ms)\n", g.GetNode(poly)->Millis, un->Millis);

        // ── 第 7.2 节转换矩阵：每一格都有节点或隐式转换 ──
        {
            Fixture m;
            Graph& h = m.g;
            auto o = [&](NodeId id, int port = 0) -> const DataList& { return h.GetNode(id)->Outputs[port]; };
            auto conv = [&](const char* type, NodeId src, int fromPort = 0)
            {
                const NodeId n = Add(h, type);
                h.Connect({ src, fromPort, n, 0 });
                return n;
            };
            auto isType = [&](NodeId id, int port, DataType t) { return h.GetNode(id)->State != NodeState::Error && !o(id, port).empty() && o(id, port)[0].Type() == t; };
            auto solid = [&](NodeId id) { return isType(id, 0, DataType::Body) && m.kernel->IsSolid(o(id)[0].AsBody()); };
            auto surface = [&](NodeId id) { return isType(id, 0, DataType::Body) && !m.kernel->IsSolid(o(id)[0].AsBody()); };

            const NodeId circle = Add(h, "curve.circle");
            const NodeId c2c = conv("curve.info", circle);            // 曲线 → 空间曲线（隐式）
            const NodeId c2s = conv("surface.extrude", circle);       // 曲线 → 曲面
            const NodeId c2b = conv("solid.extrude", circle);         // 曲线 → 实体
            const NodeId c2m = conv("mesh.fromcurve", circle);        // 曲线 → 网格
            const NodeId s2c = conv("surface.isocurve", c2s);         // 曲面 → 曲线
            const NodeId s2s = conv("surface.offset", c2s);           // 曲面 → 曲面
            const NodeId s2b = conv("surface.thicken", c2s);          // 曲面 → 实体
            const NodeId s2m = conv("mesh.info", c2s);                // 曲面 → 网格（隐式）
            const NodeId b2c = conv("solid.section", c2b);            // 实体 → 曲线
            const NodeId b2s = conv("surface.explode", c2b);          // 实体 → 曲面
            const NodeId b2m = conv("mesh.from", c2b);                // 实体 → 网格
            const NodeId m2c = conv("mesh.edges", c2m);               // 网格 → 曲线（N10）
            const NodeId m2s = conv("mesh.tobody", c2m);              // 网格 → 曲面（开放网格）
            const NodeId m2b = conv("mesh.tobody", b2m);              // 网格 → 实体（封闭网格）
            m.Eval();
            Check(isType(c2c, 0, DataType::Boolean), "N10 matrix: curve → curve (implicit to Curve3D)");
            Check(surface(c2s) && solid(c2b) && isType(c2m, 0, DataType::Mesh), "N10 matrix: curve → surface / solid / mesh");
            Check(isType(s2c, 0, DataType::Curve3D) && surface(s2s) && solid(s2b) && isType(s2m, 0, DataType::Integer),
                  "N10 matrix: surface → curve / surface / solid / mesh (implicit)");
            Check(isType(b2c, 1, DataType::Curve3D) && surface(b2s) && isType(b2m, 0, DataType::Mesh), "N10 matrix: solid → curve / surface / mesh");
            Check(isType(m2c, 0, DataType::Curve3D) && surface(m2s) && solid(m2b), "N10 matrix: mesh → curve / surface / solid");
        }

        // ── 示例图：assets/nodes/examples 里的节点图文件，按索引逐个插入空图，求值后检查结果 ──
        const std::string exampleDir = std::string(MINICAD_ASSET_DIR) + "/nodes/examples";
        auto example = [&](const char* file) { return ReadTextFile(exampleDir + "/" + file); };
        const auto examples = ReadExampleIndex(exampleDir);
        Check(examples.size() == 9, "examples: index lists 9 example files");
        for (const auto& ex : examples)
        {
            Fixture e;
            if (!ex.NeedsKernel)
                e.ctx.Kernel = nullptr;   // 不需要内核的示例在网页版也能用
            const auto ids = e.g.PasteFromString(ReadTextFile(ex.Path), 0, 0);
            e.Eval();
            int errors = 0;
            for (const auto& n : e.g.GetNodes())
                if (n->State == NodeState::Error)
                {
                    std::printf("  %s: %s — %s\n", ex.Name.c_str(), n->Title().c_str(), n->Message.c_str());
                    ++errors;
                }
            Check(!ids.empty() && errors == 0, ("N10 example evaluates without errors: " + ex.Name).c_str());
        }
        {
            // 网格截面与求交：球与长方体一圈交线、7 圈等高线、轮廓线（没有内核）
            Fixture e;
            e.ctx.Kernel = nullptr;
            e.g.PasteFromString(example("mesh-curves.mcnode"), 0, 0);
            e.Eval();
            const Node* cut = FirstOfType(e.g, "mesh.intersect");
            const Node* contours = FirstOfType(e.g, "mesh.section");
            const Node* outline = FirstOfType(e.g, "mesh.outline");
            Check(cut && cut->Outputs[1].size() == 1 && cut->Outputs[1][0].AsCurve3D()->IsClosed() && contours && contours->Outputs[1].size() == 7 &&
                      outline && !outline->Outputs[0].empty(),
                  "N10 example mesh curves: one intersection loop, 7 contours, an outline (no kernel)");
        }
        {
            // 演示：曲线（没有内核）——3 条偏移线，外圆 18 等分的小圆与放射线；边数改 8 后变成 24 个
            Fixture e;
            e.ctx.Kernel = nullptr;
            e.g.PasteFromString(example("curves.mcnode"), 0, 0);
            e.Eval();
            const Node* offset = FirstOfType(e.g, "curve.offset");
            const Node* rays = FirstOfType(e.g, "curve.line");
            const Node* sides = FirstOfType(e.g, "input.integer");
            Check(offset && offset->Outputs[0].size() == 3 && rays && rays->Outputs[0].size() == 18, "example curve demo: 3 offsets, 18 rays");
            if (sides)
                e.g.SetParam(sides->Id, 0, Value(int64_t(8)));
            e.Eval();
            Check(rays && rays->Outputs[0].size() == 24, "example curve demo: the sides slider drives the division");
        }
        {
            // 演示：曲面——花瓶、波浪面、碗都是曲面（不封闭）
            Fixture e;
            e.g.PasteFromString(example("surfaces.mcnode"), 0, 0);
            e.Eval();
            int surfaces = 0;
            for (const char* type : { "surface.loft", "surface.param", "surface.revolve" })
                if (const Node* n = FirstOfType(e.g, type); n && !n->Outputs[0].empty() && !e.kernel->IsSolid(n->Outputs[0][0].AsBody()))
                    ++surfaces;
            Check(surfaces == 3, "example surface demo: vase, wave and bowl surfaces");
        }
        {
            // 演示：实体——法兰座是实体，体积小于没打孔的本体；孔数滑块改 8 后体积变小
            Fixture e;
            e.g.PasteFromString(example("solids.mcnode"), 0, 0);
            e.Eval();
            const Node* body = FirstOfType(e.g, "solid.union");
            const Node* flange = FirstOfType(e.g, "solid.difference");
            auto volume = [&](const Node* n) { return n && !n->Outputs[0].empty() ? e.kernel->Volume(n->Outputs[0][0].AsBody()) : 0.0; };
            const double v6 = volume(flange);
            Check(v6 > 0 && e.kernel->IsSolid(flange->Outputs[0][0].AsBody()) && v6 < volume(body) - 6 * kPi * 9 * 8 * 0.9,
                  "example solid demo: flange is a solid with the holes cut");
            if (const Node* holes = FirstOfType(e.g, "input.integer"))
                e.g.SetParam(holes->Id, 0, Value(int64_t(8)));
            e.Eval();
            Check(Near(v6 - volume(flange), 2 * kPi * 9 * 8, 0.01), "example solid demo: the hole count slider adds two holes");
        }
        {
            // 演示：网格（没有内核）——16 × 16 点的三角网 450 个三角形；光顺后的块仍封闭
            Fixture e;
            e.ctx.Kernel = nullptr;
            e.g.PasteFromString(example("meshes.mcnode"), 0, 0);
            e.Eval();
            const Node* terrain = FirstOfType(e.g, "mesh.delaunay");
            const Node* smooth = FirstOfType(e.g, "mesh.smooth");
            Check(terrain && terrain->Outputs[0].size() == 1 && terrain->Outputs[0][0].AsMesh()->TriangleCount() == 450 && smooth &&
                      smooth->Outputs[0].size() == 1 && MeshOps::IsClosed(*smooth->Outputs[0][0].AsMesh()),
                  "example mesh demo: terrain triangulation and a closed smoothed block");
        }
        {
            // 窗框：体积 = 厚 × (宽 × 高 − (宽 − 2 框宽) × (高 − 3 框宽))；改宽度后重新计算
            Fixture e;
            e.g.PasteFromString(example("window.mcnode"), 0, 0);
            e.Eval();
            const Node* frame = FirstOfType(e.g, "solid.combine");   // 最终结果是叶子输出（预览、烘焙）
            auto volume = [&] { return frame && !frame->Outputs[0].empty() ? e.kernel->Volume(frame->Outputs[0][0].AsBody()) : 0.0; };
            Check(Near(volume(), 80.0 * (1200 * 1500 - 1080.0 * 1320)),
                  "N10 example window: frame volume from the parameters");
            for (NodeId id : e.g.ParameterNodes())
                if (e.g.GetNode(id)->Params[0].AsText() == "宽")
                    e.g.SetParam(id, 2, 1000.0);
            e.Eval();
            Check(Near(volume(), 80.0 * (1000 * 1500 - 880.0 * 1320)),
                  "N10 example window: changing the width parameter updates the frame");
        }
        {
            // 曲面屋顶：加厚后的实体减去天窗，仍是实体、体积略小
            Fixture e;
            e.g.PasteFromString(example("roof.mcnode"), 0, 0);
            e.Eval();
            const Node* thick = FirstOfType(e.g, "surface.thicken");
            const Node* roof = FirstOfType(e.g, "solid.difference");
            const double before = thick && !thick->Outputs[0].empty() ? e.kernel->Volume(thick->Outputs[0][0].AsBody()) : 0.0;
            const double after = roof && !roof->Outputs[0].empty() ? e.kernel->Volume(roof->Outputs[0][0].AsBody()) : 0.0;
            Check(before > 20 * 12 * 0.3 && e.kernel->IsSolid(roof->Outputs[0][0].AsBody()) && after < before - kPi * 1.5 * 1.5 * 0.29 &&
                      after > before - kPi * 1.5 * 1.5 * 0.4,
                  "N10 example roof: thickened network surface minus the skylight");
        }
        {
            // 千厮门大桥：塔身是开了洞的实体；两片桁架各 90 根腹杆、上下弦各一根；单索面 10 对 20 根拉索，拉索对数改 8 后 16 根
            Fixture e;
            e.g.PasteFromString(example("qiansimen-bridge.mcnode"), 0, 0);
            e.Eval();
            const Node* outline = FirstOfType(e.g, "solid.extrude");
            const Node* tower = FirstOfType(e.g, "solid.combine");
            const Node* truss = FirstOfType(e.g, "xform.lineararray");
            const Node* cables = FirstOfType(e.g, "surface.pipe");
            const double full = outline && !outline->Outputs[0].empty() ? e.kernel->Volume(outline->Outputs[0][0].AsBody()) : 0.0;
            const double cut = tower && !tower->Outputs[0].empty() ? e.kernel->Volume(tower->Outputs[0][0].AsBody()) : 0.0;
            Check(cut > 0 && e.kernel->IsSolid(tower->Outputs[0][0].AsBody()) && cut < full * 0.85, "example bridge: the tower is a solid with the eye cut");
            Check(truss && truss->Outputs[0].size() == 2 * (90 + 2) && cables && cables->Outputs[0].size() == 20,
                  "example bridge: two truss planes and 20 stay cables");
            for (const auto& n : e.g.GetNodes())
                if (n->Type && n->Type->Id == "input.param" && n->Params[0].AsText() == "拉索对数")
                    e.g.SetParam(n->Id, 2, Value(8.0));
            e.Eval();
            Check(cables && cables->Outputs[0].size() == 16, "example bridge: the cable count parameter drives the cables");
        }
        {
            // 网格化导出：封闭网格 + 6 圈等高线；烘焙后导出 DXF，读回仍是同样面数的网格
            Document doc;
            Graph& dg = doc.GetNodeGraph();
            dg.PasteFromString(example("mesh-export.mcnode"), 0, 0);
            SolveContext ctx;
            ctx.Kernel = f.kernel;
            dg.Evaluate(ctx);
            const Node* mesh = FirstOfType(dg, "mesh.from");
            const Node* contours = FirstOfType(dg, "mesh.section");
            size_t loops = 0;
            for (const auto& v : contours->Outputs[1])
                loops += v.AsCurve3D()->IsClosed() ? 1 : 0;
            const Solid::Mesh* m = mesh && !mesh->Outputs[0].empty() ? mesh->Outputs[0][0].AsMesh().get() : nullptr;
            Check(m && MeshOps::IsClosed(*m) && loops == 6 && contours->Outputs[0].size() == 6, "N10 example mesh export: closed mesh and 6 contours");
            const int baked = Bake(dg, {}, doc.GetScene(), doc.GetCommandStack(), f.kernel);
            size_t faces = 0;
            doc.GetScene().ForEachObject([&](const Object& o) { if (o.IsKindOf<MeshEntity>()) faces = static_cast<const MeshEntity&>(o).GetLocalMesh().TriangleCount(); });
            const auto path = (std::filesystem::temp_directory_path() / "minicad_node_example.dxf").string();
            Check(m && baked == 1 + 12 && faces == m->TriangleCount() && doc.SaveToFile(path),
                  "N10 example mesh export: bake the mesh and 6 contours (polyline + spline each), save DXF");
            Document back;
            size_t backFaces = 0;
            if (back.LoadFromFile(path))
                back.GetScene().ForEachObject([&](const Object& o) { if (o.IsKindOf<MeshEntity>()) backFaces = static_cast<const MeshEntity&>(o).GetLocalMesh().TriangleCount(); });
            Check(faces > 0 && backFaces == faces, "N10 example mesh export: the DXF reads back as the same polyface mesh");
        }
    }
}
