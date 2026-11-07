#pragma once
#include "Import/TchShape.h"
#include <cmath>
#include <vector>

namespace MiniCAD::Tch
{
    // 房间（TCH_SPACE）：代理图形为空（8 字节头、0 条记录），几何是私有数据里的轮廓。
    // 加密对象（基类 9～14）：CipherReader::ReadBase 之后
    //   version(9) + 若干短字段 + BS 顶点数 + 顶点数组（每顶点 X/Y 为两个明文 RD，
    //   组内另有 2 位；实测 t5~t20V9 同一张图为 130 位一组，8 个顶点）。
    // 顶点数组结束位置恰好等于主数据流末尾（Exact 对齐），据此定位数组起点。
    // 名称等其余字段未核对，只画房间轮廓。
    struct TchSpace
    {
        std::vector<MiniDWG::XY> Outline;
        int Version = 0;
    };

    namespace Detail
    {
        inline bool CheckSpace(const TchSpace& s)
        {
            if (s.Outline.size() < 3 || s.Outline.size() > 4096) return false;
            for (const auto& p : s.Outline)
                if (!std::isfinite(p.X) || !std::isfinite(p.Y) || std::abs(p.X) > 1e9 || std::abs(p.Y) > 1e9) return false;
            return true;
        }
    }

    inline bool DecodeSpace(const MiniDWG::UnknownEntity& entity, TchSpace& space, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_SPACE") return false;
        if (!Detail::IsCipherObject(data) || data.Main.empty() || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported TCH_SPACE layout");
        Detail::CipherReader f(entity);
        if (!f.ReadBase()) return fail("unsupported Tianzheng space base layout");
        TchSpace s;
        s.Version = f.Main.ReadByte();
        f.ReadKey();
        const auto afterKey = f.Main.PositionInBits();
        if (afterKey >= data.MainBits) return fail("space payload is incomplete");
        // 顶点数组：从某个 BS 顶点数起，n 个 (X,Y) 明文 RD，结束后恰好到主数据流末尾。
        // 组内顶点步长按实测 130 位（两个 RD 加 2 位），数组起始位在 afterKey 之后。
        bool found = false;
        for (unsigned long long probe = afterKey; probe < data.MainBits; ++probe)
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            r.SetPositionInBits(probe);
            const int n = r.ReadBitShort();
            if (n < 3 || n > 4096) continue;
            const auto first = r.PositionInBits();
            const unsigned long long step = 130;
            if (first + step * static_cast<unsigned long long>(n - 1) + 128 > data.MainBits) continue;
            std::vector<XY> pts;
            bool ok = true;
            for (int i = 0; i < n && ok; ++i)
            {
                DwgBitReader v(data.Main, data.Version, Codec::CodePage::Gbk);
                v.SetPositionInBits(first + step * static_cast<unsigned long long>(i));
                const double x = v.ReadRawDouble();
                const double y = v.ReadRawDouble();
                if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1e9 || std::abs(y) > 1e9 ||
                    (std::abs(x) < 1e-6 && std::abs(y) < 1e-6))
                {
                    ok = false;
                    break;
                }
                pts.push_back({x, y});
            }
            if (!ok) continue;
            // 数组末尾必须正好落在主数据流末尾（允许末顶点后的 2 位对齐）
            const auto endBit = first + step * static_cast<unsigned long long>(n - 1) + 128;
            if (endBit + 8 < data.MainBits) continue;
            s.Outline = std::move(pts);
            found = true;
            break;
        }
        if (!found) return fail("native space outline is not recognized");
        if (!Detail::CheckSpace(s)) return fail("invalid native space outline");
        space = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildSpace(const TchSpace& s)
    {
        TchGraphics g;
        TchGraphics::Path p;
        for (const auto& pt : s.Outline) p.Points.push_back({pt.X, pt.Y});
        if (!p.Points.empty()) p.Points.push_back(p.Points.front());
        g.Paths.push_back(std::move(p));
        return g;
    }
}
