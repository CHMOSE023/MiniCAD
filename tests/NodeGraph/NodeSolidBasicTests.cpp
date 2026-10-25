// NodeSolidTests：基础实体节点（N2）、N6 的内核部分、N11 后台求值。
// 基本体、拉伸（含圆角矩形，验证凸度方向）、旋转、扫掠、阵列 + 并集 / 差集 / 交集、圆角 / 倒角、实体信息、截面、变换、
// 预览（离散缓存、绘图区着色面）与烘焙；N6：引用图纸里的三维实体、定向 / 任意轴旋转 / 非均匀缩放 / 沿曲线阵列、平面预览；
// N11：工作线程求值实体节点图的副本、主线程同时用内核、求值中途取消
// 实现：src/NodeGraph/BuiltinSolidNodes.cpp、BuiltinGeometryNodes.cpp、GeometryOps.cpp、Preview.cpp、Graph.cpp（后台求值）
// 计划：docs/NodeGraph/PLAN.md 第 11 节（N2、N6、N11）、第 3.4 节（后台求值）
#include "NodeSolidCommon.h"

namespace NodeSolidTest
{
    void TestPrimitives()
    {
        Fixture f;
        const NodeId box = Add(f.g, "solid.box"), cyl = Add(f.g, "solid.cylinder"), sph = Add(f.g, "solid.sphere");
        f.Eval();
        Check(f.Ok(box) && Near(f.Volume(box), 1000.0), "box: 10 × 10 × 10");
        Check(Near(f.Volume(cyl), kPi * 25 * 10), "cylinder: r 5, h 10");
        Check(Near(f.Volume(sph), 4.0 / 3.0 * kPi * 125, 1e-2), "sphere: r 5");

        // 滑块驱动边长，信息节点
        const NodeId slider = Add(f.g, "input.number"), info = Add(f.g, "solid.info");
        f.g.SetParam(slider, 0, 4.0);
        f.g.Connect({ slider, 0, box, 3 });
        f.g.Connect({ box, 0, info, 0 });
        f.g.SetParam(box, 0, true);   // 居中
        f.Eval();
        const auto& outs = f.g.GetNode(info)->Outputs;
        Check(Near(outs[0][0].AsNumber(), 400.0) && outs[1][0].AsInteger() == 12, "info: volume and 12 edges");
        Check(Near(outs[2][0].AsVector().x, -5.0) && Near(outs[3][0].AsVector().z, 4.0), "box: centered base, height from the slider");

        f.g.SetInputValue(box, 1, -1.0);
        f.Eval();
        Check(f.g.GetNode(box)->State == NodeState::Error && f.g.GetNode(info)->State == NodeState::Warning, "box: negative size fails, downstream warns");
    }

