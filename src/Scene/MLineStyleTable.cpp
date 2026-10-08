#include "Scene/MLineStyleTable.h"
#include "Serialization/ISerializer.h"
#include <algorithm>
#include <cctype>

namespace MiniCAD
{
    namespace
    {
        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string Trim(const std::string& s)
        {
            const size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos) return {};
            const size_t e = s.find_last_not_of(" \t");
            return s.substr(b, e - b + 1);
        }
    }

    double MLineStyleRecord::MaxOffset() const
    {
        double m = 0.0;
        bool first = true;
        for (const auto& e : Elements) { m = first ? e.Offset : std::max(m, e.Offset); first = false; }
        return m;
    }

    double MLineStyleRecord::MinOffset() const
    {
        double m = 0.0;
        bool first = true;
        for (const auto& e : Elements) { m = first ? e.Offset : std::min(m, e.Offset); first = false; }
        return m;
    }

    void MLineStyleRecord::Normalize()
    {
        std::stable_sort(Elements.begin(), Elements.end(),
                         [](const MLineElement& a, const MLineElement& b) { return a.Offset > b.Offset; });
    }

    MLineStyleTable::MLineStyleTable()
    {
        MLineStyleRecord std;
        std.Id          = 0;
        std.Name        = "Standard";
        std.Description = "两条平行线";
        std.Elements    = { { 0.5, false, {} }, { -0.5, false, {} } };
        m_records.push_back(std);
        m_nextId = 1;
    }

    MLineStyleID MLineStyleTable::Add(MLineStyleRecord rec)
    {
        rec.Name = Trim(rec.Name);
        if (rec.Name.empty() || rec.Elements.empty() || FindByName(rec.Name) != InvalidID)
            return InvalidID;
        rec.Normalize();
        rec.Id = m_nextId++;
        m_records.push_back(std::move(rec));
        return m_records.back().Id;
    }

    const MLineStyleRecord* MLineStyleTable::Find(MLineStyleID id) const
    {
        for (const auto& r : m_records)
            if (r.Id == id)
                return &r;
        return nullptr;
    }

    MLineStyleRecord* MLineStyleTable::Find(MLineStyleID id)
    {
        return const_cast<MLineStyleRecord*>(static_cast<const MLineStyleTable*>(this)->Find(id));
    }

    MLineStyleID MLineStyleTable::FindByName(const std::string& name) const
    {
        const std::string key = ToLower(Trim(name));
        for (const auto& r : m_records)
            if (ToLower(r.Name) == key)
                return r.Id;
        return InvalidID;
    }

    const MLineStyleRecord& MLineStyleTable::Resolve(MLineStyleID id) const
    {
        if (const MLineStyleRecord* r = Find(id))
            return *r;
        return *Find(StandardID);
    }

    bool MLineStyleTable::Update(MLineStyleID id, const MLineStyleRecord& rec)
    {
        MLineStyleRecord* r = Find(id);
        if (!r || rec.Elements.empty()) return false;
        r->Description = rec.Description;
        r->Elements    = rec.Elements;
        r->StartCap    = rec.StartCap;
        r->EndCap      = rec.EndCap;
        r->Joints      = rec.Joints;
        r->Normalize();
        return true;
    }

    bool MLineStyleTable::Rename(MLineStyleID id, const std::string& name)
    {
        const std::string n = Trim(name);
        MLineStyleRecord* r = Find(id);
        if (!r || id == StandardID || n.empty())
            return false;
        const MLineStyleID other = FindByName(n);
        if (other != InvalidID && other != id)
            return false;
        r->Name = n;
        return true;
    }

    bool MLineStyleTable::Remove(MLineStyleID id)
    {
        if (id == StandardID)
            return false;
        auto it = std::find_if(m_records.begin(), m_records.end(), [id](const MLineStyleRecord& r) { return r.Id == id; });
        if (it == m_records.end())
            return false;
        m_records.erase(it);
        return true;
    }

    void MLineStyleTable::SerializeRecord(ISerializer& s, MLineStyleRecord& r)
    {
        s.Value("id", r.Id);
        s.Value("name", r.Name);
        s.Value("description", r.Description);
        s.Value("startCap", r.StartCap);
        s.Value("endCap", r.EndCap);
        s.Value("joints", r.Joints);

        size_t n = r.Elements.size();
        if (s.BeginArray("elements", n))
        {
            if (s.IsLoading())
                r.Elements.assign(n, {});
            for (size_t i = 0; i < n; ++i)
            {
                if (!s.BeginElement(i)) continue;
                MLineElement& e = r.Elements[i];
                s.Value("offset", e.Offset);
                s.Value("useColor", e.UseColor);
                s.Value("r", e.Color.r);
                s.Value("g", e.Color.g);
                s.Value("b", e.Color.b);
                s.Value("a", e.Color.a);
                s.EndElement();
            }
            s.EndArray();
        }
    }

    void MLineStyleTable::Serialize(ISerializer& s) const
    {
        const_cast<MLineStyleTable*>(this)->Deserialize(s);   // 读写合一，写路径不修改自身
    }

    void MLineStyleTable::Deserialize(ISerializer& s)
    {
        uint32_t next = m_nextId;
        s.Value("nextId", next);

        size_t n = m_records.size();
        if (!s.BeginArray("items", n))
            return;     // 旧文件无多线样式表：保留预置项

        std::vector<MLineStyleRecord> loaded;
        std::vector<MLineStyleRecord>& recs = s.IsLoading() ? loaded : m_records;
        if (s.IsLoading())
            recs.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i)) continue;
            SerializeRecord(s, recs[i]);
            s.EndElement();
        }
        s.EndArray();

        if (!s.IsLoading())
            return;

        // 读入：丢弃无名 / 无元素线 / 重复 ID 的记录；缺少 Standard 时补上预置的 Standard
        m_records.clear();
        for (auto& r : loaded)
            if (!r.Name.empty() && !r.Elements.empty() && !Find(r.Id))
            {
                r.Normalize();
                m_records.push_back(std::move(r));
            }
        if (!Find(StandardID))
            m_records.insert(m_records.begin(), MLineStyleTable().m_records.front());
        MLineStyleID maxId = 0;
        for (const auto& r : m_records)
            maxId = std::max(maxId, r.Id);
        m_nextId = std::max(next, maxId + 1);
    }
}
