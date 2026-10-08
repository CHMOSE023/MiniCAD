#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD
{
    class ISerializer;
    using TextStyleID = uint32_t;

    // 文字样式记录（对应 DXF STYLE 表项）。字体文件名不含路径时到字体目录（fonts/）查找。
    struct TextStyleRecord
    {
        TextStyleID Id          = 0;
        std::string Name;                       // 样式名，如 "Standard"
        std::string FontFile;                   // 主字体：.shx 或 .ttf（DXF 组码 3）
        std::string BigFontFile;                // SHX 大字体，用于中文等双字节字符（组码 4），TTF 主字体时忽略
        double      Height      = 0.0;          // 固定字高（组码 40）；0 = 不固定，新建文字时再定
        double      WidthFactor = 1.0;          // 宽度因子（组码 41）
        double      ObliqueDeg  = 0.0;          // 倾斜角，度（组码 50），向右倾为正

        bool IsShx() const;                     // 主字体是否为 SHX（按扩展名）
    };

    // 文字样式表。ID 稳定（不随删除改变），实体按 ID 引用；找不到的 ID 按 Standard 显示。
    //
    // 预置样式与旧版本全局字体样式的编号保持一致，旧图纸中的样式编号含义不变：
    //   0 = Standard（tssdeng.shx + 大字体 TSSDCHN.SHX，原先找不到样式时使用的探索者字体）
    //   1 = Simplex （simplex.shx）
    //   2 = GB2312  （GB2312.ttf）
    class TextStyleTable
    {
    public:
        static constexpr TextStyleID StandardID = 0;

        TextStyleTable();

        // 名称不区分大小写，不能重名、不能为空。成功返回新 ID，失败返回 InvalidID。
        static constexpr TextStyleID InvalidID = 0xFFFFFFFFu;
        TextStyleID Add(TextStyleRecord rec);

        const TextStyleRecord*              Find(TextStyleID id) const;
        TextStyleRecord*                    Find(TextStyleID id);
        TextStyleID                         FindByName(const std::string& name) const;   // 未找到返回 InvalidID
        const std::vector<TextStyleRecord>& Records() const { return m_records; }

        // 找不到 ID 时退回 Standard（绘制用）
        const TextStyleRecord& Resolve(TextStyleID id) const;

        // 修改字体参数（名称不在此修改）。返回是否找到
        bool Update(TextStyleID id, const TextStyleRecord& rec);
        // 改名：不能改 Standard，不能与其他样式重名，不能为空
        bool Rename(TextStyleID id, const std::string& name);
        // 删除：不能删 Standard；是否被引用由调用方检查
        bool Remove(TextStyleID id);

        // ── 序列化（整表替换；旧文件无此表时保留预置项）──────────────────
        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        std::vector<TextStyleRecord> m_records;
        TextStyleID                  m_nextId = 0;
    };
}
