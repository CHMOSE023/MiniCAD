#include "NodeGraph/Graph.h"
#include "NodeGraph/Expression.h"
#include "NodeGraph/MeshOps.h"
#include "Serialization/ISerializer.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <set>

namespace MiniCAD::Nodes
{
    // ═════════════════════════════ 节点与连线 ═════════════════════════════

    Graph::Graph(const NodeRegistry& registry) : m_registry(&registry)
    {
        static std::atomic<uint64_t> s_uid{ 0 };
        m_uid       = ++s_uid;
        m_ownGroups = std::make_unique<GroupLibrary>(registry);
        m_groups    = m_ownGroups.get();
    }

    const NodeTypeInfo* Graph::FindType(const std::string& id) const
    {
        return IsGroupTypeId(id) ? m_groups->Find(id) : m_registry->Find(id);
    }
    Graph::~Graph() = default;

    Node* Graph::NewNode(const std::string& typeId, NodeId id)
    {
        auto node    = std::make_unique<Node>();
        node->Id     = id;
        node->TypeId = typeId;
        node->Type   = FindType(typeId);
        SetDirty(*node);
        if (node->Type)
        {
            for (const auto& p : node->Type->Inputs)
                node->InputValues.push_back(p.Default);
            for (const auto& p : node->Type->Params)
                node->Params.push_back(p.Default);
            node->Outputs.resize(node->Type->Outputs.size());
        }
        m_nextId = std::max(m_nextId, id + 1);
        m_nodes.push_back(std::move(node));
        Touch();
        return m_nodes.back().get();
    }

    Node* Graph::AddNode(const std::string& typeId, float x, float y)
    {
        if (!FindType(typeId) || !CanAddType(typeId))
            return nullptr;
        Node* n = NewNode(typeId, m_nextId);
        n->X = x;
        n->Y = y;
        UniqueParamName(*n);
        return n;
    }

