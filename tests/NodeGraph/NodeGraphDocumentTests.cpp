// NodeGraphTests：节点图与图纸、文档（N2、N7 参数、N11）。
// 引用节点读图纸、文档保存节点图；命名参数、公式与族类型（N7）；预览收集、烘焙与撤销；后台求值（合并、过时丢弃、取消）
// 实现：src/NodeGraph/BuiltinGeometryNodes.cpp、BuiltinCurveXformNodes.cpp（doc.* 引用节点）、Graph.cpp（参数、后台求值）、
// Preview.cpp、src/Document/Document.cpp（GetNodeGraph）
// 计划：docs/NodeGraph/PLAN.md 第 3.4 节（后台求值）、第 5 节（参数）、第 11 节
#include "NodeGraphTestCommon.h"

namespace NodeGraphTest
{
    void TestReferenceAndDocument()
    {
        Document doc;
        Scene& scene = doc.GetScene();
        const auto lineId = scene.NextObjectID();
        scene.AddEntity(std::make_unique<LineEntity>(lineId, Math::Point3{ 0, 0, 0 }, Math::Point3{ 3, 4, 0 }));
        const auto pointId = scene.NextObjectID();
        scene.AddEntity(std::make_unique<PointEntity>(pointId, Math::Point3{ 1, 1, 0 }));

        Graph& g = doc.GetNodeGraph();
        const NodeId ref = Add(g, "doc.curves"), len = Add(g, "curve.length");
        g.Connect({ ref, 0, len, 0 });
        SolveContext ctx;
        ctx.DocScene     = &scene;
        ctx.SceneVersion = scene.GeometryVersion();
        g.Evaluate(ctx);
        Check(g.GetNode(ref)->State == NodeState::Warning, "reference: nothing referenced yet warns");
        g.SetParam(ref, 0, std::to_string(lineId) + "," + std::to_string(pointId));
        g.Evaluate(ctx);
        Check(Out(g, ref).size() == 1 && Near(Num(g, len), 5.0) && g.GetNode(ref)->State == NodeState::Warning,
              "reference: curves are read, non-curves are reported");
        const NodeId refPts = Add(g, "doc.points");
        g.SetParam(refPts, 0, std::to_string(lineId) + "," + std::to_string(pointId));
        g.Evaluate(ctx);
        Check(Out(g, refPts).size() == 1 && Near(Out(g, refPts)[0].AsVector().x, 1.0) && g.GetNode(refPts)->State == NodeState::Warning,
              "reference: points are read, non-points are reported");
        g.RemoveNode(refPts);

        // 图纸改了：场景版本变化时依赖场景的节点重算
        auto* line = static_cast<LineEntity*>(scene.GetEntity(lineId));
        line->SetLine(Line({ 0, 0, 0 }, { 6, 8, 0 }));
        scene.MarkEntityDirty(lineId);
        ctx.SceneVersion = scene.GeometryVersion();
        Check(g.NeedsEvaluate(ctx), "reference: a new scene version needs evaluation");
        g.Evaluate(ctx);
        Check(Near(Num(g, len), 10.0), "reference: follows the drawing");

        // 样条也是曲线：能引用，且拟合点样条按段精确转成三次 NURBS（端点节点经 Curve → Curve3D）
        const auto splineId = scene.NextObjectID();
        scene.AddEntity(std::make_unique<SplineEntity>(splineId, std::vector<Math::Point3>{ { 0, 0, 0 }, { 5, 5, 0 }, { 10, 0, 0 } }));
        const NodeId refSpline = Add(g, "doc.curves"), ends = Add(g, "curve.endpoints");
        g.Connect({ refSpline, 0, ends, 0 });
        g.SetParam(refSpline, 0, std::to_string(splineId));
        ctx.SceneVersion = scene.GeometryVersion();
        g.Evaluate(ctx);
        const DataList& startPt = Out(g, ends, 0);
        const DataList& endPt   = Out(g, ends, 1);
        Check(Out(g, refSpline).size() == 1 && g.GetNode(refSpline)->State == NodeState::Ok &&
              startPt.size() == 1 && endPt.size() == 1 &&
              Near(startPt[0].AsVector().x, 0.0) && Near(endPt[0].AsVector().x, 10.0),
              "reference: splines are curves and convert to NURBS");
        g.RemoveNode(ends);
        g.RemoveNode(refSpline);

        // 椭圆：整椭圆 → 有理二次 NURBS（闭合、阶数 2，四段共 9 个控制点）
        const auto ellipseId = scene.NextObjectID();
        scene.AddEntity(std::make_unique<EllipseEntity>(ellipseId, Ellipse(Math::Point3{ 0, 0, 0 }, 10.0, 5.0)));
        const NodeId refEllipse = Add(g, "doc.curves"), info = Add(g, "curve.info");
        g.Connect({ refEllipse, 0, info, 0 });
        g.SetParam(refEllipse, 0, std::to_string(ellipseId));
        ctx.SceneVersion = scene.GeometryVersion();
        g.Evaluate(ctx);
        const DataList& closed = Out(g, info, 0);
        const DataList& degree = Out(g, info, 2);
        Check(Out(g, refEllipse).size() == 1 && g.GetNode(refEllipse)->State == NodeState::Ok &&
              closed.size() == 1 && closed[0].Type() == DataType::Boolean && closed[0].AsBool() &&
              degree.size() == 1 && degree[0].AsInteger() == 2 && Out(g, info, 3).size() == 9,
              "reference: ellipses are curves and convert to rational NURBS");
        g.RemoveNode(info);
        g.RemoveNode(refEllipse);

        // 文档保存 / 读取节点图（JSON 与 .mcad）
        const auto dir = std::filesystem::temp_directory_path();
        for (const char* name : { "minicad_nodegraph_test.json", "minicad_nodegraph_test.mcad" })
        {
            const std::string path = (dir / name).string();
            Check(doc.SaveToFile(path), "document: save with a node graph");
            Document loaded;
            Check(loaded.LoadFromFile(path), "document: load");
            const Nodes::Graph* lg = loaded.FindNodeGraph();
            Check(lg && lg->GetNodes().size() == 2 && lg->GetLinks().size() == 1 &&
                  lg->GetNode(ref)->Params[0].AsText() == std::to_string(lineId) + "," + std::to_string(pointId),
                  name[std::string(name).size() - 1] == 'n' ? "document: node graph round trips through JSON" : "document: node graph round trips through .mcad");
            std::filesystem::remove(path);
        }
        Document empty;
        Check(empty.SaveToString().find("nodeGraph") == std::string::npos, "document: no node graph → nothing written");
    }

