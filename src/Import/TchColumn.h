#pragma once
#include "Import/TchOpening.h"
#include <vector>

namespace MiniCAD::Tch
{
    struct TchColumn
    {
        struct Vertex { double X = 0, Y = 0, Bulge = 0; };

        MiniDWG::XYZ        Position;   // 柱的插入点（Z 为底标高）
        std::vector<Vertex> Outline;    // 闭合轮廓，世界坐标；Bulge 为该顶点到下一顶点的凸度
        double              Height = 0;
    };

    // TCH_COLUMN 柱：按 TDbColumn 的私有布局
    // 版本 < 7 的明文分支解析，必须恰好读完主数据流。轮廓顶点为 2RD（filer 的二维点）加 BD 凸度，
    // 坐标即世界坐标；真实样例为 600×600 方柱，与天正 2014 显示及分解出的轮廓一致
    inline bool DecodeColumn(const MiniDWG::UnknownEntity& entity, TchColumn& column, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_COLUMN") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native column");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 6) return fail("unsupported native column version");

        TchColumn c;
        c.Position = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
        r.ReadByte();                                           // 形状类型
        const int count = r.ReadBitShort();
        if (count < 2 || count > 4096) return fail("invalid column outline vertex count");
        c.Outline.resize(static_cast<std::size_t>(count));
        for (auto& v : c.Outline)
        {
            v.X = r.ReadRawDouble();
            v.Y = r.ReadRawDouble();
            v.Bulge = r.ReadBitDouble();
        }
        c.Height = r.ReadBitDouble();
        r.ReadByte();
        r.ReadByte();                                           // 句柄个数（句柄在句柄流）
        if (version >= 2) r.ReadByte();
        if (version > 3)
        {
            const int flags = r.ReadBitShort();
            if (flags & 8)                                      // 字符串在文字流，两个句柄在句柄流
            {
                r.ReadRawDouble(); r.ReadRawDouble();
                r.ReadBitDouble();
            }
        }
        if (version > 4) r.ReadByte();                          // 句柄个数
        if (version > 5) r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native column fields do not consume the payload exactly");

        for (const auto& v : c.Outline)
            if (!std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Bulge) || std::abs(v.X) > 1e12 ||
                std::abs(v.Y) > 1e12 || std::abs(v.Bulge) > 1e6)
                return fail("nonfinite column outline");
        if (!std::isfinite(c.Position.Z) || std::abs(c.Position.Z) > 1e12) return fail("nonfinite column position");
        column = std::move(c);
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
