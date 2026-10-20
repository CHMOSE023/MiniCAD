// 节点图的三维实体节点测试（docs/NodeGraph/PLAN.md，MINICAD_WITH_SOLID）：
// 基本体、拉伸（含圆角矩形，验证凸度方向）、旋转、扫掠、阵列 + 并集、差集、交集、圆角 / 倒角、实体信息、截面、
// 变换、预览（离散缓存、绘图区着色面）与烘焙；空间曲线与曲面（放样、加厚、旋转曲面、点阵曲面、管道、读写往返与导出）；
// N6：引用图纸里的三维实体、实体的定向 / 任意轴旋转 / 非均匀缩放 / 沿曲线阵列、平面预览
#include "NodeGraph/Graph.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
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

using namespace MiniCAD;
using namespace MiniCAD::Nodes;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = std::numbers::pi;
    bool Near(double a, double b, double rel = 1e-3) { return std::abs(a - b) <= rel * std::max(1.0, std::abs(b)); }

    NodeId Add(Graph& g, const char* type) { Node* n = g.AddNode(type); return n ? n->Id : kInvalidNode; }

    // SMLib 静态库整个进程只能用一个内核对象（见 SMLibKernel.h）
    Solid::ISolidKernel* g_kernel = nullptr;

    struct Fixture
    {
        Solid::ISolidKernel* kernel = g_kernel;
        Graph        g;
        SolveContext ctx;
        Fixture() { ctx.Kernel = kernel; }

        void Eval() { g.Evaluate(ctx); }
        // 节点 id 第一个输出的实体体积之和
        double Volume(NodeId id, int port = 0)
        {
            const Node* n = g.GetNode(id);
            double v = 0.0;
            for (const auto& item : n->Outputs[port])
                v += kernel->Volume(item.AsBody());
            return v;
        }
        size_t Count(NodeId id, int port = 0) { return g.GetNode(id)->Outputs[port].size(); }
        // 节点 id 第 port 个输出第一项的数值 / 实体
        double Number(NodeId id, int port = 0) { const auto& o = g.GetNode(id)->Outputs[port]; return o.empty() ? -1.0 : o[0].AsNumber(); }
        Solid::BodyPtr Body(NodeId id, int port = 0) { const auto& o = g.GetNode(id)->Outputs[port]; return o.empty() ? nullptr : o[0].AsBody(); }
        NodeState State(NodeId id) { return g.GetNode(id)->State; }
        bool   Ok(NodeId id) { return g.GetNode(id)->State == NodeState::Ok; }
    };

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
}

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
        const NodeId extrude = Add(f.g, "solid.extrude");
        Check(f.g.CanConnect({ line, 0, ce, 0 }) && !f.g.CanConnect({ interp, 0, extrude, 0 }),
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
}

namespace
{
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
}

int main()
{
    auto kernel = Solid::CreateSMLibKernel();
    g_kernel = kernel.get();
    TestPrimitives();
    TestExtrudeRevolveSweep();
    TestArraysAndBooleans();
    TestFilletAndSection();
    TestPreviewAndBake();
    TestCurve3D();
    TestSurfaceNodes();
    TestSurfacePreviewBakeAndFiles();
    TestN6WithKernel();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}
