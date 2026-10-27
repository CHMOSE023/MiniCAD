// NodeSolidTests：曲线修改（N6b）。
// 偏移、曲线圆角、分割、子曲线、延伸、连接、炸开、反转、投影、重建；水平面内的曲线实体结果还原成直线 / 圆弧 / 圆，
// 空间曲线按 NURBS 计算
// 实现：src/NodeGraph/BuiltinCurveModifyNodes.cpp、CurveOps.cpp
// 计划：docs/NodeGraph/PLAN.md 第 4.2 节“曲线/修改”、第 4.5 节
#include "NodeSolidCommon.h"

namespace NodeSolidTest
{
    // N6b：曲线修改——偏移、曲线圆角、分割、子曲线、延伸、连接、炸开、反转、投影、重建。
    // 水平面内的曲线实体与偏移命令同一套多段线算法，结果还原成直线 / 圆弧 / 圆；空间曲线按 NURBS 计算
    void TestN6bCurves()
    {
        Fixture f;
        Graph& g = f.g;
        auto out = [&](NodeId id, int port = 0) -> const DataList& { return g.GetNode(id)->Outputs[port]; };
        auto entity = [&](NodeId id, size_t i = 0, int port = 0) -> const Entity* { return i < out(id, port).size() ? out(id, port)[i].AsCurve().get() : nullptr; };
        auto nurbs = [&](NodeId id, size_t i = 0, int port = 0) -> const Solid::NurbsCurve* { return i < out(id, port).size() ? out(id, port)[i].AsCurve3D().get() : nullptr; };
        auto len = [&](NodeId id, size_t i = 0) -> double
        {
            const Value& v = out(id)[i];
            return v.Type() == DataType::Curve ? Nodes::Geo::Length(*v.AsCurve()) : v.AsCurve3D()->Length();
        };
        auto pt = [&](Math::Vec3 p)
        {
            const NodeId n = Add(g, "vector.point");
            g.SetInputValue(n, 0, p.x);
            g.SetInputValue(n, 1, p.y);
            g.SetInputValue(n, 2, p.z);
            return n;
        };
        auto line = [&](Math::Vec3 a, Math::Vec3 b)
        {
            const NodeId l = Add(g, "curve.line");
            g.SetInputValue(l, 0, Value(a));
            g.SetInputValue(l, 1, Value(b));
            return l;
        };
        auto ctrl = [&](std::vector<Math::Vec3> pts, int64_t degree)
        {
            const NodeId c = Add(g, "nurbs.control");
            for (size_t i = 0; i < pts.size(); ++i)
                g.Connect({ pt(pts[i]), 0, c, 0 }, i == 0);
            g.SetInputValue(c, 1, Value(degree));
            return c;
        };
        auto op = [&](const char* type, NodeId src, int port = 0)
        {
            const NodeId n = Add(g, type);
            g.Connect({ src, 0, n, port });
            return n;
        };
        auto isA = [](const Entity* e, auto tag) { return e && e->IsKindOf<std::remove_pointer_t<decltype(tag)>>(); };
        auto closePt = [](const Math::Point3& a, Math::Vec3 b) { return (a - Math::Point3{ b.x, b.y, b.z }).Length() < 1e-6; };

        // ── 偏移 ──
        // 矩形 20 × 10 向外偏移 1 → 22 × 12 的多段线（与偏移命令同一算法：尖角延伸）
        const NodeId rect = Add(g, "curve.rectangle");
        const NodeId offRect = op("curve.offset", rect);
        // 直线向左偏 2；圆向外偏 1；圆弧（逆时针）向左（圆心一侧）偏 1
        const NodeId offLine = op("curve.offset", line({ 0, 0, 0 }, { 10, 0, 0 }));
        g.SetInputValue(offLine, 1, 2.0);
        const NodeId offCircle = op("curve.offset", Add(g, "curve.circle"));
        const NodeId offArc = op("curve.offset", Add(g, "curve.arc"));
        f.Eval();
        const Entity* er = entity(offRect);
        Check(f.Ok(offRect) && isA(er, (PolylineEntity*)nullptr) &&
                  Near(std::abs(CurveOps::SignedArea(static_cast<const PolylineEntity*>(er)->GetPolyline())), 22 * 12),
              "N6b offset: rectangle outward like the OFFSET command");
        const Entity* el = entity(offLine);
        Check(isA(el, (LineEntity*)nullptr) && closePt(static_cast<const LineEntity*>(el)->GetLine().Start, { 0, 2, 0 }), "N6b offset: line to the left stays a line");
        const Entity* ec = entity(offCircle);
        Check(isA(ec, (CircleEntity*)nullptr) && Near(static_cast<const CircleEntity*>(ec)->GetCircle().Radius, 6.0), "N6b offset: circle outward stays a circle");
        const Entity* ea = entity(offArc);
        Check(isA(ea, (ArcEntity*)nullptr) && Near(static_cast<const ArcEntity*>(ea)->GetArc().Radius, 4.0), "N6b offset: arc to the left (toward the center)");

        // 竖直平面（XZ）内的空间曲线：经内核偏移，结果仍在该平面内，到原曲线的距离为 1
        const NodeId arch = ctrl({ { 0, 0, 0 }, { 5, 0, 5 }, { 10, 0, 0 } }, 2);
        const NodeId offArch = op("curve.offset", arch);
        f.Eval();
        bool inPlane = f.Ok(offArch) && nurbs(offArch);
        double worst = 0.0;
        if (inPlane)
        {
            const Solid::NurbsCurve& src = *nurbs(arch);
            for (const auto& p : nurbs(offArch)->Sample(8))
            {
                inPlane = inPlane && std::abs(p.y) < 1e-6;
                worst = std::max(worst, std::abs((src.PointAt(CurveOps::ClosestParam(src, p)) - p).Length() - 1.0));
            }
        }
        Check(inPlane && worst < 1e-3, "N6b offset: space curve in a vertical plane via the kernel");

        // ── 曲线圆角 ──
        // 矩形 20 × 10 倒 r 2 → 周长 60 − 16 + 4π，4 段圆弧；r 6 时短边容不下，4 个角都保持尖角并警告
        const NodeId fil = op("curve.fillet", rect);
        g.SetInputValue(fil, 1, 2.0);
        const NodeId filBig = op("curve.fillet", rect);
        g.SetInputValue(filBig, 1, 6.0);
        // 空间折线 (0,0,0)→(10,0,0)→(10,0,10)，r 2 → 16 + π
        const NodeId bent = ctrl({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 } }, 1);
        const NodeId fil3d = op("curve.fillet", bent);
        g.SetInputValue(fil3d, 1, 2.0);
        f.Eval();
        Check(f.Ok(fil) && Near(len(fil), 44 + 4 * kPi) && static_cast<const PolylineEntity*>(entity(fil))->GetPolyline().SegCount() == 8,
              "N6b fillet: rectangle corners");
        Check(f.State(filBig) == NodeState::Warning && Near(len(filBig), 60.0), "N6b fillet: radius too large keeps the corners");
        Check(f.Ok(fil3d) && Near(len(fil3d), 16 + kPi), "N6b fillet: space polyline");