    void TestExtrudeRevolveSweep()
    {
        Fixture f;
        const NodeId rect = Add(f.g, "curve.rectangle"), ext = Add(f.g, "solid.extrude");
        f.g.Connect({ rect, 0, ext, 0 });
        f.Eval();
        Check(Near(f.Volume(ext), 20 * 10 * 10), "extrude: rectangle 20 × 10 × height 10");
        f.g.SetInputValue(rect, 3, 3.0);   // 圆角 3：面积 = 200 − (4 − π)·9；凸度方向错了圆弧会凸到外侧
        f.Eval();
        Check(Near(f.Volume(ext), (200 - (4 - kPi) * 9) * 10), "extrude: rounded rectangle (bulge direction)");
        f.g.SetInputValue(ext, 1, -5.0);
        f.Eval();
        Check(Near(f.Volume(ext), (200 - (4 - kPi) * 9) * 5), "extrude: negative height");

        const NodeId poly = Add(f.g, "curve.polygon"), ext2 = Add(f.g, "solid.extrude");
        f.g.Connect({ poly, 0, ext2, 0 });
        f.Eval();
        Check(Near(f.Volume(ext2), 1.5 * std::sqrt(3.0) * 25 * 10), "extrude: hexagon");

        const NodeId line = Add(f.g, "curve.line"), bad = Add(f.g, "solid.extrude");
        f.g.Connect({ line, 0, bad, 0 });
        f.Eval();
        Check(f.g.GetNode(bad)->State == NodeState::Error, "extrude: open curve fails");

        // 椭圆节点输出空间曲线：10 × 5 的椭圆拉伸 10 → π·10·5·10
        const NodeId ell = Add(f.g, "curve.ellipse"), extE = Add(f.g, "solid.extrude");
        f.g.Connect({ ell, 0, extE, 0 });
        f.Eval();
        Check(f.Ok(extE) && Near(f.Volume(extE), kPi * 50 * 10), "extrude: ellipse (space curve)");
        // 椭圆实体（引用图纸里的椭圆）
        const NodeId extEE = Add(f.g, "solid.extrude");
        f.g.SetInputValue(extEE, 0, Value(CurvePtr(std::make_shared<EllipseEntity>(Object::InvalidID, Math::Point3{ 0, 0, 0 }, 10.0, 5.0))));
        f.Eval();
        Check(f.Ok(extEE) && Near(f.Volume(extEE), kPi * 50 * 10), "extrude: ellipse entity");
        // 闭合插值样条（圆上取 12 点）：体积接近圆柱
        const NodeId circ = Add(f.g, "curve.circle"), div = Add(f.g, "curve.divide"), interp = Add(f.g, "nurbs.interpolate"), extS = Add(f.g, "solid.extrude");
        f.g.SetInputValue(div, 1, int64_t(12));
        f.g.SetInputValue(interp, 1, true);
        f.g.Connect({ circ, 0, div, 0 });
        f.g.Connect({ div, 0, interp, 0 });
        f.g.Connect({ interp, 0, extS, 0 });
        f.Eval();
        Check(f.Ok(extS) && Near(f.Volume(extS), kPi * 25 * 10, 1e-2), "extrude: closed interpolated spline (space curve)");
        // 闭合的拟合点样条实体
        const NodeId extSE = Add(f.g, "solid.extrude");
        f.g.SetInputValue(extSE, 0, Value(CurvePtr(std::make_shared<SplineEntity>(Object::InvalidID,
            std::vector<Math::Point3>{ { 5, 0, 0 }, { 0, 5, 0 }, { -5, 0, 0 }, { 0, -5, 0 } }, SplineBoundary::Closed))));
        f.Eval();
        Check(f.Ok(extSE) && f.Volume(extSE) > 50 * 10 && f.Volume(extSE) < 100 * 10, "extrude: closed spline entity (between the diamond and its bounding square)");
        // 不闭合的多段线按首尾相连：10 × 10 的正方形少画最后一段
        const NodeId extP = Add(f.g, "solid.extrude");
        f.g.SetInputValue(extP, 0, Value(Geo::MakePolyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } }, {}, false)));
        f.Eval();
        Check(f.Ok(extP) && Near(f.Volume(extP), 1000.0), "extrude: open polyline is closed implicitly");

        // 旋转：x ∈ [5, 7]、y ∈ [0, 2] 的矩形绕 Y 轴一圈 → 圆环体积 = π(7² − 5²)·2
        const NodeId ring = Add(f.g, "curve.rectangle"), rev = Add(f.g, "solid.revolve");
        f.g.SetInputValue(ring, 0, Value(Math::Vec3{ 6, 1, 0 }));
        f.g.SetInputValue(ring, 1, 2.0);
        f.g.SetInputValue(ring, 2, 2.0);
        f.g.Connect({ ring, 0, rev, 0 });
        f.Eval();
        Check(f.Ok(rev) && Near(f.Volume(rev), kPi * (49 - 25) * 2), "revolve: ring around Y");

        // 扫掠：半径 1 的圆沿长 20 的直线
        const NodeId circle = Add(f.g, "curve.circle"), path = Add(f.g, "curve.line"), sweep = Add(f.g, "solid.sweep");
        f.g.SetInputValue(circle, 1, 1.0);
        f.g.SetInputValue(path, 1, Value(Math::Vec3{ 20, 0, 0 }));
        f.g.Connect({ circle, 0, sweep, 0 });
        f.g.Connect({ path, 0, sweep, 1 });
        f.Eval();
        Check(f.Ok(sweep) && Near(f.Volume(sweep), kPi * 20, 1e-2), "sweep: circle along a line");
    }

    void TestArraysAndBooleans()
    {
        Fixture f;
        // 5 个长方体沿 X 间隔 5（边长 10，相互重叠）→ 并集 = 30 × 10 × 10
        const NodeId box = Add(f.g, "solid.box"), arr = Add(f.g, "xform.lineararray"), uni = Add(f.g, "solid.union");
        f.g.SetInputValue(arr, 1, Value(Math::Vec3{ 5, 0, 0 }));
        f.g.Connect({ box, 0, arr, 0 });
        f.g.Connect({ arr, 0, uni, 0 });
        f.Eval();
        Check(f.Count(arr) == 5 && Near(f.Volume(arr), 5000.0), "array: 5 boxes");
        Check(f.Count(uni) == 1 && Near(f.Volume(uni), 30 * 10 * 10), "union: overlapping boxes merge to 30 × 10 × 10");

        // 差集：减去穿透的圆柱（底面中心 (15, 5, -1)，r 2，h 12）
        const NodeId cyl = Add(f.g, "solid.cylinder"), diff = Add(f.g, "solid.difference");
        f.g.SetInputValue(cyl, 0, Value(Math::Vec3{ 15, 5, -1 }));
        f.g.SetInputValue(cyl, 1, 2.0);
        f.g.SetInputValue(cyl, 2, 12.0);
        f.g.Connect({ uni, 0, diff, 0 });
        f.g.Connect({ cyl, 0, diff, 1 });
        f.Eval();
        Check(Near(f.Volume(diff), 3000 - kPi * 4 * 10, 1e-2), "difference: hole through the bar");

        // 参数改动沿依赖链传播：数量 5 → 3
        const uint64_t cylSolves = f.g.GetNode(cyl)->SolveCount;
        f.g.SetInputValue(arr, 2, int64_t(3));
        f.Eval();
        Check(Near(f.Volume(uni), 20 * 10 * 10) && Near(f.Volume(diff), 2000 - kPi * 4 * 10, 1e-2) && f.g.GetNode(cyl)->SolveCount == cylSolves,
              "propagation: changing the count re-solves the chain but not the cylinder");

        // 交集：两个错开 5 的长方体
        const NodeId b2 = Add(f.g, "solid.box"), mv = Add(f.g, "xform.move"), merge = Add(f.g, "list.merge"), inter = Add(f.g, "solid.intersection");
        f.g.SetInputValue(mv, 1, Value(Math::Vec3{ 5, 5, 0 }));
        f.g.Connect({ b2, 0, mv, 0 });
        f.g.Connect({ b2, 0, merge, 0 });
        f.g.Connect({ mv, 0, merge, 1 });
        f.g.Connect({ merge, 0, inter, 0 });
        f.Eval();
        Check(Near(f.Volume(inter), 5 * 5 * 10), "intersection: overlap of two shifted boxes");

        // 环形阵列 + 旋转（实体可绕 X 轴）
        const NodeId polar = Add(f.g, "xform.polararray"), rotX = Add(f.g, "xform.rotate");
        f.g.SetInputValue(polar, 1, Value(Math::Vec3{ -20, 0, 0 }));
        f.g.SetInputValue(polar, 2, int64_t(4));
        f.g.Connect({ b2, 0, polar, 0 });
        f.g.Connect({ b2, 0, rotX, 0 });
        f.g.SetParam(rotX, 0, int64_t(0));
        f.g.SetInputValue(rotX, 1, 90.0);
        f.Eval();
        Check(f.Count(polar) == 4 && Near(f.Volume(polar), 4000.0), "polar array: 4 solids");
        const AABB rb = f.kernel->Bounds(f.g.GetNode(rotX)->Outputs[0][0].AsBody());
        Check(f.Ok(rotX) && Near(rb.Min.y, -10.0) && Near(rb.Max.z, 10.0), "rotate: solid about X by 90°");
    }

    void TestFilletAndSection()
    {
        Fixture f;
        const NodeId box = Add(f.g, "solid.box"), fil = Add(f.g, "solid.fillet"), cham = Add(f.g, "solid.chamfer");
        f.g.Connect({ box, 0, fil, 0 });
        f.g.Connect({ box, 0, cham, 0 });
        f.Eval();
        Check(f.Ok(fil) && f.Volume(fil) < 1000.0 && f.Volume(fil) > 950.0, "fillet: all edges (r 1)");
        Check(f.Ok(cham) && f.Volume(cham) < f.Volume(fil), "chamfer: all edges removes more than the fillet");

        const NodeId edges = Add(f.g, "input.integer"), one = Add(f.g, "solid.fillet");
        f.g.SetParam(edges, 0, int64_t(0));
        f.g.Connect({ box, 0, one, 0 });
        f.g.Connect({ edges, 0, one, 2 });
        f.g.SetInputValue(one, 1, 2.0);
        f.Eval();
        Check(f.Ok(one) && Near(f.Volume(one), 1000 - (4 - kPi) * 10, 1e-2), "fillet: a single edge (r 2)");
        f.g.SetParam(edges, 2, int64_t(50));
        f.g.SetParam(edges, 0, int64_t(40));
        f.Eval();
        Check(f.g.GetNode(one)->State == NodeState::Error, "fillet: edge index out of range");

        const NodeId sec = Add(f.g, "solid.section"), len = Add(f.g, "curve.length");
        f.g.Connect({ box, 0, sec, 0 });
        f.g.Connect({ sec, 0, len, 0 });
        f.Eval();
        double total = 0.0;
        for (const auto& v : f.g.GetNode(len)->Outputs[0])
            total += v.AsNumber();
        Check(f.Count(sec) >= 1 && Near(total, 40.0), "section: horizontal section of the box has length 40");
    }

    void TestPreviewAndBake()
    {
        Solid::ISolidKernel* kernel = g_kernel;
        Document doc;
        Viewport viewport(800.0f, 600.0f);
        Editor   editor;
        editor.Bind(doc, viewport);
        editor.SetSolidKernel(kernel);

        Graph& g = doc.GetNodeGraph();
        const NodeId box = Add(g, "solid.box"), arr = Add(g, "xform.lineararray"), circle = Add(g, "curve.circle");
        g.SetInputValue(arr, 1, Value(Math::Vec3{ 15, 0, 0 }));
        g.SetInputValue(arr, 2, int64_t(3));
        g.Connect({ box, 0, arr, 0 });
        SolveContext ctx;
        ctx.Kernel = kernel;
        g.Evaluate(ctx);

        PreviewBuilder builder;
        PreviewSet set = builder.Build(g, {}, kernel);
        Check(set.Entities.size() == 4, "preview: 3 arrayed boxes + 1 circle (the box itself feeds the array)");
        const auto* first = static_cast<const Solid3DEntity*>(set.Entities[0].get());
        const void* meshData = &first->GetLocalMesh();

        // 只改圆的半径：实体没变，离散结果复用
        g.SetInputValue(circle, 1, 8.0);
        g.Evaluate(ctx);
        PreviewSet set2 = builder.Build(g, {}, kernel);
        Check(set2.Entities.size() == 4 && &static_cast<const Solid3DEntity*>(set2.Entities[0].get())->GetLocalMesh() == meshData,
              "preview: unchanged solids reuse their tessellation");

        editor.SetPreviewGeometry(set2.Entities, set2.Points, set2.HighlightPoints);
        editor.Render();
        Check(editor.GetPreviewEntityCount() == 4 && !editor.GetShadedVertices().empty() && !editor.GetSilhouetteVertices().empty(),
              "editor: preview solids are shaded and their lines drawn");
        AABB pb;
        Check(editor.GetPreviewBounds(pb) && Near(pb.Max.x, 40.0), "editor: preview bounds");
        Check(doc.GetScene().EntityCount() == 0, "editor: preview does not touch the scene");
        editor.ClearPreviewGeometry();
        editor.Render();
        Check(editor.GetShadedVertices().empty(), "editor: clearing the preview removes the shaded faces");

        const int n = Bake(g, {}, doc.GetScene(), doc.GetCommandStack(), kernel);
        int solids = 0;
        doc.GetScene().ForEachObject([&](const Object& o) { solids += o.IsKindOf<Solid3DEntity>() ? 1 : 0; });
        Check(n == 4 && solids == 3, "bake: solids and the circle become document entities");
        doc.Undo();
        Check(doc.GetScene().EntityCount() == 0, "bake: one undo step");
    }

    void TestN6WithKernel()
    {
        Fixture f;
        Graph& g = f.g;

        // 引用图纸里的三维实体（带变换矩阵）：体积与位置都是图纸里的样子
        Document doc;
        Scene& scene = doc.GetScene();
        const auto solidId = scene.NextObjectID();
        auto solid = Solid3DEntity::Create(solidId, *f.kernel, f.kernel->Box({ 0, 0, 0 }, 10, 10, 10));
        solid->ApplyTransform(Math::Mat4::Translation({ 100, 0, 0 }));
        scene.AddEntity(std::move(solid));
        f.ctx.DocScene     = &scene;
        f.ctx.SceneVersion = scene.GeometryVersion();
        const NodeId ref = Add(g, "doc.bodies"), info = Add(g, "solid.info");
        g.SetParam(ref, 0, std::to_string(solidId));
        g.Connect({ ref, 0, info, 0 });
        f.Eval();
        const AABB rb = f.Count(ref) == 1 ? f.kernel->Bounds(f.Body(ref)) : AABB::Empty();
        Check(f.Ok(ref) && Near(f.Volume(ref), 1000.0) && Near(rb.Min.x, 100.0) && Near(f.Number(info), 1000.0),
              "N6 reference: a drawing solid (with its transform) becomes a body");

        // 实体定向到竖直平面：体积不变，Z 方向的高度变成了宽度
        const NodeId box = Add(g, "solid.box"), orient = Add(g, "xform.orient"), target = Add(g, "plane.make");
        g.SetInputValue(box, 3, 2.0);   // 10 × 10 × 2
        g.Connect({ box, 0, orient, 0 });
        g.SetInputValue(target, 1, Value(Math::Vec3{ 1, 0, 0 }));   // 法向 +X 的竖直平面
        g.Connect({ target, 0, orient, 2 });
        f.Eval();
        const AABB ob = f.Count(orient) == 1 ? f.kernel->Bounds(f.Body(orient)) : AABB::Empty();
        Check(f.Ok(orient) && Near(f.Volume(orient), 200.0) && Near(ob.Max.x - ob.Min.x, 2.0) && Near(ob.Max.z - ob.Min.z, 10.0),
              "N6 orient: a slab laid onto a vertical plane");

        // 任意轴旋转与非均匀缩放
        const NodeId rot = Add(g, "xform.rotate"), nu = Add(g, "xform.scalenu");
        g.Connect({ box, 0, rot, 0 });
        g.SetParam(rot, 0, int64_t(3));                              // 自定义轴
        g.SetInputValue(rot, 1, 90.0);
        g.SetInputValue(rot, 3, Value(Math::Vec3{ 0, 1, 0 }));       // 绕 Y 轴 90°：Z 高度 2 变成 X 宽度
        g.Connect({ box, 0, nu, 0 });
        g.SetInputValue(nu, 2, Value(Math::Vec3{ 1, 3, 2 }));
        f.Eval();
        const AABB rb2 = f.Count(rot) == 1 ? f.kernel->Bounds(f.Body(rot)) : AABB::Empty();
        Check(f.Ok(rot) && Near(rb2.Max.x - rb2.Min.x, 2.0) && Near(f.Volume(rot), 200.0), "N6 rotate: a body about a custom axis");
        Check(f.Ok(nu) && Near(f.Volume(nu), 1200.0), "N6 scale: non-uniform scaling multiplies the volume by 1 × 3 × 2");

        // 沿曲线阵列实体
        const NodeId cyl = Add(g, "solid.cylinder"), circle = Add(g, "curve.circle"), arr = Add(g, "xform.curvearray");
        g.SetInputValue(cyl, 1, 1.0);
        g.SetInputValue(circle, 1, 30.0);
        g.Connect({ cyl, 0, arr, 0 });
        g.Connect({ circle, 0, arr, 1 });
        g.SetInputValue(arr, 2, int64_t(6));
        f.Eval();
        Check(f.Ok(arr) && f.Count(arr) == 6 && Near(f.Volume(arr), 6 * kPi * 10, 1e-2), "N6 array: bodies along a circle");

        // 平面预览：方框 4 条边 + 3 条轴；烘焙时跳过
        Graph pg;
        const NodeId pl = Add(pg, "plane.standard");
        pg.Evaluate(f.ctx);
        PreviewBuilder builder;
        const PreviewSet set = builder.Build(pg, {}, f.kernel);
        Check(set.Entities.size() == 7, "N6 preview: a plane shows a square and three axes");
        Check(Bake(pg, {}, scene, doc.GetCommandStack(), f.kernel) == 0, "N6 bake: planes are not baked");
        (void)pl;
    }

    // N11：后台求值。工作线程求值实体节点图的副本，同时主线程也在用内核（内核内部加锁轮流使用），
    // 实体最后在哪个线程上释放都可以；求值中途取消，算完的节点照样合并
    void TestN11Background()
    {
        Fixture f;
        const NodeId box = Add(f.g, "solid.box"), arr = Add(f.g, "xform.lineararray"), uni = Add(f.g, "solid.union");
        f.g.SetInputValue(arr, 1, Value(Math::Vec3{ 5, 0, 0 }));
        f.g.SetInputValue(arr, 2, int64_t(20));
        f.g.Connect({ box, 0, arr, 0 });
        f.g.Connect({ arr, 0, uni, 0 });
        auto copy = f.g.PrepareBackground(f.ctx);
        std::atomic<bool> done{ false };
        SolveContext wctx;
        wctx.Kernel = f.kernel;
        std::thread worker([&] { copy->Evaluate(wctx); done = true; });
        int mainCalls = 0;
        bool mainOk = true;
        while (!done || mainCalls < 5)
        {
            Solid::BodyPtr b = f.kernel->Box({ 0, 0, 0 }, 1, 2, 3);
            mainOk = mainOk && b && Near(f.kernel->Volume(b), 6.0);
            ++mainCalls;
        }
        worker.join();
        Check(mainOk && f.g.MergeEvaluated(*copy) == 3 && Near(f.Volume(uni), (10 + 19 * 5) * 100.0, 1e-3),
              "background: worker thread and main thread share the kernel");
        copy.reset();   // 副本里的实体在主线程上释放

        // 实体在工作线程上释放（最后一个引用）
        Solid::BodyPtr body = f.kernel->Box({ 0, 0, 0 }, 1, 1, 1);
        std::thread([b = std::move(body)]() mutable { b.reset(); }).join();

        // 求值中途取消：长方体算完后取消，阵列与并集保持为脏，之后同步补算
        f.g.SetInputValue(box, 1, 20.0);
        std::atomic<bool> cancel{ false };
        wctx.Cancel = &cancel;
        wctx.Solved = [&] { cancel = true; };
        copy = f.g.PrepareBackground(f.ctx);
        std::thread([&] { copy->Evaluate(wctx); }).join();
        Check(f.g.MergeEvaluated(*copy) == 1 && !f.g.GetNode(box)->Dirty && f.g.GetNode(uni)->Dirty,
              "background: cancelled after the first node, finished nodes merge");
        f.Eval();
        Check(Near(f.Volume(uni), (20 + 19 * 5) * 100.0, 1e-3), "background: the rest is solved afterwards");
    }
}
