// 天正双跑楼梯（TCH_RECTSTAIR）：私有布局解码与显示
// 合成对象的数值取自真实样例与用 MiniCADTchPatch 构造的变体，期望的图元取自天正 2014 的分解结果（docs/Tch-compatibility.md）：
// 线段在楼梯局部坐标中合并共线重叠部分后逐条比对，箭头与文字按分解出的多段线、TEXT 插入点比对
#include "Import/TchStair.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniDWG;
using MiniCAD::Tch::TchGraphics;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    bool Near(double a, double b, double tol = 0.15) { return std::abs(a - b) <= tol; }

    struct StairSpec
    {
        const char* Name = "";
        double X = 0, Y = 0, Rotation = 0, Scale = 100;
        double StepHeight = 150, TreadWidth = 270, FlightWidth = 1230, LandingWidth = 1200;
        int Steps1 = 10, Steps2 = 10;
        double TotalWidth = 2560;
        int Floor = 1, Mirror = 0;
        double RailWidth = 60, RailOffset = 0;
        std::uint32_t Flags = 394757;
        double ExtTop = 60, ExtBottom = 60, CutHeight = 675, CutAngle = 0.52359877559829882, TextHeight = 3.5;
        int Version = 4;
    };

    // TDbRectStair 版本 4；样例 36b（1716 位）插入点 (54375.96, 23304.78)
    std::unique_ptr<UnknownEntity> StairObject(const StairSpec& s)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_RECTSTAIR";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(s.Scale); r.WriteBitDouble(0); r.WriteByte(1); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(2); r.WriteByte(static_cast<std::uint8_t>(s.Version));
        r.WriteBitDouble(s.X); r.WriteBitDouble(s.Y); r.WriteBitDouble(0);
        r.WriteBitDouble(s.Rotation); r.WriteBitDouble(s.StepHeight); r.WriteBitDouble(s.TreadWidth);
        r.WriteBitDouble(s.FlightWidth); r.WriteBitDouble(s.LandingWidth);
        r.WriteByte(static_cast<std::uint8_t>(s.Steps1)); r.WriteByte(static_cast<std::uint8_t>(s.Steps2));
        r.WriteBitDouble(s.TotalWidth); r.WriteBitDouble(900);
        r.WriteByte(static_cast<std::uint8_t>(s.Floor)); r.WriteByte(static_cast<std::uint8_t>(s.Mirror)); r.WriteByte(2);
        r.WriteBitDouble(s.RailWidth); r.WriteBitDouble(s.RailOffset);
        r.WriteBitLong(static_cast<std::int32_t>(s.Flags));
        r.WriteBitDouble(200); r.WriteBitDouble(120); r.WriteBitDouble(120);
        r.WriteBitDouble(477.73865802687783); r.WriteBitDouble(872.2613419731222); r.WriteByte(0);
        r.WriteBitDouble(s.ExtTop); r.WriteBitDouble(s.ExtBottom); r.WriteBitDouble(s.CutHeight); r.WriteBitDouble(s.CutAngle);
        r.WriteBitDouble(s.TextHeight);
        for (int i = 0; i < 4; ++i) r.WriteBitDouble(1200);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        raw.TextBits = 0;
        r.WriteHandle(DwgRef::SoftPointer, 0);
        r.WriteHandle(DwgRef::HardPointer, 0x369);
        r.WriteHandle(DwgRef::HardPointer, 0);
        r.WriteHandle(DwgRef::HardPointer, 0);
        r.WriteHandle(DwgRef::HardPointer, 0x36a);
        r.WriteHandle(DwgRef::HardPointer, 0x366);
        r.WriteHandle(DwgRef::HardPointer, 0x294);
        r.WriteHandle(DwgRef::HardPointer, 0x367);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    struct Segment { double X1, Y1, X2, Y2; };
    struct Pt { double X, Y; };
    struct TextRef { double X, Y, Height; };
    struct Case
    {
        StairSpec Spec;
        std::vector<Segment> Lines;                 // 局部坐标，已合并
        std::vector<std::vector<Pt>> Arrows;        // 局部坐标，含箭尖
        std::vector<TextRef> Texts;                 // 世界坐标：TEXT 插入点（左下）与字高
    };

    // 共线重叠的线段合并为一条（方向、偏移相同，区间相接）
    struct Merged { double Ux, Uy, Off, T1, T2; };
    std::vector<Merged> Merge(const std::vector<Segment>& segs)
    {
        std::vector<Merged> all;
        for (const auto& s : segs)
        {
            double dx = s.X2 - s.X1, dy = s.Y2 - s.Y1;
            const double len = std::hypot(dx, dy);
            if (len < 1e-3) continue;
            double ux = dx / len, uy = dy / len;
            if (ux < -1e-4 || (std::abs(ux) <= 1e-4 && uy < 0)) { ux = -ux; uy = -uy; }
            const double t1 = ux * s.X1 + uy * s.Y1, t2 = ux * s.X2 + uy * s.Y2;
            all.push_back({ux, uy, -uy * s.X1 + ux * s.Y1, std::min(t1, t2), std::max(t1, t2)});
        }
        std::sort(all.begin(), all.end(), [](const Merged& a, const Merged& b) { return a.T1 < b.T1; });
        std::vector<Merged> out;
        std::vector<bool> used(all.size());
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            if (used[i]) continue;
            Merged cur = all[i];
            for (std::size_t j = i + 1; j < all.size(); ++j)
            {
                const Merged& m = all[j];
                if (used[j] || std::abs(m.Ux - cur.Ux) > 1e-4 || std::abs(m.Uy - cur.Uy) > 1e-4 || std::abs(m.Off - cur.Off) > 0.05) continue;
                if (m.T1 > cur.T2 + 0.02) continue;
                cur.T2 = std::max(cur.T2, m.T2);
                used[j] = true;
            }
            out.push_back(cur);
        }
        return out;
    }

    int Unmatched(const std::vector<Merged>& a, const std::vector<Merged>& b)
    {
        int missing = 0;
        for (const auto& x : a)
        {
            bool found = false;
            for (const auto& y : b)
                found = found || (std::abs(x.Ux - y.Ux) < 1e-3 && std::abs(x.Uy - y.Uy) < 1e-3 && Near(x.Off, y.Off, 0.2) && Near(x.T1, y.T1) && Near(x.T2, y.T2));
            missing += !found;
        }
        return missing;
    }

    bool CompareCase(const Case& c, const TchGraphics& g, std::string& detail)
    {
        const auto& s = c.Spec;
        const double cr = std::cos(s.Rotation), sr = std::sin(s.Rotation);
        auto local = [&](const TchGraphics::Point& p) {
            const double x = p.X - s.X, y = p.Y - s.Y;
            return Pt{x * cr + y * sr, -x * sr + y * cr};
        };
        std::vector<Segment> mine;
        for (const auto& l : g.Lines)
        {
            const Pt a = local(l.A), b = local(l.B);
            mine.push_back({a.X, a.Y, b.X, b.Y});
        }
        const auto ref = Merge(c.Lines), got = Merge(mine);
        const int missing = Unmatched(ref, got), extra = Unmatched(got, ref);
        bool ok = missing == 0 && extra == 0;
        if (!ok) detail += " lines missing " + std::to_string(missing) + " extra " + std::to_string(extra);

        // 箭头：杆为多段线（不含箭尖），箭头为三角形（底边中点为倒数第二点，半宽 0.4 × 比例）
        bool arrows = g.Paths.size() == c.Arrows.size() && g.Arrows.size() == c.Arrows.size();
        for (std::size_t i = 0; arrows && i < c.Arrows.size(); ++i)
        {
            const auto& r = c.Arrows[i];
            const auto& path = g.Paths[i];
            const auto& tri = g.Arrows[i];
            arrows = path.Points.size() + 1 == r.size() && path.Layer == 1 && tri.Layer == 1;
            for (std::size_t k = 0; arrows && k < path.Points.size(); ++k)
            {
                const Pt p = local(path.Points[k].P);
                arrows = Near(p.X, r[k].X) && Near(p.Y, r[k].Y);
            }
            const Pt tip = local(tri.C), a = local(tri.A), b = local(tri.B);
            arrows = arrows && Near(tip.X, r.back().X) && Near(tip.Y, r.back().Y) && Near((a.X + b.X) / 2, r[r.size() - 2].X) &&
                     Near((a.Y + b.Y) / 2, r[r.size() - 2].Y) && Near(std::hypot(a.X - b.X, a.Y - b.Y), 0.8 * s.Scale);
        }
        if (!arrows) { ok = false; detail += " arrows"; }

        // 文字：左中对齐，Center = 插入点 + (0, 字高 / 2)
        bool texts = g.Texts.size() == c.Texts.size();
        for (std::size_t i = 0; texts && i < c.Texts.size(); ++i)
        {
            const auto& t = g.Texts[i];
            texts = t.Attachment == 4 && t.Layer == 1 && t.Color == 7 && Near(t.Height, c.Texts[i].Height, 0.01) &&
                    Near(t.Center.X, c.Texts[i].X, 0.06) && Near(t.Center.Y - t.Height / 2, c.Texts[i].Y, 0.06);
        }
        if (!texts) { ok = false; detail += " texts"; }
        return ok;
    }

    DxfClass Class(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
    }

    template <class Decode, class T>
    int AcceptedTruncations(const UnknownEntity& full, Decode decode, T& out)
    {
        int accepted = 0;
        for (std::uint64_t bits = 0; bits < full.Raw.Dwg.MainBits; bits += 7)
        {
            UnknownEntity cut;
            cut.Raw = full.Raw;
            cut.Raw.Dwg.MainBits = bits;
            accepted += decode(cut, out, nullptr);
        }
        return accepted;
    }

    // 天正 2014 分解结果（分解日志按局部坐标整理）。首层扶手与 L 形平台的折线以显示截图为准：
    // 分解出的首层扶手是完整 U 形、平台折线穿过扶手，显示中扶手只有第一跑一侧且被折断线截断、折线在扶手处断开
    const std::vector<Case>& Cases()
    {
        static const std::vector<Case> cases{
        {{"样例（插入点移到原点）", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 1, 0, 60, 0, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -1200}, {1170, -3690, 1170, -1140}, {0, -3630, 0, -2827.8054}, {0, -2712.3354, 0, 0}, {504.4145, -2337.5672, 550.5959, -2509.9187}, {662.1214, -2330.0594, 710.4594, -2510.4594}, {504.4145, -2337.5672, 710.4594, -2510.4594}, {0, -2827.8054, 550.5959, -2509.9187}, {688.9163, -2430.0594, 1230, -2117.6646}, {0, -2712.3354, 523.801, -2409.9187}, {662.1214, -2330.0594, 1230, -2002.1946}, {1170, -3690, 1390, -3690}, {0, -3630, 1170, -3630}, {1230, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {13.5194, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 281.1731, -2550}, {481.1731, -2550, 1170, -2550}, {1390, -2550, 2560, -2550}, {0, -2280, 748.8269, -2280}, {948.8269, -2280, 1170, -2280}, {1390, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2560, -2010}, {0, -1740, 1170, -1740}, {1390, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {1390, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1170, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{615, -3765}, {615, -2850}, {615, -2550}}, {{1945, -3765}, {1945, -600}, {615, -600}, {615, -1980}, {615, -2280}}},
         {{510.7447, -4220, 337.5887}, {1850.0532, -4220, 337.5887}}},
        {{"首层", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 0, 0, 60, 0, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{1230, -3660, 1230, -2059.9296}, {1170, -3660, 1170, -2094.5706}, {0, -3630, 0, -2770.0704}, {504.4145, -2337.5672, 537.1985, -2459.9187}, {675.5188, -2380.0594, 710.4594, -2510.4594}, {504.4145, -2337.5672, 710.4594, -2510.4594}, {0, -2770.0704, 537.1985, -2459.9187}, {675.5188, -2380.0594, 1230, -2059.9296}, {1170, -3660, 1230, -3660}, {0, -3630, 1170, -3630}, {0, -3360, 1170, -3360}, {0, -3090, 1170, -3090}, {0, -2820, 1170, -2820}, {381.1731, -2550, 1170, -2550}, {848.8269, -2280, 1170, -2280}},
         {{{615, -3765}, {615, -2850}, {615, -2550}}},
         {{510.7447, -4220, 337.5887}}},
        {{"顶层", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 2, 0, 60, 0, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -1200}, {1170, -3630, 1170, -1140}, {0, -3690, 0, 0}, {0, -3690, 1390, -3690}, {0, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {0, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 1170, -2550}, {1390, -2550, 2560, -2550}, {0, -2280, 1170, -2280}, {1390, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2560, -2010}, {0, -1740, 1170, -1740}, {1390, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {1390, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1170, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{1945, -3765}, {1945, -600}, {615, -600}, {615, -3195}, {615, -3495}}},
         {{1850.0532, -4220, 337.5887}}},
        {{"翻转", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 1, 1, 60, 0, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, -2117.6646}, {2560, -2002.1946, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -1200}, {1170, -3690, 1170, -1140}, {0, -3630, 0, 0}, {1834.4145, -2337.5672, 1880.5959, -2509.9187}, {1992.1214, -2330.0594, 2040.4594, -2510.4594}, {1834.4145, -2337.5672, 2040.4594, -2510.4594}, {1330, -2827.8054, 1880.5959, -2509.9187}, {2018.9163, -2430.0594, 2560, -2117.6646}, {1330, -2712.3354, 1853.801, -2409.9187}, {1992.1214, -2330.0594, 2560, -2002.1946}, {1170, -3690, 1390, -3690}, {0, -3630, 1170, -3630}, {1230, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {0, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 1170, -2550}, {1390, -2550, 1611.1731, -2550}, {1811.1731, -2550, 2560, -2550}, {0, -2280, 1170, -2280}, {1390, -2280, 2078.8269, -2280}, {2278.8269, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2546.4806, -2010}, {0, -1740, 1170, -1740}, {1390, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {1390, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1170, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{1945, -3765}, {1945, -2850}, {1945, -2550}}, {{615, -3765}, {615, -600}, {1945, -600}, {1945, -1980}, {1945, -2280}}},
         {{1840.7447, -4220, 337.5887}, {520.0532, -4220, 337.5887}}},
        {{"第二跑较短：L 形平台", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 8, 2560, 1, 0, 60, 0, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -1200}, {1170, -3690, 1170, -1140}, {0, -3630, 0, -2827.8054}, {0, -2712.3354, 0, 0}, {504.4145, -2337.5672, 550.5959, -2509.9187}, {662.1214, -2330.0594, 710.4594, -2510.4594}, {504.4145, -2337.5672, 710.4594, -2510.4594}, {0, -2827.8054, 550.5959, -2509.9187}, {688.9163, -2430.0594, 1230, -2117.6646}, {0, -2712.3354, 523.801, -2409.9187}, {662.1214, -2330.0594, 1230, -2002.1946}, {1170, -3690, 1390, -3690}, {0, -3630, 1170, -3630}, {1230, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {13.5194, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 281.1731, -2550}, {481.1731, -2550, 1170, -2550}, {1390, -2550, 2560, -2550}, {0, -2280, 748.8269, -2280}, {948.8269, -2280, 1170, -2280}, {1390, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2560, -2010}, {0, -1740, 1170, -1740}, {1330, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {0, -1200, 1330, -1200}, {1170, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{615, -3765}, {615, -2850}, {615, -2550}}, {{1945, -3765}, {1945, -600}, {615, -600}, {615, -1980}, {615, -2280}}},
         {{510.7447, -4220, 337.5887}, {1850.0532, -4220, 337.5887}}},
        {{"扶手距边 50", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 1, 0, 60, 50, 394757u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1440, -3690, 1440, -1140}, {1380, -3630, 1380, -1200}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -2117.6646}, {1230, -2002.1946, 1230, -1200}, {1180, -3630, 1180, -1200}, {1120, -3690, 1120, -1140}, {0, -3630, 0, -2827.8054}, {0, -2712.3354, 0, 0}, {504.4145, -2337.5672, 550.5959, -2509.9187}, {662.1214, -2330.0594, 710.4594, -2510.4594}, {504.4145, -2337.5672, 710.4594, -2510.4594}, {0, -2827.8054, 550.5959, -2509.9187}, {688.9163, -2430.0594, 1230, -2117.6646}, {0, -2712.3354, 523.801, -2409.9187}, {662.1214, -2330.0594, 1230, -2002.1946}, {1120, -3690, 1440, -3690}, {0, -3630, 1120, -3630}, {1180, -3630, 1380, -3630}, {1440, -3630, 2560, -3630}, {0, -3360, 1120, -3360}, {1180, -3360, 1230, -3360}, {1330, -3360, 1380, -3360}, {1440, -3360, 2560, -3360}, {0, -3090, 1120, -3090}, {1180, -3090, 1230, -3090}, {1330, -3090, 1380, -3090}, {1440, -3090, 2560, -3090}, {13.5194, -2820, 1120, -2820}, {1180, -2820, 1230, -2820}, {1330, -2820, 1380, -2820}, {1440, -2820, 2560, -2820}, {0, -2550, 281.1731, -2550}, {481.1731, -2550, 1120, -2550}, {1180, -2550, 1230, -2550}, {1330, -2550, 1380, -2550}, {1440, -2550, 2560, -2550}, {0, -2280, 748.8269, -2280}, {948.8269, -2280, 1120, -2280}, {1180, -2280, 1230, -2280}, {1330, -2280, 1380, -2280}, {1440, -2280, 2560, -2280}, {0, -2010, 1120, -2010}, {1180, -2010, 1216.4806, -2010}, {1330, -2010, 1380, -2010}, {1440, -2010, 2560, -2010}, {0, -1740, 1120, -1740}, {1180, -1740, 1230, -1740}, {1330, -1740, 1380, -1740}, {1440, -1740, 2560, -1740}, {0, -1470, 1120, -1470}, {1180, -1470, 1230, -1470}, {1330, -1470, 1380, -1470}, {1440, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1120, -1140, 1440, -1140}, {0, 0, 2560, 0}},
         {{{615, -3765}, {615, -2850}, {615, -2550}}, {{1945, -3765}, {1945, -600}, {615, -600}, {615, -1980}, {615, -2280}}},
         {{510.7447, -4220, 337.5887}, {1850.0532, -4220, 337.5887}}},
        {{"踏步穿过剖断中心", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 1, 0, 60, 0, 394757u, 60, 60, 600, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1200}, {1230, -3630, 1230, -1200}, {1170, -3690, 1170, -1140}, {0, -3630, 0, -2962.8054}, {0, -2847.3354, 0, 0}, {504.4145, -2472.5672, 550.5959, -2644.9187}, {662.1214, -2465.0594, 710.4594, -2645.4594}, {504.4145, -2472.5672, 710.4594, -2645.4594}, {0, -2962.8054, 550.5959, -2644.9187}, {688.9163, -2565.0594, 1230, -2252.6646}, {0, -2847.3354, 523.801, -2544.9187}, {662.1214, -2465.0594, 1230, -2137.1946}, {1170, -3690, 1390, -3690}, {0, -3630, 1170, -3630}, {1230, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {0, -2820, 47.3463, -2820}, {247.3463, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 515, -2550}, {715, -2550, 1170, -2550}, {1390, -2550, 2560, -2550}, {0, -2280, 982.6537, -2280}, {1390, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2560, -2010}, {0, -1740, 1170, -1740}, {1390, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {1390, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1170, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{615, -3765}, {615, -2985}, {615, -2685}}, {{1945, -3765}, {1945, -600}, {615, -600}, {615, -2115}, {615, -2415}}},
         {{510.7447, -4220, 337.5887}, {1850.0532, -4220, 337.5887}}},
        {{"顶层、扶手不连通", 0, 0, 0, 100, 150, 270, 1230, 1200, 10, 10, 2560, 2, 0, 60, 0, 263685u, 60, 60, 675, 0.52359877559829882, 3.5},
         {{2560, -3630, 2560, 0}, {1390, -3690, 1390, -1140}, {1330, -3630, 1330, -1140}, {1230, -3630, 1230, -1140}, {1170, -3630, 1170, -1140}, {0, -3690, 0, 0}, {0, -3690, 1390, -3690}, {0, -3630, 1330, -3630}, {1390, -3630, 2560, -3630}, {0, -3360, 1170, -3360}, {1390, -3360, 2560, -3360}, {0, -3090, 1170, -3090}, {1390, -3090, 2560, -3090}, {0, -2820, 1170, -2820}, {1390, -2820, 2560, -2820}, {0, -2550, 1170, -2550}, {1390, -2550, 2560, -2550}, {0, -2280, 1170, -2280}, {1390, -2280, 2560, -2280}, {0, -2010, 1170, -2010}, {1390, -2010, 2560, -2010}, {0, -1740, 1170, -1740}, {1390, -1740, 2560, -1740}, {0, -1470, 1170, -1470}, {1390, -1470, 2560, -1470}, {0, -1200, 2560, -1200}, {1170, -1140, 1230, -1140}, {1330, -1140, 1390, -1140}, {0, 0, 2560, 0}},
         {{{1945, -3765}, {1945, -600}, {615, -600}, {615, -3195}, {615, -3495}}},
         {{1850.0532, -4220, 337.5887}}},
        {{"首层、翻转、坡度受限", 1572.6829273601998, -1495.9248784249712, 0, 100, 175, 270, 1100, 1500, 12, 13, 2460, 0, 1, 50, 0, 393733u, 60, 100, 500, 0.90000000000000002, 5},
         {{2460, -4740, 2460, -3332.1429}, {1410, -4815, 1410, -4547.1429}, {1360, -4815, 1360, -4605}, {1780.1221, -3931.7377, 1851.2586, -4036.5436}, {1955.6927, -3915.6985, 2031.5087, -4027.3987}, {1360, -4605, 1851.2586, -4036.5436}, {1955.6927, -3915.6985, 2460, -3332.1429}, {1780.1221, -3931.7377, 2031.5087, -4027.3987}, {1360, -4815, 1410, -4815}, {1410, -4740, 2460, -4740}, {1476.6667, -4470, 2460, -4470}, {1710, -4200, 2460, -4200}, {1965.3998, -3930, 2460, -3930}, {2176.6667, -3660, 2460, -3660}, {2410, -3390, 2460, -3390}},
         {},
         {}},
        {{"顶层、旋转、扶手不连通", -219.67255405999731, -3214.7828166242643, 0.29999999999999999, 50, 150, 270, 1230, 1500, 13, 9, 2720, 2, 0, 90, 0, 517u, 40, 100, 500, 0.59999999999999998, 3.5},
         {{2720, -4740, 2720, -2580}, {1580, -4840, 1580, -1460}, {1490, -4750, 1490, -1460}, {1230, -4740, 1230, -1459.9999}, {1140, -4740, 1139.9999, -1460}, {-0, -4840, -0, -4750}, {-0, -4740, 0, -1500.0001}, {-0, -4840, 1580, -4840}, {-0, -4750, 1490, -4750}, {-0, -4740, 1490.0013, -4739.9999}, {1580, -4740, 2720, -4740}, {0, -4470, 1140, -4470}, {1580, -4470, 2720, -4470}, {-0, -4200, 1140, -4200}, {1580, -4200, 2720, -4200}, {0, -3930, 1140, -3930}, {1580, -3930, 2720, -3930}, {-0, -3660, 1140, -3660}, {1580.0001, -3660, 2720, -3660}, {-0, -3390, 1140, -3390}, {1580, -3390, 2720, -3390}, {-0, -3120, 1140, -3120}, {1580, -3120, 2720, -3120}, {-0, -2850, 1140, -2850}, {1580, -2850, 2720, -2850}, {0, -2580, 1140, -2580}, {1580, -2580, 2720, -2580}, {-0, -2310, 1140, -2310}, {0, -2040.0001, 1140, -2040}, {-0, -1770, 1140, -1770}, {0, -1500.0001, 1140, -1500}, {1139.9999, -1460, 1230, -1460}, {1490, -1460, 1580, -1460}},
         {},
         {}},
        {{"中间层、翻转、旋转、扶手距边", 4979.0371868658895, -4803.6929062969821, 1, 50, 150, 300, 1400, 1000, 9, 13, 2800, 1, 1, 60, 40, 132613u, 40, 30, 675, 0.90000000000000002, 5},
         {{2800, -4600, 2800, -2448.3255}, {2799.9999, -2287.453, 2800, -2200}, {1500, -4630, 1500, -960}, {1440, -4570, 1440, -1020}, {1400, -4570, 1400, -1020}, {1360, -4570, 1360, -1020}, {1300, -4630, 1300, -960}, {0, -4600, 0, -1000}, {1968.6937, -3218.6365, 2074.9945, -3361.9472}, {2112.6004, -3153.6851, 2223.8652, -3303.688}, {1400, -4212.547, 2074.9945, -3361.9472}, {2174.2772, -3236.8352, 2800, -2448.3255}, {1400, -4051.6745, 2013.3178, -3278.797}, {2112.6004, -3153.6851, 2799.9999, -2287.453}, {1968.6937, -3218.6365, 2223.8652, -3303.688}, {1300, -4630, 1500, -4630}, {0, -4600, 1300, -4600}, {1500, -4600, 2800, -4600}, {1360, -4570, 1440, -4570}, {0, -4300, 1300, -4300}, {1360, -4300, 1440.0148, -4299.9998}, {1500, -4300, 2800, -4300}, {0, -4000, 1300, -4000}, {1360, -4000, 1439.9862, -4000.0001}, {1568.667, -4000, 2800, -4000}, {0, -3700, 1299.9999, -3700}, {1360, -3700, 1439.995, -3700.0001}, {1500, -3700, 1679.0717, -3700}, {1806.7323, -3700, 2800, -3700}, {0, -3399.9999, 1300, -3400}, {1360, -3400, 1439.9955, -3400.0001}, {1500, -3400, 1917.137, -3400}, {2044.7976, -3400, 2800, -3400}, {-0.0001, -3100, 1300, -3100}, {1360, -3100, 1439.9959, -3100.0001}, {1500, -3100, 2155.2023, -3100}, {2282.863, -3100, 2800, -3100}, {-0.0001, -2800, 1300, -2800}, {1360, -2800, 1439.9963, -2800.0001}, {1500, -2800, 2393.2677, -2800}, {2520.9283, -2800, 2800, -2800}, {-0.0001, -2500, 1300, -2500}, {1360, -2500, 1440, -2500.0001}, {1500, -2500, 2631.333, -2500.0001}, {2758.9937, -2500, 2800, -2500}, {0, -2200.0001, 1300, -2200}, {1360, -2200, 1440, -2200}, {1500, -2200, 2800, -2200}, {0, -1900, 1300, -1900}, {1360, -1900, 1400, -1900}, {0, -1600, 1300, -1600}, {1360, -1600, 1400, -1600}, {0, -1300, 1300, -1300}, {1360, -1300, 1400, -1300}, {1360, -1020, 1440, -1020}, {0, -1000, 1300, -1000}, {1300, -960, 1500, -960}},
         {{{2100, -4750}, {2100, -3550}, {2100, -3400}}, {{700, -4750}, {700, -500}, {2100, -500}, {2100, -2949.9999}, {2100, -3100}}},
         {{10173.7695, -5768.5625, 241.1348}, {9417.3463, -6946.6218, 241.1348}}},
        {{"首层、翻转、旋转", 954.36505840669633, -1038.4002354176514, 0.20000000000000001, 100, 175, 300, 1100, 1000, 13, 13, 2300, 0, 1, 90, 0, 132613u, 40, 30, 900, 0.90000000000000002, 3.5},
         {{2300, -4600, 2300, -2364.0558}, {1290, -4585, 1290, -3636.8156}, {1200, -4600, 1200, -3750.2299}, {1618.6937, -3025.7794, 1694.1561, -3127.515}, {1793.4388, -3002.4031, 1873.8652, -3110.8309}, {1200, -3750.2299, 1694.1561, -3127.515}, {1793.4388, -3002.4031, 2300, -2364.0558}, {1618.6937, -3025.7794, 1873.8652, -3110.8309}, {1200, -4600, 2300, -4600}, {1200, -4585, 1290, -4585}, {1290, -4300, 2300, -4300}, {1290, -4000, 2300, -4000}, {1290, -3700, 2300, -3700}, {1477.9253, -3400, 2300, -3400}, {1673.7469, -3100, 1841.3704, -3100}, {1865.8314, -3100, 2300, -3100}, {1954.056, -2800, 2300, -2800}, {2192.1214, -2500, 2300, -2500}},
         {{{1750, -4750}, {1750, -3507.1429}, {1750, -3207.1428}}},
         {{3529.7659, -5798.9521, 337.5887}}},
        };
        return cases;
    }
}

int main()
{
    using namespace MiniCAD::Tch;
    std::string why;

    // ── 样例：逐位重现与字段 ──
    {
        StairSpec spec;
        spec.X = 54375.96024345199; spec.Y = 23304.781327441466;
        auto sample = StairObject(spec);
        Check(sample->Raw.Dwg.MainBits == 1716, "synthetic stair reproduces the 1716-bit real layout");
        TchRectStair s;
        Check(DecodeRectStair(*sample, s, &why) && s.Steps1 == 10 && s.Steps2 == 10 && s.Floor == 1 && !s.Mirror && Near(s.FlightWidth, 1230) &&
                  Near(s.TotalWidth, 2560) && Near(s.CutHeight, 675) && s.ShowArrows && s.LinkedRails && s.ShowLanding &&
                  s.TextStyle == 0x294 && s.ArrowLayer == 0x367 && Near(s.X, 54375.96, 0.01),
              ("sample stair decodes dimensions, options, text style and arrow layer " + why).c_str());
        const TchGraphics g = BuildRectStair(s);
        // 样例原位：天正 2014 分解出的平台外框与“上”字插入点
        bool landing = false;
        for (const auto& l : g.Lines)
            landing = landing || (Near(l.A.X, 54376.0, 0.1) && Near(l.A.Y, 23304.8, 0.1) && Near(l.B.X, 56936.0, 0.1) && Near(l.B.Y, 23304.8, 0.1)) ||
                      (Near(l.B.X, 54376.0, 0.1) && Near(l.B.Y, 23304.8, 0.1) && Near(l.A.X, 56936.0, 0.1) && Near(l.A.Y, 23304.8, 0.1));
        Check(landing && g.Texts.size() == 2 && Near(g.Texts[0].Center.X, 54886.7, 0.1) && Near(g.Texts[0].Center.Y - g.Texts[0].Height / 2, 19084.8, 0.1),
              "sample stair keeps its landing edge and arrow text at the exploded positions");
        Check(AcceptedTruncations(*sample, [](auto& e, auto& o, std::string* w) { return DecodeRectStair(e, o, w); }, s) == 0,
              "every truncated stair payload is rejected");
    }

    // ── 变体：与天正 2014 分解结果逐条比对 ──
    for (const auto& c : Cases())
    {
        TchRectStair s;
        std::string label = std::string("stair ") + c.Spec.Name;
        if (!DecodeRectStair(*StairObject(c.Spec), s, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
        std::string detail;
        const bool ok = CompareCase(c, BuildRectStair(s), detail);
        Check(ok, (label + detail).c_str());
    }

    // ── 未实现的选项与超出范围的剖切位置不显示 ──
    {
        TchRectStair s;
        for (int bit : {1, 3, 13, 20, 21})
        {
            StairSpec spec;
            spec.Flags |= 1u << bit;
            Check(!DecodeRectStair(*StairObject(spec), s), ("stair option bit " + std::to_string(bit) + " (changes the drawing) is not guessed").c_str());
        }
        StairSpec floor3; floor3.Floor = 3;
        Check(!DecodeRectStair(*StairObject(floor3), s), "unverified floor type 3 is not drawn");
        StairSpec version5; version5.Version = 5;
        Check(!DecodeRectStair(*StairObject(version5), s), "stair version 5 is not guessed");
        // 剖断中心高于第一跑顶 − T/2：天正会重算剖切位置与角度（6 跑、剖切高 900、踏步高 175、宽 300）
        StairSpec high; high.Steps1 = 6; high.Steps2 = 7; high.TreadWidth = 300; high.StepHeight = 175; high.CutHeight = 900;
        Check(!DecodeRectStair(*StairObject(high), s), "a cut above the first flight is not drawn");
        high.Floor = 2;
        Check(DecodeRectStair(*StairObject(high), s), "top floor ignores the cut position");
    }

    // ── 端到端：DWG → Scene，箭头与文字在 DIM_SYMB 图层 ──
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027);
    db.CreateDefaults();
    db.Classes.push_back(Class("TCH_RECTSTAIR", "TDbRectStair", 500));
    auto symbolLayer = std::make_unique<MiniDWG::Layer>();
    symbolLayer->Name = "DIM_SYMB";
    symbolLayer->Color = Color(std::int16_t(3));
    const Handle symbol = db.AddTableEntry(db.Layers(), std::move(symbolLayer))->ObjectHandle;
    auto stair = StairObject({});
    {
        // 文字样式与箭头图层换成测试库里的句柄
        auto& raw = stair->Raw.Dwg;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteHandle(DwgRef::SoftPointer, 0);
        for (Handle h : {Handle(0), Handle(0), Handle(0), Handle(0), Handle(0)}) r.WriteHandle(DwgRef::HardPointer, h);
        r.WriteHandle(DwgRef::HardPointer, db.FindTableEntry(db.TextStyles(), "Standard")->ObjectHandle);
        r.WriteHandle(DwgRef::HardPointer, symbol);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
    }
    db.AddEntity(db.ModelSpace(), std::move(stair));
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    Check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 1, "stair appears in the scene");
    int lines = 0, onSymbol = 0, symbolItems = 0;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
        if (!insert || !insert->GetBlock()) return;
        for (const auto& e : insert->GetBlock()->GetEntities())
        {
            const auto* layer = scene.GetLayerManager().GetLayer(e->GetAttr().LayerId);
            const bool symbolic = dynamic_cast<const MiniCAD::SolidEntity*>(e.get()) || dynamic_cast<const MiniCAD::PolylineEntity*>(e.get()) ||
                                  dynamic_cast<const MiniCAD::MTextEntity*>(e.get());
            lines += dynamic_cast<const MiniCAD::LineEntity*>(e.get()) != nullptr;
            symbolItems += symbolic;
            onSymbol += symbolic && layer && layer->GetName() == "DIM_SYMB";
        }
    });
    Check(lines > 40 && symbolItems == 6 && onSymbol == 6, "stair lines stay on its layer, arrows and texts move to DIM_SYMB");
    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf})
    {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        Check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 1, "displayed stair survives plain export and reimport");
    }
    return g_failed;
}