        // ── 分割 ──
        // 圆在 (5,0) 与 (−5,0) 处分成两个半圆（圆弧实体）；直线在 t = 0.3 处分成 3 + 7；
        // 椭圆（空间曲线）被 x 轴切成两段，长度之和不变
        const NodeId splitCircle = op("curve.split", Add(g, "curve.circle"));
        g.Connect({ pt({ 5, 0, 0 }), 0, splitCircle, 2 });
        g.Connect({ pt({ -5, 0, 0 }), 0, splitCircle, 2 }, false);
        const NodeId splitLine = op("curve.split", line({ 0, 0, 0 }, { 10, 0, 0 }));
        const NodeId t03 = Add(g, "input.number");
        g.SetParam(t03, 0, 0.3);
        g.Connect({ t03, 0, splitLine, 1 });
        const NodeId ellipse = Add(g, "curve.ellipse");
        const NodeId splitEllipse = op("curve.split", ellipse);
        g.Connect({ line({ -20, 0, 0 }, { 20, 0, 0 }), 0, splitEllipse, 3 });
        f.Eval();
        Check(f.Ok(splitCircle) && out(splitCircle).size() == 2 && isA(entity(splitCircle, 0), (ArcEntity*)nullptr) && Near(len(splitCircle, 0), 5 * kPi) &&
                  Near(len(splitCircle, 1), 5 * kPi),
              "N6b split: circle at two points gives two arcs");
        Check(out(splitLine).size() == 2 && isA(entity(splitLine, 0), (LineEntity*)nullptr) && Near(len(splitLine, 0), 3.0) && Near(len(splitLine, 1), 7.0),
              "N6b split: line at t");
        Check(f.Ok(splitEllipse) && out(splitEllipse).size() == 2 && out(splitEllipse, 1).size() == 2 &&
                  Near(len(splitEllipse, 0) + len(splitEllipse, 1), nurbs(ellipse)->Length()) && Near(len(splitEllipse, 0), len(splitEllipse, 1)),
              "N6b split: closed space curve by a cutter");

