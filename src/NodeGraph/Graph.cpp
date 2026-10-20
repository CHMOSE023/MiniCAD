#include "NodeGraph/Graph.h"
#include "Serialization/ISerializer.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <set>

namespace MiniCAD::Nodes
{
    // ═════════════════════════════ 节点与连线 ═════════════════════════════

    Graph::Graph(const NodeRegistry& registry) : m_registry(&registry) {}
    Graph::~Graph() = default;

    Node* Graph::NewNode(const std::string& typeId, NodeId id)
    {
        auto node    = std::make_unique<Node>();
        node->Id     = id;
        node->TypeId = typeId;
        node->Type   = m_registry->Find(typeId);
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
        if (!m_registry->Find(typeId))
            return nullptr;
        Node* n = NewNode(typeId, m_nextId);
        n->X = x;
        n->Y = y;
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
        m_nextId = 1;
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
            if (!src->Type)   // 本程序不认识的节点类型不粘贴
                continue;
            Node* n        = NewNode(src->TypeId, m_nextId);
            n->X           = src->X - minX + x;
            n->Y           = src->Y - minY + y;
            n->InputValues = src->InputValues;
            n->Params      = src->Params;
            n->Preview     = src->Preview;
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
                n->Dirty = true;
            for (const auto& l : m_links)
                if (l.FromNode == cur)
                    stack.push_back(l.ToNode);
        }
    }

    void Graph::MarkAllDirty()
    {
        for (auto& n : m_nodes)
            n->Dirty = true;
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
        for (auto& n : m_nodes)
            if (n->Type && n->Type->DependsOnScene && n->m_sceneVersion != ctx.SceneVersion)
            {
                n->m_sceneVersion = ctx.SceneVersion;
                MarkDirty(n->Id);
            }

        bool solved = false;
        for (NodeId id : TopologicalOrder())
        {
            Node* n = GetNode(id);
            if (!n || !n->Dirty)
                continue;
            SolveNode(*n, ctx);
            solved = true;
        }
        if (solved)
            ++m_evalVersion;
        return solved;
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

    void Graph::SolveNode(Node& node, const SolveContext& ctx)
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
                if (!Convert(raw[k], spec.Type, v))
                    return finish(NodeState::Error, "输入“" + spec.Name + "”第 " + std::to_string(k + 1) + " 项（" + TypeName(raw[k].Type()) +
                                                    "）不能转换为" + TypeName(spec.Type));
                inputs[i].push_back(std::move(v));
            }
        }
        if (type.Display && !inputs.empty())
            node.DisplayData = inputs[0];

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

        if (!args.m_error.empty())
        {
            std::string msg = args.m_error;
            if (count > 1)
                msg += "（" + std::to_string(args.m_errorCount) + " / " + std::to_string(count) + " 项失败）";
            return finish(NodeState::Error, msg);
        }
        if (!args.m_warning.empty())
            return finish(NodeState::Warning, args.m_warning);
        finish(NodeState::Ok, {});
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
}
