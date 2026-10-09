#pragma once
#include "Import/TchDimension.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace MiniCAD::Tch
{
    // TCH_RECTSTAIR 双跑楼梯（CADReader TDbRectStair 读取函数 0x6e8170，版本 < 7 为明文分支）。
    // 局部坐标：插入点为休息平台靠第一跑一侧的外角，楼梯沿 -Y 方向展开；绕插入点转 Rotation
    struct TchRectStair
    {
        double X = 0, Y = 0, Rotation = 0;
        double Scale = 100;                 // 天正基类比例：箭头与文字按它缩放
        double StepHeight = 150;            // 踏步高
        double TreadWidth = 270;            // 踏步宽
        double FlightWidth = 1230;          // 梯段宽
        double LandingWidth = 1200;         // 休息平台宽（Y 向）
        double TotalWidth = 2560;           // 梯间宽（X 向）
        int    Steps1 = 10, Steps2 = 10;    // 第一跑（上楼）、第二跑的踏步数（踢面数）
        int    Floor = 1;                   // 0 首层、1 中间层、2 顶层
        bool   Mirror = false;              // 第一跑在右侧
        double RailWidth = 60;              // 扶手宽
        double RailOffset = 0;              // 扶手距梯段内侧边
        double RailExtTop = 60;             // 扶手在平台端伸出
        double RailExtBottom = 60;          // 扶手在起步端伸出
        double CutHeight = 675;             // 剖切高度：剖断中心距起步线 CutHeight / 踏步高 × 踏步宽
        double CutAngle = std::numbers::pi / 6;
        double TextHeight = 3.5;            // “上”“下”的名义字高（× Scale）
        bool   ShowArrows = true;           // 标志位 10
        bool   LinkedRails = true;          // 标志位 17：两侧扶手在梯井端连成一体
        bool   ShowLanding = true;          // 标志位 18
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
        MiniDWG::Handle ArrowLayer = MiniDWG::kNullHandle;   // 箭头与文字所在图层（样例为 DIM_SYMB）
    };

    // 按 TDbRectStair 版本 4 的明文分支解析，必须恰好读完主数据流。
    // 楼梯基类（0x6e6cb0）：天正基类、RC 版本；版本 < 3 时句柄流有一个句柄（版本 3 另有编码分支，不支持）。
    // 天正 2014 分解核对过的标志位：1、3、13、20、21 会改变图形（起步多一级、圆弧平台、梯边梁等），未实现，遇到即拒绝；
    // 层类型 3 也未实现。剖断中心超出第一跑 [T/2, 跑长 − T/2] 时天正会重算位置与角度，规律未明，同样拒绝
    inline bool DecodeRectStair(const MiniDWG::UnknownEntity& entity, TchRectStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_RECTSTAIR") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native stair");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        const int version = r.ReadByte();
        if (version != 4) return fail("unsupported native stair version");
        TchRectStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.TreadWidth = r.ReadBitDouble();
        s.FlightWidth = r.ReadBitDouble();
        s.LandingWidth = r.ReadBitDouble();
        s.Steps1 = r.ReadByte();
        s.Steps2 = r.ReadByte();
        s.TotalWidth = r.ReadBitDouble();
        r.ReadBitDouble();                                  // 扶手高
        s.Floor = r.ReadByte();
        const int mirror = r.ReadByte();
        r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailOffset = r.ReadBitDouble();
        const auto flags = static_cast<std::uint32_t>(r.ReadBitLong());
        for (int i = 0; i < 3; ++i) r.ReadBitDouble();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadByte();  // 版本 ≥ 3：不影响平面显示
        s.RailExtTop = r.ReadBitDouble();
        s.RailExtBottom = r.ReadBitDouble();
        s.CutHeight = r.ReadBitDouble();
        s.CutAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        for (int i = 0; i < 4; ++i) r.ReadBitDouble();      // 平台相关的 4 个长度，矩形平台下不影响显示
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native stair fields do not consume the payload exactly");

        constexpr std::uint32_t unsupported = (1u << 1) | (1u << 3) | (1u << 13) | (1u << 20) | (1u << 21) | 0xff000000u;
        if (flags & unsupported) return fail("unsupported stair options");
        if (s.Floor < 0 || s.Floor > 2) return fail("unsupported stair floor type");
        if (mirror != 0 && mirror != 1) return fail("unsupported stair mirror mode");
        s.Mirror = mirror == 1;
        s.ShowArrows = (flags >> 10) & 1;
        s.LinkedRails = (flags >> 17) & 1;
        s.ShowLanding = (flags >> 18) & 1;
        const double values[] = {s.X, s.Y, s.Rotation, s.Scale, s.StepHeight, s.TreadWidth, s.FlightWidth, s.LandingWidth, s.TotalWidth,
                                 s.RailWidth, s.RailOffset, s.RailExtTop, s.RailExtBottom, s.CutHeight, s.CutAngle, s.TextHeight};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite stair settings");
        if (s.Scale <= 0 || s.StepHeight <= 0 || s.TreadWidth <= 0 || s.FlightWidth <= 0 || s.LandingWidth < 0 ||
            s.TotalWidth < 2 * s.FlightWidth - 1e-9 || s.Steps1 < 2 || s.Steps2 < 2 || s.RailWidth < 0 || s.RailOffset < 0 ||
            s.RailWidth + s.RailOffset > s.FlightWidth || s.TextHeight <= 0)
            return fail("invalid stair settings");
        if (s.Floor != 2)
        {
            if (!(s.CutAngle > 0 && s.CutAngle < std::numbers::pi / 2)) return fail("unsupported stair cut angle");
            const double cut = s.CutHeight / s.StepHeight * s.TreadWidth;
            if (cut < s.TreadWidth / 2 - 1e-6 || cut > (s.Steps1 - 1.5) * s.TreadWidth + 1e-6)
                return fail("stair cut position is outside the first flight");
        }

        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount + 1 + 4 + 1; ++i) handles.ReadHandle(entity.ObjectHandle);   // 楼梯基类、扶手与栏杆图层等、文字图层
        s.TextStyle = handles.ReadHandle(entity.ObjectHandle);
        s.ArrowLayer = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits) return fail("stair handle stream is incomplete");
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        using StairPoint = TchGraphics::Point;
        using StairSegment = std::pair<StairPoint, StairPoint>;

        // 线段与折线组的交点参数（0～1）
        inline void CrossParams(const StairSegment& s, const std::vector<StairPoint>& line, std::vector<double>& ts, bool open)
        {
            const double dx = s.second.X - s.first.X, dy = s.second.Y - s.first.Y;
            for (std::size_t i = 0; i + 1 < line.size(); ++i)
            {
                const double ex = line[i + 1].X - line[i].X, ey = line[i + 1].Y - line[i].Y;
                const double den = dx * ey - dy * ex;
                if (std::abs(den) < 1e-12) continue;
                const double ox = line[i].X - s.first.X, oy = line[i].Y - s.first.Y;
                const double t = (ox * ey - oy * ex) / den, u = (ox * dy - oy * dx) / den;
                if ((open ? (t > 0 && t < 1) : (t >= 0 && t <= 1)) && u >= 0 && u <= 1) ts.push_back(t);
            }
        }

        inline StairPoint Lerp(const StairSegment& s, double t)
        {
            return {s.first.X + (s.second.X - s.first.X) * t, s.first.Y + (s.second.Y - s.first.Y) * t};
        }
    }

    // 双跑楼梯（与天正 2014 对 110 余个构造变体的分解结果核对，见 docs/Tch-compatibility.md）。
    // 线条在对象图层；箭头与文字 Layer = 1（ArrowLayer）。“上”“下”按 GBCBIG 的墨迹宽度摆放（天正按 textbox 计算）
    inline TchGraphics BuildRectStair(const TchRectStair& s)
    {
        using P = TchGraphics::Point;
        using Seg = Detail::StairSegment;
        constexpr double deg = std::numbers::pi / 180;
        TchGraphics g;
        const double T = s.TreadWidth, W = s.FlightWidth, L = s.LandingWidth, G = s.TotalWidth;
        const double rw = s.RailWidth, roff = s.RailOffset, extTop = s.RailExtTop, extBot = s.RailExtBottom;
        const int floor = s.Floor;
        const bool linked = s.LinkedRails;

        // 第一跑（上楼）与第二跑：外侧边（靠墙）、内侧边（靠梯井）、从外向内的方向
        struct Flight { double Outer, Inner, Dir; int Steps; double Top; };
        Flight f1 = s.Mirror ? Flight{G, G - W, -1, s.Steps1, 0} : Flight{0, W, 1, s.Steps1, 0};
        Flight f2 = s.Mirror ? Flight{0, W, 1, s.Steps2, 0} : Flight{G, G - W, -1, s.Steps2, 0};
        const double y0 = -L - (std::max(s.Steps1, s.Steps2) - 1) * T;    // 两跑共用的起步线
        f1.Top = y0 + (f1.Steps - 1) * T;
        f2.Top = y0 + (f2.Steps - 1) * T;
        const double xc1 = (f1.Outer + f1.Inner) / 2, xc2 = (f2.Outer + f2.Inner) / 2;

        // 剖断中心；两端超出 [y0 + T/2, 第一跑顶 − T/2] 时减小坡度
        const double yc = y0 + s.CutHeight / s.StepHeight * T;
        const double half = std::min({W / 2 * std::tan(s.CutAngle), yc - (y0 + T / 2), f1.Top - T / 2 - yc});
        const double angle = std::atan2(2 * half, W);
        const double ca = std::cos(angle), sa = std::sin(angle);
        auto local = [&](double u, double n) { return P{xc1 + u * ca - n * sa, yc + u * sa + n * ca}; };
        auto yline = [&](double n, double x) { return yc + (x - xc1) * std::tan(angle) + n / ca; };
        // 锯齿：两个尖点在以剖断中心为圆心、半径 135 的圆上（折断线方向坐标系中 115° 与 −75°），两侧斜段方向 75°
        const P pb{135 * std::cos(115 * deg), 135 * std::sin(115 * deg)};
        const P pc{135 * std::cos(-75 * deg), 135 * std::sin(-75 * deg)};
        auto along = [&](const P& p, double n) { const double t = (n - p.Y) / std::sin(75 * deg); return P{p.X + t * std::cos(75 * deg), n}; };

        const double railTop = -L + extTop;
        const double yo = y0 - extBot, yi = y0 - extBot + rw;              // 顶层护栏外、内边
        const double innerBot = floor == 2 || (floor == 1 && linked) ? std::max(y0, yi) : y0;
        auto innerTop = [&](double top) { return linked && floor != 0 ? std::min(top, -L + extTop - rw) : top; };
        auto railBottom = [&](int k) {
            if (floor == 0) return y0 - extBot + (linked ? rw / 2 : 0);
            if (floor == 2 && k == 0) return std::max(y0, yi);
            return y0 - extBot;
        };
        const double xl = std::min(f1.Inner, f2.Inner), xr = std::max(f1.Inner, f2.Inner);
        const double oL = xl - roff - rw, oR = xr + roff + rw, iL = xl - roff, iR = xr + roff;
        const double iTop = -L + extTop - rw;
        const double o1 = s.Mirror ? oR : oL, i1 = s.Mirror ? iR : iL;     // 第一跑一侧扶手的外、内缘
        const double o2 = s.Mirror ? oL : oR, i2 = s.Mirror ? iL : iR;
        const double gx1 = std::min(f1.Outer, o2), gx2 = std::max(f1.Outer, o2);

        // 踏步线在扶手覆盖处断开；扶手距边时扶手与内侧边之间的一段只在内侧边范围内画
        auto spans = [&](const Flight& f, int k, double y) {
            std::vector<std::pair<double, double>> out;
            if (railBottom(k) - 1e-9 <= y && y <= railTop + 1e-9)
            {
                out.emplace_back(f.Outer, f.Inner - f.Dir * (roff + rw));
                if (roff > 0 && innerBot - 1e-9 <= y && y <= innerTop(f.Top) + 1e-9) out.emplace_back(f.Inner - f.Dir * roff, f.Inner);
            }
            else
                out.emplace_back(f.Outer, f.Inner);
            return out;
        };

        // 梯段：踏步线、外侧边、内侧边
        std::vector<std::pair<int, Seg>> flight;
        for (int k = 0; k < 2; ++k)
        {
            if (floor == 0 && k == 1) continue;
            const Flight& f = k == 0 ? f1 : f2;
            for (int i = 0; i < f.Steps; ++i)
            {
                const double y = y0 + i * T;
                for (const auto& [a, b] : spans(f, k, y))
                {
                    if (floor == 2 && yo < y && y < yi - 1e-9 && gx1 - 1e-9 <= std::min(a, b) && std::max(a, b) <= gx2 + 1e-9)
                        continue;                                           // 顶层落在护栏内的踏步不画
                    flight.push_back({k, {{a, y}, {b, y}}});
                }
            }
            flight.push_back({k, {{f.Outer, y0}, {f.Outer, f.Top}}});
            flight.push_back({k, {{f.Inner, innerBot}, {f.Inner, innerTop(f.Top)}}});
        }

        auto line = [&](const P& a, const P& b) { g.Lines.push_back({a, b}); };
        const double lo1 = std::min(f1.Outer, f1.Inner), hi1 = std::max(f1.Outer, f1.Inner);
        if (floor == 1)
        {
            // 中间层：双折断线（相距 100），两线之间的踏步与边线剪掉：从与折断线（含锯齿）的第一个交点到最后一个交点
            const P pa = along(pb, -50), x2l = along(pb, 50), pd = along(pc, 50), x2r = along(pc, -50);
            const std::vector<std::vector<P>> breaks{
                {{lo1 - 1, yline(50, lo1 - 1)}, local(x2l.X, x2l.Y)},
                {{lo1 - 1, yline(-50, lo1 - 1)}, local(pa.X, pa.Y), local(pb.X, pb.Y), local(pc.X, pc.Y), local(pd.X, pd.Y), {hi1 + 1, yline(50, hi1 + 1)}},
                {local(x2r.X, x2r.Y), {hi1 + 1, yline(-50, hi1 + 1)}}};
            auto inBand = [&](const P& p) { return std::abs(-(p.X - xc1) * sa + (p.Y - yc) * ca) < 50; };
            for (const auto& [k, seg] : flight)
            {
                if (k != 0) { line(seg.first, seg.second); continue; }
                std::vector<double> ts;
                for (const auto& b : breaks) Detail::CrossParams(seg, b, ts, false);
                if (inBand(seg.first)) ts.push_back(0);
                if (inBand(seg.second)) ts.push_back(1);
                if (ts.empty()) { line(seg.first, seg.second); continue; }
                const auto [t1, t2] = std::minmax_element(ts.begin(), ts.end());
                if (*t1 > 1e-9) line(seg.first, Detail::Lerp(seg, *t1));
                if (*t2 < 1 - 1e-9) line(Detail::Lerp(seg, *t2), seg.second);
            }
            line({lo1, yline(50, lo1)}, local(x2l.X, x2l.Y));
            line({lo1, yline(-50, lo1)}, local(pa.X, pa.Y));
            line(local(pa.X, pa.Y), local(pb.X, pb.Y));
            line(local(pb.X, pb.Y), local(pc.X, pc.Y));
            line(local(pc.X, pc.Y), local(pd.X, pd.Y));
            line(local(pd.X, pd.Y), {hi1, yline(50, hi1)});
            line(local(x2r.X, x2r.Y), {hi1, yline(-50, hi1)});
        }
        else if (floor == 0)
        {
            // 首层：单折断线以下的部分（奇偶规则，锯齿处可能多次进出）
            const P a0 = along(pb, 0), d0 = along(pc, 0);
            const std::vector<P> brk{{hi1, yline(0, hi1)}, local(d0.X, d0.Y), local(pc.X, pc.Y), local(pb.X, pb.Y), local(a0.X, a0.Y), {lo1, yline(0, lo1)}};
            std::vector<P> poly{{hi1 + 1, y0 - 1e6}, {hi1 + 1, yline(0, hi1 + 1)}};
            poly.insert(poly.end(), brk.begin() + 1, brk.end() - 1);
            poly.push_back({lo1 - 1, yline(0, lo1 - 1)});
            poly.push_back({lo1 - 1, y0 - 1e6});
            std::vector<P> ring = poly;
            ring.push_back(poly.front());
            auto inside = [&](const P& p) {
                bool c = false;
                for (std::size_t i = 0; i < poly.size(); ++i)
                {
                    const P& a = poly[i];
                    const P& b = poly[(i + 1) % poly.size()];
                    if ((a.Y > p.Y) != (b.Y > p.Y) && p.X < a.X + (p.Y - a.Y) * (b.X - a.X) / (b.Y - a.Y)) c = !c;
                }
                return c;
            };
            // 扶手：天正显示只画第一跑一侧，同样被折断线截断（分解结果是完整的 U 形，与显示不同，以截图为准）
            const double yb = yo + (linked ? rw / 2 : 0);
            for (const Seg& r : {Seg{{o1, yb}, {o1, railTop}}, Seg{{i1, yb}, {i1, railTop}}, Seg{{o1, yb}, {i1, yb}}, Seg{{o1, railTop}, {i1, railTop}}})
                flight.push_back({0, r});
            for (const auto& [k, seg] : flight)
            {
                std::vector<double> ts{0, 1};
                Detail::CrossParams(seg, ring, ts, true);
                std::sort(ts.begin(), ts.end());
                for (std::size_t i = 0; i + 1 < ts.size(); ++i)
                    if (ts[i + 1] - ts[i] > 1e-9 && inside(Detail::Lerp(seg, (ts[i] + ts[i + 1]) / 2)))
                        line(Detail::Lerp(seg, ts[i]), Detail::Lerp(seg, ts[i + 1]));
            }
            for (std::size_t i = 0; i + 1 < brk.size(); ++i) line(brk[i], brk[i + 1]);
        }
        else
            for (const auto& [k, seg] : flight) line(seg.first, seg.second);
        if (floor != 0) line({f1.Inner, innerBot}, {f2.Inner, innerBot});   // 梯井底线

        // 扶手
        auto rect = [&](double x1, double x2, double ya, double yb) {
            line({x1, ya}, {x1, yb}); line({x1, yb}, {x2, yb}); line({x2, yb}, {x2, ya}); line({x2, ya}, {x1, ya});
        };
        if (floor == 1)
        {
            if (linked) { rect(oL, oR, yo, railTop); rect(iL, iR, yo + rw, iTop); }
            else { rect(oL, iL, yo, railTop); rect(iR, oR, yo, railTop); }
        }
        else if (floor == 0) {}
        else if (!linked)
        {
            const double y1b = std::max(y0, yi);
            rect(o1, i1, y1b, railTop);
            line({i2, railTop}, {o2, railTop}); line({o2, railTop}, {o2, yo}); line({o2, yo}, {f1.Outer, yo});
            line({i2, railTop}, {i2, yi}); line({i2, yi}, {f1.Outer, yi}); line({f1.Outer, yo}, {f1.Outer, yi});
        }
        else
        {
            // 顶层：第二跑一侧扶手沿第一跑起步线延伸为护栏
            line({o1, yi}, {o1, railTop}); line({oL, railTop}, {oR, railTop}); line({o2, railTop}, {o2, yo}); line({o2, yo}, {f1.Outer, yo});
            line({i1, yi}, {i1, iTop}); line({iL, iTop}, {iR, iTop}); line({i2, iTop}, {i2, yi}); line({i2, yi}, {f1.Outer, yi});
            line({o1, yi}, {i1, yi}); line({f1.Outer, yo}, {f1.Outer, yi});
        }

        // 休息平台：两跑步数不同时在短跑内侧边折成 L 形；折线的横段像踏步线一样在扶手处断开（天正显示如此，分解结果不断开）
        if (floor != 0 && s.ShowLanding)
        {
            line({0, 0}, {G, 0});
            if (f1.Steps == f2.Steps)
            {
                line({0, -L}, {G, -L}); line({0, 0}, {0, -L}); line({G, 0}, {G, -L});
            }
            else
            {
                const Flight& fs = f1.Steps < f2.Steps ? f1 : f2;
                const Flight& fl = f1.Steps < f2.Steps ? f2 : f1;
                line({fs.Outer, 0}, {fs.Outer, fs.Top});
                for (const auto& [a, b] : spans(fs, &fs == &f1 ? 0 : 1, fs.Top)) line({a, fs.Top}, {b, fs.Top});
                line({fs.Inner, fs.Top}, {fs.Inner, -L}); line({fs.Inner, -L}, {fl.Outer, -L}); line({fl.Outer, 0}, {fl.Outer, -L});
            }
        }

        // 箭头：起点在起步线外 T/2，箭头长 3 × 比例、宽 0.8 × 比例；上楼箭头止于剖断中心下 T/2
        struct Arrow { std::vector<P> Points; const char* Text; };
        std::vector<Arrow> arrows;
        const double head = 3 * s.Scale;
        if (s.ShowArrows)
        {
            if (floor != 2)
            {
                const double tip = yc - T / 2;
                arrows.push_back({{{xc1, y0 - T / 2}, {xc1, tip - head}, {xc1, tip}}, "\xe4\xb8\x8a"});                     // 上
            }
            if (floor != 0)
            {
                const double tip = floor == 1 ? yc + T / 2 : y0 + T / 2;
                arrows.push_back({{{xc2, y0 - T / 2}, {xc2, -L / 2}, {xc1, -L / 2}, {xc1, tip + head}, {xc1, tip}}, "\xe4\xb8\x8b"});   // 下
            }
        }

        // 局部坐标 → 世界坐标
        const double cr = std::cos(s.Rotation), sr = std::sin(s.Rotation);
        auto world = [&](const P& q) { return P{s.X + q.X * cr - q.Y * sr, s.Y + q.X * sr + q.Y * cr}; };
        for (auto& l : g.Lines) { l.A = world(l.A); l.B = world(l.B); }
        const double hn = s.TextHeight * s.Scale, h = 0.96454 * hn;
        const double dx = -sr, dy = cr;                                     // 箭头起始方向（局部 +Y）
        for (const auto& a : arrows)
        {
            TchGraphics::Path shaft;
            shaft.Layer = 1;
            for (std::size_t i = 0; i + 1 < a.Points.size(); ++i) shaft.Points.push_back({world(a.Points[i])});
            g.Paths.push_back(std::move(shaft));
            const P base = a.Points[a.Points.size() - 2], tip = a.Points.back();
            const double len = std::hypot(tip.X - base.X, tip.Y - base.Y);
            const double nx = -(tip.Y - base.Y) / len * 0.4 * s.Scale, ny = (tip.X - base.X) / len * 0.4 * s.Scale;
            g.Arrows.push_back({world({base.X + nx, base.Y + ny}), world({base.X - nx, base.Y - ny}), world(tip), 1});

            // 文字：起点沿反方向退 0.3 × 名义字高，文字框（宽为墨迹宽度、高为名义字高）贴在箭头的反方向一侧
            const P c = world({a.Points[0].X, a.Points[0].Y - 0.3 * hn});
            const double w = (a.Text[2] == '\x8a' ? 0.617648 : 0.562501) * h;      // GBCBIG 中“上”“下”的墨迹宽度
            P ins;
            if (std::abs(dy) >= std::abs(dx)) ins = {c.X - w / 2, dy > 0 ? c.Y - hn : c.Y};
            else ins = {dx > 0 ? c.X - w : c.X, c.Y - hn / 2};
            TchGraphics::Text t{{ins.X, ins.Y + h / 2}, h, 0, a.Text, 4, 0, 7};
            t.Layer = 1;
            g.Texts.push_back(std::move(t));
        }
        return g;
    }
}