        // ── 子曲线 ──
        // 直线 [0.2, 0.5] → 长 3；圆 [0.75, 0.25] 绕过接缝 → 半圆弧
        const NodeId subLine = op("curve.sub", line({ 0, 0, 0 }, { 10, 0, 0 }));
        g.SetInputValue(subLine, 1, Value(Nodes::Domain{ 0.2, 0.5 }));
        const NodeId subCircle = op("curve.sub", Add(g, "curve.circle"));
        g.SetInputValue(subCircle, 1, Value(Nodes::Domain{ 0.75, 0.25 }));
        const NodeId subEllipse = op("curve.sub", ellipse);
        g.SetInputValue(subEllipse, 1, Value(Nodes::Domain{ 0.75, 0.25 }));
        f.Eval();
        Check(f.Ok(subLine) && isA(entity(subLine), (LineEntity*)nullptr) && Near(len(subLine), 3.0), "N6b sub: part of a line");
        Check(f.Ok(subCircle) && isA(entity(subCircle), (ArcEntity*)nullptr) && Near(len(subCircle), 5 * kPi), "N6b sub: across the seam of a circle");
        Check(f.Ok(subEllipse) && Near(len(subEllipse), nurbs(ellipse)->Length() / 2), "N6b sub: across the seam of an ellipse");

        // ── 延伸 ──
        // 直线两端各延伸 2 / 3；90° 圆弧终点沿圆弧延伸 5π/2 到 180°；“直线”方式在圆弧末端接切线
        const NodeId extLine = op("curve.extend", line({ 0, 0, 0 }, { 10, 0, 0 }));
        g.SetInputValue(extLine, 1, 2.0);
        g.SetInputValue(extLine, 2, 3.0);
        const NodeId extArc = op("curve.extend", Add(g, "curve.arc"));
        g.SetInputValue(extArc, 2, 2.5 * kPi);
        const NodeId extArcLine = op("curve.extend", Add(g, "curve.arc"));
        g.SetParam(extArcLine, 0, int64_t(1));
        // 抛物线（二次）终点延伸 2：自然延伸长度增加 2，直线延伸终点沿端点切线
        const NodeId para = ctrl({ { 0, 0, 0 }, { 5, 5, 0 }, { 10, 0, 0 } }, 2);
        const NodeId extSmooth = op("curve.extend", para), extStraight = op("curve.extend", para);
        g.SetInputValue(extSmooth, 2, 2.0);
        g.SetInputValue(extStraight, 2, 2.0);
        g.SetParam(extStraight, 0, int64_t(1));
        f.Eval();
        const Entity* xl = entity(extLine);
        Check(f.Ok(extLine) && isA(xl, (LineEntity*)nullptr) && closePt(static_cast<const LineEntity*>(xl)->GetLine().Start, { -2, 0, 0 }) &&
                  closePt(static_cast<const LineEntity*>(xl)->GetLine().End, { 13, 0, 0 }),
              "N6b extend: line at both ends");
        const Entity* xa = entity(extArc);
        Check(f.Ok(extArc) && isA(xa, (ArcEntity*)nullptr) && Near(static_cast<const ArcEntity*>(xa)->GetArc().EndAngle, kPi), "N6b extend: arc along its circle");
        Check(f.Ok(extArcLine) && isA(entity(extArcLine), (PolylineEntity*)nullptr) && Near(len(extArcLine), 2.5 * kPi + 5.0), "N6b extend: tangent line at an arc end");
        const Solid::NurbsCurve* xs = nurbs(extStraight);
        const Math::Vec3 tanDir = Math::Vec3{ 5, -5, 0 }.Normalized();
        Check(f.Ok(extSmooth) && Near(len(extSmooth), nurbs(para)->Length() + 2.0) && xs &&
                  closePt(xs->PointAt(xs->EndParam()), Math::Vec3{ 10, 0, 0 } + tanDir * 2.0),
              "N6b extend: space curve smooth and straight");

