#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Import/TchCipher.h"
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
        std::uint32_t Flags = 0;    // 受保护几何里的墙标志
        // 材料码（决定填充图案）：10 轻质隔墙、20 玻璃幕墙、30 / 31 / 32 填充墙、
        // 40 砖墙、50 石墙、60 钢筋混凝土（tianzheng.dwg 的砖墙为 40、钢筋混凝土墙为 60，与天正特性面板一致）
        int Material = 0;
        double Scale = 100;     // 出图比例：小于图纸设置的详图比例时按详图图案填充
        // 两端相接的墙：墙最先的 6 个软指针里的后 4 个（句柄流第 3～6 个），起点两个、终点两个；
        // 空句柄为没有（tianzheng.dwg 的钢筋混凝土墙 3B1：起点 3B3 / 3B2，终点 3B0 / 3B0）
        std::array<MiniDWG::Handle, 4> Connections{};

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
        // 标记 7 时先把原始字节的高低半字节互换再 ^ 密钥（暖通平面样图，与同一栋楼天正电气图中的墙逐字节对照得出）
        inline void Unprotect(std::vector<std::uint8_t>& bytes, int mark, std::uint8_t key)
        {
            for (auto& b : bytes)
            {
                if (mark == 7) b = static_cast<std::uint8_t>((b << 4) | (b >> 4));
                b ^= key;
            }
        }
    }

    // 幕墙（TCH_CURTAIN_WALL，先是墙的全部字段）与墙共用的前缀：
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
        // 墙的几何是否合理；不合理时返回原因
        inline const char* CheckWall(const TchWall& w, double height, double insulation)
        {
            const std::array<double, 10> values{w.Start.X, w.Start.Y, w.Start.Z, w.End.X, w.End.Y, w.End.Z, w.LeftWidth, w.RightWidth, height, insulation};
            for (double value : values)
                if (!std::isfinite(value) || std::abs(value) > 1e12) return "nonfinite native wall geometry";
            const double length = std::hypot(w.End.X - w.Start.X, w.End.Y - w.Start.Y);
            // 曲线参数为弧墙的圆心角（天正 2014 生成的弧墙样例核对：弦长 6000、拱高 1500 时为 1.8546）
            if (!std::isfinite(w.Angle) || std::abs(w.Angle) >= 2 * 3.14159265358979 - 1e-6) return "invalid native wall arc angle";
            // 一侧宽度可以为 0（轴线贴墙边）；短墙（墙垛、门垛）的长度可以小于墙厚，端部连接量也可以超过墙长，
            // 一侧连接量之和不小于墙长时该侧边退化为点（建筑平面样图均有此类墙）
            if (length < 1e-6 || w.Start.Z != w.End.Z || w.LeftWidth < 0 || w.RightWidth < 0 ||
                w.LeftWidth + w.RightWidth <= 0 || height <= 0 || insulation < 0)
                return "invalid native wall geometry";
            const double joinLimit = length + 2 * (w.LeftWidth + w.RightWidth);
            for (double join : w.Joins)
                if (!std::isfinite(join) || std::abs(join) >= joinLimit) return "invalid native wall join";
            return nullptr;
        }

        // 新版天正（基类版本 9～14，见 TchCipher.h）的墙版本 12：RC 版本、RC 密钥；
        // 起点、终点（三维点）、圆心角、标志、左宽、右宽、高；6 个句柄（后 4 个为两端相接的墙）；数值；标志含 0x10 时一段多段线
        //（未核对，不支持）；门窗数与门窗句柄；材料码；墙体造型数与句柄；保温层厚与 4 个连接量；6 个句柄；个数与句柄；
        // 布尔、字符串、数值、2 个句柄、个数与句柄。幕墙自己的字段未核对，不支持
        inline bool DecodeWallCipher(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic, bool curtain)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            if (curtain) return fail("unsupported native curtain wall layout");
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 12) return fail("unsupported native wall version");
            f.ReadKey();
            TchWall result;
            result.Start = f.Point3();
            result.End = f.Point3();
            result.Angle = f.Double();
            result.Flags = static_cast<std::uint32_t>(f.Long());
            result.LeftWidth = f.Double();
            result.RightWidth = f.Double();
            const double height = f.Double();
            f.Handle(); f.Handle();
            for (auto& h : result.Connections) h = f.Handle();
            f.Byte();
            if (result.Flags & 0x10) return fail("walls with a profile polyline are not supported");
            result.OpeningCount = f.Short();
            for (int i = 0; i < result.OpeningCount && f.Ok(); ++i) f.Handle();
            result.Material = f.Byte();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Handle();
            const double insulation = f.Double();
            for (auto& join : result.Joins) join = f.Double();
            for (int i = 0; i < 6; ++i) f.Handle();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Handle();
            f.Bool(); f.Text(); f.Double();
            f.Handle(); f.Handle();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Handle();
            if (!f.Exact()) return fail("native wall fields do not consume the payload exactly");
            result.Scale = f.Scale;
            if (const char* why = CheckWall(result, height, insulation)) return fail(why);
            wall = result;
            if (diagnostic) diagnostic->clear();
            return true;
        }

        // 墙的字段；幕墙（curtain）的这些字段之后还有幕墙自己的字段
        inline bool DecodeWallFields(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic, bool curtain)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            if (IsCipherObject(data)) return DecodeWallCipher(entity, wall, diagnostic, curtain);
            if (diagnostic) *diagnostic = "native wall fields do not match verified schema-5 straight-wall layout";
            // 门窗数为 0 时 BS 占 2 位（1157 位），1～255 时占 10 位（1165 位），更多时占 18 位；幕墙自己的字段至多 4 个 RC + 6 个 BD
            const std::uint64_t maxBits = 1173 + (curtain ? 4 * 8 + 6 * 66 : 0);
            if (data.MainBits < 773 || data.MainBits > maxBits)
            {
                if (diagnostic) *diagnostic = "unsupported native wall layout: " + std::to_string(data.MainBits) +
                    " bits; verified range is 773.." + std::to_string(maxBits);
                return false;
            }
            // AC1018（2004 格式，电气平面样图）、AC1024（2010 格式，建筑平面样图）与 AC1027 的这段位流布局相同；其他版本未核对
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
            // 受保护数据之后：RC 0、门窗数 BS（门窗句柄在句柄流）、材料码 RC、墙体造型数 RC
            //（造型 TCH_WALL_PATCH 的句柄在句柄流）。门窗数为 0 的墙这一段在 tianzheng.dwg 里恰好是字节 00 8A 00 加 2 位 0；
            // 有门窗的墙多出的位数与句柄流中多出的 TCH_OPENING 引用一致（两份真实样例：各 1 个门窗，1165 位）。
            // 建筑平面样图的材料码有 10 / 30 / 31 / 32 / 40 / 60；造型数只见过 0 和 1
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
            // 最后是一个字符串（R2007 起在文字流）；R2004 就地在主数据流（电气平面样图的墙均为空串，占 2 位）
            if (data.Version < CadVersion::AC1021) r.ReadVariableText();
            if (!signature) return fail("native wall trailing signature mismatch");
            if (curtain)
            {
                // 幕墙自己的字段：RC 版本（≤ 2）、RC、6 个 BD、2 个 RC（按其值新建对象，不再读数据）；3 个硬指针在句柄流
                // （建筑平面样图的 38 个幕墙这一段都是 300 位）
                const int version = r.ReadByte();
                r.ReadByte();
                for (int i = 0; i < 6; ++i) r.ReadBitDouble();
                r.ReadByte();
                r.ReadByte();
                if (version > 2) return fail("unsupported native curtain wall version " + std::to_string(version));
            }
            if (r.Failed() || geometry.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native wall fields end at bit " + std::to_string(r.PositionInBits()) + " of " + std::to_string(data.MainBits));
            // 标志低位：识别为外墙等（散水样例为 1）；建筑样图还有 0x400 / 0x2000 / 0x4000 等高位。
            // 幕墙另有 0x8000 以上的位（建筑样图为 0x18100 / 0x1c100 / 0x1810c 等），0x4 为玻璃幕墙
            if ((flags & (curtain ? ~0x3FFFF : ~0x7FFF)) != 0)
                return fail("unsupported native wall flags " + std::to_string(flags));
            result.Flags = static_cast<std::uint32_t>(flags);
            result.Angle = curveParameter;
            if (const char* why = CheckWall(result, height, insulation)) return fail(why);
            // 相接的墙在句柄流里；没有句柄流（或不足 6 个）时按两端都没有相接的墙
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            std::array<Handle, 6> soft{};
            for (auto& h : soft) h = handles.ReadHandle(entity.ObjectHandle);
            if (!handles.Failed() && handles.PositionInBits() <= data.HandleBits)
                std::copy(soft.begin() + 2, soft.end(), result.Connections.begin());
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

    // 幕墙（TCH_CURTAIN_WALL）：先是墙的全部字段（连接量、材料码等）
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
        std::array<std::string, 8> Detail{"", "", "", "", "普通砖", "天然石材", "钢筋混凝土", "金属"};   // i18n: 数据
        // 内视符号编号的大小（对象自己没设时取图纸设置里的这两项）：
        // 编号与半径之比、大小算法。没有设置对象时为默认值 0.6、0；设置版本 < 7 时天正
        // 读完把比例置为 0.8
        double IndexTextRatio = 0.6;
        int    IndexTextMode = 0;
    };

    // 材料码 → 图案序号：轻质隔墙、填充墙、填充墙1、填充墙2、砖、石、钢筋混凝土、钢（天正按同样的顺序
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

    // 按版本 4～9 读：RC 版本、BD、BD、BS、RC 填充、3 个 RC、BD、BD、2 个 RC、
    // BL、BD、BD 详图比例；12 个字符串依次为轻质隔墙、填充墙、砖、石、钢筋混凝土、钢的标准图案与同序的详图图案，
    // 12 个 BD；版本 6 起 2 个字符串后为填充墙1 的标准、详图图案，2 个 BD，再是填充墙2 的。字符串 R2007 起在文字流，之前就地
    // 在主数据流。tianzheng.dwg 把“填充”这一位清掉后，天正 2014 里所有墙柱的填充都不见了；建筑平面样图改过轻质隔墙等的详图图案
    //（轻质隔墙为卵石砌体，天正里显示成圆圈）。图案名读不出时保留默认值。
    // 版本 10、11（新版，tianzheng2.dwg 为 11）前面的布局相同；版本 6 起图案之后还有 8 个 B、8 个 BS（句柄在句柄流），
    // 版本 7 起再是 BD、RC、BD 内视符号编号比例、RC 编号大小算法
    inline bool DecodeConfig(const MiniDWG::UnknownObject& object, TchConfig& config)
    {
        using namespace MiniDWG;
        const auto& data = object.Raw.Dwg;
        if (object.Raw.DxfName != "TCH_DBCONFIG" || data.Main.size() < (data.MainBits + 7) / 8) return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        const int version = r.ReadByte();
        if (version < 4 || version > 11) return false;
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

        // 内视符号编号的设置在图案之后，单独判断：读不出时保留默认值，不影响前面的图案
        if (version >= 6)
        {
            r.ReadBitDouble(); r.ReadBitDouble();
            for (int i = 0; i < 8; ++i) r.ReadBit();
            for (int i = 0; i < 8; ++i) r.ReadBitShort();
        }
        double ratio = 0.8;
        int mode = 0;
        if (version >= 7)
        {
            r.ReadBitDouble(); r.ReadByte();
            ratio = r.ReadBitDouble();
            mode = r.ReadByte();
        }
        if (!r.Failed() && r.PositionInBits() <= data.MainBits && std::isfinite(ratio))
        {
            config.IndexTextRatio = ratio;
            config.IndexTextMode = mode;
        }
        return true;
    }

    // 墙、柱的填充图案名：出图比例小于详图比例时取详图图案；空为不填充
    inline std::string_view MaterialHatch(const TchConfig& config, int material, bool detail)
    {
        const int slot = MaterialSlot(material);
        if (slot < 0) return {};
        return detail ? config.Detail[static_cast<std::size_t>(slot)] : config.Standard[static_cast<std::size_t>(slot)];
    }
}
