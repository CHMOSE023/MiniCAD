#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace MiniCAD::Tch
{
    struct TchWall
    {
        MiniDWG::XYZ Start, End;
        double LeftWidth = 0, RightWidth = 0;
        std::array<double, 4> Joins{};
        int OpeningCount = 0;   // 墙上的门窗数；门窗句柄在句柄流中（TCH_OPENING 引用）
        double Angle = 0;       // 弧墙的圆心角（弧度，正值为自起点逆时针到终点）；直墙为 0
        std::uint32_t Flags = 0;    // 受保护几何里的墙标志（内核 this+0x140）
        // 材料码（内核 this+0xe8，TDbWall::IsLineHatch 按它分支）：10 轻质隔墙、20 玻璃幕墙、30 / 31 / 32 填充墙、
        // 40 砖墙、50 石墙、60 钢筋混凝土（tianzheng.dwg 的砖墙为 40、钢筋混凝土墙为 60，与天正特性面板一致）
        int Material = 0;
        double Scale = 100;     // 出图比例：小于图纸设置的详图比例时按详图图案填充

        // 弧墙的圆心与轴线半径
        bool ArcCenter(double& cx, double& cy, double& radius) const
        {
            if (Angle == 0) return false;
            const double dx = End.X - Start.X, dy = End.Y - Start.Y, chord = std::hypot(dx, dy);
            radius = chord / 2 / std::sin(std::abs(Angle) / 2);
            const double h = radius * std::cos(Angle / 2) * (Angle > 0 ? 1 : -1);   // 弦中点到圆心（弦左侧为正）
            cx = (Start.X + End.X) / 2 - dy / chord * h;
            cy = (Start.Y + End.Y) / 2 + dx / chord * h;
            return true;
        }
    };

    namespace Detail
    {
        // 受保护几何（84 字节）按其前的密钥字节逐字节解出：标记 8 时为原始字节 ^ 密钥；
        // 标记 7 时先把原始字节的高低半字节互换再 ^ 密钥（天正暖通 1#楼平面图，与同一栋楼天正电气图中的墙逐字节对照得出）
        inline void Unprotect(std::vector<std::uint8_t>& bytes, int mark, std::uint8_t key)
        {
            for (auto& b : bytes)
            {
                if (mark == 7) b = static_cast<std::uint8_t>((b << 4) | (b >> 4));
                b ^= key;
            }
        }
    }

    // 幕墙（TCH_CURTAIN_WALL，内核 TDbCurtainWall::dwgInFields 先调用 TDbWall::dwgInFields）与墙共用的前缀：
    // 只读到受保护几何（起终点、左右宽），用于取幕墙上门窗的墙厚；其后的幕墙私有字段不解析
    inline bool DecodeWallPrefix(const MiniDWG::UnknownEntity& entity, TchWall& wall)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_WALL" && entity.Raw.DxfName != "TCH_CURTAIN_WALL") return false;
        if ((data.Version != CadVersion::AC1018 && data.Version != CadVersion::AC1024 && data.Version != CadVersion::AC1027) || data.Main.size() < (data.MainBits + 7) / 8 ||
            data.MainBits < 773)
            return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        if (r.ReadByte() != 5) return false;
        const double scale = r.ReadBitDouble();
        if (!std::isfinite(scale) || scale <= 0 ||
            r.ReadBitShort() != 0 || r.ReadByte() != 3 || r.Read2Bits() != 0 ||
            r.ReadRawShort() != 512)
            return false;
        const int mark = r.ReadByte();
        if (mark != 8 && mark != 7) return false;
        const auto protectionKey = r.ReadByte();
        auto decoded = r.ReadBytes(84);
        Detail::Unprotect(decoded, mark, protectionKey);
        DwgBitReader geometry(decoded, data.Version, Codec::CodePage::Gbk);
        TchWall result;
        result.Start = geometry.Read3RawDouble();
        result.End = geometry.Read3RawDouble();
        result.Angle = geometry.ReadRawDouble();
        result.Flags = static_cast<std::uint32_t>(geometry.ReadRawLong());
        result.LeftWidth = geometry.ReadRawDouble();
        result.RightWidth = geometry.ReadRawDouble();
        if (r.Failed() || geometry.Failed()) return false;
        const std::array<double, 9> values{result.Start.X, result.Start.Y, result.Start.Z, result.End.X, result.End.Y, result.End.Z,
                                           result.LeftWidth, result.RightWidth, result.Angle};
        for (double value : values)
            if (!std::isfinite(value) || std::abs(value) > 1e12) return false;
        if (result.LeftWidth < 0 || result.RightWidth < 0 || result.LeftWidth + result.RightWidth <= 0 ||
            std::hypot(result.End.X - result.Start.X, result.End.Y - result.Start.Y) < 1e-6)
            return false;
        wall = result;
        return true;
    }

    namespace Detail
    {
        // 天正内核 TDbWall::dwgInFields 读的字段；幕墙（curtain）的这些字段之后还有幕墙自己的字段
        inline bool DecodeWallFields(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic, bool curtain)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            if (diagnostic) *diagnostic = "native wall fields do not match verified schema-5 straight-wall layout";
            // 门窗数为 0 时 BS 占 2 位（1157 位），1～255 时占 10 位（1165 位），更多时占 18 位；幕墙自己的字段至多 4 个 RC + 6 个 BD
            const std::uint64_t maxBits = 1173 + (curtain ? 4 * 8 + 6 * 66 : 0);
            if (data.MainBits < 773 || data.MainBits > maxBits)
            {
                if (diagnostic) *diagnostic = "unsupported native wall layout: " + std::to_string(data.MainBits) +
                    " bits; verified range is 773.." + std::to_string(maxBits);
                return false;
            }
            // AC1018（2004 格式，天正电气 1#楼平面图）、AC1024（2010 格式，8#楼-A平面图）与 AC1027 的这段位流布局相同；其他版本未核对
            if ((data.Version != CadVersion::AC1018 && data.Version != CadVersion::AC1024 && data.Version != CadVersion::AC1027) || data.Main.size() < (data.MainBits + 7) / 8)
                return false;
            auto fail = [&](const std::string& why) { if (diagnostic) *diagnostic = why; return false; };
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            if (const int v = r.ReadByte(); v != 5) return fail("unsupported native wall base version " + std::to_string(v));
            const double scale = r.ReadBitDouble();
            const int bs = r.ReadBitShort(), rc = r.ReadByte(), b2 = r.Read2Bits(), raw = r.ReadRawShort(), rc8 = r.ReadByte();
            if (!std::isfinite(scale) || scale <= 0 || bs != 0 || rc != 3 || b2 != 0 || raw != 512 || (rc8 != 8 && rc8 != 7))
                return fail("native wall header mismatch: scale=" + std::to_string(scale) + " bs=" + std::to_string(bs) + " rc=" + std::to_string(rc) +
                            " bits=" + std::to_string(b2) + " raw=" + std::to_string(raw) + " rc8=" + std::to_string(rc8));
            const auto protectionKey = r.ReadByte();
            auto decoded = r.ReadBytes(84);
            Detail::Unprotect(decoded, rc8, protectionKey);
            DwgBitReader geometry(decoded, data.Version, Codec::CodePage::Gbk);
            TchWall result;
            result.Start = geometry.Read3RawDouble();
            result.End = geometry.Read3RawDouble();
            const double curveParameter = geometry.ReadRawDouble();
            const auto flags = geometry.ReadRawLong();
            result.LeftWidth = geometry.ReadRawDouble();
            result.RightWidth = geometry.ReadRawDouble();
            const double height = geometry.ReadRawDouble();
            // 受保护数据之后（天正内核 TDbWall::dwgInFields）：RC 0、门窗数 BS（门窗句柄在句柄流）、材料码 RC、墙体造型数 RC
            //（造型 TCH_WALL_PATCH 的句柄在句柄流）。门窗数为 0 的墙这一段在 tianzheng.dwg 里恰好是字节 00 8A 00 加 2 位 0；
            // 有门窗的墙多出的位数与句柄流中多出的 TCH_OPENING 引用一致（两份真实样例：各 1 个门窗，1165 位）。
            // 8#楼-A平面图 的材料码有 10 / 30 / 31 / 32 / 40 / 60；造型数只见过 0 和 1
            if (r.ReadByte() != 0) return fail("native wall opening prefix mismatch");
            const int openings = r.ReadBitShort();
            if (openings < 0 || openings > 1024) return fail("invalid native wall opening count");
            const int material = r.ReadByte(), patches = r.ReadByte();
            if (material >= 64 || patches > 1)
                return fail("native wall flags mismatch: material=" + std::to_string(material) + " patches=" + std::to_string(patches));
            result.Material = material;
            result.OpeningCount = openings;
            result.Scale = scale;
            const double insulation = r.ReadBitDouble();
            for (auto& join : result.Joins) join = r.ReadBitDouble();
            // The remaining 11-bit signature is 00000000 010.
            const bool signature = r.ReadByte() == 0 && r.Read3Bits() == 2;
            // 最后是一个字符串（R2007 起在文字流）；R2004 就地在主数据流（1#楼平面图的墙均为空串，占 2 位）
            if (data.Version < CadVersion::AC1021) r.ReadVariableText();
            if (!signature) return fail("native wall trailing signature mismatch");
            if (curtain)
            {
                // TDbCurtainWall::dwgInFields：RC 版本（≤ 2）、RC、6 个 BD、2 个 RC（按其值新建对象，不再读数据）；3 个硬指针在句柄流
                // （8#楼-A平面图 的 38 个幕墙这一段都是 300 位）
                const int version = r.ReadByte();
                r.ReadByte();
                for (int i = 0; i < 6; ++i) r.ReadBitDouble();
                r.ReadByte();
                r.ReadByte();
                if (version > 2) return fail("unsupported native curtain wall version " + std::to_string(version));
            }
            if (r.Failed() || geometry.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native wall fields end at bit " + std::to_string(r.PositionInBits()) + " of " + std::to_string(data.MainBits));
            // 标志低位：识别为外墙等（散水样例为 1）；8#楼 还有 0x400 / 0x2000 / 0x4000 等高位。
            // 幕墙另有 0x8000 以上的位（8#楼 为 0x18100 / 0x1c100 / 0x1810c 等），0x4 为玻璃幕墙
            if ((flags & (curtain ? ~0x3FFFF : ~0x7FFF)) != 0)
                return fail("unsupported native wall flags " + std::to_string(flags));
            result.Flags = static_cast<std::uint32_t>(flags);
            const std::array<double, 10> values{result.Start.X, result.Start.Y, result.Start.Z,
                result.End.X, result.End.Y, result.End.Z, result.LeftWidth, result.RightWidth, height, insulation};
            for (double value : values)
                if (!std::isfinite(value) || std::abs(value) > 1e12) return fail("nonfinite native wall geometry");
            const double length = std::hypot(result.End.X - result.Start.X, result.End.Y - result.Start.Y);
            // 曲线参数为弧墙的圆心角（天正 2014 生成的弧墙样例核对：弦长 6000、拱高 1500 时为 1.8546）
            if (!std::isfinite(curveParameter) || std::abs(curveParameter) >= 2 * 3.14159265358979 - 1e-6) return fail("invalid native wall arc angle");
            result.Angle = curveParameter;
            // 一侧宽度可以为 0（轴线贴墙边）；短墙（墙垛、门垛）的长度可以小于墙厚，端部连接量也可以超过墙长，
            // 一侧连接量之和不小于墙长时该侧边退化为点（8#楼-A平面图 均有此类墙）
            if (length < 1e-6 || result.Start.Z != result.End.Z || result.LeftWidth < 0 || result.RightWidth < 0 ||
                result.LeftWidth + result.RightWidth <= 0 || height <= 0 || insulation < 0)
                return fail("invalid native wall geometry");
            const double joinLimit = length + 2 * (result.LeftWidth + result.RightWidth);
            for (double join : result.Joins)
                if (!std::isfinite(join) || std::abs(join) >= joinLimit) return fail("invalid native wall join");
            wall = result;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // Read-only compatibility adapter for the schema-5 straight-wall layout
    // observed in tianzheng.dwg. Unidentified fields are checked as signatures,
    // not assigned speculative meanings. Other layouts remain unknown entities.
    // See docs/Tianzheng-wall-compatibility.md for evidence and limitations.
    inline bool DecodeWall(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_WALL") return false;
        return Detail::DecodeWallFields(entity, wall, diagnostic, false);
    }

    // 幕墙（TCH_CURTAIN_WALL）：内核 TDbCurtainWall::dwgInFields 先调用 TDbWall::dwgInFields，墙的字段（连接量、材料码等）
    // 与墙相同，其后是幕墙自己的字段（只核对，不取值）。读不出时退回 DecodeWallPrefix（只有轴线与左右宽）
    inline bool DecodeCurtainWall(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_CURTAIN_WALL") return false;
        return Detail::DecodeWallFields(entity, wall, diagnostic, true);
    }

    // 图纸的天正设置（非图形对象 TCH_DBCONFIG）里与墙柱填充有关的部分。缺省为天正 2014 的默认值（sys\TOptionsDef.xml“墙柱加粗填充”）
    struct TchConfig
    {
        bool   Fill = false;        // 天正选项“墙柱填充”（状态栏的“填充”开关）；天正的默认值为否
        double DetailScale = 100;   // 详图比例：对象的出图比例小于它时用详图图案
        // 各材料的标准、详图填充图案名（序号见 MaterialSlot；空串为不填充）
        std::array<std::string, 8> Standard{"", "", "", "", "", "", "SOLID", ""};
        std::array<std::string, 8> Detail{"", "", "", "", "普通砖", "天然石材", "钢筋混凝土", "金属"};
    };

    // 材料码 → 图案序号：轻质隔墙、填充墙、填充墙1、填充墙2、砖、石、钢筋混凝土、钢（天正内核 TDbWall::IsLineHatch 按同样的顺序
    // 取设置里的标志）；-1 为不填充（如玻璃幕墙 20）
    inline int MaterialSlot(int material)
    {
        switch (material)
        {
        case 10: return 0;
        case 30: return 1;
        case 31: return 2;
        case 32: return 3;
        case 40: return 4;
        case 50: return 5;
        case 60: return 6;
        case 70: return 7;
        default: return -1;
        }
    }

    // 按天正内核 TDbConfig::dwgInFields（版本 4～9）读：RC 版本、BD、BD、BS、RC 填充（this+0x22）、3 个 RC、BD、BD、2 个 RC、
    // BL、BD、BD 详图比例（this+0x48）；12 个字符串依次为轻质隔墙、填充墙、砖、石、钢筋混凝土、钢的标准图案与同序的详图图案，
    // 12 个 BD；版本 6 起 2 个字符串后为填充墙1 的标准、详图图案，2 个 BD，再是填充墙2 的。字符串 R2007 起在文字流，之前就地
    // 在主数据流。tianzheng.dwg 把“填充”这一位清掉后，天正 2014 里所有墙柱的填充都不见了；8#楼-A平面图 改过轻质隔墙等的详图图案
    //（轻质隔墙为卵石砌体，天正里显示成圆圈）。图案名读不出时保留默认值
    inline bool DecodeConfig(const MiniDWG::UnknownObject& object, TchConfig& config)
    {
        using namespace MiniDWG;
        const auto& data = object.Raw.Dwg;
        if (object.Raw.DxfName != "TCH_DBCONFIG" || data.Main.size() < (data.MainBits + 7) / 8) return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        const int version = r.ReadByte();
        if (version < 4 || version > 9) return false;
        r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadBitShort();
        const int fill = r.ReadByte();
        r.ReadByte(); r.ReadByte(); r.ReadByte();
        r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte(); r.ReadByte();
        r.ReadBitLong();
        r.ReadBitDouble();
        const double detail = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() > data.MainBits || fill > 1 || !std::isfinite(detail) || detail <= 0 || detail > 1e6)
            return false;
        config.Fill = fill != 0;
        config.DetailScale = detail;

        const bool inlineText = data.Version < CadVersion::AC1021;
        DwgBitReader text(data.Text, data.Version, Codec::CodePage::Gbk);
        auto next = [&] { return inlineText ? r.ReadVariableText() : text.ReadVariableText(); };
        TchConfig patterns = config;
        for (auto* list : {&patterns.Standard, &patterns.Detail})
            for (const int slot : {0, 1, 4, 5, 6, 7}) (*list)[slot] = next();
        for (int i = 0; i < 12; ++i) r.ReadBitDouble();
        if (version >= 6)
        {
            next(); next();
            patterns.Standard[2] = next(); patterns.Detail[2] = next();
            r.ReadBitDouble(); r.ReadBitDouble();
            patterns.Standard[3] = next(); patterns.Detail[3] = next();
        }
        const bool namesOk = std::ranges::all_of(patterns.Standard, [](const std::string& s) { return s.size() < 256; }) &&
                             std::ranges::all_of(patterns.Detail, [](const std::string& s) { return s.size() < 256; });
        if (!r.Failed() && !text.Failed() && r.PositionInBits() <= data.MainBits && namesOk)
            config = std::move(patterns);
        return true;
    }

    // 墙、柱的填充图案名：出图比例小于详图比例时取详图图案；空为不填充
    inline std::string_view MaterialHatch(const TchConfig& config, int material, bool detail)
    {
        const int slot = MaterialSlot(material);
        if (slot < 0) return {};
        return detail ? config.Detail[static_cast<std::size_t>(slot)] : config.Standard[static_cast<std::size_t>(slot)];
    }

    // 图案库里没有天正图案时的替代（.pat 格式，单位为图面毫米，乘出图比例）：普通砖为 45° 斜线、间距 2.5
    //（与天正分解出的填充一致）；钢筋混凝土为同样的斜线加骨料：小三角形（三族线的虚线段在格点 i·(4.5,0) + j·(1.6,3.8)
    // 处围成三角形，边长为格向量的 0.15）与两组点，样子近似天正的显示。其余图案（天然石材、金属及图纸设置里改用的卵石砌体、
    // 加气混凝土等）没有替代：把天正的 acad.pat 放进 assets/patterns 才画得出来
    inline const char* MaterialHatchFallback(std::string_view name)
    {
        if (name == "普通砖")
            return "*普通砖\n45, 0,0, 0,2.5\n";
        if (name == "钢筋混凝土")
            return "*钢筋混凝土\n45, 0,0, 0,2.5\n"
                   "0, 0,0, 1.6,3.8, 0.675,-3.825\n"
                   "67.1663, 0,0, -1.7463,4.1474, 0.6185,-3.5046\n"
                   "127.3493, 0.675,0, 2.73,3.5773, 0.717,-4.0631\n"
                   "0, 2.2,1.3, 1.6,3.8, 0,-4.5\n"
                   "0, 3.3,2.6, 1.6,3.8, 0,-4.5\n";
        return nullptr;
    }
}
