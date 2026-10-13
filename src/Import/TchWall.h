#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

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

    // 幕墙（TCH_CURTAIN_WALL，内核 TDbCurtainWall::dwgInFields 先调用 TDbWall::dwgInFields）与墙共用的前缀：
    // 只读到受保护几何（起终点、左右宽），用于取幕墙上门窗的墙厚；其后的幕墙私有字段不解析
    inline bool DecodeWallPrefix(const MiniDWG::UnknownEntity& entity, TchWall& wall)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_WALL" && entity.Raw.DxfName != "TCH_CURTAIN_WALL") return false;
        if ((data.Version != CadVersion::AC1024 && data.Version != CadVersion::AC1027) || data.Main.size() < (data.MainBits + 7) / 8 ||
            data.MainBits < 773)
            return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        if (r.ReadByte() != 5) return false;
        const double scale = r.ReadBitDouble();
        if (!std::isfinite(scale) || scale <= 0 ||
            r.ReadBitShort() != 0 || r.ReadByte() != 3 || r.Read2Bits() != 0 ||
            r.ReadRawShort() != 512 || r.ReadByte() != 8)
            return false;
        const auto protectionKey = r.ReadByte();
        auto decoded = r.ReadBytes(84);
        for (auto& byte : decoded) byte ^= protectionKey;
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

    // Read-only compatibility adapter for the schema-5 straight-wall layout
    // observed in tianzheng.dwg. Unidentified fields are checked as signatures,
    // not assigned speculative meanings. Other layouts remain unknown entities.
    // See docs/Tianzheng-wall-compatibility.md for evidence and limitations.
    inline bool DecodeWall(const MiniDWG::UnknownEntity& entity, TchWall& wall, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_WALL") return false;
        if (diagnostic) *diagnostic = "native wall fields do not match verified schema-5 straight-wall layout";
        // 门窗数为 0 时 BS 占 2 位（1157 位），1～255 时占 10 位（1165 位），更多时占 18 位
        if (data.MainBits < 773 || data.MainBits > 1173)
        {
            if (diagnostic) *diagnostic = "unsupported native wall layout: " + std::to_string(data.MainBits) +
                " bits; verified range is 773..1173";
            return false;
        }
        // AC1024（2010 格式，8#楼-A平面图）与 AC1027 的这段位流布局相同；其他版本未核对
        if ((data.Version != CadVersion::AC1024 && data.Version != CadVersion::AC1027) || data.Main.size() < (data.MainBits + 7) / 8)
            return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        if (r.ReadByte() != 5) return false;
        const double scale = r.ReadBitDouble();
        if (!std::isfinite(scale) || scale <= 0 ||
            r.ReadBitShort() != 0 || r.ReadByte() != 3 || r.Read2Bits() != 0 ||
            r.ReadRawShort() != 512 || r.ReadByte() != 8)
            return false;
        const auto protectionKey = r.ReadByte();
        auto decoded = r.ReadBytes(84);
        for (auto& byte : decoded) byte ^= protectionKey;
        DwgBitReader geometry(decoded, data.Version, Codec::CodePage::Gbk);
        TchWall result;
        result.Start = geometry.Read3RawDouble();
        result.End = geometry.Read3RawDouble();
        const double curveParameter = geometry.ReadRawDouble();
        const auto flags = geometry.ReadRawLong();
        result.LeftWidth = geometry.ReadRawDouble();
        result.RightWidth = geometry.ReadRawDouble();
        const double height = geometry.ReadRawDouble();
        // 受保护数据之后：RC 0、门窗数 BS、2 位 0、4 位标志、RC 标志、2 位 0。
        // 门窗数为 0 的墙这一段在 tianzheng.dwg 里恰好是字节 00 8A 00 加 2 位 0；有门窗的墙多出的位数与句柄流中多出的
        // TCH_OPENING 引用一致（两份真实样例：各 1 个门窗，1165 位）。
        // 4 位标志与其后的 RC 含义未明：8#楼-A平面图 的墙有 1010 / 1000 / 0111 / 1111 / 0010 与 0 / 128 / 192 多种取值，不影响几何
        if (r.ReadByte() != 0)
            return false;
        const int openings = r.ReadBitShort();
        if (openings < 0 || openings > 1024 || r.Read2Bits() != 0)
            return false;
        r.Read2Bits(); r.Read2Bits();
        if ((r.ReadByte() & 0x3F) != 0 || r.Read2Bits() > 1)        // 最后 2 位：附有墙体造型（TCH_WALL_PATCH）时为 1
            return false;
        result.OpeningCount = openings;
        const double insulation = r.ReadBitDouble();
        for (auto& join : result.Joins) join = r.ReadBitDouble();
        // The remaining 11-bit signature is 00000000 010.
        if (r.ReadByte() != 0 || r.Read3Bits() != 2 || r.Failed() || geometry.Failed() ||
            r.PositionInBits() != data.MainBits || (flags & ~0x7FFF) != 0)   // 标志低位：识别为外墙等（散水样例为 1）；8#楼 还有 0x400 / 0x2000 / 0x4000 等高位
            return false;
        const std::array<double, 10> values{result.Start.X, result.Start.Y, result.Start.Z,
            result.End.X, result.End.Y, result.End.Z, result.LeftWidth, result.RightWidth, height, insulation};
        for (double value : values)
            if (!std::isfinite(value) || std::abs(value) > 1e12) return false;
        const double length = std::hypot(result.End.X - result.Start.X, result.End.Y - result.Start.Y);
        // 曲线参数为弧墙的圆心角（天正 2014 生成的弧墙样例核对：弦长 6000、拱高 1500 时为 1.8546）
        if (!std::isfinite(curveParameter) || std::abs(curveParameter) >= 2 * 3.14159265358979 - 1e-6) return false;
        result.Angle = curveParameter;
        // 一侧宽度可以为 0（轴线贴墙边）；短墙（墙垛、门垛）的长度可以小于墙厚，端部连接量也可以超过墙长，
        // 一侧连接量之和不小于墙长时该侧边退化为点（8#楼-A平面图 均有此类墙）
        if (length < 1e-6 || result.Start.Z != result.End.Z || result.LeftWidth < 0 || result.RightWidth < 0 ||
            result.LeftWidth + result.RightWidth <= 0 || height <= 0 || insulation < 0)
            return false;
        const double joinLimit = length + 2 * (result.LeftWidth + result.RightWidth);
        for (double join : result.Joins)
            if (!std::isfinite(join) || std::abs(join) >= joinLimit) return false;
        wall = result;
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
