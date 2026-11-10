#pragma once
#include "Import/TchShape.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    struct TchSpace
    {
        std::vector<MiniDWG::XY> Outline;
        int Version = 0;
        std::string Name;        // 房间名（加密对象文本流里的明文，如"房间"）
        double Scale = 1.0;     // 天正出图比例，用来定标注字高
    };

    namespace Detail
    {
        inline bool PlausibleCoordinate(double v)
        {
            return v == 0 || (std::isfinite(v) && std::abs(v) >= 1e-3 && std::abs(v) < 1e9);
        }

        inline bool CheckSpace(const TchSpace& s)
        {
            if (s.Outline.size() < 3 || s.Outline.size() > 4096) return false;
            double area = 0;
            for (std::size_t i = 0; i < s.Outline.size(); ++i)
            {
                const auto& p = s.Outline[i];
                const auto& q = s.Outline[(i + 1) % s.Outline.size()];
                if (!PlausibleCoordinate(p.X) || !PlausibleCoordinate(p.Y)) return false;
                area += p.X * q.Y - q.X * p.Y;
            }
            return std::abs(area) > 1e-6;
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
                if (!Detail::PlausibleCoordinate(x) || !Detail::PlausibleCoordinate(y) || (x == 0 && y == 0))
                {
                    ok = false;
                    break;
                }
                pts.push_back({x, y});
            }
            if (!ok) continue;
            const auto endBit = first + step * static_cast<unsigned long long>(n - 1) + 128;
            if (endBit + 8 < data.MainBits) continue;
            TchSpace candidate;
            candidate.Outline = std::move(pts);
            if (!Detail::CheckSpace(candidate)) continue;
            s.Outline = std::move(candidate.Outline);
            found = true;
            break;
        }
        if (!found) return fail("native space outline is not recognized");
        if (!Detail::CheckSpace(s)) return fail("invalid native space outline");

        // 房间名在天正加密对象的文本流里：ReadBase 之后连续若干明文字符串，
        // 其中有"CJK 中文"的那条就是房间名（编号是纯数字、其余是加密数值的密文）。
        // 面积不取数据里的数值，直接由轮廓多边形算（mm² → m²），与天正显示一致。
        {
            Detail::CipherReader f(entity);
            if (f.ReadBase())
            {
                s.Scale = f.Scale > 0 ? f.Scale : 1.0;
                for (int i = 0; i < 64 && f.Ok(); ++i)
                {
                    const std::string t = f.Text();
                    if (t.empty()) break;
                    // 中文明文：UTF-8 三字节码点落在 CJK 统一表意文字区（U+4E00 起）。
                    // 加密数值串是 U+03xx 组合音符（两字节），编号是纯 ASCII，都不会命中。
                    bool hasCjk = false;
                    for (std::size_t k = 0; k + 2 < t.size();)
                    {
                        const unsigned char c = static_cast<unsigned char>(t[k]);
                        const int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                        if (len == 3 && (c == 0xE4 || (c >= 0xE5 && c <= 0xE9))) hasCjk = true;
                        k += static_cast<std::size_t>(len);
                    }
                    if (hasCjk && s.Name.empty()) s.Name = t;
                }
            }
        }
        space = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildSpace(const TchSpace& s)
    {
        TchGraphics g;
        TchGraphics::Path p;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300, twiceArea = 0;
        for (std::size_t i = 0; i < s.Outline.size(); ++i)
        {
            const auto& a = s.Outline[i];
            const auto& b = s.Outline[(i + 1) % s.Outline.size()];
            p.Points.push_back({a.X, a.Y});
            minX = std::min(minX, a.X); maxX = std::max(maxX, a.X);
            minY = std::min(minY, a.Y); maxY = std::max(maxY, a.Y);
            twiceArea += a.X * b.Y - b.X * a.Y;
        }
        if (!p.Points.empty()) p.Points.push_back(p.Points.front());
        g.Paths.push_back(std::move(p));

        // 房间名与面积：天正把它们画在房间轮廓中间，两行居中。
        // 面积由轮廓自己算（图纸单位 mm，1 m² = 1e6 mm²），与天正显示一致；
        // 字高按"3.5mm 标准字高 × 出图比例"，并限制在轮廓短边的 1/6 以内，小房间不溢出。
        const double area = std::abs(twiceArea) * 0.5 / 1e6;
        if (!(area > 0) || minX >= maxX || minY >= maxY) return g;
        const double shortSide = std::min(maxX - minX, maxY - minY);
        double h = 3.5 * (s.Scale > 0 ? s.Scale : 1.0);
        if (h <= 0 || h > shortSide / 6.0) h = shortSide / 6.0;
        if (h <= 0) return g;
        const double cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;

        if (!s.Name.empty())
            g.Texts.push_back({{cx, cy + h * 0.6}, h, 0.0, s.Name, 5, 0, 256});
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.2f", area);
        g.Texts.push_back({{cx, cy - (s.Name.empty() ? 0.0 : h * 0.6)}, h, 0.0,
                           std::string(buf) + "m\xC2\xB2", 5, 0, 256});
        return g;
    }
}