    // N7：命名参数（名称唯一、公式依赖链、表达式引用参数名、循环引用与错误）、类别、族类型（保存、切换、读写往返）、撤销
    void TestParameters()
    {
        Graph g;
        SolveContext ctx;
        const NodeId w = Add(g, kParamNodeType), h = Add(g, kParamNodeType), area = Add(g, kParamNodeType);
        auto name = [&](NodeId id) { return g.GetNode(id)->Params[0].AsText(); };
        Check(name(w) == "参数1" && name(h) == "参数2" && name(area) == "参数3", "params: new parameters get unique names");
        const std::vector<NodeId> dup = g.Duplicate({ w }, 10, 10);
        Check(dup.size() == 1 && name(dup[0]) == "参数4", "params: a duplicated parameter is renamed");
        g.RemoveNode(dup[0]);

        g.SetParam(w, 0, std::string("宽度"));
        g.SetParam(w, 2, 600.0);
        g.SetParam(h, 0, std::string("高度"));
        g.SetParam(h, 3, std::string("宽度 × 2"));
        g.SetParam(area, 0, std::string("面积"));
        g.SetParam(area, 3, std::string("宽度 * 高度"));
        const NodeId expr = Add(g, "math.expression");
        g.SetParam(expr, 0, std::string("宽度 + a"));
        g.SetInputValue(expr, 0, 1.0);
        g.Evaluate(ctx);
        Check(Near(Num(g, w), 600) && Near(Num(g, h), 1200) && Near(Num(g, area), 720000), "params: formulas follow the dependency chain");
        Check(Near(Num(g, expr), 601), "params: an expression node uses parameter names");

        // 改一个参数：依赖它的公式参数与表达式都重算
        g.SetParam(w, 2, 800.0);
        g.Evaluate(ctx);
        Check(Near(Num(g, h), 1600) && Near(Num(g, area), 1280000) && Near(Num(g, expr), 801), "params: a change propagates through formulas");
        const uint64_t solvesBefore = g.GetNode(expr)->SolveCount;
        g.SetParam(area, 2, 5.0);   // 有公式的参数改“值”不影响结果
        g.Evaluate(ctx);
        Check(g.GetNode(expr)->SolveCount == solvesBefore, "params: unrelated changes do not re-solve the expression");

        // 类别：整数取整，是否为布尔
        const NodeId cnt = Add(g, kParamNodeType), flag = Add(g, kParamNodeType);
        g.SetParam(cnt, 0, std::string("数量"));
        g.SetParam(cnt, 1, int64_t(static_cast<int>(ParamCategory::Integer)));
        g.SetParam(cnt, 3, std::string("宽度 / 300"));
        g.SetParam(flag, 0, std::string("开启"));
        g.SetParam(flag, 1, int64_t(static_cast<int>(ParamCategory::YesNo)));
        g.SetParam(flag, 3, std::string("宽度 > 700"));
        g.Evaluate(ctx);
        Check(Out(g, cnt).size() == 1 && Out(g, cnt)[0].Type() == DataType::Integer && Out(g, cnt)[0].AsInteger() == 3, "params: integer category rounds");
        Check(Out(g, flag).size() == 1 && Out(g, flag)[0].Type() == DataType::Boolean && Out(g, flag)[0].AsBool(), "params: yes / no category is a boolean");

        // 错误：循环引用、未知参数、名称重复、名称不能当变量
        g.SetParam(w, 3, std::string("面积 / 2"));
        g.Evaluate(ctx);
        Check(g.GetNode(w)->State == NodeState::Error && g.GetNode(w)->Message.find("循环引用") != std::string::npos &&
              g.GetNode(area)->State == NodeState::Error && g.GetNode(expr)->State == NodeState::Error, "params: a cycle is reported");
        g.SetParam(w, 3, std::string("长度 + 1"));
        g.Evaluate(ctx);
        Check(g.GetNode(w)->State == NodeState::Error && g.GetNode(w)->Message.find("不是参数") != std::string::npos, "params: unknown name in a formula");
        g.SetParam(w, 3, std::string());
        g.Evaluate(ctx);
        Check(g.GetNode(w)->State == NodeState::Ok && Near(Num(g, h), 1600), "params: fixing the formula recovers");
        g.SetParam(cnt, 0, std::string("宽度"));
        g.Evaluate(ctx);
        Check(g.GetNode(cnt)->State == NodeState::Error && g.GetNode(cnt)->Message.find("重复") != std::string::npos, "params: duplicate names");
        g.SetParam(cnt, 0, std::string("a b"));
        g.Evaluate(ctx);
        Check(g.GetNode(cnt)->State == NodeState::Error, "params: a name with a space is rejected");
        g.SetParam(cnt, 0, std::string("数量"));

        // 类型：保存两组值，切换后参数与下游都变；有公式的参数不受影响
        g.SaveParamType("600 宽");
        g.SetParam(w, 2, 600.0);
        g.SaveParamType("小", { { "宽度", 300.0 } });
        g.SetParam(w, 2, 800.0);
        Check(g.ParamTypes().size() == 2 && Near(g.ParamTypes()[0].Values.at("宽度"), 800) && g.ParamTypes()[0].Values.count("高度") == 0,
              "types: saved with the values of parameters without formulas");
        Check(g.ApplyParamType("小"), "types: applying changes a parameter");
        g.Evaluate(ctx);
        Check(Near(Num(g, w), 300) && Near(Num(g, h), 600) && Near(Num(g, expr), 301), "types: switching a type updates the graph");
        Check(!g.ApplyParamType("小") && !g.ApplyParamType("不存在"), "types: applying the current or a missing type changes nothing");

        // 撤销：切换类型可以一步撤销（快照包括类型）
        GraphHistory history;
        history.Snapshot(g);
        g.ApplyParamType("600 宽");
        g.Evaluate(ctx);
        Check(Near(Num(g, w), 800), "types: switched back");
        history.Undo(g);
        g.Evaluate(ctx);
        Check(Near(Num(g, w), 300) && g.ParamTypes().size() == 2, "types: undo restores the values and keeps the types");

        // 读写往返
        Graph copy;
        Check(copy.LoadFromString(g.SaveToString()) && copy.ParamTypes().size() == 2 && copy.ParamTypes()[1].Name == "小" &&
              Near(copy.ParamTypes()[1].Values.at("宽度"), 300), "types: round trip");
        copy.Evaluate(ctx);
        Check(Near(Num(copy, area), 300.0 * 600.0), "params: round trip evaluates the formulas");
        Check(copy.RemoveParamType("小") && copy.ParamTypes().size() == 1, "types: remove");
    }

