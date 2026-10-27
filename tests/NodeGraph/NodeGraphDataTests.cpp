// NodeGraphTests：不需要实体内核的节点（N2 曲线部分、N6）。
// 曲线 / 变换节点与没有内核时实体节点的报错、表达式、数学 / 列表 / 向量 / 平面 / 区间等数据节点
// 实现：src/NodeGraph/BuiltinGeometryNodes.cpp、BuiltinCurveXformNodes.cpp、BuiltinMathNodes.cpp、BuiltinDataNodes.cpp、
// Expression.cpp
// 计划：docs/NodeGraph/PLAN.md 第 4.2 节
#include "NodeGraphTestCommon.h"

namespace NodeGraphTest
{
    void TestCurvesWithoutKernel()
    {
        Graph g;
        const NodeId rect = Add(g, "curve.rectangle"), len = Add(g, "curve.length");
        g.Connect({ rect, 0, len, 0 });
        g.Evaluate({});
        Check(Near(Num(g, len), 60.0, 1e-6), "curve: rectangle 20 × 10 has perimeter 60");
        g.SetInputValue(rect, 3, 2.0);   // 圆角 2：周长 = 60 − 8·2 + 2π·2
        g.Evaluate({});
        Check(Near(Num(g, len), 60.0 - 16.0 + 4.0 * std::numbers::pi, 1e-2), "curve: rounded rectangle perimeter");
        g.SetInputValue(rect, 3, 6.0);
        g.Evaluate({});
        Check(g.GetNode(rect)->State == NodeState::Error, "curve: fillet radius larger than half the short side fails");

        const NodeId circle = Add(g, "curve.circle"), div = Add(g, "curve.divide");
        g.SetInputValue(div, 1, int64_t(4));
        g.Connect({ circle, 0, div, 0 });
        g.Evaluate({});
        const auto& pts = Out(g, div);
        Check(pts.size() == 4 && Near(pts[0].AsVector().x, 5.0, 1e-6) && Near(pts[1].AsVector().y, 5.0, 1e-2),
              "curve: dividing a closed circle into 4 gives 4 points");

        const NodeId line = Add(g, "curve.line"), div2 = Add(g, "curve.divide");
        g.SetInputValue(div2, 1, int64_t(5));
        g.Connect({ line, 0, div2, 0 });
        g.Evaluate({});
        Check(Out(g, div2).size() == 6 && Near(Out(g, div2)[5].AsVector().x, 10.0, 1e-9), "curve: dividing an open line into 5 gives 6 points");

        // 变换与阵列：曲线、点
        const NodeId move = Add(g, "xform.move"), arr = Add(g, "xform.polararray");
        g.Connect({ circle, 0, move, 0 });
        g.SetInputValue(move, 1, Value(Math::Vec3{ 0, 0, 5 }));
        g.Connect({ div2, 0, arr, 0 });
        g.SetInputValue(arr, 2, int64_t(4));
        g.Evaluate({});
        const auto& moved = Out(g, move);
        Check(moved.size() == 1 && moved[0].AsCurve()->IsKindOf<CircleEntity>() &&
              Near(static_cast<const CircleEntity&>(*moved[0].AsCurve()).GetCircle().Center.z, 5.0), "transform: move a circle up");
        Check(Out(g, arr).size() == 24, "transform: polar array of 6 points × 4 copies");
        const Value& last = Out(g, arr).back();   // 第 6 个点 (10, 0) 转 270°
        Check(last.Type() == DataType::Vector && Near(last.AsVector().x, 0.0, 1e-9) && Near(last.AsVector().y, -10.0, 1e-9),
              "transform: polar array rotates about Z");

        // 曲线绕 X 轴旋转：转出水平面，变成空间曲线（N6 之前报错）
        const NodeId rot = Add(g, "xform.rotate");
        g.Connect({ circle, 0, rot, 0 });
        g.SetParam(rot, 0, int64_t(0));   // 绕 X 轴 45°
        g.Evaluate({});
        {
            const auto& out = Out(g, rot);
            double maxZ = 0.0;
            if (out.size() == 1 && out[0].Type() == DataType::Curve3D)
                for (const auto& p : out[0].AsCurve3D()->Sample())
                    maxZ = std::max(maxZ, std::abs(p.z));
            Check(out.size() == 1 && out[0].Type() == DataType::Curve3D && Near(out[0].AsCurve3D()->Length(), 2 * std::numbers::pi * 5, 1e-3) &&
                  Near(maxZ, 5 * std::sin(std::numbers::pi / 4), 1e-3), "transform: rotating a curve about X gives a spatial curve");
        }
        g.SetParam(rot, 0, int64_t(2));   // 绕 Z：仍是曲线实体
        g.Evaluate({});
        Check(Out(g, rot).size() == 1 && Out(g, rot)[0].Type() == DataType::Curve, "transform: rotating about Z keeps the curve entity");

        // 没有内核时实体节点报错
        const NodeId box = Add(g, "solid.box");
        g.Evaluate({});
        Check(g.GetNode(box)->State == NodeState::Error && g.GetNode(box)->Message.find("内核") != std::string::npos,
              "solid: without a kernel solid nodes report it");
    }

