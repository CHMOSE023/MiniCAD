#include "Scene/TextStyleTable.h"
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

    bool TextStyleRecord::IsShx() const
    {
        const std::string f = ToLower(FontFile);
        return f.size() >= 4 && f.compare(f.size() - 4, 4, ".shx") == 0;
    }

    TextStyleTable::TextStyleTable()
    {
        // 顺序与 ID 对齐，见头文件说明
        m_records.push_back({ 0, "Standard", "tssdeng.shx", "TSSDCHN.SHX", 0.0, 1.0, 0.0 });
        m_records.push_back({ 1, "Simplex",  "simplex.shx", "",            0.0, 1.0, 0.0 });
        m_records.push_back({ 2, "GB2312",   "GB2312.ttf",  "",            0.0, 1.0, 0.0 });
        m_nextId = 3;
    }

    TextStyleID TextStyleTable::Add(TextStyleRecord rec)
    {
        rec.Name = Trim(rec.Name);
        if (rec.Name.empty() || FindByName(rec.Name) != InvalidID)
            return InvalidID;
        rec.Id = m_nextId++;
        m_records.push_back(std::move(rec));
        return m_records.back().Id;
    }

    const TextStyleRecord* TextStyleTable::Find(TextStyleID id) const
    {
        for (const auto& r : m_records)
            if (r.Id == id)
                return &r;
        return nullptr;
    }

    TextStyleRecord* TextStyleTable::Find(TextStyleID id)
    {
        return const_cast<TextStyleRecord*>(static_cast<const TextStyleTable*>(this)->Find(id));
    }

    TextStyleID TextStyleTable::FindByName(const std::string& name) const
    {
        const std::string key = ToLower(Trim(name));
        for (const auto& r : m_records)
            if (ToLower(r.Name) == key)
                return r.Id;
        return InvalidID;
    }

    const TextStyleRecord& TextStyleTable::Resolve(TextStyleID id) const
    {
        if (const TextStyleRecord* r = Find(id))
            return *r;
        return *Find(StandardID);
    }

    bool TextStyleTable::Update(TextStyleID id, const TextStyleRecord& rec)
    {
        TextStyleRecord* r = Find(id);
        if (!r) return false;
        r->FontFile    = rec.FontFile;
        r->BigFontFile = rec.BigFontFile;
        r->Height      = std::max(0.0, rec.Height);
        r->WidthFactor = rec.WidthFactor > 0.0 ? rec.WidthFactor : 1.0;
        r->ObliqueDeg  = std::clamp(rec.ObliqueDeg, -85.0, 85.0);
        return true;
    }

    bool TextStyleTable::Rename(TextStyleID id, const std::string& name)
    {
        const std::string n = Trim(name);
        TextStyleRecord* r = Find(id);
        if (!r || id == StandardID || n.empty())
            return false;
        const TextStyleID other = FindByName(n);
        if (other != InvalidID && other != id)
            return false;
        r->Name = n;
        return true;
    }

    bool TextStyleTable::Remove(TextStyleID id)
    {
        if (id == StandardID)
            return false;
        auto it = std::find_if(m_records.begin(), m_records.end(), [id](const TextStyleRecord& r) { return r.Id == id; });
        if (it == m_records.end())
            return false;
        m_records.erase(it);
        return true;
    }

    void TextStyleTable::Serialize(ISerializer& s) const
    {
        const_cast<TextStyleTable*>(this)->Deserialize(s);   // 读写合一，写路径不修改自身
    }

    void TextStyleTable::Deserialize(ISerializer& s)
    {
        uint32_t next = m_nextId;
        s.Value("nextId", next);

        size_t n = m_records.size();
        if (!s.BeginArray("items", n))
            return;     // 旧文件无文字样式表：保留预置项

        std::vector<TextStyleRecord> loaded;
        std::vector<TextStyleRecord>& recs = s.IsLoading() ? loaded : m_records;
        if (s.IsLoading())
            recs.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i)) continue;
            TextStyleRecord& r = recs[i];
            s.Value("id", r.Id);
            s.Value("name", r.Name);
            s.Value("font", r.FontFile);
            s.Value("bigFont", r.BigFontFile);
            s.Value("height", r.Height);
            s.Value("widthFactor", r.WidthFactor);
            s.Value("oblique", r.ObliqueDeg);
            s.EndElement();
        }
        s.EndArray();

        if (!s.IsLoading())
            return;

        // 读入：丢弃无名 / 重复 ID 的记录；缺少 Standard 时补上预置的 Standard
        m_records.clear();
        for (auto& r : loaded)
            if (!r.Name.empty() && !Find(r.Id))
                m_records.push_back(std::move(r));
        if (!Find(StandardID))
            m_records.insert(m_records.begin(), TextStyleTable().m_records.front());
        TextStyleID maxId = 0;
        for (const auto& r : m_records)
            maxId = std::max(maxId, r.Id);
        m_nextId = std::max(next, maxId + 1);
    }
}
