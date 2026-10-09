#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <array>
#include <cmath>
#include <string>

namespace MiniCAD::Tch
{
    struct TchWall
    {
        MiniDWG::XYZ Start, End;
        double LeftWidth = 0, RightWidth = 0;
        std::array<double, 4> Joins{};
        int OpeningCount = 0;   // 墙上的门窗数；门窗句柄在句柄流中（TCH_OPENING 引用）
    };

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
        if (data.Version != CadVersion::AC1027 || data.Main.size() != (data.MainBits + 7) / 8)
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
        // 受保护数据之后：RC 0、门窗数 BS、6 位签名 001010、RC 0、2 位 0。
        // 门窗数为 0 的墙这一段恰好是字节 00 8A 00 加 2 位 0；有门窗的墙多出的位数与句柄流中多出的
        // TCH_OPENING 引用一致（两份真实样例：各 1 个门窗，1165 位）
        if (r.ReadByte() != 0)
            return false;
        const int openings = r.ReadBitShort();
        if (openings < 0 || openings > 1024 || r.Read2Bits() != 0 || r.Read2Bits() != 2 || r.Read2Bits() != 2 ||
            r.ReadByte() != 0 || r.Read2Bits() != 0)
            return false;
        result.OpeningCount = openings;
        const double insulation = r.ReadBitDouble();
        for (auto& join : result.Joins) join = r.ReadBitDouble();
        // The remaining 11-bit signature is 00000000 010.
        if (r.ReadByte() != 0 || r.Read3Bits() != 2 || r.Failed() || geometry.Failed() ||
            r.PositionInBits() != data.MainBits || curveParameter != 0 || flags != 0)
            return false;
        const std::array<double, 10> values{result.Start.X, result.Start.Y, result.Start.Z,
            result.End.X, result.End.Y, result.End.Z, result.LeftWidth, result.RightWidth, height, insulation};
        for (double value : values)
            if (!std::isfinite(value) || std::abs(value) > 1e12) return false;
        const double length = std::hypot(result.End.X - result.Start.X, result.End.Y - result.Start.Y);
        if (length < 1e-6 || result.Start.Z != result.End.Z || result.LeftWidth <= 0 || result.RightWidth <= 0 ||
            result.LeftWidth + result.RightWidth >= length || height <= 0 || insulation < 0)
            return false;
        for (double join : result.Joins)
            if (!std::isfinite(join) || std::abs(join) >= length) return false;
        if (result.Joins[0] + result.Joins[1] >= length || result.Joins[2] + result.Joins[3] >= length)
            return false;
        wall = result;
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
