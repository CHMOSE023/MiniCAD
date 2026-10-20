// 节点图核心测试（docs/NodeGraph/PLAN.md）：不需要实体内核的部分。
// 注册表与分类顺序表、按端口搜索、连线规则（类型、环、替换 / 追加、断开输出）、脏标记与只重算脏节点、最长列表匹配、类型转换、
// 读写往返（含未知节点类型）、撤销 / 重做、复制、曲线 / 变换 / 引用节点、没有内核时实体节点的报错、
// 文档保存节点图、预览收集、烘焙与撤销
#include "NodeGraph/Graph.h"
#include "NodeGraph/GraphHistory.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <numbers>
#include <string>

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

    bool Near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

    const DataList& Out(const Graph& g, NodeId id, int port = 0)
    {
        static const DataList kEmpty;
        const Node* n = g.GetNode(id);
        return n && port < static_cast<int>(n->Outputs.size()) ? n->Outputs[port] : kEmpty;
    }

    double Num(const Graph& g, NodeId id, int port = 0, size_t i = 0)
    {
        const auto& l = Out(g, id, port);
        Value v;
        return i < l.size() && Convert(l[i], DataType::Number, v) ? v.AsNumber() : NAN;
    }

    NodeId Add(Graph& g, const char* type) { Node* n = g.AddNode(type); return n ? n->Id : kInvalidNode; }

    void TestRegistry()
    {
        const auto& r = NodeRegistry::Builtin();
        Check(r.All().size() >= 50, "registry: built-in node types are registered");
        Check(r.Find("solid.box") && r.Find("math.add") && r.Find("curve.rectangle") && r.Find("doc.curves") && !r.Find("nope"),
              "registry: lookup by id");
        const auto cats = r.Categories();
        Check(cats == NodeRegistry::CategoryOrder(), "registry: categories follow the category order table");
        Check(r.Find("solid.box")->Category == "实体/创建" && r.Find("solid.extrude")->Category == "实体/创建" &&
              r.Find("solid.union")->Category == "实体/运算" && r.Find("solid.fillet")->Category == "实体/修改" &&
              r.Find("solid.info")->Category == "实体/分析", "registry: solid nodes are grouped (create, operate, modify, analyze)");
        Check(TopCategory("实体/运算") == "实体" && SubCategory("实体/运算") == "运算" && TopCategory("列表") == "列表" && SubCategory("列表").empty(),
              "registry: category path helpers");
        bool defaultsTyped = true;
        for (const auto& t : r.All())
            for (const auto& p : t->Inputs)
                if (!p.Default.IsEmpty() && p.Type != DataType::Any && p.Type != DataType::Geometry && p.Default.Type() != p.Type)
                    defaultsTyped = false;
        Check(defaultsTyped, "registry: inline defaults are converted to the port type");
    }

    void TestBasicsAndDirty()
    {
        Graph g;
        const NodeId slider = Add(g, "input.number");
        const NodeId add    = Add(g, "math.add");
        const NodeId other  = Add(g, "math.mul");
        g.SetParam(slider, 0, 3.0);
        g.SetInputValue(add, 1, 4.0);
        Check(g.Connect({ slider, 0, add, 0 }), "connect: slider → add.A");
        g.Evaluate({});
        Check(Near(Num(g, add), 7.0), "evaluate: 3 + 4 = 7");
        Check(Near(Num(g, other), 1.0), "evaluate: unconnected inputs use inline values (1 × 1)");

        const uint64_t addCount = g.GetNode(add)->SolveCount, otherCount = g.GetNode(other)->SolveCount;
        Check(!g.Evaluate({}), "evaluate: nothing dirty → nothing solved");
        g.SetParam(slider, 0, 5.5);
        Check(g.GetNode(add)->Dirty && !g.GetNode(other)->Dirty, "dirty: propagates downstream only");
        g.Evaluate({});
        Check(Near(Num(g, add), 9.5) && g.GetNode(add)->SolveCount == addCount + 1 && g.GetNode(other)->SolveCount == otherCount,
              "evaluate: only dirty nodes are re-solved");

        g.SetParam(slider, 0, 50.0);   // 超出滑块范围 [0, 10] → 输出取最大值
        g.Evaluate({});
        Check(Near(Num(g, slider), 10.0), "slider: value clamped to its range");
        g.SetParam(slider, 3, int64_t(0));
        g.SetParam(slider, 0, 3.7);
        g.Evaluate({});
        Check(Near(Num(g, slider), 4.0), "slider: rounded to the number of decimals");

        // 除以 0：错误状态与消息，下游缺少输入
        const NodeId div = Add(g, "math.div");
        g.SetInputValue(div, 1, 0.0);
        const NodeId neg = Add(g, "math.neg");
        g.Connect({ div, 0, neg, 0 });
        g.Evaluate({});
        Check(g.GetNode(div)->State == NodeState::Error && Out(g, div).empty(), "error: division by zero");
        Check(g.GetNode(neg)->State == NodeState::Warning && g.GetNode(neg)->Message.find("没有数据") != std::string::npos,
              "error: downstream of a failed node warns about missing input");
    }

    // 分类顺序表与菜单一致（docs/NodeGraph/PLAN.md 第 4.1 节）：每个节点的分类都在表里、表里每个分类都有节点、
    // ID 不重复、同一分类里显示名不重复；菜单顺序按表、同一分类里按 Group
    void TestCategoryOrder()
    {
        const auto& r = NodeRegistry::Builtin();
        const auto& order = NodeRegistry::CategoryOrder();
        std::set<std::string> used, ids;
        std::map<std::string, std::set<std::string>> names;
        bool inTable = true, uniqueIds = true, uniqueNames = true;
        for (const auto& t : r.All())
        {
            inTable &= std::find(order.begin(), order.end(), t->Category) != order.end();
            used.insert(t->Category);
            uniqueIds &= ids.insert(t->Id).second;
            uniqueNames &= names[t->Category].insert(t->Name).second;
        }
        Check(inTable, "order: every node category is in the order table");
        Check(used.size() == order.size(), "order: every category in the table has nodes (no empty submenu)");
        Check(uniqueIds && uniqueNames, "order: ids are unique, names are unique within a category");

        const auto& ordered = r.Ordered();
        auto rank = [&](const NodeTypeInfo* t) { return std::find(order.begin(), order.end(), t->Category) - order.begin(); };
        bool sorted = ordered.size() == r.All().size();
        for (size_t i = 1; i < ordered.size(); ++i)
        {
            const auto a = rank(ordered[i - 1]), b = rank(ordered[i]);
            sorted &= a < b || (a == b && ordered[i - 1]->Group <= ordered[i]->Group);
        }
        Check(sorted, "order: menu order follows the table, then Group");
        auto pos = [&](const char* id) { return std::find_if(ordered.begin(), ordered.end(), [&](auto* t) { return t->Id == id; }) - ordered.begin(); };
        Check(pos("solid.extrude") < pos("solid.revolve") && pos("solid.sweep") < pos("solid.box") && pos("solid.box") < pos("solid.sphere") &&
              pos("input.number") == 0 && pos("curve.line") < pos("surface.loft") && pos("surface.loft") < pos("solid.extrude") &&
              pos("solid.extrude") < pos("xform.move"), "order: forms before primitives; curves → surfaces → solids → transforms");
        Check(r.Find("solid.sweep")->Name == "放样" && r.Find("surface.loft")->Name == "放样曲面" && r.Find("solid.revolve")->Name == "旋转",
              "order: Revit names for solid forms, surface loft renamed");
    }

    // 从端口拖出连线后的搜索（docs/NodeGraph/PLAN.md 第 9.2 节）
    void TestSearchForPort()
    {
        const auto& r = NodeRegistry::Builtin();
        auto ids = [&](const std::string& q, DataType t, BodyKind k, bool fromOutput)
        {
            std::vector<std::string> out;
            for (const PortCandidate& c : r.SearchForPort(q, t, k, fromOutput))
                out.push_back(c.Type->Id);
            return out;
        };
        auto has = [](const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); };
        auto index = [](const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) - v.begin(); };

        // 从“管道”的路径输入（空间曲线）拖出：曲线、NURBS 节点都能提供，实体不能
        const auto toCurve3D = ids("", DataType::Curve3D, BodyKind::Any, /*fromOutput=*/false);
        Check(has(toCurve3D, "nurbs.interpolate") && has(toCurve3D, "curve.line") && !has(toCurve3D, "solid.box") && !has(toCurve3D, "math.add"),
              "port search: from a Curve3D input lists curve nodes (with conversion), not solids");
        Check(index(toCurve3D, "nurbs.interpolate") < index(toCurve3D, "curve.line"), "port search: exact type ranks before conversion");
        Check(!has(toCurve3D, "list.item") && has(ids("列表项", DataType::Curve3D, BodyKind::Any, false), "list.item"),
              "port search: generic (Any) nodes are hidden until typed");

        // 从“加厚”的曲面输入拖出：曲面节点排在实体节点前面
        const auto toSurface = ids("", DataType::Body, BodyKind::Surface, false);
        Check(has(toSurface, "surface.loft") && has(toSurface, "solid.box") && index(toSurface, "surface.loft") < index(toSurface, "solid.box"),
              "port search: surface producers rank before solids for a surface input");
        // 从数值输出拖出：只看候选的输入端口
        const auto fromNumber = ids("", DataType::Number, BodyKind::Any, /*fromOutput=*/true);
        Check(has(fromNumber, "math.add") && has(fromNumber, "solid.box") && !has(fromNumber, "solid.union") && !has(fromNumber, "list.merge"),
              "port search: from a number output lists nodes with numeric inputs");
        // 有文字时按文字匹配度
        const auto typed = ids("放样", DataType::Curve, BodyKind::Any, true);
        Check(!typed.empty() && typed.front() == "solid.sweep" && has(typed, "surface.loft"), "port search: text match first (Revit name)");

        const PortCandidate best = NodeRegistry::BestPort(*r.Find("surface.thicken"), DataType::Body, BodyKind::Surface, true);
        Check(best.Port == 0 && best.Match == PortMatch::Exact, "port search: best port and exact kind match");
        Check(MatchPorts(DataType::Body, BodyKind::Solid, DataType::Body, BodyKind::Surface) == PortMatch::Convert &&
              MatchPorts(DataType::Curve, BodyKind::Any, DataType::Curve3D, BodyKind::Any) == PortMatch::Convert &&
              MatchPorts(DataType::Number, BodyKind::Any, DataType::Any, BodyKind::Any) == PortMatch::Generic &&
              MatchPorts(DataType::Number, BodyKind::Any, DataType::Curve, BodyKind::Any) == PortMatch::None,
              "port search: match levels");
        Check(r.Search("扫掠").front()->Id == "solid.sweep" && r.Search("旋转成体").front()->Id == "solid.revolve" &&
              r.Search("loft").front()->Id == "surface.loft", "search: keywords keep old names and English aliases");
    }

    void TestLinksRules()
    {
        Graph g;
        const NodeId a = Add(g, "math.add"), b = Add(g, "math.add"), c = Add(g, "math.add");
        const NodeId circle = Add(g, "curve.circle"), length = Add(g, "curve.length");
        std::string why;
        Check(g.Connect({ a, 0, b, 0 }) && g.Connect({ b, 0, c, 0 }), "links: chain a → b → c");
        Check(!g.CanConnect({ c, 0, a, 0 }, &why) && why.find("环") != std::string::npos, "links: cycles are rejected");
        Check(!g.CanConnect({ a, 0, a, 1 }), "links: self loop is rejected");
        Check(!g.CanConnect({ a, 0, length, 0 }, &why) && why.find("类型") != std::string::npos, "links: number → curve is incompatible");
        Check(g.CanConnect({ circle, 0, length, 0 }), "links: curve → curve");
        Check(!g.CanConnect({ a, 0, b, 5 }) && !g.CanConnect({ a, 3, b, 0 }), "links: invalid port index");

        // 替换与追加
        const NodeId series = Add(g, "math.series"), sum = Add(g, "math.sum"), s2 = Add(g, "math.series");
        g.SetInputValue(series, 2, int64_t(3));   // 0 1 2
        g.SetInputValue(s2, 0, 10.0);
        g.SetInputValue(s2, 2, int64_t(2));       // 10 11
        g.Connect({ series, 0, sum, 0 });
        g.Connect({ s2, 0, sum, 0 }, /*replace=*/false);
        g.Evaluate({});
        Check(g.LinksInto(sum, 0).size() == 2 && Near(Num(g, sum), 24.0), "links: appended wires are concatenated (0+1+2+10+11)");
        g.Connect({ series, 0, sum, 0 }, /*replace=*/true);
        g.Evaluate({});
        Check(g.LinksInto(sum, 0).size() == 1 && Near(Num(g, sum), 3.0), "links: replace keeps only the new wire");
        g.DisconnectInput(sum, 0);
        g.Evaluate({});
        Check(g.GetNode(sum)->State == NodeState::Warning, "links: disconnecting a required list input warns");

        // 断开输出：一个输出上的全部连线（Alt + 单击输出引脚）
        const NodeId fan = Add(g, "math.add"), t1 = Add(g, "math.add"), t2 = Add(g, "math.add");
        g.Connect({ fan, 0, t1, 0 });
        g.Connect({ fan, 0, t2, 1 });
        g.Evaluate({});
        Check(g.LinksFrom(fan, 0).size() == 2, "links: links from an output");
        const size_t before = g.GetLinks().size();
        g.DisconnectOutput(fan, 0);
        Check(g.LinksFrom(fan, 0).empty() && g.GetLinks().size() == before - 2 && g.GetNode(t1)->Dirty && g.GetNode(t2)->Dirty,
              "links: disconnecting an output removes all its wires and dirties downstream");
        g.RemoveNode(fan);
        g.RemoveNode(t1);
        g.RemoveNode(t2);

        g.RemoveNode(b);
        Check(g.GetLinks().size() == 0 && !g.GetNode(b), "links: removing a node removes its wires");
    }

    void TestLongestList()
    {
        Graph g;
        const NodeId series = Add(g, "math.series"), pair = Add(g, "math.series"), add = Add(g, "math.add");
        g.SetInputValue(series, 2, int64_t(5));   // 0..4
        g.SetInputValue(pair, 0, 10.0);
        g.SetInputValue(pair, 1, 10.0);
        g.SetInputValue(pair, 2, int64_t(2));     // 10 20
        g.Connect({ series, 0, add, 0 });
        g.Connect({ pair, 0, add, 1 });
        g.Evaluate({});
        const auto& out = Out(g, add);
        Check(out.size() == 5 && Near(Num(g, add, 0, 0), 10) && Near(Num(g, add, 0, 1), 21) && Near(Num(g, add, 0, 4), 24),
              "longest list: shorter list repeats its last item");

        // 整数 → 数值、文字 → 数值（失败报错）
        const NodeId isl = Add(g, "input.integer"), neg = Add(g, "math.neg");
        g.Connect({ isl, 0, neg, 0 });
        g.Evaluate({});
        Check(Near(Num(g, neg), -5.0), "convert: integer → number");
        const NodeId panel = Add(g, "input.panel"), neg2 = Add(g, "math.neg");
        g.SetParam(panel, 0, std::string("2.5"));
        g.Connect({ panel, 0, neg2, 0 });
        g.Evaluate({});
        Check(Near(Num(g, neg2), -2.5), "convert: text \"2.5\" → number");
        g.SetParam(panel, 0, std::string("abc"));
        g.Evaluate({});
        Check(g.GetNode(neg2)->State == NodeState::Error, "convert: text \"abc\" → number fails with an error");
        Check(g.GetNode(panel)->DisplayData.empty(), "panel: display data is the incoming list (nothing connected)");

        // 列表节点
        const NodeId item = Add(g, "list.item"), len = Add(g, "list.length"), rev = Add(g, "list.reverse");
        g.Connect({ series, 0, item, 0 });
        g.SetInputValue(item, 1, int64_t(-1));
        g.Connect({ series, 0, len, 0 });
        g.Connect({ series, 0, rev, 0 });
        g.Evaluate({});
        Check(Near(Num(g, item), 4) && Near(Num(g, len), 5) && Near(Num(g, rev, 0, 0), 4), "list: item (wrap), length, reverse");
    }

    void TestSerialization()
    {
        Graph g;
        const NodeId slider = Add(g, "input.number"), box = Add(g, "vector.point"), add = Add(g, "math.add");
        g.SetParam(slider, 0, 7.25);
        g.SetPosition(slider, 12.5f, -30.0f);
        g.SetInputValue(box, 1, 3.0);
        g.SetPreview(box, false);
        g.Connect({ slider, 0, add, 0 });
        g.Connect({ slider, 0, box, 0 });
        g.Evaluate({});

        const std::string text = g.SaveToString();
        Graph h;
        Check(h.LoadFromString(text), "serialize: load");
        h.Evaluate({});
        const Node* s2 = h.GetNode(slider);
        Check(h.GetNodes().size() == 3 && h.GetLinks().size() == 2 && s2 && Near(s2->X, 12.5) && Near(s2->Y, -30.0),
              "serialize: nodes, positions and links round trip");
        Check(Near(Num(h, add), 7.25) && !h.GetNode(box)->Preview && Out(h, box).size() == 1 &&
              Near(Out(h, box)[0].AsVector().y, 3.0), "serialize: params, inline vectors and preview flag round trip");
        Node* n4 = h.AddNode("math.add");
        Check(n4 && n4->Id == 4, "serialize: new ids continue after the loaded ones");

        // 未知节点类型：保留数据与连线，报错；再保存不丢
        std::string patched = text;
        const size_t pos = patched.find("\"math.add\"");
        patched.replace(pos, 10, "\"future.op\"");
        Graph m;
        Check(m.LoadFromString(patched), "serialize: graph with an unknown node type loads");
        m.Evaluate({});
        const Node* unk = m.GetNode(add);
        Check(unk && !unk->Type && unk->State == NodeState::Error && m.GetLinks().size() == 2, "serialize: unknown node kept as an error node with its wire");
        Check(m.SaveToString().find("future.op") != std::string::npos, "serialize: unknown node survives re-saving");
        Check(!m.LoadFromString("{\"format\":\"other\"}") && m.Empty(), "serialize: wrong format is rejected");
    }

    void TestHistoryAndDuplicate()
    {
        Graph g;
        GraphHistory hist;
        hist.Snapshot(g);
        const NodeId a = Add(g, "math.add");
        hist.Snapshot(g);
        const NodeId b = Add(g, "math.neg");
        g.Connect({ a, 0, b, 0 });
        Check(hist.Undo(g) && g.GetNodes().size() == 1 && g.GetLinks().empty(), "history: undo restores the previous graph");
        Check(hist.Redo(g) && g.GetNodes().size() == 2 && g.GetLinks().size() == 1, "history: redo");
        Check(hist.Undo(g) && hist.Undo(g) && g.Empty() && !hist.CanUndo(), "history: undo to the beginning");

        Graph d;
        const NodeId s = Add(d, "math.series"), sum = Add(d, "math.sum"), outside = Add(d, "math.neg");
        d.Connect({ s, 0, sum, 0 });
        d.Connect({ sum, 0, outside, 0 });
        const auto copies = d.Duplicate({ s, sum }, 20, 10);
        Check(copies.size() == 2 && d.GetNodes().size() == 5 && d.GetLinks().size() == 3, "duplicate: nodes and the wires between them");
        Check(Near(d.GetNode(copies[0])->X, d.GetNode(s)->X + 20), "duplicate: offset position");
    }

    void TestCopyPaste()
    {
        // 源图：in → s → sum → outside，复制 s 与 sum
        Graph g;
        const NodeId in = Add(g, "input.integer"), s = Add(g, "math.series"), sum = Add(g, "math.sum"), outside = Add(g, "math.neg");
        g.SetPosition(s, 100.0f, 50.0f);
        g.SetPosition(sum, 340.0f, 80.0f);
        g.SetInputValue(s, 2, int64_t(4));   // 0 1 2 3
        g.SetPreview(sum, false);
        g.Connect({ in, 0, s, 1 });
        g.Connect({ s, 0, sum, 0 });
        g.Connect({ sum, 0, outside, 0 });
        const std::string clip = g.CopyToString({ s, sum });
        Check(!clip.empty() && clip.find("MiniCAD-NodeGraph") != std::string::npos, "copy: sub-graph written as node graph JSON");
        Check(g.CopyToString({}).empty() && g.CopyToString({ 999 }).empty(), "copy: nothing to copy → empty text");
        {
            Graph sub;
            sub.LoadFromString(clip);
            Check(sub.GetNodes().size() == 2 && sub.GetLinks().size() == 1 && sub.GetLinks()[0] == Link{ s, 0, sum, 0 },
                  "copy: only the selected nodes and the wire between them");
        }

        // 粘贴回同一张图：新 ID，内部连线保留，外部连线（in → s、sum → outside）丢弃，左上角放在指定位置
        const auto pasted = g.PasteFromString(clip, 500.0f, 600.0f);
        Check(pasted.size() == 2 && pasted[0] != s && pasted[1] != sum && pasted[0] > outside && pasted[1] > outside &&
              g.GetNodes().size() == 6, "paste: ids are reassigned");
        Check(g.GetLinks().size() == 4 && std::find(g.GetLinks().begin(), g.GetLinks().end(), Link{ pasted[0], 0, pasted[1], 0 }) != g.GetLinks().end(),
              "paste: internal wire kept");
        Check(g.LinksInto(pasted[0], 1).empty() && !g.IsOutputConnected(pasted[1], 0), "paste: external wires dropped");
        const Node* ps = g.GetNode(pasted[0]);
        const Node* pm = g.GetNode(pasted[1]);
        Check(ps && pm && Near(ps->X, 500.0) && Near(ps->Y, 600.0) && Near(pm->X, 740.0) && Near(pm->Y, 630.0),
              "paste: placed at the target, relative layout kept");
        g.Evaluate({});
        Check(ps->InputValues[2].AsInteger() == 4 && !pm->Preview && Near(Num(g, pasted[1]), 6.0), "paste: inline values, preview flag and result");

        // 粘贴到另一张图、粘贴两次，不是节点图的文字不粘贴
        Graph h;
        Add(h, "math.add");
        const auto p1 = h.PasteFromString(clip, 0.0f, 0.0f);
        const auto p2 = h.PasteFromString(clip, 0.0f, 300.0f);
        Check(p1.size() == 2 && p2.size() == 2 && h.GetNodes().size() == 5 && h.GetLinks().size() == 2 && p2[0] == p1[1] + 1,
              "paste: into another graph, twice");
        Check(h.PasteFromString("hello", 0, 0).empty() && h.PasteFromString("{\"format\":\"other\"}", 0, 0).empty() &&
              h.GetNodes().size() == 5, "paste: text that is not a node graph is ignored");
    }

    void TestSearch()
    {
        const auto& r = NodeRegistry::Builtin();
        auto first = [&](const char* q) { const auto res = r.Search(q); return res.empty() ? std::string() : res.front()->Id; };
        auto contains = [&](const char* q, const char* id)
        {
            for (const NodeTypeInfo* t : r.Search(q))
                if (t->Id == id)
                    return true;
            return false;
        };
        Check(r.Search("").size() == r.All().size(), "search: empty query lists every type");
        Check(first("长方体") == "solid.box" && first("box") == "solid.box" && first("BOX") == "solid.box", "search: Chinese name, id, case-insensitive");
        Check(first("加") == "math.add" && first("math.add") == "math.add", "search: exact name / id ranks first");
        Check(contains("lnrarr", "xform.lineararray"), "search: fuzzy subsequence on the id");
        Check(contains("实体", "solid.union") && contains("实体 并", "solid.union") && !contains("实体 并", "solid.box"),
              "search: category and multiple terms");
        Check(r.Search("zzzzqqq").empty(), "search: no match");
    }

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

        const NodeId rot = Add(g, "xform.rotate");
        g.Connect({ circle, 0, rot, 0 });
        g.SetParam(rot, 0, int64_t(0));   // 绕 X 轴
        g.Evaluate({});
        Check(g.GetNode(rot)->State == NodeState::Error, "transform: curves can only rotate about Z");

        // 没有内核时实体节点报错
        const NodeId box = Add(g, "solid.box");
        g.Evaluate({});
        Check(g.GetNode(box)->State == NodeState::Error && g.GetNode(box)->Message.find("内核") != std::string::npos,
              "solid: without a kernel solid nodes report it");
    }

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

        // 图纸改了：场景版本变化时依赖场景的节点重算
        auto* line = static_cast<LineEntity*>(scene.GetEntity(lineId));
        line->SetLine(Line({ 0, 0, 0 }, { 6, 8, 0 }));
        scene.MarkEntityDirty(lineId);
        ctx.SceneVersion = scene.GeometryVersion();
        Check(g.NeedsEvaluate(ctx), "reference: a new scene version needs evaluation");
        g.Evaluate(ctx);
        Check(Near(Num(g, len), 10.0), "reference: follows the drawing");

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
}

