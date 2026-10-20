#include "NodeGraph/NodeType.h"
#include "Core/Log.h"
#include <algorithm>
#include <tuple>

namespace MiniCAD::Nodes
{
    double SolveArgs::ParamNumber(int index) const
    {
        Value out;
        return Convert(Param(index), DataType::Number, out) ? out.AsNumber() : 0.0;
    }

    int64_t SolveArgs::ParamInteger(int index) const
    {
        Value out;
        return Convert(Param(index), DataType::Integer, out) ? out.AsInteger() : 0;
    }

    Solid::ISolidKernel* SolveArgs::Kernel()
    {
        if (!Context().Kernel)
            Error("三维实体内核不可用（网页版暂不支持三维实体）");
        return Context().Kernel;
    }

    void NodeRegistry::Register(NodeTypeInfo info)
    {
        if (m_byId.count(info.Id))
        {
            LOG_WARN("节点类型重复注册：%s", info.Id.c_str());
            return;
        }
        // 输入端口的内联值转换为端口类型，保证编辑器与存档里的值类型一致
        for (auto& p : info.Inputs)
            if (!p.Default.IsEmpty() && p.Type != DataType::Any && p.Type != DataType::Geometry)
            {
                Value v;
                if (Convert(p.Default, p.Type, v))
                    p.Default = v;
            }
        auto owned = std::make_unique<NodeTypeInfo>(std::move(info));
        m_byId[owned->Id] = owned.get();
        m_types.push_back(std::move(owned));
        m_ordered.clear();
    }

    const NodeTypeInfo* NodeRegistry::Find(const std::string& id) const
    {
        auto it = m_byId.find(id);
        return it == m_byId.end() ? nullptr : it->second;
    }

    const std::vector<std::string>& NodeRegistry::CategoryOrder()
    {
        // 从数据到几何、从低维到高维，最后是作用于所有几何的变换；小类按 创建 → 运算 → 修改 → 分析 / 转换。
        // 新增分类时按 docs/NodeGraph/PLAN.md 第 4.2 节的位置插入（每个分类至少要有一个节点，NodeGraphTests 检查）
        static const std::vector<std::string> kOrder = {
            "输入",
            "图纸",
            "数学/运算", "数学/函数", "数学/表达式", "数学/序列",
            "列表",
            "向量/点", "向量/向量", "向量/平面",
            "曲线/基本", "曲线/NURBS", "曲线/分析",
            "曲面/创建", "曲面/修改", "曲面/分析",
            "实体/创建", "实体/空心", "实体/运算", "实体/修改", "实体/分析",
            "变换/基本", "变换/阵列",
        };
        return kOrder;
    }

