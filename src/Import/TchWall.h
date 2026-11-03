#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Import/TchOpening.h"
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
        bool HasConnections = false;

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
                if (mark == 7 || mark == 10) b = static_cast<std::uint8_t>((b << 4) | (b >> 4));
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
        inline const char* CheckWall(const TchWall& w, double height, double insulation)
        {
            const std::array<double, 10> values{w.Start.X, w.Start.Y, w.Start.Z, w.End.X, w.End.Y, w.End.Z, w.LeftWidth, w.RightWidth, height, insulation};
            for (double value : values)
                if (!std::isfinite(value) || std::abs(value) > 1e12) return "nonfinite native wall geometry";
            const double length = std::hypot(w.End.X - w.Start.X, w.End.Y - w.Start.Y);
            if (!std::isfinite(w.Angle) || std::abs(w.Angle) >= 2 * 3.14159265358979 - 1e-6) return "invalid native wall arc angle"; 
            if (length < 1e-6 || w.Start.Z != w.End.Z || w.LeftWidth < 0 || w.RightWidth < 0 ||
                w.LeftWidth + w.RightWidth <= 0 || height <= 0 || insulation < 0)
                return "invalid native wall geometry";
            const double joinLimit = length + 2 * (w.LeftWidth + w.RightWidth);
            for (double join : w.Joins)
                if (!std::isfinite(join) || std::abs(join) >= joinLimit) return "invalid native wall join";
            return nullptr;
        }

        
        inline bool DecodeWallCipher(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic, bool curtain)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
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
            result.HasConnections = true;
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
            if (curtain)
            {
                if (f.Main.ReadByte() != 3) return fail("unsupported native curtain wall version");
                f.ReadKey();
                f.Byte();
                for (int i = 0; i < 6; ++i) f.Double();
                const int rows = f.Byte(), columns = f.Byte();
                if (!(result.Flags & 0x100)) for (int i = 0; i < rows && f.Ok(); ++i) f.Double();
                if (!(result.Flags & 0x10000)) for (int i = 0; i < columns && f.Ok(); ++i) f.Double();
                f.Handle(); f.Handle(); f.Handle();
            }
            if (!f.Exact()) return fail("native wall fields do not consume the payload exactly");
            result.Scale = f.Scale;
            if (const char* why = CheckWall(result, height, insulation)) return fail(why);
            wall = result;
            if (diagnostic) diagnostic->clear();
            return true;
        }

        inline bool DecodeWallFields(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic, bool curtain)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            if (IsCipherObject(data)) return DecodeWallCipher(entity, wall, diagnostic, curtain);
            if (diagnostic) *diagnostic = "native wall fields do not match verified schema-5 straight-wall layout";
            const bool base7 = !data.Main.empty() && data.Main[0] == 7;
            const std::uint64_t maxBits = 1173 + (curtain ? 4 * 8 + (6 + 2 * 255) * 66 : 0);
            if (!base7 && (data.MainBits < 773 || data.MainBits > maxBits))
            {
                if (diagnostic) *diagnostic = "unsupported native wall layout: " + std::to_string(data.MainBits) +
                    " bits; verified range is 773.." + std::to_string(maxBits);
                return false;
            }
            if ((data.Version != CadVersion::AC1018 && data.Version != CadVersion::AC1024 && data.Version != CadVersion::AC1027 &&
                 !(base7 && data.Version == CadVersion::AC1032)) || data.Main.size() < (data.MainBits + 7) / 8)
                return false;
            auto fail = [&](const std::string& why) { if (diagnostic) *diagnostic = why; return false; };
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            double scale = 0;
            int rc8 = 0, baseHandles = 0;
            if (base7)
            {
                TchBase base;
                if (!ReadTchBase(r, base, &data)) return fail("unsupported native wall base layout");
                scale = base.Scale;
                baseHandles = base.HandleCount;
                rc8 = r.ReadByte();
                if (!std::isfinite(scale) || scale <= 0 || rc8 != 10)
                    return fail("native wall header mismatch: scale=" + std::to_string(scale) + " rc8=" + std::to_string(rc8));
            }
            else
            {
                if (const int v = r.ReadByte(); v != 5) return fail("unsupported native wall base version " + std::to_string(v));
                scale = r.ReadBitDouble();
                const int bs = r.ReadBitShort(), rc = r.ReadByte(), b2 = r.Read2Bits(), raw = r.ReadRawShort();
                rc8 = r.ReadByte();
                if (!std::isfinite(scale) || scale <= 0 || bs != 0 || rc != 3 || b2 != 0 || raw != 512 || (rc8 != 8 && rc8 != 7))
                    return fail("native wall header mismatch: scale=" + std::to_string(scale) + " bs=" + std::to_string(bs) + " rc=" + std::to_string(rc) +
                                " bits=" + std::to_string(b2) + " raw=" + std::to_string(raw) + " rc8=" + std::to_string(rc8));
            }
            const auto protectionKey = r.ReadByte();
            auto decoded = r.ReadBytes(84);
            Detail::Unprotect(decoded, rc8, protectionKey);
            DwgBitReader geometry(decoded, data.Version, Codec::CodePage::Gbk);
            TchWall result;
            result.Start = geometry.Read3RawDouble();
            result.End   = geometry.Read3RawDouble();
            const double curveParameter = geometry.ReadRawDouble();
            const auto flags    = geometry.ReadRawLong();
            double height       = rc8 == 10 ? geometry.ReadRawDouble() : 0;
            result.LeftWidth    = geometry.ReadRawDouble();
            result.RightWidth   = geometry.ReadRawDouble();
            if (rc8 != 10) height = geometry.ReadRawDouble();
          
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
            const bool signature = r.ReadByte() == 0 && r.Read3Bits() == 2;
            if (data.Version < CadVersion::AC1021) r.ReadVariableText();
            if (!signature) return fail("native wall trailing signature mismatch");
            if (rc8 == 10 && !curtain)
            {
                // 墙版本 10 在结尾标记后多出的位：样例为 13 个 0，含义未核对
                const auto tail = data.MainBits - r.PositionInBits();
                if (tail > 16) return fail("unsupported native wall version 10 tail");
                for (std::uint64_t i = 0; i < tail; ++i)
                    if (r.ReadBit()) return fail("unsupported native wall version 10 tail");
            }
            if (curtain)
            {
                const int version = r.ReadByte();
                r.ReadByte();
                for (int i = 0; i < 6; ++i) r.ReadBitDouble();
                const int rows = r.ReadByte(), columns = r.ReadByte();
                if (!(flags & 0x100)) for (int i = 0; i < rows && !r.Failed(); ++i) r.ReadBitDouble();
                if (!(flags & 0x10000)) for (int i = 0; i < columns && !r.Failed(); ++i) r.ReadBitDouble();
                if (version > 2) return fail("unsupported native curtain wall version " + std::to_string(version));
            }
            if (r.Failed() || geometry.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native wall fields end at bit " + std::to_string(r.PositionInBits()) + " of " + std::to_string(data.MainBits));
            if ((flags & (curtain ? ~0x3FFFF : ~0x7FFF)) != 0)
                return fail("unsupported native wall flags " + std::to_string(flags));
            result.Flags = static_cast<std::uint32_t>(flags);
            result.Angle = curveParameter;
            if (const char* why = CheckWall(result, height, insulation)) return fail(why);
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < baseHandles; ++i) handles.ReadHandle(entity.ObjectHandle);
            std::array<Handle, 6> soft{};
            for (auto& h : soft) h = handles.ReadHandle(entity.ObjectHandle);
            if (!handles.Failed() && handles.PositionInBits() <= data.HandleBits)
            {
                std::copy(soft.begin() + 2, soft.end(), result.Connections.begin());
                result.HasConnections = true;
            }
            wall = result;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }
     
    inline bool DecodeWall(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_WALL") return false;
        return Detail::DecodeWallFields(entity, wall, diagnostic, false);
    }
     
    inline bool DecodeCurtainWall(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_CURTAIN_WALL") return false;
        return Detail::DecodeWallFields(entity, wall, diagnostic, true);
    }

    struct TchConfig
    {
        bool   Fill = false;         
        double DetailScale = 100;    
        std::array<std::string, 8> Standard{"", "", "", "", "", "", "SOLID", ""};
        std::array<std::string, 8> Detail{"", "", "", "", "普通砖", "天然石材", "钢筋混凝土", "金属"};   // i18n: 数据
       
        double IndexTextRatio = 0.6;
        int    IndexTextMode = 0;
    };
 
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

    inline std::string_view MaterialHatch(const TchConfig& config, int material, bool detail)
    {
        const int slot = MaterialSlot(material);
        if (slot < 0) return {};
        return detail ? config.Detail[static_cast<std::size_t>(slot)] : config.Standard[static_cast<std::size_t>(slot)];
    }
}
