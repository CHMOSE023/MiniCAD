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
    //
    // 带字母或符号的复杂线型(AutoCAD 的 complex linetype)把「文字/形」也编进
    // 这条序列:GAS_LINE 的 .lin 定义
    //     A,.5,-.2,["GAS",STANDARD,S=.1,U=0.0,X=-.1,Y=-.05],-.25
    // 在 DWG/DXF 里就是 4 个组码 49 段:0.5 / -0.2(文字) / -0.2(文字) / -0.25
    // (DWG 把「形段」与其后紧跟的 dash 段合并存储,所以个数比 .lin 少一个)。
    //
    // 因此 Pattern **保持全量不变** —— 是否实线、周期长度、纯虚线的画法全部沿用
    // 旧规则,只有落在 Symbols 里的下标才在绘制时额外铺一个字母/符号。
    // 这样旧图纸、旧序列化数据的含义完全不变,向后兼容。
    struct LineTypeSymbol
    {
        size_t      PatternIndex = 0;    // 该符号对应 Pattern 的下标(第几个 49 段)
        std::string Text;                // 文字内容(形段为空);DXF 组码 9
        double      Rotation     = 0.0;  // 弧度;组码 50。0 = 沿线方向(形段另有自己的朝向)
        double      Scale        = 1.0;  // 符号高度 = 文字样式字高 × Scale;组码 46
        double      OffsetX      = 0.0;  // 沿线方向的偏移;组码 44
        double      OffsetY      = 0.0;  // 垂直沿线、向上为正的偏移;组码 45
        uint32_t    StyleId      = 0;    // 文字样式(TextStyleTable ID);组码 340
        // 组码 74 位 1(RotationIsAbsolute):旋转相对 X 轴而非沿线方向。
        // DWG 存 flags 位,这里只留一个 bool —— 渲染只需要「是否绝对旋转」。
        bool        AbsoluteRotation = false;
        // 形符号(组码 75):0 = 文字段。非 0 时是 SHX 形号(TRACKS/ZIGZAG/BATTING
        // 用的 130/131/134 等),字形取自 ShapeFont 指向的 SHX 形文件。
        uint32_t    Shape = 0;
        // 形段的字体文件(DXF 组码 340 指向的 STYLE 的组码 3)。
        // 与 StyleId 分开存,是因为线型形段的 STYLE 名字常为空字符串,按名字
        // 进不了文字样式表(Add 拒绝空名),退回 Standard 就丢了形文件路径。
        // 例:TRACKS/ZIGZAG/BATTING 都是 "ltypeshp.shx"。文字段为空。
        std::string ShapeFont;
    };

    struct LineTypeRecord
    {
        std::string                  Name;                // 线型名,如 "DASHED"、"Continuous"
        std::string                  Description;         // 说明文字(DXF 组码 3)
        std::vector<double>          Pattern;             // dash 序列(DXF 组码 49/74…)
        double                       PatternLength = 0.0; // 一个完整周期长度(组码 40)
        std::vector<LineTypeSymbol>  Symbols;             // 文字/形段(可能为空)

        bool IsContinuous() const { return Pattern.empty(); }
        // 有字母/符号要铺的线型:Pattern 仍按虚线走,绘制时多铺符号
        bool HasSymbols() const { return !Symbols.empty(); }
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
