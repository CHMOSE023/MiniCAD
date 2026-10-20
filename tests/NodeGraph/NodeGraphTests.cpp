// 节点图核心测试（docs/NodeGraph/PLAN.md）：不需要实体内核的部分。
// 注册表与分类顺序表、按端口搜索、连线规则（类型、环、替换 / 追加、断开输出）、脏标记与只重算脏节点、最长列表匹配、类型转换、
// 读写往返（含未知节点类型）、撤销 / 重做、复制、曲线 / 变换 / 引用节点、没有内核时实体节点的报错、
// 文档保存节点图、预览收集、烘焙与撤销；命名参数、公式与族类型（N7）
#include "NodeGraph/Expression.h"
#include "NodeGraph/Graph.h"
#include "NodeGraph/GraphHistory.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <numbers>
#include <string>
#include <thread>

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

    // 组节点（N11b，PLAN.md 第 12 节）：创建、求值与组内增量、多个实例、读写、组内编辑与接口变化、禁止递归、嵌套、解组、复制粘贴、后台求值
    void TestGroups()
    {
        Graph g;
        const NodeId slider = Add(g, "input.number"), add = Add(g, "math.add"), mul = Add(g, "math.mul"), neg = Add(g, "math.neg");
        g.SetParam(slider, 0, 3.0);
        g.SetInputValue(add, 1, 1.0);
        g.SetInputValue(mul, 1, 2.0);
        g.SetPosition(add, 200, 0);
        g.SetPosition(mul, 400, 0);
        g.SetPosition(neg, 600, 0);
        g.Connect({ slider, 0, add, 0 });
        g.Connect({ add, 0, mul, 0 });
        g.Connect({ mul, 0, neg, 0 });
        g.Evaluate({});
        Check(Near(Num(g, neg), -8.0), "group: before grouping -(3 + 1) × 2 = -8");

        // 创建组：边界连线变成接口
        const NodeId inst = g.CreateGroup({ add, mul });
        const Node* in = g.GetNode(inst);
        const GroupDef* def = in ? g.Groups().Get(in->TypeId) : nullptr;
        Check(in && def && !g.GetNode(add) && !g.GetNode(mul) && g.GetNodes().size() == 3 && def->Name == "组 1",
              "group: selected nodes are replaced by an instance");
        Check(def && def->Type->Inputs.size() == 1 && def->Type->Outputs.size() == 1 && def->Type->Inputs[0].Type == DataType::Number &&
              def->Type->Outputs[0].Type == DataType::Number && def->Type->Category == "组",
              "group: one input and one output, types taken from the inner ports");
        Check(g.IsInputConnected(inst, 0) && g.IsOutputConnected(inst, 0) && g.GetLinks().size() == 2, "group: outer links reconnected to the instance");
        g.Evaluate({});
        Check(Near(Num(g, neg), -8.0) && g.GetNode(inst)->Inner && g.GetNode(inst)->State == NodeState::Ok, "group: instance evaluates its inner graph");

        // 实例的输入没变：组内不重算；输入变了：组内重算
        const uint64_t innerSolves = g.GetNode(inst)->Inner->GetNode(mul)->SolveCount;
        g.MarkDirty(inst);
        g.Evaluate({});
        Check(g.GetNode(inst)->Inner->GetNode(mul)->SolveCount == innerSolves, "group: same inputs → inner nodes are not re-solved");
        g.SetParam(slider, 0, 5.0);
        g.Evaluate({});
        Check(Near(Num(g, neg), -12.0) && g.GetNode(inst)->Inner->GetNode(mul)->SolveCount == innerSolves + 1, "group: new inputs → inner graph re-solved");

        // 第二个实例：输入不同，结果互不影响
        const NodeId inst2 = Add(g, def->Id.c_str());
        g.SetInputValue(inst2, 0, Value(10.0));   // 内联值（没有连线时用）
        g.Evaluate({});
        Check(Near(Num(g, inst2), 22.0) && Near(Num(g, neg), -12.0), "group: a second instance has its own inner graph");

        // 实例上没有数据、组输入也没有默认：组内缺少输入，实例没有输出
        const NodeId inst3 = Add(g, def->Id.c_str());
        g.Evaluate({});
        Check(Out(g, inst3).empty() && g.GetNode(inst3)->State == NodeState::Ok, "group: no data and no default → no output");

        // 读写往返
        {
            Graph h;
            Check(h.LoadFromString(g.SaveToString()) && !h.Groups().Empty() && h.Groups().Get(def->Id), "group: definitions are saved with the graph");
            h.Evaluate({});
            Check(Near(Num(h, neg), -12.0) && Near(Num(h, inst2), 22.0), "group: loaded graph evaluates the same");
        }

        // 组内编辑：沿路径进入实例，改值后写回定义，所有实例更新
        std::vector<NodeId> path{ inst };
        Graph* inner = g.ResolvePath(path);
        Check(inner && inner == g.GetNode(inst)->Inner.get() && inner->OwnerGroup() == def->Id, "group: resolve path gives the instance's inner graph");
        inner->SetInputValue(mul, 1, 3.0);
        g.CommitGroupEdit(path, true);
        g.Evaluate({});
        Check(Near(Num(g, neg), -18.0) && Near(Num(g, inst2), 33.0), "group: editing inside updates the definition and every instance");
        Check(!inner->CanAddType(def->Id) && !inner->AddNode(def->Id), "group: an instance of the group cannot be placed inside itself");

        // 组输入的“默认”：实例上没有数据时输出（组内的滑块 6 → (6 + 1) × 3）
        const NodeId innerSlider = inner->AddNode("input.number", -480, 0)->Id;
        inner->SetParam(innerSlider, 0, 6.0);
        inner->Connect({ innerSlider, 0, def->InputNodes[0], 0 });
        g.CommitGroupEdit(path, true);
        g.Evaluate({});
        Check(Near(Num(g, inst3), 21.0) && Near(Num(g, inst2), 33.0) && Near(Num(g, neg), -18.0),
              "group: the group input's default is used only when the instance has no data");

        // 接口变化：新增一个组输入（放在下面）→ 实例多一个输入，原有连线按名称保留
        Node* gi2 = inner->AddNode(kGroupInputType, -240, 300);
        inner->SetParam(gi2->Id, 0, Value("系数"));
        inner->Connect({ gi2->Id, 0, mul, 1 });
        g.CommitGroupEdit(path, true);
        Check(def->Type->Inputs.size() == 2 && def->Type->Inputs[1].Name == "系数" && g.IsInputConnected(inst, 0) &&
              g.GetNode(inst2)->InputValues.size() == 2 && g.GetNode(inst2)->InputValues[0].Type() == DataType::Number,
              "group: a new group input adds an instance port; links and inline values follow by name");
        g.SetInputValue(inst, 1, Value(4.0));
        g.Evaluate({});
        Check(Near(Num(g, neg), -24.0), "group: the new port feeds the inner graph");
        // 改名：按名称对应不上的连线丢弃
        const std::string firstName = def->Type->Inputs[0].Name;
        inner->SetParam(def->InputNodes[0], 0, Value("x"));
        g.CommitGroupEdit(path, true);
        Check(def->Type->Inputs[0].Name == "x" && !g.IsInputConnected(inst, 0), "group: renaming a port drops links that no longer match by name");
        g.Connect({ slider, 0, inst, 0 });
        g.Evaluate({});
        Check(Near(Num(g, neg), -24.0), "group: reconnected after rename");

        // 组内出错：汇总到实例上
        inner->SetInputValue(mul, 0, Value(0.0));
        const NodeId div = inner->AddNode("math.div", 400, 200)->Id;
        inner->SetInputValue(div, 1, 0.0);
        g.CommitGroupEdit(path, true);
        g.Evaluate({});
        Check(g.GetNode(inst)->State == NodeState::Warning && g.GetNode(inst)->Message.find("组内 1 个节点出错") != std::string::npos,
              "group: inner errors are summarized on the instance");
        inner->RemoveNode(div);
        g.CommitGroupEdit(path, true);

        // 嵌套：把实例与取负再收成一个组（取负后面接一个节点，组才有输出）
        const NodeId sink = Add(g, "math.neg");
        g.Connect({ neg, 0, sink, 0 });
        const NodeId outer = g.CreateGroup({ inst, neg });
        const GroupDef* def2 = g.Groups().Get(g.GetNode(outer)->TypeId);
        g.Evaluate({});
        Check(def2 && g.Groups().Uses(def2->Id, def->Id) && !g.Groups().Uses(def->Id, def2->Id) && Out(g, outer).size() == 1 &&
              Near(Num(g, outer), -24.0), "group: nested group evaluates");
        std::vector<NodeId> deep{ outer, inst };
        Graph* deepGraph = g.ResolvePath(deep);
        Check(deep.size() == 2 && deepGraph && deepGraph->OwnerGroup() == def->Id && !deepGraph->CanAddType(def2->Id),
              "group: path into a nested instance; the outer group cannot be placed inside the inner one");
        deepGraph->SetInputValue(add, 1, 2.0);
        g.CommitGroupEdit(deep, true);
        g.Evaluate({});
        Check(Near(Num(g, outer), -28.0), "group: editing a nested group marks the outer instance dirty");

        // 解组：内部节点放回，外部连线恢复
        const auto restored = g.Ungroup(outer);
        g.Evaluate({});
        NodeId negBack = kInvalidNode;
        for (NodeId id : restored)
            if (g.GetNode(id)->TypeId == "math.neg")
                negBack = id;
        Check(restored.size() == 2 && !g.GetNode(outer) && negBack != kInvalidNode && Near(Num(g, negBack), -28.0),
              "group: ungroup restores the nodes and the outer links");
        Check(g.RemoveGroupIfUnused(def2->Id) && !g.Groups().Get(def2->Id) && !g.RemoveGroupIfUnused(def->Id),
              "group: unused definitions can be removed, used ones are kept");

        // 复制粘贴：带上组定义；粘贴到另一张图新建定义，同一张图里内容相同时复用
        NodeId instBack = kInvalidNode;
        for (NodeId id : restored)
            if (g.GetNode(id)->IsGroup())
                instBack = id;
        const std::string clip = g.CopyToString({ instBack, slider });
        Graph other;
        const auto pasted = other.PasteFromString(clip, 0, 0);
        other.Evaluate({});
        NodeId otherInst = kInvalidNode;
        for (NodeId id : pasted)
            if (other.GetNode(id)->IsGroup())
                otherInst = id;
        Check(pasted.size() == 2 && other.Groups().Defs().size() == 1 && otherInst != kInvalidNode && Out(other, otherInst).size() == 1,
              "group: paste into another graph brings the definition");
        const size_t defsBefore = g.Groups().Defs().size();
        g.PasteFromString(clip, 0, 300);
        Check(g.Groups().Defs().size() == defsBefore, "group: pasting the same definition reuses it");
        inner = g.ResolvePath(path = { instBack });
        inner->SetInputValue(mul, 1, 7.0);
        g.CommitGroupEdit(path, true);
        g.PasteFromString(clip, 0, 600);
        Check(g.Groups().Defs().size() == defsBefore + 1, "group: pasting a changed definition creates a new one");

        // 后台求值：副本带着组内图，合并回来时组内图一起合并
        g.SetParam(slider, 0, 1.0);
        auto copy = g.PrepareBackground({});
        Graph* liveInner = g.GetNode(instBack)->Inner.get();
        copy->Evaluate({});
        g.MergeEvaluated(*copy);
        Check(!g.AnyDirty() && g.GetNode(instBack)->Inner.get() == liveInner && !liveInner->AnyDirty() && Out(g, instBack).size() == 1,
              "group: background results merge into the live inner graph");
        g.SetParam(slider, 0, 2.0);
        copy = g.PrepareBackground({});
        const auto pending = copy->DirtyStamps();
        inner = g.ResolvePath(path = { instBack });
        inner->SetInputValue(mul, 1, 8.0);
        g.CommitGroupEdit(path, true);
        Check(g.IsStale(pending), "group: editing inside a group makes the background job stale");

        // 文档保存（.mcad / JSON）
        for (const char* name : { "minicad_group_test.json", "minicad_group_test.mcad" })
        {
            Document doc;
            doc.GetNodeGraph().LoadFromString(g.SaveToString());
            const std::string file = (std::filesystem::temp_directory_path() / name).string();
            Document loaded;
            Check(doc.SaveToFile(file) && loaded.LoadFromFile(file) && loaded.FindNodeGraph() &&
                  loaded.FindNodeGraph()->Groups().Defs().size() == g.Groups().Defs().size(), "group: document save / load keeps the definitions");
        }

        // 只有组定义、没有节点的图也保存组库
        Graph empty;
        empty.Groups().Add("空组", nullptr);
        Graph back;
        Check(back.LoadFromString(empty.SaveToString()) && back.Groups().Defs().size() == 1, "group: an empty graph with a definition round-trips");
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
    TestExpression();
    TestDataNodes();
    TestCurvesWithoutKernel();
    TestReferenceAndDocument();
    TestPreviewAndBake();
    TestParameters();
    TestBackground();
    TestGroups();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}