    // 表达式（docs/NodeGraph/PLAN.md 第 4.2 节）
    void TestExpression()
    {
        auto eval = [](const char* text, double& out, std::string& error)
        {
            const auto e = Expression::Parse(text, error);
            return e && e->Evaluate([](const std::string& name, double& v)
            {
                if (name == "a") { v = 3; return true; }
                if (name == "宽度") { v = 600; return true; }
                return false;
            }, out, error);
        };
        double v = 0;
        std::string err;
        Check(eval("1 + 2 * 3", v, err) && Near(v, 7), "expr: precedence");
        Check(eval("-2^2", v, err) && Near(v, -4) && eval("2^3^2", v, err) && Near(v, 512), "expr: power binds tighter than minus, right assoc");
        Check(eval("(a + 1) * 2", v, err) && Near(v, 8), "expr: variables and parentheses");
        Check(eval("宽度 / 2 + 100", v, err) && Near(v, 400), "expr: Chinese variable names");
        Check(eval("（a ＋ 1）", v, err) == false, "expr: full-width plus is not an operator");
        Check(eval("（a + 1）× 2 ÷ 4", v, err) && Near(v, 2), "expr: full-width parentheses, × and ÷");
        Check(eval("if(a > 2, 10, 20)", v, err) && Near(v, 10) && eval("if(a = 2, 1 / 0, 5)", v, err) && Near(v, 5),
              "expr: if evaluates only the chosen branch");
        Check(eval("sin(30) + cos(60) + round(2.5) + roundup(1.2) + rounddown(-1.2)", v, err) && Near(v, 1 + 3 + 2 - 2, 1e-12), "expr: functions in degrees");
        Check(eval("max(1, a, 2) + min(4, 5) + 7 % 3 + abs(-1) + sqrt(16) + pi", v, err) && Near(v, 3 + 4 + 1 + 1 + 4 + std::numbers::pi),
              "expr: min / max / mod / abs / sqrt / pi");
        Check(eval("and(1, not(0)) + or(0, 0) + (2 <> 3) + (2 <= 2)", v, err) && Near(v, 3), "expr: logic and comparisons");
        Check(!eval("1 / 0", v, err) && err.find("0") != std::string::npos, "expr: division by zero");
        Check(!eval("b + 1", v, err) && err.find("b") != std::string::npos, "expr: unknown variable");
        Check(!eval("(1 + 2", v, err) && err.find("第 7 个字符") != std::string::npos, "expr: error position (missing parenthesis)");
        Check(!eval("1 + 2)", v, err) && err.find("第 6 个字符") != std::string::npos, "expr: error position (extra parenthesis)");
        Check(!eval("foo(1)", v, err) && err.find("foo") != std::string::npos, "expr: unknown function");
        const auto e = Expression::Parse("a * b + a", err);
        Check(e && e->Variables() == std::vector<std::string>{ "a", "b" }, "expr: variables listed once in order");
    }

