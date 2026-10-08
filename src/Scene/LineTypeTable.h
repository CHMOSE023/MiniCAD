#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD
{
    class ISerializer;
    using LineTypeID = uint32_t;

    // 命名线型记录(对应 DXF LTYPE 表项)。Pattern 为 dash/gap 序列,单位为绘图单位:
    //   > 0 实线段长度,< 0 空白段长度,== 0 点。空 Pattern 表示连续实线。
    struct LineTypeRecord
    {
        std::string         Name;                // 线型名,如 "DASHED"、"Continuous"
        std::string         Description;         // 说明文字(DXF 组码 3)
        std::vector<double> Pattern;             // dash 序列(DXF 组码 49/74…)
        double              PatternLength = 0.0; // 一个完整周期长度(组码 40)

        bool IsContinuous() const { return Pattern.empty(); }
    };

    // 命名线型表。内置保留项:ByLayer / ByBlock / Continuous,其余可自由追加。
    class LineTypeTable
    {
    public:
        // 保留 ID(与 EntityAttr 默认值约定一致:0 = ByLayer)。
        static constexpr LineTypeID ByLayerID    = 0;
        static constexpr LineTypeID ByBlockID    = 1;
        static constexpr LineTypeID ContinuousID = 2;

        LineTypeTable();   // 预置内置线型 + 几个常用线型

        // 追加/查找。名称不区分大小写。
        LineTypeID                         Add(LineTypeRecord rec);
        const LineTypeRecord*              Find(LineTypeID id) const;
        LineTypeID                         FindByName(const std::string& name) const; // 未找到返回 ContinuousID
        const std::vector<LineTypeRecord>& Records() const { return m_records; }

        // ── 序列化(Deserialize 整表替换,索引即 ID)──────────────────────
        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        std::vector<LineTypeRecord> m_records;   // 索引即 LineTypeID
    };
}