    bool Graph::RemoveNode(NodeId id)
    {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const auto& n) { return n->Id == id; });
        if (it == m_nodes.end())
            return false;
        // 下游失去输入，要重算
        for (const auto& l : m_links)
            if (l.FromNode == id)
                MarkDirty(l.ToNode);
        std::erase_if(m_links, [&](const Link& l) { return l.FromNode == id || l.ToNode == id; });
        m_nodes.erase(it);
        Touch();
        return true;
    }

    Node* Graph::GetNode(NodeId id)
    {
        for (auto& n : m_nodes)
            if (n->Id == id) return n.get();
        return nullptr;
    }

    const Node* Graph::GetNode(NodeId id) const
    {
        for (const auto& n : m_nodes)
            if (n->Id == id) return n.get();
        return nullptr;
    }

    void Graph::BringToFront(NodeId id)
    {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const auto& n) { return n->Id == id; });
        if (it != m_nodes.end() && it + 1 != m_nodes.end())
            std::rotate(it, it + 1, m_nodes.end());
    }

    void Graph::Clear()
    {
        m_nodes.clear();
        m_links.clear();
        m_paramTypes.clear();
        m_params = {};
        m_nextId = 1;
        m_groupInputs.clear();
        if (IsTopLevel())
            m_ownGroups->Clear();   // 组库属于顶层的图
        Touch();
    }

    bool Graph::Reaches(NodeId from, NodeId to) const
    {
        if (from == to)
            return true;
        std::vector<NodeId> stack{ from };
        std::set<NodeId>    seen{ from };
        while (!stack.empty())
        {
            const NodeId cur = stack.back();
            stack.pop_back();
            for (const auto& l : m_links)
                if (l.FromNode == cur && seen.insert(l.ToNode).second)
                {
                    if (l.ToNode == to)
                        return true;
                    stack.push_back(l.ToNode);
                }
        }
        return false;
    }

    bool Graph::CanConnect(const Link& link, std::string* why) const
    {
        auto fail = [&](const char* msg) { if (why) *why = msg; return false; };
        const Node* from = GetNode(link.FromNode);
        const Node* to   = GetNode(link.ToNode);
        if (!from || !to)
            return fail("节点不存在");
        if (!from->Type || !to->Type)
            return fail("缺失的节点不能连线");
        if (link.FromPort < 0 || link.FromPort >= static_cast<int>(from->Type->Outputs.size()) ||
            link.ToPort < 0 || link.ToPort >= static_cast<int>(to->Type->Inputs.size()))
            return fail("端口不存在");
        if (!CanConvert(from->Type->Outputs[link.FromPort].Type, to->Type->Inputs[link.ToPort].Type))
            return fail("数据类型不兼容");
        if (Reaches(link.ToNode, link.FromNode))
            return fail("连线会形成环");
        if (std::find(m_links.begin(), m_links.end(), link) != m_links.end())
            return fail("已经连接");
        return true;
    }

    bool Graph::Connect(const Link& link, bool replace)
    {
        if (replace)
        {
            // 替换时先看去掉旧连线后是否仍然有效（同一条连线重复连接视为成功）
            if (std::find(m_links.begin(), m_links.end(), link) != m_links.end())
            {
                std::erase_if(m_links, [&](const Link& l) { return l.ToNode == link.ToNode && l.ToPort == link.ToPort && !(l == link); });
                MarkDirty(link.ToNode);
                Touch();
                return true;
            }
        }
        if (!CanConnect(link))
            return false;
        if (replace)
            std::erase_if(m_links, [&](const Link& l) { return l.ToNode == link.ToNode && l.ToPort == link.ToPort; });
        m_links.push_back(link);
        MarkDirty(link.ToNode);
        Touch();
        return true;
    }

    bool Graph::Disconnect(const Link& link)
    {
        auto it = std::find(m_links.begin(), m_links.end(), link);
        if (it == m_links.end())
            return false;
        m_links.erase(it);
        MarkDirty(link.ToNode);
        Touch();
        return true;
    }

    void Graph::DisconnectInput(NodeId node, int port)
    {
        if (std::erase_if(m_links, [&](const Link& l) { return l.ToNode == node && l.ToPort == port; }) > 0)
        {
            MarkDirty(node);
            Touch();
        }
    }

    void Graph::DisconnectOutput(NodeId node, int port)
    {
        const std::vector<Link> links = LinksFrom(node, port);
        if (links.empty())
            return;
        std::erase_if(m_links, [&](const Link& l) { return l.FromNode == node && l.FromPort == port; });
        for (const Link& l : links)
            MarkDirty(l.ToNode);
        Touch();
    }

    std::vector<Link> Graph::LinksFrom(NodeId node, int port) const
    {
        std::vector<Link> out;
        for (const auto& l : m_links)
            if (l.FromNode == node && l.FromPort == port)
                out.push_back(l);
        return out;
    }

    std::vector<Link> Graph::LinksInto(NodeId node, int port) const
    {
        std::vector<Link> out;
        for (const auto& l : m_links)
            if (l.ToNode == node && l.ToPort == port)
                out.push_back(l);
        return out;
    }

    bool Graph::IsOutputConnected(NodeId node, int port) const
    {
        return std::any_of(m_links.begin(), m_links.end(), [&](const Link& l) { return l.FromNode == node && l.FromPort == port; });
    }

    bool Graph::IsInputConnected(NodeId node, int port) const
    {
        return std::any_of(m_links.begin(), m_links.end(), [&](const Link& l) { return l.ToNode == node && l.ToPort == port; });
    }

    // ═════════════════════════════ 值 ═════════════════════════════

    bool Graph::SetInputValue(NodeId id, int port, const Value& v)
    {
        Node* n = GetNode(id);
        if (!n || !n->Type || port < 0 || port >= static_cast<int>(n->InputValues.size()))
            return false;
        Value converted = v;
        const DataType t = n->Type->Inputs[port].Type;
        if (!v.IsEmpty() && t != DataType::Any && t != DataType::Geometry && !Convert(v, t, converted))
            return false;
        n->InputValues[port] = converted;
        MarkDirty(id);
        Touch();
        return true;
    }

    bool Graph::SetParam(NodeId id, int index, const Value& v)
    {
        Node* n = GetNode(id);
        if (!n || !n->Type || index < 0 || index >= static_cast<int>(n->Params.size()))
            return false;
        const ParamSpec& spec = n->Type->Params[index];
        Value converted;
        if (!Convert(v, spec.Default.Type(), converted))
            return false;
        if (converted.Type() == DataType::Number)
            converted = Value(std::clamp(converted.AsNumber(), spec.Min, spec.Max));
        else if (converted.Type() == DataType::Integer)
        {
            int64_t i = converted.AsInteger();
            if (spec.Widget == ParamWidget::Choice)
                i = std::clamp<int64_t>(i, 0, std::max<int64_t>(0, static_cast<int64_t>(spec.Choices.size()) - 1));
            else
                i = static_cast<int64_t>(std::clamp(static_cast<double>(i), spec.Min, spec.Max));
            converted = Value(i);
        }
        n->Params[index] = converted;
        MarkDirty(id);
        Touch();
        return true;
    }

    void Graph::SetPreview(NodeId id, bool on)
    {
        if (Node* n = GetNode(id); n && n->Preview != on)
        {
            n->Preview = on;
            Touch();
        }
    }

    void Graph::SetPosition(NodeId id, float x, float y)
    {
        if (Node* n = GetNode(id))
        {
            n->X = x;
            n->Y = y;
            Touch();
        }
    }

    std::vector<NodeId> Graph::Duplicate(const std::vector<NodeId>& ids, float dx, float dy)
    {
        std::map<NodeId, NodeId> remap;
        std::vector<NodeId> out;
        for (NodeId id : ids)
        {
            const Node* src = GetNode(id);
            if (!src || !src->Type || remap.count(id))
                continue;
            Node* n        = NewNode(src->TypeId, m_nextId);
            n->X           = src->X + dx;
            n->Y           = src->Y + dy;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
            UniqueParamName(*n);
            remap[id] = n->Id;
            out.push_back(n->Id);
        }
        const std::vector<Link> links = m_links;
        for (const auto& l : links)
        {
            auto f = remap.find(l.FromNode), t = remap.find(l.ToNode);
            if (f != remap.end() && t != remap.end())
                m_links.push_back({ f->second, l.FromPort, t->second, l.ToPort });
        }
        return out;
    }

    std::string Graph::CopyToString(const std::vector<NodeId>& ids) const
    {
        Graph sub(*m_registry);
        // 用到的组定义（含嵌套）一起带上，被用到的在前
        std::set<std::string> needed;
        std::function<void(const std::string&)> need = [&](const std::string& id)
        {
            const GroupDef* d = m_groups->Get(id);
            if (!d || !needed.insert(id).second)
                return;
            for (const auto& n : d->Inner->GetNodes())
                if (IsGroupTypeId(n->TypeId))
                    need(n->TypeId);
        };
        for (NodeId id : ids)
            if (const Node* src = GetNode(id); src && IsGroupTypeId(src->TypeId))
                need(src->TypeId);
        for (const GroupDef* d : m_groups->DependencyOrder())
            if (needed.count(d->Id))
                sub.Groups().Add(d->Name, d->Inner->CloneDefinition(&sub.Groups()), d->Id);
        for (NodeId id : ids)
        {
            const Node* src = GetNode(id);
            if (!src || !src->Type || sub.GetNode(id))
                continue;
            Node* n        = sub.NewNode(src->TypeId, id);
            n->X           = src->X;
            n->Y           = src->Y;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
        }
        if (sub.Empty())
            return {};
        for (const Link& l : m_links)
            if (sub.GetNode(l.FromNode) && sub.GetNode(l.ToNode))
                sub.m_links.push_back(l);
        return sub.SaveToString();
    }

    std::vector<NodeId> Graph::PasteFromString(const std::string& json, float x, float y)
    {
        Graph sub(*m_registry);
        if (json.find("MiniCAD-NodeGraph") == std::string::npos || !sub.LoadFromString(json))
            return {};
        // 组定义：组库里已有同 ID、内容相同的直接用；同 ID 内容不同的作为新定义（改 ID），被用到的先处理
        std::map<std::string, std::string> typeRemap;
        for (const GroupDef* d : sub.Groups().DependencyOrder())
        {
            auto candidate = d->Inner->CloneDefinition(m_groups, &typeRemap);
            const GroupDef* existing = m_groups->Get(d->Id);
            if (existing && existing->Name == d->Name && existing->Inner->SaveToString() == candidate->SaveToString())
                typeRemap[d->Id] = existing->Id;
            else
                typeRemap[d->Id] = m_groups->Add(d->Name, std::move(candidate), existing ? std::string() : d->Id).Id;
        }
        float minX = 1e30f, minY = 1e30f;
        for (const auto& n : sub.m_nodes)
            if (n->Type)
            {
                minX = std::min(minX, n->X);
                minY = std::min(minY, n->Y);
            }
        std::map<NodeId, NodeId> remap;
        std::vector<NodeId> out;
        for (const auto& src : sub.m_nodes)
        {
            const auto mapped = typeRemap.find(src->TypeId);
            const std::string typeId = mapped != typeRemap.end() ? mapped->second : src->TypeId;
            if (!src->Type || !CanAddType(typeId))   // 本程序不认识的节点类型不粘贴；组内不粘贴用到本组的组实例
                continue;
            Node* n        = NewNode(typeId, m_nextId);
            n->X           = src->X - minX + x;
            n->Y           = src->Y - minY + y;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
            UniqueParamName(*n);
            remap[src->Id] = n->Id;
            out.push_back(n->Id);
        }
        for (const Link& l : sub.m_links)
        {
            auto f = remap.find(l.FromNode), t = remap.find(l.ToNode);
            if (f != remap.end() && t != remap.end())
                m_links.push_back({ f->second, l.FromPort, t->second, l.ToPort });
        }
        return out;
    }

    // ═════════════════════════════ 求值 ═════════════════════════════

    void Graph::MarkDirty(NodeId id)
    {
        std::vector<NodeId> stack{ id };
        std::set<NodeId>    seen;
        while (!stack.empty())
        {
            const NodeId cur = stack.back();
            stack.pop_back();
            if (!seen.insert(cur).second)
                continue;
            if (Node* n = GetNode(cur))
                SetDirty(*n);
            for (const auto& l : m_links)
                if (l.FromNode == cur)
                    stack.push_back(l.ToNode);
        }
    }

    void Graph::MarkAllDirty()
    {
        for (auto& n : m_nodes)
            SetDirty(*n);
    }

    bool Graph::NeedsEvaluate(const SolveContext& ctx) const
    {
        for (const auto& n : m_nodes)
            if (n->Dirty || (n->Type && n->Type->DependsOnScene && n->m_sceneVersion != ctx.SceneVersion))
                return true;
        return false;
    }

    std::vector<NodeId> Graph::TopologicalOrder() const
    {
        std::map<NodeId, int> indeg;
        for (const auto& n : m_nodes)
            indeg[n->Id] = 0;
        for (const auto& l : m_links)
            ++indeg[l.ToNode];
        std::vector<NodeId> ready, order;
        for (const auto& n : m_nodes)     // 按节点顺序，结果稳定
            if (indeg[n->Id] == 0)
                ready.push_back(n->Id);
        while (!ready.empty())
        {
            const NodeId cur = ready.front();
            ready.erase(ready.begin());
            order.push_back(cur);
            for (const auto& l : m_links)
                if (l.FromNode == cur && --indeg[l.ToNode] == 0)
                    ready.push_back(l.ToNode);
        }
        return order;   // 图无环，包含全部节点
    }

    bool Graph::Evaluate(const SolveContext& ctx)
    {
        ResolveParameters();
        SolveContext c = ctx;
        c.Params = &m_params;

        for (auto& n : m_nodes)
            if (n->Type && n->Type->DependsOnScene && n->m_sceneVersion != ctx.SceneVersion)
            {
                n->m_sceneVersion = ctx.SceneVersion;
                MarkDirty(n->Id);
            }

        bool solved = false;
        for (NodeId id : TopologicalOrder())
        {
            if (c.Cancelled())
                break;
            Node* n = GetNode(id);
            if (!n || !n->Dirty)
                continue;
            if (!SolveNode(*n, c))
                break;
            solved = true;
            if (c.Solved)
                c.Solved();
        }
        if (solved)
            ++m_evalVersion;
        return solved;
    }

    std::unique_ptr<Graph> Graph::PrepareBackground(const SolveContext& ctx)
    {
        ResolveParameters();
        SolveContext c = ctx;
        c.Params = &m_params;

        // 依赖图纸的节点（引用节点、组内有引用节点的组实例）读场景，在这里先算好，连同它们的脏上游；副本里它们就是干净的
        for (auto& n : m_nodes)
            if (n->Type && n->Type->DependsOnScene && n->m_sceneVersion != ctx.SceneVersion)
            {
                n->m_sceneVersion = ctx.SceneVersion;
                MarkDirty(n->Id);
            }
        std::set<NodeId> here;
        std::vector<NodeId> stack;
        for (const auto& n : m_nodes)
            if (n->Dirty && n->Type && n->Type->DependsOnScene)
                stack.push_back(n->Id);
        while (!stack.empty())
        {
            const NodeId cur = stack.back();
            stack.pop_back();
            if (!here.insert(cur).second)
                continue;
            for (const auto& l : m_links)
                if (l.ToNode == cur)
                    if (const Node* src = GetNode(l.FromNode); src && src->Dirty)
                        stack.push_back(l.FromNode);
        }
        bool solved = false;
        if (!here.empty())
            for (NodeId id : TopologicalOrder())
                if (Node* n = GetNode(id); n && n->Dirty && here.count(id))
                {
                    SolveNode(*n, c);
                    solved = true;
                }
        if (solved)
            ++m_evalVersion;

        std::set<NodeId> dirty;
        for (const auto& n : m_nodes)
            if (n->Dirty)
                dirty.insert(n->Id);
        if (dirty.empty())
            return nullptr;
        // 干净节点的输出只在连到脏节点时才需要
        std::set<NodeId> feeding;
        for (const auto& l : m_links)
            if (dirty.count(l.ToNode))
                feeding.insert(l.FromNode);

        // 组库深复制：界面线程可能在后台求值期间编辑组定义
        auto copy = std::make_unique<Graph>(*m_registry);
        copy->m_ownGroups = m_groups->Clone();
        copy->m_groups    = copy->m_ownGroups.get();
        copy->m_owner     = m_owner;
        for (const auto& n : m_nodes)
        {
            auto c2 = std::make_unique<Node>();
            Node& d = *c2;
            d.Id = n->Id;  d.TypeId = n->TypeId;  d.Type = copy->FindType(n->TypeId);
            d.InputValues = n->InputValues;
            d.Params      = n->Params;
            d.Dirty       = n->Dirty;
            d.m_sceneVersion = n->m_sceneVersion;
            d.m_stamp        = n->m_stamp;
            if (!n->Dirty && feeding.count(n->Id))
                d.Outputs = n->Outputs;
            else
                d.Outputs.resize(n->Outputs.size());
            if (n->Dirty && n->Inner)   // 要重算的组实例：带上组内图（只重算组内脏的部分）
            {
                d.Inner        = n->Inner->CloneFull(copy->m_groups);
                d.InnerVersion = n->InnerVersion;
            }
            copy->m_nodes.push_back(std::move(c2));
        }
        copy->m_links      = m_links;
        copy->m_nextId     = m_nextId;
        copy->m_params     = m_params;
        copy->m_paramTypes = m_paramTypes;
        copy->m_stampSeq   = m_stampSeq;
        return copy;
    }

    int Graph::MergeEvaluated(Graph& solved)
    {
        int merged = 0, shown = 0;
        for (auto& s : solved.m_nodes)
        {
            if (s->Dirty)
                continue;
            Node* n = GetNode(s->Id);
            if (!n || !n->Dirty || n->TypeId != s->TypeId)
                continue;
            if (n->m_stamp != s->m_stamp)
            {
                // 期间被改动：结果已过时，节点保持为脏；先拿来显示（脏节点的输出在重算前只用于预览与提示，下游重算时上游先重算）
                if (n->Outputs.size() == s->Outputs.size())
                {
                    n->Outputs     = std::move(s->Outputs);
                    n->DisplayData = std::move(s->DisplayData);
                    n->State       = s->State;
                    n->Message     = std::move(s->Message);
                    n->Millis      = s->Millis;
                    ++shown;
                }
                continue;
            }
            if (s->Inner)
            {
                if (!n->Inner || n->InnerVersion != s->InnerVersion)
                {
                    // 副本里重建过组内图：整个拿过来，改用本图的组库
                    n->Inner        = std::move(s->Inner);
                    n->InnerVersion = s->InnerVersion;
                    n->Inner->BindLibrary(m_groups);
                }
                else   // 实例的脏戳没变，组内图期间也没被改过：整个采用副本的结果（对象不换，编辑器可能正显示着它）
                    n->Inner->AdoptEvaluated(*s->Inner);
            }
            n->Outputs     = std::move(s->Outputs);
            n->DisplayData = std::move(s->DisplayData);
            n->State       = s->State;
            n->Message     = std::move(s->Message);
            n->Millis      = s->Millis;
            n->SolveCount += s->SolveCount;
            n->Dirty       = false;
            ++merged;
        }
        if (merged > 0 || shown > 0)
            ++m_evalVersion;
        return merged;
    }

    double Graph::EstimatedMillis() const
    {
        double ms = 0.0;
        for (const auto& n : m_nodes)
            if (n->Dirty)
            {
                if (n->SolveCount == 0)
                    return -1.0;
                ms += n->Millis;
            }
        return ms;
    }

    void Graph::AdoptEvaluated(Graph& solved)
    {
        for (auto& s : solved.m_nodes)
        {
            Node* n = GetNode(s->Id);
            if (!n || n->TypeId != s->TypeId)
                continue;
            n->Outputs     = std::move(s->Outputs);
            n->DisplayData = std::move(s->DisplayData);
            n->State       = s->State;
            n->Message     = std::move(s->Message);
            n->Millis      = s->Millis;
            n->SolveCount += s->SolveCount;
            n->Dirty       = s->Dirty;
            n->m_stamp        = s->m_stamp;
            n->m_sceneVersion = s->m_sceneVersion;
            if (s->Inner)
            {
                if (n->Inner && n->InnerVersion == s->InnerVersion)
                    n->Inner->AdoptEvaluated(*s->Inner);
                else
                {
                    n->Inner        = std::move(s->Inner);
                    n->InnerVersion = s->InnerVersion;
                    n->Inner->BindLibrary(m_groups);
                }
            }
        }
        m_stampSeq    = std::max(m_stampSeq, solved.m_stampSeq);
        m_params      = std::move(solved.m_params);
        m_groupInputs = std::move(solved.m_groupInputs);
        ++m_evalVersion;
    }

    bool Graph::AnyDirty() const
    {
        return std::any_of(m_nodes.begin(), m_nodes.end(), [](const auto& n) { return n->Dirty; });
    }

    std::vector<std::pair<NodeId, uint64_t>> Graph::DirtyStamps() const
    {
        std::vector<std::pair<NodeId, uint64_t>> out;
        for (const auto& n : m_nodes)
            if (n->Dirty)
                out.emplace_back(n->Id, n->m_stamp);
        return out;
    }

    bool Graph::IsStale(const std::vector<std::pair<NodeId, uint64_t>>& pending) const
    {
        for (const auto& [id, stamp] : pending)
        {
            const Node* n = GetNode(id);
            if (!n || n->m_stamp != stamp)
                return true;
        }
        return false;
    }

    DataList GatherInput(const Graph& g, const Node& node, int port)
    {
        DataList list;
        bool linked = false;
        for (const auto& l : g.GetLinks())
        {
            if (l.ToNode != node.Id || l.ToPort != port)
                continue;
            linked = true;
            const Node* src = g.GetNode(l.FromNode);
            if (src && l.FromPort >= 0 && l.FromPort < static_cast<int>(src->Outputs.size()))
                list.insert(list.end(), src->Outputs[l.FromPort].begin(), src->Outputs[l.FromPort].end());
        }
        if (!linked && port >= 0 && port < static_cast<int>(node.InputValues.size()) && !node.InputValues[port].IsEmpty())
            list.push_back(node.InputValues[port]);
        return list;
    }

    namespace
    {
        class Args final : public SolveArgs
        {
        public:
            Args(Node& node, const SolveContext& ctx, std::vector<DataList>& inputs)
                : m_node(node), m_ctx(ctx), m_inputs(inputs), m_current(inputs.size()), m_has(inputs.size(), false) {}

            void SetIteration(int it)
            {
                m_iteration = it;
                for (size_t i = 0; i < m_inputs.size(); ++i)
                {
                    const DataList& l = m_inputs[i];
                    m_has[i]     = !l.empty();
                    m_current[i] = l.empty() ? Value() : l[std::min<size_t>(static_cast<size_t>(it), l.size() - 1)];
                }
            }

            const Value& In(int port) const override
            {
                static const Value kEmpty;
                return port >= 0 && port < static_cast<int>(m_current.size()) ? m_current[port] : kEmpty;
            }
            const DataList& List(int port) const override
            {
                static const DataList kEmpty;
                return port >= 0 && port < static_cast<int>(m_inputs.size()) ? m_inputs[port] : kEmpty;
            }
            bool Has(int port) const override { return port >= 0 && port < static_cast<int>(m_has.size()) && m_has[port]; }
            const Value& Param(int index) const override
            {
                static const Value kEmpty;
                return index >= 0 && index < static_cast<int>(m_node.Params.size()) ? m_node.Params[index] : kEmpty;
            }
            void Out(int port, Value v) override
            {
                if (port >= 0 && port < static_cast<int>(m_node.Outputs.size()) && !v.IsEmpty())
                    m_node.Outputs[port].push_back(std::move(v));
            }
            void Warning(const std::string& msg) override { if (m_warning.empty()) m_warning = msg; }
            void Error(const std::string& msg) override
            {
                if (m_error.empty()) m_error = msg;
                ++m_errorCount;
            }
            int Iteration() const override { return m_iteration; }
            const SolveContext& Context() const override { return m_ctx; }

            std::string m_warning, m_error;
            int         m_errorCount = 0;

        private:
            Node&                  m_node;
            const SolveContext&    m_ctx;
            std::vector<DataList>& m_inputs;
            std::vector<Value>     m_current;
            std::vector<bool>      m_has;
            int                    m_iteration = 0;
        };
    }

    bool Graph::SolveNode(Node& node, const SolveContext& ctx)
    {
        const auto t0 = std::chrono::steady_clock::now();
        node.Dirty = false;
        ++node.SolveCount;
        for (auto& o : node.Outputs)
            o.clear();
        node.DisplayData.clear();
        node.State   = NodeState::Ok;
        node.Message.clear();

        auto finish = [&](NodeState st, std::string msg)
        {
            node.State   = st;
            node.Message = std::move(msg);
            node.Millis  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return true;
        };

        if (!node.Type)
            return finish(NodeState::Error, "未知的节点类型：" + node.TypeId);
        const NodeTypeInfo& type = *node.Type;

        // 收集输入并转换为端口类型
        std::vector<DataList> inputs(type.Inputs.size());
        for (size_t i = 0; i < type.Inputs.size(); ++i)
        {
            const PortSpec& spec = type.Inputs[i];
            DataList raw = GatherInput(*this, node, static_cast<int>(i));
            inputs[i].reserve(raw.size());
            for (size_t k = 0; k < raw.size(); ++k)
            {
                Value v;
                if (spec.Type == DataType::Mesh && raw[k].Type() == DataType::Body)
                {
                    // 实体 / 曲面连到网格端口：按默认精度网格化（需要内核）
                    if (!ctx.Kernel)
                        return finish(NodeState::Error, "输入“" + spec.Name + "”：实体 / 曲面转网格需要实体内核");
                    Solid::Mesh mesh = raw[k].AsBody() ? MeshOps::FromBody(*ctx.Kernel, raw[k].AsBody()) : Solid::Mesh{};
                    if (mesh.TriangleCount() == 0)
                        return finish(NodeState::Error, "输入“" + spec.Name + "”第 " + std::to_string(k + 1) + " 项网格化失败");
                    v = Value(std::move(mesh));
                }
                else if (!Convert(raw[k], spec.Type, v))
                    return finish(NodeState::Error, "输入“" + spec.Name + "”第 " + std::to_string(k + 1) + " 项（" + TypeName(raw[k].Type()) +
                                                    "）不能转换为" + TypeName(spec.Type));
                inputs[i].push_back(std::move(v));
            }
        }
        if (type.Display && !inputs.empty())
            node.DisplayData = inputs[0];

        // 组输入：输出组实例传进来的数据，没有时输出“默认”
        if (type.Id == kGroupInputType)
        {
            const DataList* given = nullptr;
            if (ctx.GroupInputs)
                if (auto it = ctx.GroupInputs->find(node.Id); it != ctx.GroupInputs->end() && !it->second.empty())
                    given = &it->second;
            if (!node.Outputs.empty())
                node.Outputs[0] = given ? *given : inputs[0];
            return finish(NodeState::Ok, {});
        }
        if (node.IsGroup())
        {
            NodeState   st = NodeState::Ok;
            std::string msg;
            if (!SolveGroup(node, ctx, inputs, st, msg))
            {
                node.Dirty = true;   // 被取消：组内已算完的部分留着，下次接着算
                --node.SolveCount;
                return false;
            }
            return finish(st, std::move(msg));
        }

        // 迭代次数：按项访问的输入取最长列表；必需的输入没有数据时不求值
        size_t count = 1;
        bool   anyItem = false;
        for (size_t i = 0; i < type.Inputs.size(); ++i)
        {
            const PortSpec& spec = type.Inputs[i];
            if (inputs[i].empty() && !spec.Optional)
                return finish(NodeState::Warning, "输入“" + spec.Name + "”没有数据");
            if (spec.Acc == Access::Item && !inputs[i].empty())
            {
                count   = anyItem ? std::max(count, inputs[i].size()) : inputs[i].size();
                anyItem = true;
            }
        }
        if (!type.Solve)
            return finish(NodeState::Ok, {});

        // 按列表访问的输入不参与迭代（List 取整个列表），按项访问的输入每次取当前项（短的重复最后一项）
        Args args(node, ctx, inputs);
        for (size_t it = 0; it < count; ++it)
        {
            if (ctx.Cancelled())
            {
                node.Dirty = true;   // 结果不完整，留待下次重算（脏戳不变）
                --node.SolveCount;
                return false;
            }
            args.SetIteration(static_cast<int>(it));
            try
            {
                type.Solve(args);
            }
            catch (const std::exception& e)
            {
                args.Error(std::string("求值异常：") + e.what());
            }
            catch (...)
            {
                args.Error("求值异常");
            }
        }
        if (ctx.Cancelled())   // 节点自己检查了取消、提前返回：结果不完整
        {
            node.Dirty = true;
            --node.SolveCount;
            return false;
        }

        if (!args.m_error.empty())
        {
            std::string msg = args.m_error;
            if (count > 1)
                msg += "（" + std::to_string(args.m_errorCount) + " / " + std::to_string(count) + " 项失败）";
            return finish(NodeState::Error, msg);
        }
        if (!args.m_warning.empty())
            return finish(NodeState::Warning, args.m_warning);
        return finish(NodeState::Ok, {});
    }

    // ═════════════════════════════ 命名参数与类型 ═════════════════════════════

    const std::vector<std::string>& ParamCategoryNames()
    {
        static const std::vector<std::string> kNames = { "长度", "角度", "数值", "整数", "是否" };
        return kNames;
    }

    bool ParameterTable::Number(const std::string& name, double& out) const
    {
        const auto it = Values.find(name);
        if (it == Values.end())
            return false;
        Value v;
        if (!Convert(it->second, DataType::Number, v))
            return false;
        out = v.AsNumber();
        return true;
    }

    namespace
    {
        bool IsParamNode(const Node& n) { return n.Type && n.TypeId == kParamNodeType && n.Params.size() >= 4; }
        const std::string& ParamName(const Node& n) { return n.Params[0].AsText(); }
        const std::string& ParamFormula(const Node& n) { return n.Params[3].AsText(); }

        // 按类别把数值变成参数值：整数取整、是否为布尔
        Value ParamValueOf(const Node& n, double x)
        {
            switch (static_cast<ParamCategory>(n.Params[1].AsInteger()))
            {
            case ParamCategory::Integer: return Value(static_cast<int64_t>(std::llround(x)));
            case ParamCategory::YesNo:   return Value(x != 0.0);
            default:                     return Value(x);
            }
        }

        // 名称要能在公式里当变量用：解析出来正好是这一个变量（不能是数值、函数名、常数 pi / e，不能带运算符）
        bool IsValidParamName(const std::string& name)
        {
            std::string error;
            const auto e = Expression::Parse(name, error);
            return e && e->Variables().size() == 1 && e->Variables()[0] == name;
        }

        bool SameValue(const Value& a, const Value& b)
        {
            if (a.Type() != b.Type())
                return false;
            switch (a.Type())
            {
            case DataType::Number:  return a.AsNumber() == b.AsNumber();
            case DataType::Integer: return a.AsInteger() == b.AsInteger();
            case DataType::Boolean: return a.AsBool() == b.AsBool();
            default:                return a.IsEmpty() && b.IsEmpty();
            }
        }
    }

    std::vector<NodeId> Graph::ParameterNodes() const
    {
        std::vector<NodeId> out;
        for (const auto& n : m_nodes)
            if (IsParamNode(*n))
                out.push_back(n->Id);
        return out;
    }

    void Graph::UniqueParamName(Node& node)
    {
        if (!IsParamNode(node))
            return;
        auto taken = [&](const std::string& name)
        {
            for (const auto& n : m_nodes)
                if (n.get() != &node && IsParamNode(*n) && ParamName(*n) == name)
                    return true;
            return false;
        };
        const std::string name = ParamName(node);
        if (!name.empty() && !taken(name))
            return;
        // 去掉末尾的数字再编号：“宽度2” → “宽度3”
        std::string base = name.empty() ? std::string("参数") : name;
        while (!base.empty() && base.back() >= '0' && base.back() <= '9')
            base.pop_back();
        for (int i = 1;; ++i)
            if (!taken(base + std::to_string(i)))
            {
                node.Params[0] = Value(base + std::to_string(i));
                return;
            }
    }

    void Graph::ResolveParameters()
    {
        ParameterTable table;
        std::map<std::string, Node*> byName;
        std::vector<Node*> params;
        for (const auto& n : m_nodes)
        {
            if (!IsParamNode(*n))
                continue;
            const std::string& name = ParamName(*n);
            if (name.empty())
                continue;   // 没有名称：节点自己报错
            if (!IsValidParamName(name))
                table.Errors[name] = "名称“" + name + "”不能用在公式里（不能是数值、函数名、常数，不能含运算符或空格）";
            else if (byName.count(name))
                table.Errors[name] = "参数名称“" + name + "”重复";
            byName.emplace(name, n.get());
            params.push_back(n.get());
        }

        // 按公式依赖的深度优先顺序求值；正在求值的参数再次被引用即为循环引用
        std::map<std::string, int> state;   // 0 未求值、1 求值中、2 完成
        std::vector<std::string>   stack;
        std::function<void(const std::string&)> resolve = [&](const std::string& name)
        {
            int& st = state[name];
            if (st == 2)
                return;
            if (st == 1)
            {
                // 环：从栈里第一次出现 name 处到栈顶
                auto it = std::find(stack.begin(), stack.end(), name);
                std::string path;
                for (auto p = it; p != stack.end(); ++p)
                    path += *p + " → ";
                path += name;
                for (auto p = it; p != stack.end(); ++p)
                    table.Errors[*p] = "参数之间循环引用：" + path;
                return;
            }
            st = 1;
            stack.push_back(name);
            Node& n = *byName.at(name);
            const std::string& formula = ParamFormula(n);
            if (table.Errors.count(name) == 0)
            {
                if (formula.empty())
                {
                    Value v;
                    Convert(n.Params[2], DataType::Number, v);
                    table.Values[name] = ParamValueOf(n, v.IsEmpty() ? 0.0 : v.AsNumber());
                }
                else
                {
                    std::string error;
                    const auto e = Expression::Parse(formula, error);
                    if (!e)
                        table.Errors[name] = "公式有误，" + error;
                    else
                    {
                        for (const std::string& var : e->Variables())
                        {
                            if (!byName.count(var))
                            {
                                table.Errors[name] = "公式里的“" + var + "”不是参数";
                                break;
                            }
                            resolve(var);
                            if (table.Errors.count(name))
                                break;   // 在环上
                            if (table.Errors.count(var))
                            {
                                table.Errors[name] = "引用的参数“" + var + "”有错误";
                                break;
                            }
                        }
                        double x = 0.0;
                        if (!table.Errors.count(name))
                        {
                            if (e->Evaluate([&](const std::string& var, double& out) { return table.Number(var, out); }, x, error))
                                table.Values[name] = ParamValueOf(n, x);
                            else
                                table.Errors[name] = error;
                        }
                    }
                }
            }
            stack.pop_back();
            st = 2;
        };
        for (Node* n : params)
            resolve(ParamName(*n));

        // 值或错误变了的参数节点、以及引用了变化参数的表达式节点要重算
        std::set<std::string> changed;
        auto collect = [&](const auto& before, const auto& after, auto same)
        {
            for (const auto& [name, v] : after)
            {
                const auto it = before.find(name);
                if (it == before.end() || !same(it->second, v))
                    changed.insert(name);
            }
            for (const auto& [name, v] : before)
                if (!after.count(name))
                    changed.insert(name);
        };
        collect(m_params.Values, table.Values, SameValue);
        collect(m_params.Errors, table.Errors, [](const std::string& a, const std::string& b) { return a == b; });
        m_params = std::move(table);
        if (changed.empty())
            return;
        for (const auto& n : m_nodes)
        {
            if (IsParamNode(*n) && changed.count(ParamName(*n)))
                MarkDirty(n->Id);
            else if (n->TypeId == "math.expression" && !n->Params.empty() && n->Params[0].Type() == DataType::Text)
            {
                std::string error;
                if (const auto e = Expression::Parse(n->Params[0].AsText(), error))
                    for (const std::string& var : e->Variables())
                        if (changed.count(var))
                        {
                            MarkDirty(n->Id);
                            break;
                        }
            }
        }
    }

    void Graph::SaveParamType(const std::string& name, std::map<std::string, double> values)
    {
        if (values.empty())
            for (const auto& n : m_nodes)
                if (IsParamNode(*n) && !ParamName(*n).empty() && ParamFormula(*n).empty())
                {
                    Value v;
                    if (Convert(n->Params[2], DataType::Number, v))
                        values[ParamName(*n)] = v.AsNumber();
                }
        auto it = std::find_if(m_paramTypes.begin(), m_paramTypes.end(), [&](const ParamTypeDef& t) { return t.Name == name; });
        if (it == m_paramTypes.end())
            m_paramTypes.push_back({ name, std::move(values) });
        else
            it->Values = std::move(values);
        Touch();
    }

    bool Graph::RemoveParamType(const std::string& name)
    {
        const size_t before = m_paramTypes.size();
        std::erase_if(m_paramTypes, [&](const ParamTypeDef& t) { return t.Name == name; });
        if (m_paramTypes.size() == before)
            return false;
        Touch();
        return true;
    }

    bool Graph::ApplyParamType(const std::string& name)
    {
        auto it = std::find_if(m_paramTypes.begin(), m_paramTypes.end(), [&](const ParamTypeDef& t) { return t.Name == name; });
        if (it == m_paramTypes.end())
            return false;
        bool changed = false;
        for (const auto& n : m_nodes)
        {
            if (!IsParamNode(*n) || !ParamFormula(*n).empty())
                continue;
            const auto v = it->Values.find(ParamName(*n));
            if (v == it->Values.end())
                continue;
            Value cur;
            if (Convert(n->Params[2], DataType::Number, cur) && cur.AsNumber() == v->second)
                continue;
            SetParam(n->Id, 2, v->second);
            changed = true;
        }
        return changed;
    }

    // ═════════════════════════════ 读写 ═════════════════════════════

    namespace
    {
        // 只有简单类型（内联值、参数）需要存档；曲线、实体是求值结果，不保存
        void WriteValue(ISerializer& s, const Value& v)
        {
            std::string t = TypeKey(v.Type());
            s.Value("t", t);
            switch (v.Type())
            {
            case DataType::Number:  { double d = v.AsNumber(); s.Value("v", d); break; }
            case DataType::Integer: { int64_t i = v.AsInteger(); s.Value("v", i); break; }
            case DataType::Boolean: { bool b = v.AsBool(); s.Value("v", b); break; }
            case DataType::Text:    { std::string str = v.AsText(); s.Value("v", str); break; }
            case DataType::Vector:
            {
                std::vector<double> xyz{ v.AsVector().x, v.AsVector().y, v.AsVector().z };
                s.Value("v", xyz);
                break;
            }
            case DataType::Domain:
            {
                std::vector<double> d{ v.AsDomain().Min, v.AsDomain().Max };
                s.Value("v", d);
                break;
            }
            case DataType::Plane:
            {
                const Plane& p = v.AsPlane();
                std::vector<double> d{ p.Origin.x, p.Origin.y, p.Origin.z, p.X.x, p.X.y, p.X.z, p.Y.x, p.Y.y, p.Y.z };
                s.Value("v", d);
                break;
            }
            default: break;
            }
        }

        Value ReadValue(ISerializer& s)
        {
            std::string key;
            s.Value("t", key);
            DataType t = DataType::Any;
            TypeFromKey(key, t);
            switch (t)
            {
            case DataType::Number:  { double d = 0; s.Value("v", d); return Value(d); }
            case DataType::Integer: { int64_t i = 0; s.Value("v", i); return Value(i); }
            case DataType::Boolean: { bool b = false; s.Value("v", b); return Value(b); }
            case DataType::Text:    { std::string str; s.Value("v", str); return Value(str); }
            case DataType::Vector:
            {
                std::vector<double> xyz;
                s.Value("v", xyz);
                xyz.resize(3, 0.0);
                return Value(Math::Vec3{ xyz[0], xyz[1], xyz[2] });
            }
            case DataType::Domain:
            {
                std::vector<double> d;
                s.Value("v", d);
                d.resize(2, 0.0);
                return Value(Domain{ d[0], d[1] });
            }
            case DataType::Plane:
            {
                std::vector<double> d;
                s.Value("v", d);
                if (d.size() < 9)
                    return Value(Plane{});
                const auto p = Plane::FromAxes({ d[0], d[1], d[2] }, { d[3], d[4], d[5] }, { d[6], d[7], d[8] });
                return Value(p ? *p : Plane{ { d[0], d[1], d[2] } });
            }
            default: return {};
            }
        }

        void WriteValues(ISerializer& s, const char* key, const std::vector<Value>& values)
        {
            size_t n = values.size();
            if (!s.BeginArray(key, n))
                return;
            for (const auto& v : values)
                if (s.BeginElement(0))
                {
                    WriteValue(s, v);
                    s.EndElement();
                }
            s.EndArray();
        }

        std::vector<Value> ReadValues(ISerializer& s, const char* key)
        {
            std::vector<Value> out;
            size_t n = 0;
            if (!s.BeginArray(key, n))
                return out;
            for (size_t i = 0; i < n; ++i)
                if (s.BeginElement(i))
                {
                    out.push_back(ReadValue(s));
                    s.EndElement();
                }
                else
                    out.emplace_back();
            s.EndArray();
            return out;
        }
    }

    void Graph::Serialize(ISerializer& s) const
    {
        std::string format = "MiniCAD-NodeGraph";
        s.Value("format", format);
        int32_t version = 1;
        s.Value("version", version);
        if (IsTopLevel() && !m_groups->Empty())
            m_groups->Serialize(s);

        size_t n = m_nodes.size();
        if (s.BeginArray("nodes", n))
        {
            for (const auto& node : m_nodes)
            {
                if (!s.BeginElement(0))
                    continue;
                uint32_t id = node->Id;
                std::string type = node->TypeId;
                double x = node->X, y = node->Y;
                bool preview = node->Preview;
                s.Value("id", id);
                s.Value("type", type);
                s.Value("x", x);
                s.Value("y", y);
                s.Value("preview", preview);
                WriteValues(s, "inputs", node->InputValues);
                WriteValues(s, "params", node->Params);
                s.EndElement();
            }
            s.EndArray();
        }

        size_t types = m_paramTypes.size();
        if (!m_paramTypes.empty() && s.BeginArray("types", types))
        {
            for (const auto& t : m_paramTypes)
            {
                if (!s.BeginElement(0))
                    continue;
                std::string name = t.Name;
                s.Value("name", name);
                size_t count = t.Values.size();
                if (s.BeginArray("values", count))
                {
                    for (const auto& [param, value] : t.Values)
                        if (s.BeginElement(0))
                        {
                            std::string pn = param;
                            double pv = value;
                            s.Value("param", pn);
                            s.Value("value", pv);
                            s.EndElement();
                        }
                    s.EndArray();
                }
                s.EndElement();
            }
            s.EndArray();
        }

        size_t m = m_links.size();
        if (s.BeginArray("links", m))
        {
            for (const auto& l : m_links)
            {
                if (!s.BeginElement(0))
                    continue;
                uint32_t from = l.FromNode, to = l.ToNode;
                int32_t  fp = l.FromPort, tp = l.ToPort;
                s.Value("from", from);
                s.Value("fromPort", fp);
                s.Value("to", to);
                s.Value("toPort", tp);
                s.EndElement();
            }
            s.EndArray();
        }
    }

    bool Graph::Deserialize(ISerializer& s)
    {
        Clear();
        std::string format;
        s.Value("format", format);
        if (format != "MiniCAD-NodeGraph")
            return false;
        if (IsTopLevel())
            m_groups->Deserialize(s);   // 先读组库：节点里的组类型要用

        size_t n = 0;
        if (s.BeginArray("nodes", n))
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (!s.BeginElement(i))
                    continue;
                uint32_t id = 0;
                std::string type;
                double x = 0, y = 0;
                bool preview = true;
                s.Value("id", id);
                s.Value("type", type);
                s.Value("x", x);
                s.Value("y", y);
                s.Value("preview", preview);
                std::vector<Value> inputs = ReadValues(s, "inputs");
                std::vector<Value> params = ReadValues(s, "params");
                s.EndElement();
                if (id == kInvalidNode || GetNode(id))
                    continue;

                Node* node    = NewNode(type, id);
                node->X       = static_cast<float>(x);
                node->Y       = static_cast<float>(y);
                node->Preview = preview;
                if (!node->Type)
                {
                    node->InputValues = std::move(inputs);   // 缺失节点：原样保留
                    node->Params      = std::move(params);
                    continue;
                }
                // 端口 / 参数按下标对应；数量变了（节点类型升级）时多余的丢弃、缺少的取默认值，类型不符的取默认值
                for (size_t k = 0; k < node->InputValues.size() && k < inputs.size(); ++k)
                {
                    const DataType t = node->Type->Inputs[k].Type;
                    Value v;
                    if (inputs[k].IsEmpty())
                        node->InputValues[k] = Value();
                    else if (t == DataType::Any || t == DataType::Geometry)
                        node->InputValues[k] = inputs[k];
                    else if (Convert(inputs[k], t, v))
                        node->InputValues[k] = v;
                }
                for (size_t k = 0; k < node->Params.size() && k < params.size(); ++k)
                {
                    Value v;
                    if (Convert(params[k], node->Type->Params[k].Default.Type(), v))
                        node->Params[k] = v;
                }
            }
            s.EndArray();
        }

        size_t m = 0;
        if (s.BeginArray("links", m))
        {
            for (size_t i = 0; i < m; ++i)
            {
                if (!s.BeginElement(i))
                    continue;
                uint32_t from = 0, to = 0;
                int32_t  fp = 0, tp = 0;
                s.Value("from", from);
                s.Value("fromPort", fp);
                s.Value("to", to);
                s.Value("toPort", tp);
                s.EndElement();
                const Link link{ from, fp, to, tp };
                Node* src = GetNode(from);
                Node* dst = GetNode(to);
                if (!src || !dst)
                    continue;
                if (src->Type && dst->Type)
                {
                    if (CanConnect(link))
                        m_links.push_back(link);
                }
                else if (!Reaches(to, from) && std::find(m_links.begin(), m_links.end(), link) == m_links.end())
                {
                    m_links.push_back(link);   // 连到缺失节点的连线也保留，类型修复后恢复
                }
            }
            s.EndArray();
        }
        size_t types = 0;
        if (s.BeginArray("types", types))
        {
            for (size_t i = 0; i < types; ++i)
            {
                if (!s.BeginElement(i))
                    continue;
                ParamTypeDef t;
                s.Value("name", t.Name);
                size_t count = 0;
                if (s.BeginArray("values", count))
                {
                    for (size_t k = 0; k < count; ++k)
                        if (s.BeginElement(k))
                        {
                            std::string pn;
                            double pv = 0.0;
                            s.Value("param", pn);
                            s.Value("value", pv);
                            if (!pn.empty())
                                t.Values[pn] = pv;
                            s.EndElement();
                        }
                    s.EndArray();
                }
                s.EndElement();
                if (!t.Name.empty())
                    m_paramTypes.push_back(std::move(t));
            }
            s.EndArray();
        }
        MarkAllDirty();
        Touch();
        return true;
    }

    std::string Graph::SaveToString() const
    {
        JsonSerializer s;
        Serialize(s);
        return s.Dump();
    }

    bool Graph::LoadFromString(const std::string& json)
    {
        JsonSerializer s;
        if (!s.Parse(json))
            return false;
        return Deserialize(s);
    }

    // ═════════════════════════════ 组（PLAN.md 第 12 节）═════════════════════════════

    namespace
    {
        int IndexOf(const std::vector<uint32_t>& v, NodeId id)
        {
            const auto it = std::find(v.begin(), v.end(), id);
            return it == v.end() ? -1 : static_cast<int>(it - v.begin());
        }

        int IndexOf(const std::vector<std::string>& v, const std::string& s)
        {
            const auto it = std::find(v.begin(), v.end(), s);
            return it == v.end() ? -1 : static_cast<int>(it - v.begin());
        }
    }

    bool Graph::CanAddType(const std::string& typeId) const
    {
        return m_owner.empty() || !IsGroupTypeId(typeId) || !m_groups->Uses(typeId, m_owner);
    }

    void Graph::BindLibrary(GroupLibrary* lib)
    {
        m_groups = lib;
        for (auto& n : m_nodes)
        {
            n->Type = FindType(n->TypeId);
            if (n->Inner)
                n->Inner->BindLibrary(lib);
        }
    }

    std::unique_ptr<Graph> Graph::CloneDefinition(GroupLibrary* lib, const std::map<std::string, std::string>* typeRemap) const
    {
        auto g = std::make_unique<Graph>(*m_registry);
        g->BindLibrary(lib);
        g->m_owner = m_owner;
        for (const auto& src : m_nodes)
        {
            std::string typeId = src->TypeId;
            if (typeRemap)
                if (const auto it = typeRemap->find(typeId); it != typeRemap->end())
                    typeId = it->second;
            Node* n        = g->NewNode(typeId, src->Id);
            n->X           = src->X;
            n->Y           = src->Y;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
        }
        g->m_links      = m_links;
        g->m_nextId     = m_nextId;
        g->m_paramTypes = m_paramTypes;
        return g;
    }

    std::unique_ptr<Graph> Graph::CloneFull(GroupLibrary* lib) const
    {
        auto g = std::make_unique<Graph>(*m_registry);
        g->BindLibrary(lib);
        for (const auto& src : m_nodes)
        {
            auto n = std::make_unique<Node>();
            n->Id          = src->Id;
            n->TypeId      = src->TypeId;
            n->Type        = g->FindType(src->TypeId);
            n->X           = src->X;
            n->Y           = src->Y;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
            n->Outputs     = src->Outputs;
            n->DisplayData = src->DisplayData;
            n->State       = src->State;
            n->Message     = src->Message;
            n->Millis      = src->Millis;
            n->Dirty       = src->Dirty;
            n->m_sceneVersion = src->m_sceneVersion;
            n->m_stamp        = src->m_stamp;
            if (src->Inner)
            {
                n->Inner        = src->Inner->CloneFull(lib);
                n->InnerVersion = src->InnerVersion;
            }
            g->m_nodes.push_back(std::move(n));
        }
        g->m_links       = m_links;
        g->m_nextId      = m_nextId;
        g->m_params      = m_params;
        g->m_paramTypes  = m_paramTypes;
        g->m_stampSeq    = m_stampSeq;
        g->m_owner       = m_owner;
        g->m_groupInputs = m_groupInputs;
        return g;
    }

    bool Graph::SolveGroup(Node& node, const SolveContext& ctx, std::vector<DataList>& inputs, NodeState& state, std::string& message)
    {
        const GroupDef* def = m_groups->Get(node.TypeId);
        if (!def)
        {
            state   = NodeState::Error;
            message = "组定义不存在：" + node.TypeId;
            return true;
        }
        if (!node.Inner || node.InnerVersion != def->Version)
        {
            node.Inner        = def->Inner->CloneDefinition(m_groups);
            node.InnerVersion = def->Version;
        }
        Graph& in = *node.Inner;

        // 各输入整列交给组内对应的组输入节点；数据变了的组输入标脏（实例的输入没变时组内不重算）
        std::map<NodeId, DataList> given;
        for (size_t i = 0; i < def->InputNodes.size() && i < inputs.size(); ++i)
            given[def->InputNodes[i]] = std::move(inputs[i]);
        for (const auto& [id, list] : given)
            if (const auto it = in.m_groupInputs.find(id); it == in.m_groupInputs.end() || !SameList(it->second, list))
                in.MarkDirty(id);
        for (const auto& [id, list] : in.m_groupInputs)
            if (!given.count(id))
                in.MarkDirty(id);
        in.m_groupInputs = std::move(given);

        SolveContext ic = ctx;
        ic.Solved      = nullptr;
        ic.Params      = nullptr;
        ic.GroupInputs = &in.m_groupInputs;
        in.Evaluate(ic);
        if (ctx.Cancelled() && in.AnyDirty())
            return false;

        bool any = false;
        for (size_t k = 0; k < def->OutputNodes.size() && k < node.Outputs.size(); ++k)
            if (const Node* o = in.GetNode(def->OutputNodes[k]))
            {
                node.Outputs[k] = GatherInput(in, *o, 0);
                any = any || !node.Outputs[k].empty();
            }
        int errors = 0;
        for (const auto& n : in.m_nodes)
            errors += n->State == NodeState::Error ? 1 : 0;
        if (errors > 0)
        {
            state   = any ? NodeState::Warning : NodeState::Error;
            message = "组内 " + std::to_string(errors) + " 个节点出错（双击进入查看）";
        }
        return true;
    }

    NodeId Graph::CreateGroup(const std::vector<NodeId>& ids, const std::string& name)
    {
        std::set<NodeId> in;
        std::vector<const Node*> sel;
        for (NodeId id : ids)
            if (const Node* n = GetNode(id); n && in.insert(id).second)
                sel.push_back(n);
        if (sel.empty())
            return kInvalidNode;
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f;
        for (const Node* n : sel)
        {
            minX = std::min(minX, n->X);
            minY = std::min(minY, n->Y);
            maxX = std::max(maxX, n->X);
        }

        // 组内的图：选中的节点（ID 不变）与它们之间的连线
        auto inner = std::make_unique<Graph>(*m_registry);
        inner->BindLibrary(m_groups);
        for (const Node* n : sel)
        {
            Node* c        = inner->NewNode(n->TypeId, n->Id);
            c->X           = n->X;
            c->Y           = n->Y;
            c->InputValues = n->InputValues;
            c->Params      = n->Params;
            c->Preview     = n->Preview;
        }
        std::vector<Link> incoming, outgoing;
        for (const Link& l : m_links)
        {
            const bool from = in.count(l.FromNode) > 0, to = in.count(l.ToNode) > 0;
            if (from && to)
                inner->m_links.push_back(l);
            else if (to)
                incoming.push_back(l);
            else if (from)
                outgoing.push_back(l);
        }

        // 穿过边界的连线变成接口：同一个源端口只生成一个；组输入按目标节点的位置、组输出按源节点的位置从上到下排列
        using Key = std::pair<NodeId, int>;
        auto portName = [&](const Key& k)
        {
            const Node* n = GetNode(k.first);
            return n && n->Type && k.second >= 0 && k.second < static_cast<int>(n->Type->Outputs.size()) ? n->Type->Outputs[k.second].Name : std::string();
        };
        auto collect = [&](const std::vector<Link>& links, bool byTarget)
        {
            std::vector<Key> keys;
            std::map<Key, float> y;
            for (const Link& l : links)
            {
                const Key k{ l.FromNode, l.FromPort };
                const float ny = GetNode(byTarget ? l.ToNode : l.FromNode)->Y;
                if (!y.count(k))
                {
                    keys.push_back(k);
                    y[k] = ny;
                }
                else
                    y[k] = std::min(y[k], ny);
            }
            std::stable_sort(keys.begin(), keys.end(), [&](const Key& a, const Key& b) { return y[a] < y[b]; });
            return keys;
        };
        constexpr float kGap = 240.0f, kStep = 80.0f;
        std::map<Key, NodeId> inNode, outNode;
        const std::vector<Key> inKeys = collect(incoming, true), outKeys = collect(outgoing, false);
        for (size_t i = 0; i < inKeys.size(); ++i)
        {
            Node* gi      = inner->NewNode(kGroupInputType, inner->m_nextId);
            gi->X         = minX - kGap;
            gi->Y         = minY + kStep * static_cast<float>(i);
            gi->Params[0] = Value(portName(inKeys[i]));
            inNode[inKeys[i]] = gi->Id;
        }
        for (size_t i = 0; i < outKeys.size(); ++i)
        {
            Node* go      = inner->NewNode(kGroupOutputType, inner->m_nextId);
            go->X         = maxX + kGap;
            go->Y         = minY + kStep * static_cast<float>(i);
            go->Params[0] = Value(portName(outKeys[i]));
            outNode[outKeys[i]] = go->Id;
            inner->m_links.push_back({ outKeys[i].first, outKeys[i].second, go->Id, 0 });
        }
        for (const Link& l : incoming)
            inner->m_links.push_back({ inNode[{ l.FromNode, l.FromPort }], 0, l.ToNode, l.ToPort });

        const GroupDef& def = m_groups->Add(name, std::move(inner));
        for (NodeId id : in)
            RemoveNode(id);
        Node* inst = NewNode(def.Id, m_nextId);
        inst->X = minX;
        inst->Y = minY;
        const NodeId instId = inst->Id;
        for (const Link& l : incoming)
            if (const int k = IndexOf(def.InputNodes, inNode[{ l.FromNode, l.FromPort }]); k >= 0)
                Connect({ l.FromNode, l.FromPort, instId, k }, false);
        for (const Link& l : outgoing)
            if (const int k = IndexOf(def.OutputNodes, outNode[{ l.FromNode, l.FromPort }]); k >= 0)
                Connect({ instId, k, l.ToNode, l.ToPort }, false);
        MarkDirty(instId);
        Touch();
        return instId;
    }

    std::vector<NodeId> Graph::Ungroup(NodeId instance)
    {
        const Node* inst = GetNode(instance);
        if (!inst || !inst->IsGroup())
            return {};
        const GroupDef* def = m_groups->Get(inst->TypeId);
        if (!def)
            return {};
        const Graph& src = *def->Inner;
        auto isInterface = [](const Node& n) { return n.TypeId == kGroupInputType || n.TypeId == kGroupOutputType; };
        float minX = 1e30f, minY = 1e30f;
        for (const auto& n : src.m_nodes)
            if (!isInterface(*n))
            {
                minX = std::min(minX, n->X);
                minY = std::min(minY, n->Y);
            }
        if (minX > 1e29f)
            minX = minY = 0.0f;

        // 组内节点放回本图：左上角对齐实例
        const float x0 = inst->X, y0 = inst->Y;
        std::map<NodeId, NodeId> remap;
        std::vector<NodeId> out;
        for (const auto& n : src.m_nodes)
        {
            if (isInterface(*n) || !CanAddType(n->TypeId))
                continue;
            Node* c        = NewNode(n->TypeId, m_nextId);
            c->X           = n->X - minX + x0;
            c->Y           = n->Y - minY + y0;
            c->InputValues = n->InputValues;
            c->Params      = n->Params;
            c->Preview     = n->Preview;
            UniqueParamName(*c);
            remap[n->Id] = c->Id;
            out.push_back(c->Id);
        }
        const std::vector<Link> outerIn = [&] { std::vector<Link> v; for (const Link& l : m_links) if (l.ToNode == instance) v.push_back(l); return v; }();
        const std::vector<Link> outerOut = [&] { std::vector<Link> v; for (const Link& l : m_links) if (l.FromNode == instance) v.push_back(l); return v; }();

        // 连线：组内的照原样；组输入 → 组内节点 换成 外部源 → 组内节点（实例上没有连线时用组输入的“默认”的来源）；
        // 组内节点 → 组输出 换成 组内节点 → 外部目标
        std::vector<Link> add;
        for (const Link& l : src.m_links)
        {
            const int k = IndexOf(def->InputNodes, l.FromNode);   // 从组输入出来
            const int j = IndexOf(def->OutputNodes, l.ToNode);    // 进入组输出
            const bool fromMapped = remap.count(l.FromNode) > 0, toMapped = remap.count(l.ToNode) > 0;
            if (fromMapped && toMapped)
                add.push_back({ remap[l.FromNode], l.FromPort, remap[l.ToNode], l.ToPort });
            else if (k >= 0 && (toMapped || j >= 0))
            {
                std::vector<std::pair<NodeId, int>> sources;
                for (const Link& o : outerIn)
                    if (o.ToPort == k)
                        sources.emplace_back(o.FromNode, o.FromPort);
                if (sources.empty())
                    for (const Link& d : src.LinksInto(l.FromNode, 0))
                        if (remap.count(d.FromNode))
                            sources.emplace_back(remap[d.FromNode], d.FromPort);
                for (const auto& [sn, sp] : sources)
                {
                    if (toMapped)
                        add.push_back({ sn, sp, remap[l.ToNode], l.ToPort });
                    else   // 组输入直接连到组输出：外部源直接连到外部目标
                        for (const Link& o : outerOut)
                            if (o.FromPort == j)
                                add.push_back({ sn, sp, o.ToNode, o.ToPort });
                }
            }
            else if (j >= 0 && fromMapped)
                for (const Link& o : outerOut)
                    if (o.FromPort == j)
                        add.push_back({ remap[l.FromNode], l.FromPort, o.ToNode, o.ToPort });
        }
        RemoveNode(instance);
        for (const Link& l : add)
            Connect(l, false);
        Touch();
        return out;
    }

    bool Graph::UsesGroup(const std::string& defId) const
    {
        for (const auto& n : m_nodes)
            if (IsGroupTypeId(n->TypeId) && m_groups->Uses(n->TypeId, defId))
                return true;
        return false;
    }

    bool Graph::RemoveGroupIfUnused(const std::string& defId)
    {
        if (!m_groups->Get(defId) || UsesGroup(defId))
            return false;
        for (const auto& d : m_groups->Defs())
            if (d->Id != defId && d->Inner->UsesGroup(defId))
                return false;
        m_groups->Remove(defId);
        Touch();
        return true;
    }

    bool Graph::RenameGroup(const std::string& defId, const std::string& name)
    {
        GroupDef* def = m_groups->Get(defId);
        if (!def || name.empty() || def->Name == name)
            return false;
        def->Name = name;
        m_groups->Refresh(*def);
        Touch();
        return true;
    }

    Graph* Graph::ResolvePath(std::vector<NodeId>& path)
    {
        std::vector<Graph*> chain{ this };
        Graph* g = this;
        bool rebuilt = false;
        size_t depth = 0;
        for (; depth < path.size(); ++depth)
        {
            Node* n = g->GetNode(path[depth]);
            const GroupDef* def = n && n->IsGroup() ? m_groups->Get(n->TypeId) : nullptr;
            if (!def)
                break;
            if (!n->Inner || n->InnerVersion != def->Version)
            {
                n->Inner        = def->Inner->CloneDefinition(m_groups);
                n->InnerVersion = def->Version;
                rebuilt = true;
            }
            g = n->Inner.get();
            chain.push_back(g);
        }
        path.resize(depth);
        if (rebuilt)   // 新建的组内图还没有结果：沿途的实例重算
            for (size_t k = 0; k < path.size(); ++k)
                chain[k]->MarkDirty(path[k]);
        return g;
    }

    void Graph::CommitGroupEdit(const std::vector<NodeId>& path, bool contentChanged)
    {
        Graph* g = this;
        Node* inst = nullptr;
        for (NodeId id : path)
        {
            inst = g->GetNode(id);
            if (!inst || !inst->IsGroup() || !inst->Inner)
                return;
            g = inst->Inner.get();
        }
        GroupDef* def = inst ? m_groups->Get(inst->TypeId) : nullptr;
        if (!def)
            return;
        // 编辑的是这个实例的组内图：写回定义，这个实例不必重建
        def->Inner = inst->Inner->CloneDefinition(m_groups);
        ++def->Version;
        inst->InnerVersion = def->Version;

        GroupChange ch;
        ch.Id    = def->Id;
        ch.Ports = m_groups->Refresh(*def, &ch.OldIn, &ch.OldOut);
        for (const auto& p : def->Type->Inputs)
            ch.NewIn.push_back(p.Name);
        for (const auto& p : def->Type->Outputs)
            ch.NewOut.push_back(p.Name);
        ch.Content = contentChanged || ch.Ports;
        m_groups->RefreshAll();   // 用到它的组：端口类型、是否读图纸
        if (ch.Content)
        {
            // 全部实例（含各实例组内图里嵌套的）重算、按名称重新对应端口，沿途的实例标脏；定义里的嵌套实例也重新对应
            ApplyGroupChanges({ ch });
            for (const auto& d : m_groups->Defs())
                d->Inner->ApplyGroupChanges({ ch });
        }
        Touch();
    }

    bool Graph::ApplyGroupChanges(const std::vector<GroupChange>& changes)
    {
        bool any = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
        {
            Node& n = *m_nodes[i];
            bool mark = n.Inner && n.Inner->ApplyGroupChanges(changes);
            for (const GroupChange& ch : changes)
                if (n.TypeId == ch.Id)
                {
                    if (ch.Ports)
                        RemapGroupPorts(n, ch);
                    mark = true;
                }
            if (mark)
            {
                MarkDirty(n.Id);
                any = true;
            }
        }
        if (any)
            Touch();
        return any;
    }

    void Graph::RemapGroupPorts(Node& node, const GroupChange& ch)
    {
        std::vector<Value> inputs(ch.NewIn.size());
        for (size_t i = 0; i < ch.OldIn.size() && i < node.InputValues.size(); ++i)
            if (const int j = IndexOf(ch.NewIn, ch.OldIn[i]); j >= 0)
                inputs[j] = node.InputValues[i];
        node.InputValues = std::move(inputs);
        node.Outputs.assign(ch.NewOut.size(), {});
        std::vector<Link> kept;
        for (Link l : m_links)
        {
            if (l.ToNode == node.Id)
            {
                const int j = l.ToPort >= 0 && l.ToPort < static_cast<int>(ch.OldIn.size()) ? IndexOf(ch.NewIn, ch.OldIn[l.ToPort]) : -1;
                if (j < 0)
                    continue;   // 端口没了：连线丢弃
                l.ToPort = j;
            }
            if (l.FromNode == node.Id)
            {
                const int j = l.FromPort >= 0 && l.FromPort < static_cast<int>(ch.OldOut.size()) ? IndexOf(ch.NewOut, ch.OldOut[l.FromPort]) : -1;
                if (j < 0)
                {
                    MarkDirty(l.ToNode);
                    continue;
                }
                l.FromPort = j;
            }
            kept.push_back(l);
        }
        m_links = std::move(kept);
    }
}