        // ── 连接 ──
        // 三条直线（一条反向）连成闭合三角形多段线，面积 50；直线 + 二次曲线 → 空间曲线；二次 + 三次经内核升阶
        const NodeId joinTri = Add(g, "curve.join");
        g.Connect({ line({ 0, 0, 0 }, { 10, 0, 0 }), 0, joinTri, 0 });
        g.Connect({ line({ 0, 10, 0 }, { 10, 0, 0 }), 0, joinTri, 0 }, false);
        g.Connect({ line({ 0, 10, 0 }, { 0, 0, 0 }), 0, joinTri, 0 }, false);
        const NodeId cubic = ctrl({ { 10, 0, 0 }, { 12, -3, 0 }, { 15, 3, 0 }, { 20, 0, 0 } }, 3);
        const NodeId joinMixed = Add(g, "curve.join");
        g.Connect({ line({ -5, 0, 0 }, { 0, 0, 0 }), 0, joinMixed, 0 });
        g.Connect({ para, 0, joinMixed, 0 }, false);
        const NodeId joinKernel = Add(g, "curve.join");
        g.Connect({ cubic, 0, joinKernel, 0 });
        g.Connect({ para, 0, joinKernel, 0 }, false);
        f.Eval();
        const Entity* tri = entity(joinTri);
        Check(f.Ok(joinTri) && out(joinTri).size() == 1 && isA(tri, (PolylineEntity*)nullptr) &&
                  PolylineOps::IsRing(static_cast<const PolylineEntity*>(tri)->GetPolyline()) &&
                  Near(std::abs(CurveOps::SignedArea(static_cast<const PolylineEntity*>(tri)->GetPolyline())), 50.0),
              "N6b join: lines into a closed polyline");
        Check(f.Ok(joinMixed) && out(joinMixed).size() == 1 && Near(len(joinMixed), 5.0 + nurbs(para)->Length()), "N6b join: line + space curve");
        Check(f.Ok(joinKernel) && out(joinKernel).size() == 1 && nurbs(joinKernel)->Degree == 3 && Near(len(joinKernel), nurbs(para)->Length() + nurbs(cubic)->Length()),
              "N6b join: degree 2 + degree 3 via the kernel");

        // ── 炸开 ──
        // 矩形 → 4 条直线、4 个顶点；倒圆角后 → 4 直线 + 4 圆弧；空间圆角折线 → 3 段；椭圆不拆
        const NodeId expRect = op("curve.explode", rect), expFil = op("curve.explode", fil), expBent = op("curve.explode", fil3d);
        const NodeId expEllipse = op("curve.explode", ellipse);
        f.Eval();
        Check(f.Ok(expRect) && out(expRect).size() == 4 && out(expRect, 1).size() == 4 && isA(entity(expRect), (LineEntity*)nullptr), "N6b explode: rectangle");
        int arcs = 0;
        for (size_t i = 0; i < out(expFil).size(); ++i)
            arcs += isA(entity(expFil, i), (ArcEntity*)nullptr) ? 1 : 0;
        Check(out(expFil).size() == 8 && arcs == 4, "N6b explode: filleted rectangle into lines and arcs");
        Check(f.Ok(expBent) && out(expBent).size() == 3 && out(expBent, 1).size() == 4, "N6b explode: space curve at its kinks");
        Check(f.Ok(expEllipse) && out(expEllipse).size() == 1, "N6b explode: an ellipse has no breaks (curvature continuous)");

