// NodeGraphTests：组节点（N11b）。
// 创建 / 解组、组内增量求值、多个实例、接口默认值、读写、组内编辑与接口按名称重新对应、撤销、复制粘贴、文档保存
// 实现：src/NodeGraph/Group.cpp、Graph.cpp（CreateGroup / Ungroup / ResolvePath / CommitGroupEdit）
// 计划：docs/NodeGraph/PLAN.md 第 12 节
#include "NodeGraphTestCommon.h"

namespace NodeGraphTest
{
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
