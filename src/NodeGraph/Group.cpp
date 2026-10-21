#include "NodeGraph/Group.h"
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/Graph.h"
#include "Serialization/ISerializer.h"
#include <algorithm>
#include <functional>
#include <set>

namespace MiniCAD::Nodes
{
    GroupDef::GroupDef()  = default;
    GroupDef::~GroupDef() = default;

    GroupLibrary::GroupLibrary(const NodeRegistry& registry) : m_registry(&registry) {}
    GroupLibrary::~GroupLibrary() = default;

    GroupDef* GroupLibrary::Get(const std::string& id) const
    {
        for (const auto& d : m_defs)
            if (d->Id == id)
                return d.get();
        return nullptr;
    }

    std::string GroupLibrary::NewId()
    {
        std::string id;
        do
            id = kGroupTypePrefix + std::to_string(m_nextId++);
        while (Get(id));
        return id;
    }

    GroupDef& GroupLibrary::Add(std::string name, std::unique_ptr<Graph> inner, std::string id)
    {
        if (id.empty() || !IsGroupTypeId(id) || Get(id))
            id = NewId();
        else if (const std::string num = id.substr(std::string(kGroupTypePrefix).size());
                 !num.empty() && std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; }) && num.size() < 9)
            m_nextId = std::max(m_nextId, static_cast<uint32_t>(std::stoul(num)) + 1);
        auto def   = std::make_unique<GroupDef>();
        def->Id    = id;
        def->Name  = name.empty() ? UniqueName(kGroupCategory) : std::move(name);
        def->Inner = inner ? std::move(inner) : std::make_unique<Graph>(*m_registry);
        def->Inner->BindLibrary(this);
        def->Inner->m_owner = id;
        GroupDef& ref = *def;
        m_defs.push_back(std::move(def));
        Refresh(ref);
        return ref;
    }

    void GroupLibrary::Remove(const std::string& id)
    {
        m_types.Unregister(id);
        std::erase_if(m_defs, [&](const auto& d) { return d->Id == id; });
    }

    void GroupLibrary::Clear()
    {
        m_defs.clear();
        m_types  = NodeRegistry{};
        m_nextId = 1;
    }

    namespace
    {
        const std::string& PortNameParam(const Node& n)
        {
            static const std::string kEmpty;
            return !n.Params.empty() && n.Params[0].Type() == DataType::Text ? n.Params[0].AsText() : kEmpty;
        }
    }

    bool GroupLibrary::Refresh(GroupDef& def, std::vector<std::string>* oldIn, std::vector<std::string>* oldOut)
    {
        std::vector<std::string> prevIn, prevOut;
        if (def.Type)
        {
            for (const auto& p : def.Type->Inputs)
                prevIn.push_back(p.Name);
            for (const auto& p : def.Type->Outputs)
                prevOut.push_back(p.Name);
        }

        const Graph& g = *def.Inner;
        std::vector<const Node*> ins, outs;
        bool scene = false;
        for (const auto& n : g.GetNodes())
        {
            if (n->TypeId == kGroupInputType)
                ins.push_back(n.get());
            else if (n->TypeId == kGroupOutputType)
                outs.push_back(n.get());
            scene = scene || (n->Type && n->Type->DependsOnScene);
        }
        // 端口顺序按节点在画布上的位置：从上到下，同高时从左到右
        auto byPos = [](const Node* a, const Node* b) { return a->Y != b->Y ? a->Y < b->Y : a->X != b->X ? a->X < b->X : a->Id < b->Id; };
        std::sort(ins.begin(), ins.end(), byPos);
        std::sort(outs.begin(), outs.end(), byPos);

        std::set<std::string> used;
        auto uniqueName = [&](std::string base, const char* fallback, size_t i)
        {
            if (base.empty())
                base = fallback + std::to_string(i + 1);
            std::string name = base;
            for (int k = 2; used.count(name); ++k)
                name = base + " " + std::to_string(k);
            used.insert(name);
            return name;
        };
        // 端口类型：组内连到的端口类型都相同时取它，否则为任意
        auto merge = [](bool& first, PortSpec& p, const PortSpec& other)
        {
            if (first)
            {
                p.Type = other.Type;
                p.Kind = other.Kind;
                first  = false;
            }
            else if (p.Type != other.Type)
            {
                p.Type = DataType::Any;
                p.Kind = BodyKind::Any;
            }
            else if (p.Kind != other.Kind)
                p.Kind = BodyKind::Any;
        };

        NodeTypeInfo t;
        t.Id          = def.Id;
        t.Name        = def.Name;
        t.Category    = kGroupCategory;
        t.Description = "组“" + def.Name + "”：组内 " + std::to_string(g.GetNodes().size()) + " 个节点。双击进入编辑，右键解组";
        t.Keywords    = "group cluster 子图 组";
        t.Group       = 1;   // 菜单里排在组输入、组输出之后
        t.DependsOnScene = scene;
        def.InputNodes.clear();
        def.OutputNodes.clear();
        for (size_t i = 0; i < ins.size(); ++i)
        {
            PortSpec p;
            p.Name     = uniqueName(PortNameParam(*ins[i]), "输入 ", i);
            p.Acc      = Access::List;
            p.Optional = true;
            bool first = true;
            for (const Link& l : g.LinksFrom(ins[i]->Id, 0))
                if (const Node* d = g.GetNode(l.ToNode); d && d->Type && l.ToPort >= 0 && l.ToPort < static_cast<int>(d->Type->Inputs.size()))
                    merge(first, p, d->Type->Inputs[l.ToPort]);
            p.Tip = "没有数据时用组内“" + p.Name + "”的默认值";
            t.Inputs.push_back(std::move(p));
            def.InputNodes.push_back(ins[i]->Id);
        }
        used.clear();
        for (size_t i = 0; i < outs.size(); ++i)
        {
            PortSpec p;
            p.Name = uniqueName(PortNameParam(*outs[i]), "输出 ", i);
            bool first = true;
            for (const Link& l : g.LinksInto(outs[i]->Id, 0))
                if (const Node* s = g.GetNode(l.FromNode); s && s->Type && l.FromPort >= 0 && l.FromPort < static_cast<int>(s->Type->Outputs.size()))
                    merge(first, p, s->Type->Outputs[l.FromPort]);
            t.Outputs.push_back(std::move(p));
            def.OutputNodes.push_back(outs[i]->Id);
        }

        std::vector<std::string> newIn, newOut;
        for (const auto& p : t.Inputs)
            newIn.push_back(p.Name);
        for (const auto& p : t.Outputs)
            newOut.push_back(p.Name);
        const bool hadType = def.Type != nullptr;
        def.Type = m_types.Upsert(std::move(t));
        if (oldIn)
            *oldIn = prevIn;
        if (oldOut)
            *oldOut = prevOut;
        return hadType && (prevIn != newIn || prevOut != newOut);
    }

    std::vector<GroupDef*> GroupLibrary::DependencyOrder() const
    {
        std::vector<GroupDef*> order;
        std::set<const GroupDef*> seen;
        std::function<void(GroupDef*)> visit = [&](GroupDef* d)
        {
            if (!d || !seen.insert(d).second)
                return;
            for (const auto& n : d->Inner->GetNodes())
                if (IsGroupTypeId(n->TypeId))
                    visit(Get(n->TypeId));
            order.push_back(d);
        };
        for (const auto& d : m_defs)
            visit(d.get());
        return order;
    }

    void GroupLibrary::RefreshAll()
    {
        for (GroupDef* d : DependencyOrder())
            Refresh(*d);
    }

    bool GroupLibrary::Uses(const std::string& a, const std::string& b) const
    {
        std::set<std::string> seen;
        std::function<bool(const std::string&)> uses = [&](const std::string& id)
        {
            if (id == b)
                return true;
            const GroupDef* d = Get(id);
            if (!d || !seen.insert(id).second)
                return false;
            for (const auto& n : d->Inner->GetNodes())
                if (IsGroupTypeId(n->TypeId) && uses(n->TypeId))
                    return true;
            return false;
        };
        return uses(a);
    }

    std::string GroupLibrary::UniqueName(const std::string& base) const
    {
        for (int k = 1;; ++k)
        {
            const std::string name = base + " " + std::to_string(k);
            if (std::none_of(m_defs.begin(), m_defs.end(), [&](const auto& d) { return d->Name == name; }))
                return name;
        }
    }

    std::unique_ptr<GroupLibrary> GroupLibrary::Clone() const
    {
        auto c = std::make_unique<GroupLibrary>(*m_registry);
        for (const GroupDef* d : DependencyOrder())
        {
            auto def     = std::make_unique<GroupDef>();
            def->Id      = d->Id;
            def->Name    = d->Name;
            def->Version = d->Version;
            def->Inner   = d->Inner->CloneDefinition(c.get());
            def->Inner->m_owner = d->Id;
            GroupDef& ref = *def;
            c->m_defs.push_back(std::move(def));
            c->Refresh(ref);
        }
        c->m_nextId = m_nextId;
        return c;
    }

    void GroupLibrary::Serialize(ISerializer& s) const
    {
        size_t n = m_defs.size();
        if (!s.BeginArray("groups", n))
            return;
        for (const GroupDef* d : DependencyOrder())   // 被用到的在前：读入时嵌套的组类型已经有了
        {
            if (!s.BeginElement(0))
                continue;
            std::string id = d->Id, name = d->Name;
            s.Value("id", id);
            s.Value("name", name);
            d->Inner->Serialize(s);
            s.EndElement();
        }
        s.EndArray();
    }

    void GroupLibrary::Deserialize(ISerializer& s)
    {
        size_t n = 0;
        if (!s.BeginArray("groups", n))
            return;
        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i))
                continue;
            std::string id, name;
            s.Value("id", id);
            s.Value("name", name);
            auto inner = std::make_unique<Graph>(*m_registry);
            inner->BindLibrary(this);
            inner->m_owner = id;
            const bool ok = inner->Deserialize(s);
            s.EndElement();
            if (ok && IsGroupTypeId(id) && !Get(id))
                Add(name, std::move(inner), id);
        }
        s.EndArray();
    }

    bool SameList(const DataList& a, const DataList& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const Value& x = a[i];
            const Value& y = b[i];
            if (x.Data.index() != y.Data.index() || x.Void != y.Void)
                return false;
            const bool same = std::visit([&](const auto& u) -> bool
            {
                using T = std::decay_t<decltype(u)>;
                const T& v = std::get<T>(y.Data);
                if constexpr (std::is_same_v<T, std::monostate>)
                    return true;
                else if constexpr (std::is_same_v<T, Math::Vec3>)
                    return u.x == v.x && u.y == v.y && u.z == v.z;
                else if constexpr (std::is_same_v<T, Domain>)
                    return u.Min == v.Min && u.Max == v.Max;
                else if constexpr (std::is_same_v<T, Plane>)
                    return u.Origin.x == v.Origin.x && u.Origin.y == v.Origin.y && u.Origin.z == v.Origin.z && u.X.x == v.X.x && u.X.y == v.X.y &&
                           u.X.z == v.X.z && u.Y.x == v.Y.x && u.Y.y == v.Y.y && u.Y.z == v.Y.z;
                else
                    return u == v;   // 数值、文字；曲线、实体、网格比较共享的地址
            }, x.Data);
            if (!same)
                return false;
        }
        return true;
    }

    void RegisterGroupNodes(NodeRegistry& r)
    {
        using namespace Builtin;
        {
            NodeTypeInfo t{ .Id = kGroupInputType, .Name = "组输入", .Category = kGroupCategory,
                            .Description = "组的输入端口：组实例上对应输入收到的数据从这里输出，实例上没有数据时输出“默认”。"
                                           "端口名称取“名称”，顺序按节点在画布上从上到下" };
            t.Keywords = "group input cluster 输入端口 接口";
            t.Inputs   = { InList("默认", DataType::Any, true, "组实例的这个输入没有数据时输出的值") };
            t.Outputs  = { Out("值", DataType::Any) };
            t.Params   = { Param("名称", std::string()) };
            r.Register(std::move(t));   // 求值由 Graph::SolveNode 处理（要知道是哪个节点）
        }
        {
            NodeTypeInfo t{ .Id = kGroupOutputType, .Name = "组输出", .Category = kGroupCategory,
                            .Description = "组的输出端口：连到这里的数据成为组实例上对应输出的数据。端口名称取“名称”，顺序按节点在画布上从上到下" };
            t.Keywords = "group output cluster 输出端口 接口";
            t.Inputs   = { InList("值", DataType::Any, true) };
            t.Params   = { Param("名称", std::string()) };
            r.Register(std::move(t));
        }
    }
}
