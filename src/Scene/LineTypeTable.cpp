#include "LineTypeTable.h"
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
    }

    LineTypeTable::LineTypeTable()
    {
        // 顺序必须与保留 ID 对齐:0=ByLayer,1=ByBlock,2=Continuous。
        m_records.push_back({ "ByLayer",    "",                  {},               0.0 }); // 0
        m_records.push_back({ "ByBlock",    "",                  {},               0.0 }); // 1
        m_records.push_back({ "Continuous", "Solid line",        {},               0.0 }); // 2

        // 几个常用 ACAD 标准线型(dash 单位为绘图单位)。
        m_records.push_back({ "DASHED",  "Dashed __ __ __ __",  { 0.5, -0.25 },          0.75 });
        m_records.push_back({ "DOTTED",  "Dotted . . . . . .",  { 0.0, -0.25 },          0.25 });
        m_records.push_back({ "DASHDOT", "Dash dot _ . _ . _",  { 0.5, -0.25, 0.0, -0.25 }, 1.0 });
    }

    LineTypeID LineTypeTable::Add(LineTypeRecord rec)
    {
        // 同名复用,避免重复。
        LineTypeID existing = FindByName(rec.Name);
        if (existing != ContinuousID || ToLower(rec.Name) == "continuous")
            return existing;

        m_records.push_back(std::move(rec));
        return static_cast<LineTypeID>(m_records.size() - 1);
    }

    const LineTypeRecord* LineTypeTable::Find(LineTypeID id) const
    {
        if (id < m_records.size()) return &m_records[id];
        return nullptr;
    }

    LineTypeID LineTypeTable::FindByName(const std::string& name) const
    {
        std::string key = ToLower(name);
        for (size_t i = 0; i < m_records.size(); ++i)
            if (ToLower(m_records[i].Name) == key)
                return static_cast<LineTypeID>(i);
        return ContinuousID;
    }

    void LineTypeTable::Serialize(ISerializer& s) const
    {
        const_cast<LineTypeTable*>(this)->Deserialize(s);   // 读写合一,写路径不修改自身
    }

    void LineTypeTable::Deserialize(ISerializer& s)
    {
        size_t n = m_records.size();
        if (!s.BeginArray("items", n))
            return;   // 旧文件无线型表:保留构造预置项

        if (s.IsLoading())
            m_records.resize(std::max<size_t>(n, ContinuousID + 1));   // 至少保住保留 ID 0..2

        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i)) continue;
            LineTypeRecord& r = m_records[i];
            s.Value("name", r.Name);
            s.Value("description", r.Description);
            s.Value("pattern", r.Pattern);
            s.Value("patternLength", r.PatternLength);

            // 复杂线型的文字/形段。旧文件没有这个键:BeginArray 读返回 false、
            // 写也建不出元素 ⇒ Symbols 保持为空,旧图纸照旧按纯虚线画。
            // PatternIndex 存成 int32:JSON 里它就是普通数字,旧版读取时缺键即 0。
            size_t sym = r.Symbols.size();
            if (s.BeginArray("symbols", sym))
            {
                if (s.IsLoading())
                    r.Symbols.resize(std::min(sym, r.Pattern.size()));   // 越界的下标没有意义,丢掉
                for (size_t k = 0; k < r.Symbols.size(); ++k)
                {
                    if (!s.BeginElement(k)) continue;
                    LineTypeSymbol& sy = r.Symbols[k];
                    int32_t idx = static_cast<int32_t>(sy.PatternIndex);
                    s.Value("index", idx);
                    sy.PatternIndex = idx > 0 ? static_cast<size_t>(idx) : 0;
                    s.Value("text", sy.Text);
                    s.Value("rotation", sy.Rotation);
                    s.Value("scale", sy.Scale);
                    s.Value("offsetX", sy.OffsetX);
                    s.Value("offsetY", sy.OffsetY);
                    s.Value("style", sy.StyleId);
                    s.Value("absoluteRotation", sy.AbsoluteRotation);
                    s.EndElement();
                }
                s.EndArray();
            }
            s.EndElement();
        }
        s.EndArray();
    }
}
