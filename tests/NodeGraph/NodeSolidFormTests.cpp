// NodeSolidTests：实体形体（N7）。
// 五种形体（起点 / 终点、工作平面、部分角度、空间路径、融合与放样融合）、圆锥 / 圆环、空心、形体合并、剪切几何、
// 分割、抽壳、实心 / 空心切换、任意平面截面、旧图兼容；融合截面的起点对齐
// 实现：src/NodeGraph/BuiltinFormNodes.cpp（solid.* 形体节点）、GeometryOps.cpp
// 计划：docs/NodeGraph/PLAN.md 第 5 节
#include "NodeSolidCommon.h"

namespace NodeSolidTest
{
    // N7：五种形体（起点 / 终点、工作平面、部分角度、空间路径、融合与放样融合）、圆锥 / 圆环、空心、形体合并、剪切几何、
    // 分割、抽壳、实心 / 空心切换、任意平面截面、旧图兼容
    void TestN7Forms()
    {
        Fixture f;
        Graph& g = f.g;
        auto bounds = [&](NodeId id, int port = 0) { return f.Count(id, port) > 0 ? f.kernel->Bounds(f.Body(id, port)) : AABB::Empty(); };
        auto square = [&](double size)
        {
            const NodeId r = Add(g, "curve.rectangle");
            g.SetInputValue(r, 1, size);
            g.SetInputValue(r, 2, size);
            return r;
        };

        // 拉伸：起点 2、终点 7；放到法向 +X 的工作平面上
        const NodeId rect = Add(g, "curve.rectangle"), ext = Add(g, "solid.extrude"), vplane = Add(g, "plane.make");
        g.Connect({ rect, 0, ext, 0 });
        g.SetInputValue(ext, 1, 7.0);
        g.SetInputValue(ext, 2, 2.0);
        f.Eval();
        Check(f.Ok(ext) && Near(f.Volume(ext), 1000.0) && Near(bounds(ext).Min.z, 2.0) && Near(bounds(ext).Max.z, 7.0), "N7 extrude: from start 2 to end 7");
        g.SetInputValue(vplane, 1, Value(Math::Vec3{ 1, 0, 0 }));
        g.Connect({ vplane, 0, ext, 3 });
        f.Eval();
        Check(f.Ok(ext) && Near(f.Volume(ext), 1000.0) && Near(bounds(ext).Max.x - bounds(ext).Min.x, 5.0) && Near(bounds(ext).Min.x, 2.0),
              "N7 extrude: on a vertical work plane");

        // 融合：边长 10 → 5 的正方形，高 10 → 棱台 h/3·(A1 + A2 + √(A1·A2))
        const NodeId sq10 = square(10), sq5 = square(5), blend = Add(g, "solid.blend");
        g.Connect({ sq10, 0, blend, 0 });
        g.Connect({ sq5, 0, blend, 1 });
        f.Eval();
        Check(f.Ok(blend) && Near(f.Volume(blend), 10.0 / 3.0 * (100 + 25 + 50)) && Near(bounds(blend).Max.z, 10.0), "N7 blend: square frustum");
        const NodeId circ = Add(g, "curve.circle"), blend2 = Add(g, "solid.blend");
        g.Connect({ sq10, 0, blend2, 0 });
        g.Connect({ circ, 0, blend2, 1 });
        f.Eval();
        Check(f.Ok(blend2) && f.kernel->IsSolid(f.Body(blend2)) && f.Volume(blend2) > 0 && Near(bounds(blend2).Max.z, 10.0), "N7 blend: rectangle to circle");
        // 起点不同的同一个正方形（正四边形转 0° / 90°）：截面对齐后是直棱柱，扭了体积会变小
        const NodeId p0 = Add(g, "curve.polygon"), p90 = Add(g, "curve.polygon"), blend3 = Add(g, "solid.blend");
        for (NodeId p : { p0, p90 })
            g.SetInputValue(p, 2, 4.0);
        g.SetInputValue(p90, 3, 90.0);
        g.Connect({ p0, 0, blend3, 0 });
        g.Connect({ p90, 0, blend3, 1 });
        f.Eval();
        Check(f.Ok(blend3) && Near(f.Volume(blend3), 2 * 25 * 10), "N7 blend: same square with a different start vertex is a prism");

        // 旋转：从 90° 到 180°，体积为整圈的 1/4
        const NodeId ring = Add(g, "curve.rectangle"), rev = Add(g, "solid.revolve");
        g.SetInputValue(ring, 0, Value(Math::Vec3{ 6, 1, 0 }));
        g.SetInputValue(ring, 1, 2.0);
        g.SetInputValue(ring, 2, 2.0);
        g.Connect({ ring, 0, rev, 0 });
        g.SetInputValue(rev, 3, 180.0);
        g.SetInputValue(rev, 4, 90.0);
        f.Eval();
        const AABB rvb = bounds(rev);
        Check(f.Ok(rev) && Near(f.Volume(rev), kPi * (49 - 25) * 2 / 4), "N7 revolve: 90° to 180° about Y");
        // 轴线接直线：x = 2 处平行 Y 的线代替轴点、轴方向，截面到轴的距离 3 ~ 5
        const NodeId axisLine = Add(g, "curve.line"), rev2 = Add(g, "solid.revolve");
        g.SetInputValue(axisLine, 0, Value(Math::Vec3{ 2, 0, 0 }));
        g.SetInputValue(axisLine, 1, Value(Math::Vec3{ 2, 5, 0 }));
        g.Connect({ ring, 0, rev2, 0 });
        g.Connect({ axisLine, 0, rev2, 6 });
        f.Eval();
        Check(f.Ok(rev2) && Near(f.Volume(rev2), kPi * (25 - 9) * 2), "N7 revolve: axis from a line");

        // 放样：竖直路径、空间螺旋线、水平路径转 45°
        const NodeId c1 = Add(g, "curve.circle"), vline = Add(g, "curve.line"), sw = Add(g, "solid.sweep");
        g.SetInputValue(c1, 1, 1.0);
        g.SetInputValue(vline, 1, Value(Math::Vec3{ 0, 0, 20 }));
        g.Connect({ c1, 0, sw, 0 });
        g.Connect({ vline, 0, sw, 1 });
        f.Eval();
        Check(f.Ok(sw) && Near(f.Volume(sw), kPi * 20, 1e-2) && Near(bounds(sw).Max.z, 20.0), "N7 sweep: along a vertical line");
        const NodeId helix = Add(g, "nurbs.helix"), sw2 = Add(g, "solid.sweep");
        g.SetInputValue(helix, 1, 5.0);
        g.SetInputValue(helix, 2, 5.0);
        g.SetInputValue(helix, 3, 10.0);
        g.SetInputValue(helix, 4, 1.0);
        g.Connect({ c1, 0, sw2, 0 });
        g.Connect({ helix, 0, sw2, 1 });
        f.Eval();
        Check(f.Ok(sw2) && Near(f.Volume(sw2), kPi * std::hypot(10.0, 10 * kPi), 1e-2), "N7 sweep: along a space helix");
        const NodeId hline = Add(g, "curve.line"), sq2 = square(2), sw3 = Add(g, "solid.sweep");
        g.SetInputValue(hline, 1, Value(Math::Vec3{ 20, 0, 0 }));
        g.Connect({ sq2, 0, sw3, 0 });
        g.Connect({ hline, 0, sw3, 1 });
        g.SetInputValue(sw3, 2, 45.0);
        f.Eval();
        Check(f.Ok(sw3) && Near(f.Volume(sw3), 80.0, 1e-2) && Near(bounds(sw3).Max.z, std::sqrt(2.0), 1e-2), "N7 sweep: profile angle 45°");

        // 放样融合：直线路径上 10 → 5 的正方形（与融合相同），圆弧路径得到实体
        const NodeId sb = Add(g, "solid.sweptblend"), sb2 = Add(g, "solid.sweptblend"), arc = Add(g, "curve.arc");
        const NodeId vline10 = Add(g, "curve.line");
        g.SetInputValue(vline10, 1, Value(Math::Vec3{ 0, 0, 10 }));
        g.Connect({ vline10, 0, sb, 0 });
        g.Connect({ sq10, 0, sb, 1 });
        g.Connect({ sq5, 0, sb, 2 });
        g.SetInputValue(arc, 1, 30.0);
        g.Connect({ arc, 0, sb2, 0 });
        g.Connect({ sq5, 0, sb2, 1 });
        g.Connect({ sq2, 0, sb2, 2 });
        f.Eval();
        Check(f.Ok(sb) && Near(f.Volume(sb), 10.0 / 3.0 * (100 + 25 + 50)), "N7 swept blend: straight path equals a blend");
        Check(f.Ok(sb2) && f.kernel->IsSolid(f.Body(sb2)) && f.Volume(sb2) > 0, "N7 swept blend: along an arc");

        // 圆锥、圆环
        const NodeId cone = Add(g, "solid.cone"), torus = Add(g, "solid.torus");
        f.Eval();
        Check(f.Ok(cone) && Near(f.Volume(cone), kPi * 25 * 10 / 3, 1e-2), "N7 cone: r 5, h 10");
        Check(f.Ok(torus) && Near(f.Volume(torus), 2 * kPi * kPi * 10 * 4, 1e-2), "N7 torus: R 10, r 2");

        // 空心：形体合并 = 长方体 − 中间的空心圆柱；离得远的空心给出警告
        const NodeId box = Add(g, "solid.box"), hole = Add(g, "curve.circle"), vext = Add(g, "void.extrude"), comb = Add(g, "solid.combine");
        g.SetParam(box, 0, true);   // 居中
        g.SetInputValue(hole, 1, 2.0);
        g.Connect({ hole, 0, vext, 0 });
        g.Connect({ box, 0, comb, 0 });
        g.Connect({ vext, 0, comb, 0 }, false);
        f.Eval();
        Check(f.Ok(vext) && g.GetNode(vext)->Outputs[0][0].Void && Near(f.Volume(vext), kPi * 40), "N7 void: an extrusion marked as void");
        Check(f.Ok(comb) && Near(f.Volume(comb), 1000 - kPi * 40) && !g.GetNode(comb)->Outputs[0][0].Void, "N7 combine: solids minus voids");
        const NodeId far = Add(g, "void.extrude"), farCircle = Add(g, "curve.circle");
        g.SetInputValue(farCircle, 0, Value(Math::Vec3{ 100, 0, 0 }));
        g.Connect({ farCircle, 0, far, 0 });
        g.Connect({ far, 0, comb, 0 }, false);
        f.Eval();
        Check(f.State(comb) == NodeState::Warning && Near(f.Volume(comb), 1000 - kPi * 40), "N7 combine: a void that misses warns");

        // 空心预览为线框，不烘焙
        {
            Graph pg;
            const NodeId pc = Add(pg, "curve.circle"), pv = Add(pg, "void.extrude");
            pg.Connect({ pc, 0, pv, 0 });
            pg.Evaluate(f.ctx);
            PreviewBuilder builder;
            const PreviewSet set = builder.Build(pg, {}, f.kernel);
            bool wires = !set.Entities.empty();
            for (const auto& e : set.Entities)
                wires = wires && !e->IsKindOf<Solid3DEntity>();
            Check(wires, "N7 preview: a void is a wireframe");
            Document doc;
            int skipped = 0;
            Check(Bake(pg, {}, doc.GetScene(), doc.GetCommandStack(), f.kernel, &skipped) == 0 && skipped == 1, "N7 bake: voids are skipped");
        }

        // 剪切几何：切图纸里引用的实体
        {
            Document doc;
            Scene& scene = doc.GetScene();
            const auto id = scene.NextObjectID();
            scene.AddEntity(Solid3DEntity::Create(id, *f.kernel, f.kernel->Box({ -5, -5, 0 }, 10, 10, 10)));
            f.ctx.DocScene     = &scene;
            f.ctx.SceneVersion = scene.GeometryVersion();
            const NodeId ref = Add(g, "doc.bodies"), cut = Add(g, "solid.cut");
            g.SetParam(ref, 0, std::to_string(id));
            g.Connect({ ref, 0, cut, 0 });
            g.Connect({ vext, 0, cut, 1 });
            f.Eval();
            Check(f.Ok(cut) && Near(f.Volume(cut), 1000 - kPi * 40), "N7 cut: a void cuts a referenced drawing solid");
            g.RemoveNode(ref);
            g.RemoveNode(cut);
            f.ctx.DocScene = nullptr;
        }

        // 分割：z = 4 的平面把 10³ 长方体分成 600 + 400
        const NodeId split = Add(g, "solid.split"), zplane = Add(g, "plane.make");
        g.SetInputValue(zplane, 0, Value(Math::Vec3{ 0, 0, 4 }));
        g.Connect({ box, 0, split, 0 });
        g.Connect({ zplane, 0, split, 1 });
        f.Eval();
        Check(f.Ok(split) && Near(f.Volume(split, 0), 600.0) && Near(f.Volume(split, 1), 400.0), "N7 split: by a plane");

        // 抽壳：封闭空腔 1000 − 8³；去掉一个面后为开口的盒子 1000 − 8 × 8 × 9
        const NodeId shell = Add(g, "solid.shell");
        g.Connect({ box, 0, shell, 0 });
        f.Eval();
        Check(f.Ok(shell) && Near(f.Volume(shell), 488.0), "N7 shell: closed cavity");
        const NodeId faceId = Add(g, "input.integer");
        g.Connect({ faceId, 0, shell, 2 });
        f.Eval();
        Check(f.Ok(shell) && Near(f.Volume(shell), 424.0), "N7 shell: one face removed");

        // 实心 / 空心切换；布尔运算的角色取第一个输入的
        const NodeId role = Add(g, "solid.role"), uni = Add(g, "solid.union");
        g.Connect({ box, 0, role, 0 });
        g.Connect({ role, 0, uni, 0 });
        g.Connect({ cone, 0, uni, 0 }, false);
        f.Eval();
        Check(f.Ok(role) && g.GetNode(role)->Outputs[0][0].Void && g.GetNode(uni)->Outputs[0][0].Void, "N7 role: void flag, union keeps the first role");

        // 任意平面截面：x = 1 的竖直平面截长方体 → 一条闭合的空间折线，没有水平多段线
        const NodeId sec = Add(g, "solid.section"), xplane = Add(g, "plane.make");
        g.SetInputValue(xplane, 1, Value(Math::Vec3{ 1, 0, 0 }));
        g.Connect({ box, 0, sec, 0 });
        g.Connect({ xplane, 0, sec, 2 });
        g.SetInputValue(sec, 1, 1.0);
        f.Eval();
        const auto& secOut = g.GetNode(sec)->Outputs;
        bool onPlane = secOut[1].size() == 1;
        if (onPlane)
            for (const auto& p : secOut[1][0].AsCurve3D()->ControlPoints)
                onPlane = onPlane && std::abs(p.x - 1.0) < 1e-6;
        Check(f.Ok(sec) && secOut[0].empty() && onPlane, "N7 section: an arbitrary plane gives a space polyline");
        g.SetInputValue(sec, 1, 5.0);
        g.Disconnect({ xplane, 0, sec, 2 });
        f.Eval();
        Check(secOut[0].size() == 1 && secOut[1].size() == 1, "N7 section: horizontal plane still gives a polyline");

        // 旧图：拉伸只有“高度”，截面只有“高度”
        Graph old;
        const char* json = R"({"format":"MiniCAD-NodeGraph","version":1,"nodes":[
            {"id":1,"type":"curve.rectangle","x":0,"y":0,"preview":true,"inputs":[],"params":[]},
            {"id":2,"type":"solid.extrude","x":0,"y":0,"preview":true,"inputs":[{"t":"any"},{"t":"number","v":7.0}],"params":[]},
            {"id":3,"type":"solid.section","x":0,"y":0,"preview":true,"inputs":[{"t":"any"},{"t":"number","v":3.0}],"params":[]}],
            "links":[{"from":1,"fromPort":0,"to":2,"toPort":0},{"from":2,"fromPort":0,"to":3,"toPort":0}]})";
        Check(old.LoadFromString(json), "N7 old graph: loads");
        old.Evaluate(f.ctx);
        const Node* oe = old.GetNode(2);
        const Node* os = old.GetNode(3);
        Check(oe->State == NodeState::Ok && Near(f.kernel->Volume(oe->Outputs[0][0].AsBody()), 20 * 10 * 7) && os->Outputs[0].size() == 1,
              "N7 old graph: extrude height and horizontal section unchanged");
    }

    // 融合截面对齐：圆（从 0° 起）与正方形 / 正六边形（从别的角起）对齐后，同一参数处的两点方位角相差不大
    void TestMatchLoops()
    {
        const Solid::NurbsCurve circle = Solid::NurbsCurve::FromSegments({ { 10, 0, 0 }, { -10, 0, 0 } }, { 1.0, 1.0 }, true);
        const Solid::NurbsCurve square = Solid::NurbsCurve::FromSegments({ { -10, -10, 0 }, { 10, -10, 0 }, { 10, 10, 0 }, { -10, 10, 0 } }, {}, true);
        std::vector<Math::Point3> hex;
        for (int i = 0; i < 6; ++i)
            hex.push_back({ 5 * std::cos((i * 60 + 120) * kPi / 180), 5 * std::sin((i * 60 + 120) * kPi / 180), 0 });
        const Solid::NurbsCurve hexagon = Solid::NurbsCurve::FromSegments(hex, {}, true);
        auto worstAngle = [](Solid::NurbsCurve a, Solid::NurbsCurve b)
        {
            CurveOps::MatchLoops(a, b);
            double worst = 0.0;
            for (int i = 0; i < 200; ++i)
            {
                const double u = i / 200.0;
                const Math::Point3 p = a.PointAt(u), q = b.PointAt(u);
                double d = std::abs(std::atan2(p.y, p.x) - std::atan2(q.y, q.x));
                worst = std::max(worst, std::min(d, 2 * kPi - d));
            }
            return worst * 180 / kPi;
        };
        Check(worstAngle(circle, square) < 6.0, "blend sections: circle and square (start at a corner) line up");
        Check(worstAngle(square, circle) < 6.0, "blend sections: square and circle line up");
        Check(worstAngle(hexagon, square) < 16.0, "blend sections: hexagon and square line up");
    }
}
