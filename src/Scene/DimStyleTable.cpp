#include "DimStyleTable.h"
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
    }

    DimStyleTable::DimStyleTable()
    {
        // 0 = Standard:库内默认 DimStyle(机械箭头终端,字高 3.5)。
        m_records.push_back({ "Standard", DimStyle{} });

        // GB 机械:实心箭头终端。
        DimStyle mech;
        mech.Terminator = DimTerminator::Arrow;
        mech.TextHeight = 3.5;
        mech.ArrowSize  = 3.5;
        m_records.push_back({ "GB_机械", mech });

        // GB 建筑:45° 斜线终端。
        DimStyle arch;
        arch.Terminator = DimTerminator::Oblique;
        arch.TextHeight = 3.5;
        arch.ArrowSize  = 3.0;
        m_records.push_back({ "GB_建筑", arch });
    }

    DimStyleID DimStyleTable::Add(DimStyleRecord rec)
    {
        DimStyleID existing = FindByName(rec.Name);
        // FindByName 未找到时返回 StandardID;需排除「名字本就是 Standard」的误判。
        if (existing != StandardID || ToLower(rec.Name) == "standard")
            return existing;

        m_records.push_back(std::move(rec));
        return static_cast<DimStyleID>(m_records.size() - 1);
    }

    DimStyleID DimStyleTable::Add(const std::string& name, const DimStyle& style)
    {
        return Add(DimStyleRecord{ name, style });
    }

    const DimStyleRecord* DimStyleTable::Find(DimStyleID id) const
    {
        if (id < m_records.size()) return &m_records[id];
        return nullptr;
    }

    DimStyleID DimStyleTable::FindByName(const std::string& name) const
    {
        std::string key = ToLower(name);
        for (size_t i = 0; i < m_records.size(); ++i)
            if (ToLower(m_records[i].Name) == key)
                return static_cast<DimStyleID>(i);
        return StandardID;
    }

    const DimStyle& DimStyleTable::Style(DimStyleID id) const
    {
        const DimStyleRecord* r = Find(id);
        return r ? r->Style : m_records[StandardID].Style;
    }
}