// 按菜单顺序输出节点清单（Markdown），用来核对 docs/NodeGraph/PLAN.md 第 4.2 节
void DumpNodes()
{
    std::string cat;
    for (const NodeTypeInfo* t : NodeRegistry::Builtin().Ordered())
    {
        if (t->Category != cat)
        {
            cat = t->Category;
            std::printf("\n### %s\n\n| 节点 | ID | 输入 | 输出 |\n|---|---|---|---|\n", cat.c_str());
        }
        auto ports = [](const std::vector<PortSpec>& ps)
        {
            std::string s;
            for (const auto& p : ps)
                s += (s.empty() ? "" : "、") + p.Name + "（" + KindName(p.Type, p.Kind) + "）";
            return s;
        };
        std::printf("| %s | `%s` | %s | %s |\n", t->Name.c_str(), t->Id.c_str(), ports(t->Inputs).c_str(), ports(t->Outputs).c_str());
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--dump-nodes")
    {
        DumpNodes();
        return 0;
    }
    TestRegistry();
    TestCategoryOrder();
    TestSearchForPort();
    TestBasicsAndDirty();
    TestLinksRules();
    TestLongestList();
    TestSerialization();
    TestHistoryAndDuplicate();
    TestCopyPaste();
    TestSearch();
    TestCurvesWithoutKernel();
    TestReferenceAndDocument();
    TestPreviewAndBake();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}