        // ── 反转 ──
        const NodeId revLine = op("curve.reverse", line({ 0, 0, 0 }, { 10, 0, 0 })), revPara = op("curve.reverse", para);
        f.Eval();
        Check(closePt(static_cast<const LineEntity*>(entity(revLine))->GetLine().Start, { 10, 0, 0 }) &&
                  closePt(nurbs(revPara)->PointAt(nurbs(revPara)->StartParam()), { 10, 0, 0 }),
              "N6b reverse: line and space curve");

        // ── 投影 ──
        // 竖直平面里的拱压到 XY 平面；z = 0 的直线投影到 z = 5 的水平面仍是直线；r 3 的圆沿 −Z 投到 r 5 的球上
        const NodeId flat = op("curve.project", arch);
        const NodeId up = op("curve.project", line({ 0, 0, 0 }, { 10, 0, 0 }));
        const NodeId plane5 = Add(g, "plane.make");
        g.SetInputValue(plane5, 0, Value(Math::Vec3{ 0, 0, 5 }));
        g.Connect({ plane5, 0, up, 1 });
        const NodeId ring = Add(g, "curve.circle");
        g.SetInputValue(ring, 0, Value(Math::Vec3{ 0, 0, 20 }));
        g.SetInputValue(ring, 1, 3.0);
        const NodeId onSphere = op("curve.project", ring);
        g.Connect({ Add(g, "solid.sphere"), 0, onSphere, 1 });
        f.Eval();
        bool flattened = f.Ok(flat) && nurbs(flat);
        if (flattened)
            for (const auto& p : nurbs(flat)->ControlPoints)
                flattened = flattened && std::abs(p.z) < 1e-12;
        Check(flattened, "N6b project: onto the XY plane");
        Check(f.Ok(up) && isA(entity(up), (LineEntity*)nullptr) && closePt(static_cast<const LineEntity*>(entity(up))->GetLine().End, { 10, 0, 5 }),
              "N6b project: line onto a horizontal plane stays a line");
        bool onBall = f.Ok(onSphere) && !out(onSphere).empty();
        for (size_t i = 0; onBall && i < out(onSphere).size(); ++i)
            for (const auto& p : nurbs(onSphere, i)->Sample(4))
                onBall = onBall && std::abs(Math::Vec3{ p.x, p.y, p.z }.Length() - 5.0) < 1e-3 && std::abs(std::hypot(p.x, p.y) - 3.0) < 1e-3;
        Check(onBall, "N6b project: circle onto a sphere via the kernel");

        // ── 重建 ──
        // 圆（转换为空间曲线）用 12 个控制点的三次曲线逼近，偏差小于半径的 1%；直线两点一次重建偏差为 0
        const NodeId rb = op("curve.rebuild", Add(g, "curve.circle"));
        g.SetInputValue(rb, 1, Value(int64_t(12)));
        const NodeId rbLine = op("curve.rebuild", line({ 0, 0, 0 }, { 10, 0, 0 }));
        g.SetInputValue(rbLine, 1, Value(int64_t(2)));
        g.SetInputValue(rbLine, 2, Value(int64_t(1)));
        f.Eval();
        Check(f.Ok(rb) && nurbs(rb)->ControlPoints.size() == 12 && f.Number(rb, 1) < 0.05 && nurbs(rb)->IsClosed(), "N6b rebuild: circle with 12 control points");
        Check(f.Ok(rbLine) && f.Number(rbLine, 1) < 1e-9, "N6b rebuild: a line stays exact");
    }
}