    const std::vector<const NodeTypeInfo*>& NodeRegistry::Ordered() const
    {
        if (m_ordered.size() == m_types.size())
            return m_ordered;
        const auto& order = CategoryOrder();
        std::vector<std::string> unknown;   // 不在表里的分类排在最后，按首次出现的顺序
        auto rank = [&](const std::string& cat)
        {
            const auto it = std::find(order.begin(), order.end(), cat);
            if (it != order.end())
                return static_cast<int>(it - order.begin());
            auto u = std::find(unknown.begin(), unknown.end(), cat);
            if (u == unknown.end())
                u = unknown.insert(unknown.end(), cat);
            return static_cast<int>(order.size() + (u - unknown.begin()));
        };
        std::vector<std::tuple<int, int, size_t, const NodeTypeInfo*>> keyed;
        for (size_t i = 0; i < m_types.size(); ++i)
        {
            const NodeTypeInfo* t = m_types[i].get();
            keyed.emplace_back(rank(t->Category), t->Group, i, t);
        }
        std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b)
        {
            return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a)) < std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b));
        });
        m_ordered.clear();
        for (const auto& k : keyed)
            m_ordered.push_back(std::get<3>(k));
        return m_ordered;
    }

    std::vector<std::string> NodeRegistry::Categories() const
    {
        std::vector<std::string> out;
        for (const NodeTypeInfo* t : Ordered())
            if (out.empty() || out.back() != t->Category)
                out.push_back(t->Category);
        return out;
    }

    PortMatch MatchPorts(DataType from, BodyKind fromKind, DataType to, BodyKind toKind)
    {
        if (!CanConvert(from, to))
            return PortMatch::None;
        if (from == to && from != DataType::Any && from != DataType::Geometry)
        {
            if (from != DataType::Body || fromKind == toKind)
                return PortMatch::Exact;
            if (fromKind == BodyKind::Any || toKind == BodyKind::Any)
                return PortMatch::Type;
            return PortMatch::Convert;   // 实体接到要求曲面的端口（或反过来）：能连，但求值时多半报错
        }
        if (from == DataType::Any || to == DataType::Any || from == DataType::Geometry || to == DataType::Geometry || to == DataType::Text)
            return PortMatch::Generic;
        return PortMatch::Convert;
    }

    std::string KindName(DataType t, BodyKind k)
    {
        if (t == DataType::Body && k == BodyKind::Solid)
            return "实体";
        if (t == DataType::Body && k == BodyKind::Surface)
            return "曲面";
        return TypeName(t);
    }

    PortCandidate NodeRegistry::BestPort(const NodeTypeInfo& t, DataType type, BodyKind kind, bool fromOutput)
    {
        PortCandidate best{ &t };
        const auto& ports = fromOutput ? t.Inputs : t.Outputs;
        for (size_t i = 0; i < ports.size(); ++i)
        {
            const PortSpec& p = ports[i];
            const PortMatch m = fromOutput ? MatchPorts(type, kind, p.Type, p.Kind) : MatchPorts(p.Type, p.Kind, type, kind);
            if (m > best.Match)
            {
                best.Match = m;
                best.Port  = static_cast<int>(i);
            }
        }
        return best;
    }

    namespace
    {
        // UTF-8 → 码点（ASCII 转小写），中文按字比较
        std::u32string Fold(const std::string& s)
        {
            std::u32string out;
            for (size_t i = 0; i < s.size();)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
                char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
                for (int k = 1; k < len && i + k < s.size(); ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
                out.push_back(cp >= 'A' && cp <= 'Z' ? cp - 'A' + 'a' : cp);
                i += len;
            }
            return out;
        }

        // 按空白分词（含全角空格）
        std::vector<std::u32string> SplitTerms(const std::u32string& text)
        {
            std::vector<std::u32string> terms;
            std::u32string cur;
            for (char32_t c : text)
            {
                if (c == U' ' || c == U'\t' || c == U'　')
                {
                    if (!cur.empty())
                        terms.push_back(std::move(cur));
                    cur.clear();
                }
                else
                    cur.push_back(c);
            }
            if (!cur.empty())
                terms.push_back(std::move(cur));
            return terms;
        }

        bool IsSubsequence(const std::u32string& needle, const std::u32string& hay)
        {
            size_t k = 0;
            for (char32_t c : hay)
                if (k < needle.size() && c == needle[k])
                    ++k;
            return k == needle.size();
        }

        // 一个词与一个字段的匹配分
        int FieldScore(const std::u32string& term, const std::u32string& field, int weight)
        {
            if (field.empty())
                return 0;
            if (field == term)
                return 100 * weight;
            const size_t pos = field.find(term);
            if (pos == 0)
                return 80 * weight;
            if (pos != std::u32string::npos)
                return (field[pos - 1] == U'.' ? 75 : 60) * weight;   // ID 里 "." 之后的开头（"solid.box" 的 "box"）
            if (term.size() >= 2 && IsSubsequence(term, field))
                return 20 * weight;
            return 0;
        }
    }

    std::vector<std::pair<int, const NodeTypeInfo*>> NodeRegistry::Scored(const std::string& query) const
    {
        const std::vector<std::u32string> terms = SplitTerms(Fold(query));
        std::vector<std::pair<int, const NodeTypeInfo*>> hits;
        for (const NodeTypeInfo* t : Ordered())
        {
            const std::u32string name = Fold(t->Name), id = Fold(t->Id), cat = Fold(t->Category), desc = Fold(t->Description);
            const std::vector<std::u32string> keywords = SplitTerms(Fold(t->Keywords));
            int total = 0;
            for (const auto& term : terms)
            {
                // 显示名、ID 权重最高，关键词（别名、旧名）略低，分类再次；说明只认包含
                int s = std::max({ FieldScore(term, name, 3), FieldScore(term, id, 3), FieldScore(term, cat, 2) });
                for (const auto& k : keywords)
                    s = std::max(s, FieldScore(term, k, 3) * 9 / 10);
                if (s == 0 && desc.find(term) != std::u32string::npos)
                    s = 10;
                if (s == 0)
                {
                    total = -1;
                    break;
                }
                total += s;
            }
            if (total >= 0)
                hits.emplace_back(total, t);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        return hits;
    }

    std::vector<const NodeTypeInfo*> NodeRegistry::Search(const std::string& query) const
    {
        std::vector<const NodeTypeInfo*> out;
        for (const auto& h : Scored(query))
            out.push_back(h.second);
        return out;
    }

    std::vector<PortCandidate> NodeRegistry::SearchForPort(const std::string& query, DataType type, BodyKind kind, bool fromOutput) const
    {
        const bool browsing = SplitTerms(Fold(query)).empty();
        std::vector<std::pair<int, PortCandidate>> hits;   // (文字匹配分, 候选)
        for (const auto& [score, t] : Scored(query))
        {
            const PortCandidate c = BestPort(*t, type, kind, fromOutput);
            if (c.Match == PortMatch::None || (browsing && c.Match == PortMatch::Generic))
                continue;
            hits.emplace_back(score, c);
        }
        // Scored 已按文字分、再按菜单顺序排好；这里在同分之内按匹配程度
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b)
        {
            return a.first != b.first ? a.first > b.first : a.second.Match > b.second.Match;
        });
        std::vector<PortCandidate> out;
        out.reserve(hits.size());
        for (const auto& h : hits)
            out.push_back(h.second);
        return out;
    }

    const NodeRegistry& NodeRegistry::Builtin()
    {
        static const NodeRegistry registry = []
        {
            NodeRegistry r;
            RegisterMathNodes(r);
            RegisterDataNodes(r);
            RegisterGeometryNodes(r);
            RegisterSolidNodes(r);
            RegisterFormNodes(r);     // 在实体节点之后：同一小类里排在已有节点后面
            RegisterSurfaceNodes(r);
            RegisterCurveXformNodes(r);
            r.Ordered();   // 先建好菜单顺序的缓存：之后只读，其他线程（后台读图纸）也可以用
            return r;
        }();
        return registry;
    }
}
