#pragma once
#include "Core/Entity/DimensionEntity.hpp"   // DimStyle / DimStyleID
#include <string>
#include <vector>

namespace MiniCAD
{
    // 命名标注样式记录(对应 DXF DIMSTYLE 表项,组码 2 = 名称,余者为 DIMxxx 变量)。
    struct DimStyleRecord
    {
        std::string Name;    // 样式名,如 "Standard"、"GB_机械"、"GB_建筑"
        DimStyle    Style;   // 样式参数
    };

    // 命名标注样式表。内置保留项:Standard(ID 0)。其余可自由追加。
    class DimStyleTable
    {
    public:
        // 保留 ID(与 DimStyle_StandardID 约定一致:0 = Standard)。
        static constexpr DimStyleID StandardID = DimStyle_StandardID;

        DimStyleTable();   // 预置 Standard + 常用 GB 机械/建筑样式

        // 追加。名称不区分大小写,同名复用(返回既有 ID)。
        DimStyleID Add(DimStyleRecord rec);
        DimStyleID Add(const std::string& name, const DimStyle& style);

        const DimStyleRecord* Find(DimStyleID id) const;
        // 按名查找,未找到返回 StandardID。
        DimStyleID            FindByName(const std::string& name) const;

        // 便捷:取样式参数(找不到回退 Standard)。
        const DimStyle&       Style(DimStyleID id) const;

        const std::vector<DimStyleRecord>& Records() const { return m_records; }
        size_t Count() const { return m_records.size(); }

    private:
        std::vector<DimStyleRecord> m_records;   // 索引即 DimStyleID
    };
}
