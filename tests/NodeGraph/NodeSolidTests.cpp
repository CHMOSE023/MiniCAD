// 节点图的三维实体节点测试（docs/NodeGraph/PLAN.md，MINICAD_WITH_SOLID）：
// 基本体、拉伸（含圆角矩形，验证凸度方向）、旋转、扫掠、阵列 + 并集、差集、交集、圆角 / 倒角、实体信息、截面、
// 变换、预览（离散缓存、绘图区着色面）与烘焙；空间曲线与曲面（放样、加厚、旋转曲面、点阵曲面、管道、读写往返与导出）；
// N6：引用图纸里的三维实体、实体的定向 / 任意轴旋转 / 非均匀缩放 / 沿曲线阵列、平面预览；
// N7：五种形体与空心、圆锥 / 圆环、形体合并、剪切几何、分割、抽壳、实心 / 空心、任意平面截面、旧图兼容；
// N8：曲面的创建、修改（修剪、分割、炸开、拼接、封口、翻转、延伸、重建）与分析（分解、等参线、最近点、求交、曲率）
#include "NodeGraph/Graph.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "NodeGraph/CurveOps.h"
#include "NodeGraph/GeometryOps.h"
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
#include <type_traits>

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
    TestN7Forms();
    TestN8Surfaces();
    TestN6bCurves();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}
