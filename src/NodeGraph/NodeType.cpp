#include "NodeGraph/NodeType.h"
#include "Core/Log.h"
#include <algorithm>

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
    }

    const NodeTypeInfo* NodeRegistry::Find(const std::string& id) const
    {
        auto it = m_byId.find(id);
        return it == m_byId.end() ? nullptr : it->second;
    }

    std::vector<std::string> NodeRegistry::Categories() const
    {
        std::vector<std::string> out;
        for (const auto& t : m_types)
            if (std::find(out.begin(), out.end(), t->Category) == out.end())
                out.push_back(t->Category);
        return out;
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

    std::vector<const NodeTypeInfo*> NodeRegistry::Search(const std::string& query) const
    {
        std::vector<std::u32string> terms;
        {
            std::u32string cur;
            for (char32_t c : Fold(query))
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
        }
        std::vector<std::pair<int, const NodeTypeInfo*>> hits;
        for (const auto& t : m_types)
        {
            const std::u32string name = Fold(t->Name), id = Fold(t->Id), cat = Fold(t->Category), desc = Fold(t->Description);
            int total = 0;
            for (const auto& term : terms)
            {
                // 显示名、ID 权重最高，分类次之；说明只认包含
                int s = std::max({ FieldScore(term, name, 3), FieldScore(term, id, 3), FieldScore(term, cat, 2) });
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
                hits.emplace_back(total, t.get());
        }
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<const NodeTypeInfo*> out;
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
            RegisterGeometryNodes(r);
            RegisterSolidNodes(r);
            RegisterSurfaceNodes(r);
            return r;
        }();
        return registry;
    }
}