    void TestPreviewAndBake()
    {
        Document doc;
        Graph& g = doc.GetNodeGraph();
        const NodeId circle = Add(g, "curve.circle"), move = Add(g, "xform.move"), pt = Add(g, "vector.point");
        g.Connect({ circle, 0, move, 0 });
        g.Evaluate({});

        auto items = CollectPreview(g, {});
        Check(items.size() == 2, "preview: leaf outputs only (moved circle + point)");
        items = CollectPreview(g, { circle });
        Check(items.size() == 3 && items.back().Highlight == false &&
              std::count_if(items.begin(), items.end(), [](const PreviewItem& i) { return i.Highlight; }) == 1,
              "preview: selected node is added highlighted");
        g.SetPreview(pt, false);
        Check(CollectPreview(g, {}).size() == 1, "preview: preview flag hides a node");

        PreviewBuilder builder;
        PreviewSet set = builder.Build(g, { pt }, nullptr);
        Check(set.Entities.size() == 1 && set.HighlightPoints.size() == 1 && set.Points.empty(), "preview: builder makes entities and points");
        Check(set.Entities[0]->GetAttr().Color.Method == ColorMethod::ByRgb, "preview: preview color is a true color");

        Scene& scene = doc.GetScene();
        const int before = scene.EntityCount();
        Check(Bake(g, {}, scene, doc.GetCommandStack(), nullptr) == 1 && scene.EntityCount() == before + 1, "bake: leaf geometry becomes document entities");
        Check(Bake(g, { pt }, scene, doc.GetCommandStack(), nullptr) == 1 && scene.EntityCount() == before + 2, "bake: selected node only");
        doc.Undo();
        doc.Undo();
        Check(scene.EntityCount() == before, "bake: undoable");
    }

