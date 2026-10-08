#pragma once
#include "Core/Math/Color4.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD
{
    class ISerializer;
    using MLineStyleID = uint32_t;

    // 多线样式中的一条元素线：到中心线的偏移（沿前进方向，左侧为正）和可选的专用颜色。
    struct MLineElement
    {
        double       Offset   = 0.0;
        bool         UseColor = false;          // false = 随实体颜色（ByLayer 等）
        Math::Color4 Color    = {};
    };

    // 多线样式记录（对应 DXF MLINESTYLE）。元素线按偏移从大到小排列。
    struct MLineStyleRecord
    {
        MLineStyleID              Id = 0;
        std::string               Name;
        std::string               Description;
        std::vector<MLineElement> Elements;
        bool                      StartCap = false;    // 起点封口：用直线连接各元素线的起点
        bool                      EndCap   = false;    // 终点封口
        bool                      Joints   = false;    // 在内部顶点处画连接线（外侧两条元素线之间）

        double MaxOffset() const;                      // 最大 / 最小偏移（无元素时为 0）
        double MinOffset() const;
        void   Normalize();                            // 偏移从大到小排序
    };

    // 多线样式表。ID 稳定，实体持有样式的快照（改样式不会改已画的多线）。
    //   0 = Standard：两条线，偏移 ±0.5，无封口无连接线
    class MLineStyleTable
    {
    public:
        static constexpr MLineStyleID StandardID = 0;
        static constexpr MLineStyleID InvalidID  = 0xFFFFFFFFu;

        MLineStyleTable();

        // 名称不区分大小写，不能重名、不能为空；至少要有一条元素线。成功返回新 ID，失败返回 InvalidID。
        MLineStyleID Add(MLineStyleRecord rec);

        const MLineStyleRecord*              Find(MLineStyleID id) const;
        MLineStyleRecord*                    Find(MLineStyleID id);
        MLineStyleID                         FindByName(const std::string& name) const;
        const std::vector<MLineStyleRecord>& Records() const { return m_records; }

        // 找不到 ID 时退回 Standard
        const MLineStyleRecord& Resolve(MLineStyleID id) const;

        // 修改元素线与封口 / 连接线设置（名称不在此改）
        bool Update(MLineStyleID id, const MLineStyleRecord& rec);
        bool Rename(MLineStyleID id, const std::string& name);
        bool Remove(MLineStyleID id);       // 不能删 Standard；是否被当前样式引用由调用方检查

        // ── 序列化（整表替换；旧文件无此表时保留预置项）──────────────────
        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

        // 样式本身的读写，供多线实体保存它持有的样式快照
        static void SerializeRecord(ISerializer& s, MLineStyleRecord& rec);

    private:
        std::vector<MLineStyleRecord> m_records;
        MLineStyleID                  m_nextId = 0;
    };
}