    // N6：区间、平面类型与数学 / 列表 / 向量 / 平面 / 曲线 / 变换节点（不需要内核）
    void TestDataNodes()
    {
        // 类型转换与读写
        Value out;
        Check(Convert(Value(5.0), DataType::Domain, out) && Near(out.AsDomain().Max, 5.0) && Near(out.AsDomain().Min, 0.0), "types: number → domain [0, x]");
        Check(Convert(Value(Math::Vec3{ 1, 2, 3 }), DataType::Plane, out) && Near(out.AsPlane().Origin.z, 3.0) && Near(out.AsPlane().Normal().z, 1.0),
              "types: point → XY plane through it");
        Check(Convert(Value(Plane{ { 4, 5, 6 } }), DataType::Vector, out) && Near(out.AsVector().y, 5.0), "types: plane → its origin");
        Check(CanConvert(DataType::Plane, DataType::Geometry) && !CanConvert(DataType::Domain, DataType::Number), "types: plane is geometry, domain is not a number");

        Graph g;
        auto val = [&](NodeId id, int port = 0, size_t i = 0) -> Value { const auto& l = Out(g, id, port); return i < l.size() ? l[i] : Value(); };

        // 数学
        const NodeId mod = Add(g, "math.mod"), rnd = Add(g, "math.round"), expr = Add(g, "math.expression"), cmp = Add(g, "math.compare");
        g.SetInputValue(mod, 0, -1.0);
        g.SetInputValue(mod, 1, 3.0);
        g.SetInputValue(rnd, 0, -2.5);
        g.SetInputValue(expr, 0, 2.0);
        g.SetInputValue(expr, 1, 3.0);
        g.SetParam(expr, 0, std::string("a ^ 2 + b * 10"));
        g.SetInputValue(cmp, 0, 1.0);
        g.SetInputValue(cmp, 1, 1.0 + 1e-12);
        g.Evaluate({});
        Check(Near(Num(g, mod), 2.0) && val(rnd).Type() == DataType::Integer && val(rnd).AsInteger() == -3 && Near(Num(g, expr), 34.0) &&
              val(cmp).AsBool(), "math: mod, round half away from zero, expression, compare with tolerance");
        g.SetParam(expr, 0, std::string("a + (b"));
        g.Evaluate({});
        Check(g.GetNode(expr)->State == NodeState::Error && g.GetNode(expr)->Message.find("第") != std::string::npos, "math: expression syntax error is reported");

        const NodeId remap = Add(g, "math.remap"), parts = Add(g, "math.domainparts"), dom = Add(g, "math.domain");
        g.SetInputValue(remap, 0, 0.25);                                  // [0, 1] → [0, 10]
        g.SetInputValue(dom, 0, 2.0);
        g.SetInputValue(dom, 1, 7.0);
        g.Connect({ dom, 0, parts, 0 });
        g.Evaluate({});
        Check(Near(Num(g, remap), 2.5) && Near(Num(g, parts, 2), 5.0), "math: remap and domain parts");

        const NodeId iff = Add(g, "math.if"), atan = Add(g, "math.atan2");
        g.SetInputValue(iff, 0, false);
        g.SetInputValue(iff, 1, std::string("yes"));
        g.SetInputValue(iff, 2, std::string("no"));
        g.Evaluate({});
        Check(val(iff).Type() == DataType::Text && val(iff).AsText() == "no" && Near(Num(g, atan), 45.0), "math: if picks a branch, atan2 in degrees");

        // 列表
        const NodeId series = Add(g, "math.series"), slice = Add(g, "list.slice"), shift = Add(g, "list.shift");
        const NodeId cross = Add(g, "list.cross"), filt = Add(g, "list.filter"), sort = Add(g, "list.sort");
        g.SetInputValue(series, 2, int64_t(5));   // 0 1 2 3 4
        for (NodeId n : { slice, shift, filt })
            g.Connect({ series, 0, n, 0 });
        g.SetInputValue(slice, 1, int64_t(-3));    // 2 3 4
        g.SetInputValue(slice, 2, int64_t(2));     // 2 3
        g.SetInputValue(shift, 1, int64_t(2));     // 2 3 4 0 1
        g.SetInputValue(filt, 1, true);
        g.Connect({ series, 0, cross, 0 });
        g.Connect({ slice, 0, cross, 1 });
        g.Evaluate({});
        Check(Out(g, slice).size() == 2 && Near(Num(g, slice), 2.0) && Near(Num(g, slice, 0, 1), 3.0), "list: slice from the end");
        Check(Out(g, shift).size() == 5 && Near(Num(g, shift), 2.0) && Near(Num(g, shift, 0, 4), 1.0), "list: shift wraps");
        Check(Out(g, cross, 0).size() == 10 && Near(Num(g, cross, 0, 1), 0.0) && Near(Num(g, cross, 1, 1), 3.0) && Near(Num(g, cross, 0, 2), 1.0),
              "list: cross reference (A outer)");
        Check(Out(g, filt, 0).size() == 5 && Out(g, filt, 1).empty(), "list: filter with an all-true pattern");
        const NodeId keys = Add(g, "math.random");
        g.SetInputValue(keys, 0, int64_t(6));
        g.Connect({ keys, 0, sort, 0 });
        g.Connect({ keys, 0, sort, 1 });
        g.Evaluate({});
        bool ascending = Out(g, sort, 0).size() == 6 && Out(g, sort, 1).size() == 6;
        for (size_t i = 1; ascending && i < 6; ++i)
            ascending = Num(g, sort, 0, i - 1) <= Num(g, sort, 0, i) && Near(Num(g, sort, 0, i), Num(g, sort, 1, i));
        Check(ascending, "list: sort keys and values together");

        // 向量与平面
        const NodeId ang = Add(g, "vector.angle"), crs = Add(g, "vector.cross");
        g.SetInputValue(ang, 1, Value(Math::Vec3{ -1, 1, 0 }));
        g.Evaluate({});
        Check(Near(Num(g, ang), 135.0, 1e-9) && Near(val(crs).AsVector().z, 1.0), "vector: angle and cross product");

        const NodeId p3 = Add(g, "plane.points"), coords = Add(g, "plane.coords"), back = Add(g, "plane.tocoords"), std3 = Add(g, "plane.standard");
        g.SetInputValue(p3, 0, Value(Math::Vec3{ 0, 0, 5 }));
        g.SetInputValue(p3, 1, Value(Math::Vec3{ 0, 1, 5 }));    // X 轴沿 +Y
        g.SetInputValue(p3, 2, Value(Math::Vec3{ -1, 0, 5 }));   // Y 轴沿 −X，法向 +Z
        g.Connect({ p3, 0, coords, 0 });
        g.SetInputValue(coords, 1, Value(Math::Vec3{ 2, 3, 4 }));
        g.Connect({ p3, 0, back, 0 });
        g.Connect({ coords, 0, back, 1 });
        g.SetParam(std3, 0, int64_t(2));   // XZ 平面：法向 −Y
        g.Evaluate({});
        const Math::Vec3 world = val(coords).AsVector(), local = val(back).AsVector();
        Check(Near(world.x, -3) && Near(world.y, 2) && Near(world.z, 9) && Near(local.x, 2) && Near(local.y, 3) && Near(local.z, 4),
              "plane: plane coordinates ↔ world points");
        Check(Near(val(std3).AsPlane().Normal().y, -1.0), "plane: XZ plane normal is −Y");

        // 平面经过读写往返
        g.SetInputValue(coords, 1, Value(Math::Vec3{ 1, 0, 0 }));
        const NodeId remap2 = Add(g, "math.remap"), pdecon = Add(g, "plane.deconstruct");
        g.SetInputValue(remap2, 2, Value(Domain{ -4.0, 4.0 }));
        g.SetInputValue(pdecon, 0, Value(*Plane::FromNormal({ 1, 2, 3 }, { 1, 0, 0 })));
        Graph h;
        Check(h.LoadFromString(g.SaveToString()) && h.GetNode(remap2)->InputValues[2].Type() == DataType::Domain &&
              Near(h.GetNode(remap2)->InputValues[2].AsDomain().Min, -4.0) && h.GetNode(pdecon)->InputValues[0].Type() == DataType::Plane &&
              Near(h.GetNode(pdecon)->InputValues[0].AsPlane().Normal().x, 1.0) && Near(h.GetNode(pdecon)->InputValues[0].AsPlane().Origin.z, 3.0),
              "plane / domain: inline values round trip");

        // 曲线
        const NodeId arc3 = Add(g, "curve.arc3pt"), len = Add(g, "curve.length"), ell = Add(g, "curve.ellipse"), info = Add(g, "curve.info");
        g.SetInputValue(arc3, 1, Value(Math::Vec3{ 5, 5, 0 }));   // 半圆：(0,0) → (5,5) → (10,0)，逆时针经过顶部
        g.Connect({ arc3, 0, len, 0 });
        g.Connect({ ell, 0, info, 0 });
        g.Evaluate({});
        Check(Near(Num(g, len), std::numbers::pi * 5, 1e-3), "curve: three-point arc through the top (clockwise order)");
        g.SetInputValue(arc3, 1, Value(Math::Vec3{ 5, -5, 0 }));
        g.Evaluate({});
        Check(Near(Num(g, len), std::numbers::pi * 5, 1e-3), "curve: three-point arc through the bottom (counter-clockwise order)");
        const auto& ec = *val(ell).AsCurve3D();
        // 椭圆周长（Ramanujan 近似）
        const double ea = 10, eb = 5, h2 = (ea - eb) * (ea - eb) / ((ea + eb) * (ea + eb));
        Check(Near(ec.Length(), std::numbers::pi * (ea + eb) * (1 + 3 * h2 / (10 + std::sqrt(4 - 3 * h2))), 2e-2) && val(info, 0).AsBool() &&
              val(info, 1).AsBool() && Near(ec.PointAt(ec.StartParam()).x, 10.0), "curve: ellipse is a closed planar NURBS");

        const NodeId cl = Add(g, "curve.closest"), lineA = Add(g, "curve.line"), isect = Add(g, "curve.intersect");
        g.Connect({ ell, 0, cl, 0 });
        g.SetInputValue(cl, 1, Value(Math::Vec3{ 0, 20, 0 }));
        g.SetInputValue(lineA, 0, Value(Math::Vec3{ -20, 1, 0 }));
        g.SetInputValue(lineA, 1, Value(Math::Vec3{ 20, 1, 0 }));
        g.Connect({ ell, 0, isect, 0 });
        g.Connect({ lineA, 0, isect, 1 });
        const NodeId pint = Add(g, "curve.planeintersect"), pstd = Add(g, "plane.standard");
        g.SetParam(pstd, 0, int64_t(1));   // YZ 平面
        g.Connect({ ell, 0, pint, 0 });
        g.Connect({ pstd, 0, pint, 1 });
        g.Evaluate({});
        Check(Near(val(cl, 0).AsVector().y, 5.0, 1e-6) && Near(Num(g, cl, 2), 15.0, 1e-6), "curve: closest point on the ellipse");
        bool onBoth = Out(g, isect, 0).size() == 2;
        for (size_t i = 0; onBoth && i < 2; ++i)
        {
            const Math::Vec3 p = Out(g, isect, 0)[i].AsVector();
            onBoth = Near(p.y, 1.0, 1e-6) && Near(p.x * p.x / 100 + p.y * p.y / 25, 1.0, 1e-6);
        }
        Check(onBoth, "curve: ellipse × line gives two exact intersections");
        Check(Out(g, pint, 0).size() == 2 && Near(std::abs(Out(g, pint, 0)[0].AsVector().y), 5.0, 1e-6), "curve: ellipse × YZ plane gives two points");

        // 变换：定向、非均匀缩放、矩形阵列、沿曲线阵列
        const NodeId orient = Add(g, "xform.orient"), sq = Add(g, "curve.rectangle"), target = Add(g, "plane.make");
        g.Connect({ sq, 0, orient, 0 });
        g.SetInputValue(target, 0, Value(Math::Vec3{ 0, 0, 10 }));
        g.SetInputValue(target, 1, Value(Math::Vec3{ 1, 0, 0 }));    // 竖直平面
        g.Connect({ target, 0, orient, 2 });
        const NodeId orientFlat = Add(g, "xform.orient"), flatTarget = Add(g, "plane.points");
        g.Connect({ sq, 0, orientFlat, 0 });
        g.SetInputValue(flatTarget, 0, Value(Math::Vec3{ 5, 5, 0 }));
        g.SetInputValue(flatTarget, 1, Value(Math::Vec3{ 5, 6, 0 }));
        g.SetInputValue(flatTarget, 2, Value(Math::Vec3{ 4, 5, 0 }));
        g.Connect({ flatTarget, 0, orientFlat, 2 });
        g.Evaluate({});
        Check(val(orient).Type() == DataType::Curve3D && Near(val(orient).AsCurve3D()->PointAt(val(orient).AsCurve3D()->StartParam()).x, 0.0, 1e-9),
              "transform: orient onto a vertical plane gives a spatial curve");
        Check(val(orientFlat).Type() == DataType::Curve, "transform: orient within the XY plane keeps the curve entity");

        const NodeId nu = Add(g, "xform.scalenu"), circ = Add(g, "curve.circle");
        g.Connect({ circ, 0, nu, 0 });
        g.Evaluate({});
        Check(val(nu).Type() == DataType::Curve3D && Near(val(nu).AsCurve3D()->PointAt(val(nu).AsCurve3D()->StartParam()).x, 10.0, 1e-9),
              "transform: non-uniform scale turns a circle into an ellipse");

        const NodeId pt = Add(g, "vector.point"), pt6 = Add(g, "vector.point"), rect = Add(g, "xform.rectarray"), carr = Add(g, "xform.curvearray");
        g.Connect({ pt, 0, rect, 0 });
        g.SetInputValue(rect, 3, int64_t(4));
        g.SetInputValue(rect, 4, int64_t(2));
        g.SetInputValue(pt6, 0, 6.0);   // 圆起点 (5, 0) 外侧 1
        g.Connect({ pt6, 0, carr, 0 });
        g.Connect({ circ, 0, carr, 1 });
        g.SetInputValue(carr, 2, int64_t(4));
        g.Evaluate({});
        Check(Out(g, rect).size() == 8 && Near(Out(g, rect)[7].AsVector().x, 30.0) && Near(Out(g, rect)[7].AsVector().y, 10.0), "transform: rectangular array");
        // 圆起点外侧的点随切线转动：第 2 份在圆的对面、仍在外侧 (−6, 0)（只平移时会是 (−4, 0)）
        Check(Out(g, carr).size() == 4 && Near(Out(g, carr)[2].AsVector().x, -6.0, 1e-3) && Near(Out(g, carr)[2].AsVector().y, 0.0, 1e-3),
              "transform: array along a closed curve with tangent orientation");
    }
}