    // 后台求值（N11）：副本在另一个线程上求值，结果按脏戳合并回来；期间被改动的节点丢弃结果；取消
    void TestBackground()
    {
        Graph g;
        const NodeId slider = Add(g, "input.number"), add = Add(g, "math.add"), mul = Add(g, "math.mul"), other = Add(g, "math.neg");
        g.Connect({ slider, 0, add, 0 });
        g.Connect({ add, 0, mul, 0 });
        g.SetInputValue(add, 1, 1.0);
        g.SetInputValue(mul, 1, 2.0);
        g.Evaluate({});
        Check(!g.PrepareBackground({}), "background: nothing dirty → no copy");
        Check(Graph().Uid() != g.Uid(), "background: graphs have distinct uids");

        // 改滑块 → 副本在工作线程上算 → 合并
        g.SetParam(slider, 0, 3.0);
        const uint64_t mulSolves = g.GetNode(mul)->SolveCount, otherSolves = g.GetNode(other)->SolveCount;
        auto copy = g.PrepareBackground({});
        Check(copy && copy->DirtyStamps().size() == 3 && Out(*copy, other).empty() && Out(*copy, add).empty(),
              "background: the copy holds the dirty nodes; outputs nobody needs are not copied");
        const auto pending = copy->DirtyStamps();
        std::thread([&] { copy->Evaluate({}); }).join();
        Check(g.GetNode(mul)->Dirty && Near(Num(*copy, mul), 8.0), "background: the copy is solved, the graph is untouched");
        Check(!g.IsStale(pending) && g.MergeEvaluated(*copy) == 3 && Near(Num(g, mul), 8.0) && !g.NeedsEvaluate({}) &&
              g.GetNode(mul)->SolveCount == mulSolves + 1 && g.GetNode(other)->SolveCount == otherSolves,
              "background: results merge back (3 nodes), untouched nodes keep theirs");

        // 求值期间又改了下游的 mul：mul 的结果过时，上游的 slider、add 照样合并
        g.SetParam(slider, 0, 4.0);
        copy = g.PrepareBackground({});
        const auto pending2 = copy->DirtyStamps();
        g.SetInputValue(mul, 1, 10.0);
        Check(g.IsStale(pending2), "background: editing a node being solved makes the job stale");
        copy->Evaluate({});
        Check(g.MergeEvaluated(*copy) == 2 && Near(Num(g, add), 5.0) && g.GetNode(mul)->Dirty && !g.GetNode(add)->Dirty,
              "background: stale nodes are not merged, the others are");
        Check(Near(Num(g, mul), 10.0), "background: a stale result is shown (the node stays dirty) while dragging");
        g.Evaluate({});
        Check(Near(Num(g, mul), 50.0), "background: the stale node is solved again");

        // 删除节点也算过时；合并时找不到的节点跳过
        g.SetParam(slider, 0, 1.0);
        copy = g.PrepareBackground({});
        const auto pending3 = copy->DirtyStamps();
        g.RemoveNode(other);
        Check(!g.IsStale(pending3), "background: removing a clean node does not make the job stale");
        g.RemoveNode(mul);
        Check(g.IsStale(pending3), "background: removing a node being solved makes the job stale");
        copy->Evaluate({});
        Check(g.MergeEvaluated(*copy) == 2 && Near(Num(g, add), 2.0), "background: removed nodes are skipped when merging");

        // 耗时估计与同步求值的时间上限
        {
            Graph e;
            const NodeId a = Add(e, "math.add");
            Check(e.EstimatedMillis() < 0.0, "estimate: a never-solved dirty node has an unknown cost");
            SolveContext late;
            late.Deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
            Check(!e.Evaluate(late) && e.GetNode(a)->Dirty, "deadline: past the deadline nothing is solved, nodes stay dirty");
            e.Evaluate({});
            e.MarkDirty(a);
            Check(e.EstimatedMillis() >= 0.0 && e.EstimatedMillis() < 50.0, "estimate: sum of the last solve times of the dirty nodes");
        }

        // 取消：开始前取消什么都不算；算完第一个节点后取消，节点各项之间检查到取消，被打断的节点保持为脏
        Graph h;
        const NodeId series = Add(h, "math.series"), plus = Add(h, "math.add");
        h.SetInputValue(series, 2, int64_t(1000));
        h.Connect({ series, 0, plus, 0 });
        std::atomic<bool> cancel{ true };
        SolveContext ctx;
        ctx.Cancel = &cancel;
        copy = h.PrepareBackground({});
        Check(!copy->Evaluate(ctx) && copy->GetNode(series)->Dirty && h.MergeEvaluated(*copy) == 0, "cancel: cancelled before start → nothing solved");
        cancel = false;
        int solved = 0;
        ctx.Solved = [&] { ++solved; cancel = true; };
        copy = h.PrepareBackground({});
        copy->Evaluate(ctx);
        Check(solved == 1 && !copy->GetNode(series)->Dirty && copy->GetNode(plus)->Dirty && copy->GetNode(plus)->SolveCount == 0,
              "cancel: stops between the items of a node; the node stays dirty");
        Check(h.MergeEvaluated(*copy) == 1 && Out(h, series).size() == 1000 && h.GetNode(plus)->Dirty, "cancel: finished nodes are still merged");
        h.Evaluate({});
        Check(Out(h, plus).size() == 1000 && !h.NeedsEvaluate({}), "cancel: the rest is solved later");

        // 引用节点读场景：在 PrepareBackground（界面线程）里算好，副本里是干净的
        Document doc;
        Scene& scene = doc.GetScene();
        const auto pointId = scene.NextObjectID();
        scene.AddEntity(std::make_unique<PointEntity>(pointId, Math::Point3{ 2, 3, 0 }));
        Graph& dg = doc.GetNodeGraph();
        const NodeId ref = Add(dg, "doc.points"), dec = Add(dg, "vector.deconstruct");
        dg.SetParam(ref, 0, std::to_string(pointId));
        dg.Connect({ ref, 0, dec, 0 });
        SolveContext sctx;
        sctx.DocScene     = &scene;
        sctx.SceneVersion = scene.GeometryVersion();
        copy = dg.PrepareBackground(sctx);
        Check(copy && !dg.GetNode(ref)->Dirty && !copy->GetNode(ref)->Dirty && copy->GetNode(dec)->Dirty,
              "background: scene-dependent nodes are solved before copying");
        SolveContext wctx;
        wctx.SceneVersion = sctx.SceneVersion;   // 工作线程不带场景
        copy->Evaluate(wctx);
        Check(dg.MergeEvaluated(*copy) == 1 && Near(Num(dg, dec, 1), 3.0) && !dg.NeedsEvaluate(sctx), "background: downstream of a reference merges");
    }
}
