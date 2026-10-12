#pragma once
#include "Import/TchShape.h"
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

// 天正 2014 的双分平行、双分转角、双分三跑、交叉、剪刀、三角、矩形转角楼梯（CADReader 没有这些类，
// 布局由属性取不同值的构造变体与位流对照得出，几何按天正 2014 分解结果归纳）
namespace MiniCAD::Tch
{
    namespace Detail
    {
        // 这批楼梯共用的局部绘图器：局部坐标 x 向右、y 向上（楼梯自基点向局部 −y 展开），最后整体转到世界坐标
        struct StairSketch
        {
            using P = TchGraphics::Point;
            TchGraphics G;
            struct ArrowPath { std::vector<P> Points; std::string Text; };
            std::vector<ArrowPath> Arrows;

            void Line(P a, P b)
            {
                if (std::hypot(b.X - a.X, b.Y - a.Y) > 1e-9) G.Lines.push_back({a, b});
            }
            void Line(double x0, double y0, double x1, double y1) { Line(P{x0, y0}, P{x1, y1}); }

            // 箭头：Points 为折线（最后一点为箭头尖）；箭头长 3 × 比例、半宽 0.4 × 比例；
            // 文字（“上”“下”）基线在起点外 1.3 × 名义字高（起点处折线方向的反向）
            TchGraphics Finish(double x, double y, double rotation, double scale, double textHeight)
            {
                const double cr = std::cos(rotation), sr = std::sin(rotation);
                auto world = [&](const P& q) { return P{x + q.X * cr - q.Y * sr, y + q.X * sr + q.Y * cr}; };
                TchGraphics g = std::move(G);
                for (auto& l : g.Lines) { l.A = world(l.A); l.B = world(l.B); }
                for (auto& a : g.Arcs) { a.Center = world(a.Center); a.Start += rotation; a.End += rotation; }
                const double hn = textHeight * scale, head = 3 * scale, half = 0.4 * scale;
                for (const auto& a : Arrows)
                {
                    if (a.Points.size() < 2) continue;
                    const P tip = a.Points.back(), prev = a.Points[a.Points.size() - 2];
                    const double len = std::hypot(tip.X - prev.X, tip.Y - prev.Y);
                    if (len < 1e-9) continue;
                    const double ux = (tip.X - prev.X) / len, uy = (tip.Y - prev.Y) / len;
                    const P base{tip.X - ux * head, tip.Y - uy * head};
                    TchGraphics::Path shaft;
                    shaft.Layer = 1;
                    for (std::size_t i = 0; i + 1 < a.Points.size(); ++i) shaft.Points.push_back({world(a.Points[i])});
                    shaft.Points.push_back({world(base)});
                    g.Paths.push_back(std::move(shaft));
                    g.Arrows.push_back({world({base.X - uy * half, base.Y + ux * half}), world({base.X + uy * half, base.Y - ux * half}), world(tip), 1});
                    // 文字贴在起点外侧（起始方向的反向）、间隔 0.3 × 名义字高：竖向箭头时居中，横向箭头时竖向居中、靠起点对齐
                    const P s0 = a.Points[0], s1 = a.Points[1];
                    const double l0 = std::max(std::hypot(s1.X - s0.X, s1.Y - s0.Y), 1e-9);
                    const double dx = (s1.X - s0.X) / l0, dy = (s1.Y - s0.Y) / l0;
                    P c;
                    int align = 1;
                    if (std::abs(dy) >= std::abs(dx)) c = {s0.X, dy > 0 ? s0.Y - 1.3 * hn : s0.Y + 0.3 * hn};
                    else { c = {dx > 0 ? s0.X - 0.3 * hn : s0.X + 0.3 * hn, s0.Y - 0.5 * hn}; align = dx > 0 ? 2 : 0; }
                    c = world(c);
                    AddRuns(g, a.Text, hn, c.X, c.Y, rotation, align, 7, 1);
                }
                return g;
            }
        };

        inline const char* kUp = "\xe4\xb8\x8a";      // “上”
        inline const char* kDown = "\xe4\xb8\x8b";    // “下”

        // 这批楼梯的句柄流：楼梯基类一个，之后依次为扶手图层、两个空句柄、栏杆图层、文字图层、文字样式、箭头图层
        inline void ReadStairHandles(const MiniDWG::UnknownEntity& entity, const TchBase& base, MiniDWG::Handle& style, MiniDWG::Handle& arrowLayer)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount + 6; ++i) handles.ReadHandle(entity.ObjectHandle);
            style = handles.ReadHandle(entity.ObjectHandle);
            arrowLayer = handles.ReadHandle(entity.ObjectHandle);
            if (handles.Failed()) style = arrowLayer = kNullHandle;
        }

        // 楼层类型（属性 StairFlag）：0 首层、1 中间层、2 顶层
        enum class StairFloor { First = 0, Middle = 1, Top = 2 };

        // 一跑梯段：局部坐标 (u, v)，原点在起步端 u = 0 的一侧，v 沿上行方向，u 轴为 v 轴顺时针转 90°；
        // 宽 Width（u 0 ～ Width）、踏步数 Steps（踏步线 v = k·T，k = 0 ～ Steps − 1）。
        // RailBands：扶手占据的 u 区间（踏步线在此断开）；BreakKind：0 无、1 首层单剖断、2 中间层双剖断；
        // 剖断线中心在 (Width / 2, (BreakSteps − 0.5)·T)，方向为 u 轴转向 v 轴 BreakAngle
        struct FlightSpec
        {
            TchGraphics::Point Origin;
            double Vx = 0, Vy = 1;
            double Width = 1200, Tread = 270;
            int Steps = 10;
            std::vector<std::pair<double, double>> RailBands;
            int BreakKind = 0, BreakSteps = 5;
            double BreakAngle = std::numbers::pi / 6;
            bool Edge0 = true, Edge1 = true;

            double Length() const { return (Steps - 1) * Tread; }
            TchGraphics::Point At(double u, double v) const { return {Origin.X + u * Vy + v * Vx, Origin.Y - u * Vx + v * Vy}; }
            double BreakCenterV() const { return (BreakSteps - 0.5) * Tread; }
        };

        inline void DrawFlight(StairSketch& k, const FlightSpec& f)
        {
            using P = TchGraphics::Point;
            const double L = f.Length(), cv = f.BreakCenterV();
            // 斜放的剖断线超出梯段两端时，天正改为垂直于梯段放置（三角楼梯样例核对）
            double t = std::tan(f.BreakAngle);
            if (cv - f.Width / 2 * std::abs(t) < 0 || cv + f.Width / 2 * std::abs(t) > L) t = 0;
            const P a = f.At(0, cv - f.Width / 2 * t), b = f.At(f.Width, cv + f.Width / 2 * t);
            auto local = [&](const P& p) {
                const double dx = p.X - f.Origin.X, dy = p.Y - f.Origin.Y;
                return std::pair{dx * f.Vy - dy * f.Vx, dx * f.Vx + dy * f.Vy};      // (u, v)
            };
            auto inside = [&](const P& p) { auto [u, v] = local(p); return u >= -1e-6 && u <= f.Width + 1e-6 && v >= -1e-6 && v <= L + 1e-6; };
            auto progress = [&](const P& p) { return local(p).second / std::max(L, 1e-9); };
            const FlightBreak brk(f.BreakKind, a, b, inside, progress, progress(a), progress(b));
            if (f.Edge0) brk.AddLine(k.G, f.At(0, 0), f.At(0, L));
            if (f.Edge1) brk.AddLine(k.G, f.At(f.Width, 0), f.At(f.Width, L));
            for (int i = 0; i < f.Steps; ++i)
            {
                const double v = i * f.Tread;
                double u = 0;
                for (const auto& [r0, r1] : f.RailBands)
                {
                    if (r0 - u > 1e-9) brk.AddLine(k.G, f.At(u, v), f.At(r0, v));
                    u = std::max(u, r1);
                }
                if (f.Width - u > 1e-9) brk.AddLine(k.G, f.At(u, v), f.At(f.Width, v));
            }
            brk.AddSymbol(k.G);
        }

        // 折线扶手轮廓（依次连接的点），两条平行轮廓与两端封口
        inline void DrawRail(StairSketch& k, const std::vector<TchGraphics::Point>& outer, const std::vector<TchGraphics::Point>& inner)
        {
            for (std::size_t i = 0; i + 1 < outer.size(); ++i) k.Line(outer[i], outer[i + 1]);
            for (std::size_t i = 0; i + 1 < inner.size(); ++i) k.Line(inner[i], inner[i + 1]);
            if (!outer.empty() && !inner.empty()) { k.Line(outer.front(), inner.front()); k.Line(outer.back(), inner.back()); }
        }
    }

    // ── TCH_PARALLELSTAIR 双分平行楼梯 ──
    struct TchParallelStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：平台外侧左上角
        double StepHeight = 150, Tread = 270;
        double SideWidth = 1200, MidWidth = 2400, Landing = 1200;    // 边梯段宽、中梯段宽、平台宽（SlabSize）
        int    Steps1 = 10, Steps2 = 10;       // 起步跑、第二跑踏步数
        double SpaceWidth = 5000;              // 梯间宽
        int    Floor = 1, StartSide = 0;       // StairFlag；StartSide：0 自中间梯段起步，1 自两边起步
        double RailWidth = 60, RailToSide = 0, RailTop = 60, RailBottom = 60;   // 扶手宽、距梯段边、向平台端与起步端伸出
        int    BreakSteps = 5;                 // 剖断线中心距起步端的踏步数（SectStepNum）
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC（1～2）、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 边梯段宽、BD 中梯段宽、
    // BD 平台宽、2 个 BD、RC 第一跑步数、RC 第二跑步数、BD 梯间宽、BD 扶手高、RC 楼层、RC 起步位置、RC、
    // BD 扶手宽、BD 扶手距边、BD 向平台伸出、BD 向起步端伸出、BS 剖断步数、BD 剖断角、BD 字高、BL 标志、3 个 BD（梁高、梁宽、板厚）、RC
    inline bool DecodeParallelStair(const MiniDWG::UnknownEntity& entity, TchParallelStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_PARALLELSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_PARALLELSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native parallel stair version");
        TchParallelStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.SideWidth = r.ReadBitDouble();
        s.MidWidth = r.ReadBitDouble();
        s.Landing = r.ReadBitDouble();
        r.ReadBitDouble(); r.ReadBitDouble();
        s.Steps1 = r.ReadByte();
        s.Steps2 = r.ReadByte();
        s.SpaceWidth = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailTop = r.ReadBitDouble();
        s.RailBottom = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native parallel stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.SideWidth, s.MidWidth, s.Landing, s.SpaceWidth, s.RailWidth, s.RailToSide,
                         s.RailTop, s.RailBottom, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native parallel stair settings");
        if (s.Tread <= 0 || s.Steps1 < 2 || s.Steps2 < 2 || s.Floor < 0 || s.Floor > 2 || s.SideWidth <= 0 || s.MidWidth <= 0)
            return fail("invalid native parallel stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、起步在中间 / 两边、属性取不同值共 6 个样例核对）：
    // 平台在上（y 0 ～ −平台宽），三跑梯段在下；梯井两侧的扶手在梯段边内侧“扶手距边”处，宽为扶手宽，
    // 两侧扶手外轮廓向平台端伸出 RailTop、向起步端伸出 RailBottom 后连通；
    // 剖断线中心在起步梯段中线、距起步端 (SectStepNum − 0.5) 级踏步处，倾角 BreakAngle，只截断踏步与梯段边；
    // 首层只画起步梯段剖断线以下的部分（单剖断），顶层不剖断、两侧扶手在起步端连通
    inline TchGraphics BuildParallelStair(const TchParallelStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const double well = (s.SpaceWidth - 2 * s.SideWidth - s.MidWidth) / 2;
        const double xm0 = s.SideWidth + well, xm1 = xm0 + s.MidWidth;
        const double top = -s.Landing;
        const double len1 = (s.Steps1 - 1) * s.Tread, len2 = (s.Steps2 - 1) * s.Tread;
        const bool midStarts = s.StartSide == 0;
        const double startLen = len1, otherLen = len2;
        struct Flight { double X0, X1, Len; bool Start; };
        std::vector<Flight> flights;
        flights.push_back({0, s.SideWidth, midStarts ? otherLen : startLen, !midStarts});
        flights.push_back({xm0, xm1, midStarts ? startLen : otherLen, midStarts});
        flights.push_back({s.SpaceWidth - s.SideWidth, s.SpaceWidth, midStarts ? otherLen : startLen, !midStarts});
        const double rw = s.RailWidth, rd = s.RailToSide;
        // 各梯段内扶手占据的 x 区间（扶手在梯井一侧）
        auto railsIn = [&](const Flight& f) {
            std::vector<std::pair<double, double>> r;
            if (f.X0 > 1e-9) r.push_back({f.X0 + rd, f.X0 + rd + rw});
            if (f.X1 < s.SpaceWidth - 1e-9) r.push_back({f.X1 - rd - rw, f.X1 - rd});
            return r;
        };
        for (const auto& f : flights)
        {
            if (floor == Detail::StairFloor::First && !f.Start) continue;
            const double bottom = top - f.Len;
            const double cy = bottom + (s.BreakSteps - 0.5) * s.Tread;
            const double w = f.X1 - f.X0, t = std::tan(s.BreakAngle);
            const P a{f.X0, cy - w / 2 * t}, b{f.X1, cy + w / 2 * t};
            int kind = 0;
            if (f.Start && floor == Detail::StairFloor::Middle) kind = 2;
            if (f.Start && floor == Detail::StairFloor::First) kind = 1;
            auto inside = [&](const P& p) { return p.X >= f.X0 - 1e-6 && p.X <= f.X1 + 1e-6 && p.Y <= top + 1e-6 && p.Y >= bottom - 1e-6; };
            auto progress = [&](const P& p) { return (p.Y - bottom) / std::max(f.Len, 1e-9); };
            const double pa = (a.Y - bottom) / std::max(f.Len, 1e-9), pb = (b.Y - bottom) / std::max(f.Len, 1e-9);
            const Detail::FlightBreak brk(kind, a, b, inside, progress, pa, pb);
            // 梯段边
            brk.AddLine(k.G, {f.X0, top}, {f.X0, bottom});
            brk.AddLine(k.G, {f.X1, top}, {f.X1, bottom});
            // 踏步线：被扶手分段
            const auto rails = railsIn(f);
            for (int i = 0; i <= static_cast<int>(std::round(f.Len / s.Tread)); ++i)
            {
                const double y = top - i * s.Tread;
                double x = f.X0;
                for (const auto& [r0, r1] : rails)
                {
                    if (r0 - x > 1e-9) brk.AddLine(k.G, {x, y}, {r0, y});
                    x = r1;
                }
                if (f.X1 - x > 1e-9) brk.AddLine(k.G, {x, y}, {f.X1, y});
            }
            brk.AddSymbol(k.G);
        }
        // 平台
        if (floor != Detail::StairFloor::First)
        {
            k.Line(0, 0, s.SpaceWidth, 0);
            k.Line(0, top, s.SpaceWidth, top);
            k.Line(0, 0, 0, top);
            k.Line(s.SpaceWidth, 0, s.SpaceWidth, top);
        }
        // 梯井两侧的扶手（左、右两个梯井）
        const double bottomStart = top - startLen;
        for (const double x0 : {s.SideWidth, xm1})       // 梯井左侧梯段的右边
        {
            const double x1 = x0 + well;                  // 梯井右侧梯段的左边
            const double o0 = x0 - rd - rw, o1 = x1 + rd + rw, i0 = x0 - rd, i1 = x1 + rd;
            const double yo = top + s.RailTop, yi = yo - rw;
            k.Line(o0, yo, o1, yo);                      // 平台端外轮廓
            k.Line(i0, yi, i1, yi);
            if (floor == Detail::StairFloor::First)
            {
                const double yb = bottomStart - rw / 2;
                k.Line(o0, yo, o0, yb); k.Line(i0, yi, i0, yb); k.Line(o0, yb, i0, yb);
                k.Line(o1, yo, o1, yb); k.Line(i1, yi, i1, yb); k.Line(i1, yb, o1, yb);
            }
            else if (floor == Detail::StairFloor::Middle)
            {
                const double yb = bottomStart - s.RailBottom, ybi = yb + rw;
                k.Line(o0, yo, o0, yb); k.Line(o1, yo, o1, yb);
                k.Line(i0, yi, i0, ybi); k.Line(i1, yi, i1, ybi);
                k.Line(i0, ybi, i1, ybi);
                k.Line(o0, yb, o1, yb);
                k.Line(x0, bottomStart, x1, bottomStart);     // 起步端横跨梯井的线
            }
            else
            {
                // 顶层：梯井外侧（远离中梯段一侧）的扶手线伸到起步端外，靠中梯段一侧到起步端为止并封口
                const bool left = x0 < xm0;
                const double yb = bottomStart - s.RailBottom;
                k.Line(o0, yo, o0, left ? yb : bottomStart); k.Line(o1, yo, o1, left ? bottomStart : yb);
                k.Line(i0, yi, i0, bottomStart); k.Line(i1, yi, i1, bottomStart);
                if (left) k.Line(i1, bottomStart, o1, bottomStart); else k.Line(o0, bottomStart, i0, bottomStart);
            }
        }
        if (floor == Detail::StairFloor::Top)
        {
            // 顶层：两侧扶手在起步端外相连（外轮廓一条横线），并沿起步端画一条横线
            const double yb = bottomStart - s.RailBottom;
            k.Line(s.SideWidth - rd - rw, yb, xm1 + well + rd + rw, yb);
            k.Line(s.SideWidth - rd, bottomStart, xm1 + well + rd, bottomStart);
        }

        // 箭头：上行箭头自起步梯段起步端下方 T/2 指向剖断线中心以下 T/2；
        // 下行箭头自另一组梯段起步端下方 T/2 上行到平台中线，横穿到起步梯段中线，再下行到剖断线中心以上 T/2（顶层到起步端以上 T/2）
        const double startY = bottomStart - s.Tread / 2;
        const double cy = bottomStart + (s.BreakSteps - 0.5) * s.Tread;
        const double landY = top / 2;
        std::vector<double> startXs, otherXs;
        if (midStarts) { startXs = {(xm0 + xm1) / 2}; otherXs = {s.SideWidth / 2, s.SpaceWidth - s.SideWidth / 2}; }
        else { startXs = {s.SideWidth / 2, s.SpaceWidth - s.SideWidth / 2}; otherXs = {(xm0 + xm1) / 2}; }
        if (floor != Detail::StairFloor::Top)
            for (double x : startXs) k.Arrows.push_back({{P{x, startY}, P{x, cy - s.Tread / 2}}, Detail::kUp});
        if (floor != Detail::StairFloor::First)
        {
            const double endY = floor == Detail::StairFloor::Top ? bottomStart + s.Tread / 2 : cy + s.Tread / 2;
            for (double xo : otherXs)
                for (double xs : startXs)
                    k.Arrows.push_back({{P{xo, startY}, P{xo, landY}, P{xs, landY}, P{xs, endY}}, Detail::kDown});
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_CORNERSTAIR 双分转角楼梯 ──
    struct TchCornerStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：左边梯段起步端外侧上角
        double StepHeight = 150, Tread = 270;
        double SideWidth = 1200, MidWidth = 2400;
        double LandingDepth = 1300, LandingLength = 2500;     // 平台进深（SlabSize）、平台长（SlabLength）
        int    Steps1 = 10, Steps2 = 10;
        int    Floor = 1, StartSide = 1;       // StartSide：1 自两边起步，0 自中间梯段起步
        double RailWidth = 60, RailToSide = 0, RailFar = 60, RailStart = 60;    // 扶手向另一端伸出（RotationDist）、向起步端伸出（LayerDist）
        int    BreakSteps = 6;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 边梯段宽、BD 中梯段宽、BD 平台进深、BD 平台长、
    // RC 第一跑步数、RC 第二跑步数、BD 扶手高、RC 楼层、RC 起步位置、BD 扶手宽、BD 扶手距边、BD 向另一端伸出、BD 向起步端伸出、
    // BS 剖断步数、BD 剖断角、BD 字高、BL 标志、3 个 BD、RC
    inline bool DecodeCornerStair(const MiniDWG::UnknownEntity& entity, TchCornerStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CORNERSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_CORNERSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native corner stair version");
        TchCornerStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.SideWidth = r.ReadBitDouble();
        s.MidWidth = r.ReadBitDouble();
        s.LandingDepth = r.ReadBitDouble();
        s.LandingLength = r.ReadBitDouble();
        s.Steps1 = r.ReadByte();
        s.Steps2 = r.ReadByte();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailFar = r.ReadBitDouble();
        s.RailStart = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native corner stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.SideWidth, s.MidWidth, s.LandingDepth, s.LandingLength, s.RailWidth,
                         s.RailToSide, s.RailFar, s.RailStart, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native corner stair settings");
        if (s.Tread <= 0 || s.Steps1 < 2 || s.Steps2 < 2 || s.Floor < 0 || s.Floor > 2 || s.SideWidth <= 0 || s.MidWidth <= 0)
            return fail("invalid native corner stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对，均为两边起步）：
    // 两边梯段横向（左段向右上行、右段向左上行，宽 y 0 ～ −边梯段宽），平台居中（长 SlabLength、进深 SlabSize），
    // 中梯段自平台向下（−y）上行；转角处两条 L 形扶手沿边梯段内侧与中梯段两侧，起步端伸出 LayerDist、另一端伸出 RotationDist；
    // 首层只画起步梯段剖断线以下部分，顶层不剖断
    inline TchGraphics BuildCornerStair(const TchCornerStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const double L1 = (s.Steps1 - 1) * s.Tread, L2 = (s.Steps2 - 1) * s.Tread;
        const double total = 2 * L1 + s.LandingLength;
        const double xm0 = (total - s.MidWidth) / 2, xm1 = xm0 + s.MidWidth;
        const double rw = s.RailWidth, rd = s.RailToSide, sw = s.SideWidth;
        const bool sidesStart = s.StartSide != 0;
        const int startBreak = floor == Detail::StairFloor::Middle ? 2 : (floor == Detail::StairFloor::First ? 1 : 0);
        Detail::FlightSpec left, right, mid;
        left.Origin = {0, 0}; left.Vx = 1; left.Vy = 0;
        right.Origin = {total, -sw}; right.Vx = -1; right.Vy = 0;
        for (auto* f : {&left, &right})
        {
            f->Width = sw; f->Tread = s.Tread; f->Steps = s.Steps1;
            f->BreakSteps = s.BreakSteps; f->BreakAngle = s.BreakAngle;
            f->BreakKind = sidesStart ? startBreak : 0;
        }
        left.RailBands = {{sw - rd - rw, sw - rd}};
        right.RailBands = {{rd, rd + rw}};
        mid.Origin = {xm1, -s.LandingDepth}; mid.Vx = 0; mid.Vy = -1;
        mid.Width = s.MidWidth; mid.Tread = s.Tread; mid.Steps = s.Steps2;
        mid.BreakSteps = s.BreakSteps; mid.BreakAngle = s.BreakAngle;
        mid.BreakKind = sidesStart ? 0 : startBreak;
        mid.RailBands = {{rd, rd + rw}, {s.MidWidth - rd - rw, s.MidWidth - rd}};
        const bool drawSides = floor != Detail::StairFloor::First || sidesStart;
        const bool drawMid = floor != Detail::StairFloor::First || !sidesStart;
        if (drawSides) { Detail::DrawFlight(k, left); Detail::DrawFlight(k, right); }
        if (drawMid) Detail::DrawFlight(k, mid);
        if (floor != Detail::StairFloor::First)
        {
            // 平台：上边、与边梯段相接的两条竖线、边梯段内侧边到中梯段的短线、中梯段两侧到平台进深
            k.Line(L1, 0, total - L1, 0);
            k.Line(L1, 0, L1, -sw); k.Line(total - L1, 0, total - L1, -sw);
            k.Line(L1, -sw, xm0, -sw); k.Line(xm1, -sw, total - L1, -sw);
            k.Line(xm0, -sw, xm0, -s.LandingDepth); k.Line(xm1, -sw, xm1, -s.LandingDepth);
            k.Line(xm0, -s.LandingDepth, xm1, -s.LandingDepth);
        }
        // L 形扶手：外轮廓贴梯井一侧
        const double yEnd = -s.LandingDepth - L2 - s.RailFar;
        for (const bool isLeft : {true, false})
        {
            auto mx = [&](double x) { return isLeft ? x : total - x; };
            const double yo = -sw + rd, yi = yo + rw, xo = xm0 + rd, xi = xo + rw;
            Detail::DrawRail(k, {{mx(-s.RailStart), yo}, {mx(xo), yo}, {mx(xo), yEnd}},
                                {{mx(-s.RailStart), yi}, {mx(xi), yi}, {mx(xi), yEnd}});
        }
        // 箭头
        const double cvSide = left.BreakCenterV();
        const double yc = -sw / 2, xc = (xm0 + xm1) / 2;
        const double y0 = -s.LandingDepth - L2 - s.Tread / 2;
        if (sidesStart)
        {
            if (floor != Detail::StairFloor::Top)
            {
                k.Arrows.push_back({{P{-s.Tread / 2, yc}, P{cvSide - s.Tread / 2, yc}}, Detail::kUp});
                k.Arrows.push_back({{P{total + s.Tread / 2, yc}, P{total - cvSide + s.Tread / 2, yc}}, Detail::kUp});
            }
            if (floor != Detail::StairFloor::First)
            {
                const double endV = floor == Detail::StairFloor::Top ? s.Tread / 2 : cvSide + s.Tread / 2;
                k.Arrows.push_back({{P{xc, y0}, P{xc, yc}, P{endV, yc}}, Detail::kDown});
                k.Arrows.push_back({{P{xc, y0}, P{xc, yc}, P{total - endV, yc}}, Detail::kDown});
            }
        }
        else
        {
            // 自中间梯段起步（未见样例）：上行箭头沿中梯段指向平台，下行箭头自两边梯段起步端到中梯段
            const double cvMid = mid.BreakCenterV();
            const double bottom = -s.LandingDepth - L2;
            if (floor != Detail::StairFloor::Top)
                k.Arrows.push_back({{P{xc, y0}, P{xc, bottom + cvMid - s.Tread / 2}}, Detail::kUp});
            if (floor != Detail::StairFloor::First)
            {
                const double endY = bottom + (floor == Detail::StairFloor::Top ? s.Tread / 2 : cvMid + s.Tread / 2);
                k.Arrows.push_back({{P{-s.Tread / 2, yc}, P{xc, yc}, P{xc, endY}}, Detail::kDown});
                k.Arrows.push_back({{P{total + s.Tread / 2, yc}, P{xc, yc}, P{xc, endY}}, Detail::kDown});
            }
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_DOUBLEMULSTAIR 双分三跑楼梯 ──
    struct TchDoubleMulStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：左侧平台外侧左上角
        double StepHeight = 150, Tread = 270;
        double SideWidth = 1200, MidWidth = 2400, SecondWidth = 1200;   // 边梯段宽、中梯段宽、第二跑（横向）梯段宽
        double SidePlatLength = 1200, MidPlatLength = 1200;             // 侧平台、中平台进深（y 向）
        double SidePlatWidth = 1400, MidPlatWidth = 2700;               // 侧平台、中平台宽（x 向）
        int    Steps1 = 10, Steps2 = 10, Steps3 = 10;
        int    Floor = 1, StartSide = 1;
        double RailWidth = 60, RailToSide = 0, RailMid = 60, RailSide = 60;   // 扶手在中梯段端、边梯段端伸出（RailOutDist、RailOutDistSide）
        int    BreakSteps = 5;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 边梯段宽、BD 中梯段宽、BD 第二跑宽、
    // BD 侧平台进深、BD 中平台进深、3 个 RC 各跑步数、BD 梯间宽、BD 扶手高、RC 楼层、RC 起步位置、RC、BD 扶手宽、BD 扶手距边、
    // BD 中梯段端伸出、BD 边梯段端伸出、BS 剖断步数、BD 剖断角、BD 侧平台宽、BD 中平台宽、BD 字高、BL 标志、3 个 BD、RC
    inline bool DecodeDoubleMulStair(const MiniDWG::UnknownEntity& entity, TchDoubleMulStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_DOUBLEMULSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_DOUBLEMULSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native double three-run stair version");
        TchDoubleMulStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.SideWidth = r.ReadBitDouble();
        s.MidWidth = r.ReadBitDouble();
        s.SecondWidth = r.ReadBitDouble();
        s.SidePlatLength = r.ReadBitDouble();
        s.MidPlatLength = r.ReadBitDouble();
        s.Steps1 = r.ReadByte(); s.Steps2 = r.ReadByte(); s.Steps3 = r.ReadByte();
        r.ReadBitDouble(); r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailMid = r.ReadBitDouble();
        s.RailSide = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.SidePlatWidth = r.ReadBitDouble();
        s.MidPlatWidth = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native double three-run stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.SideWidth, s.MidWidth, s.SecondWidth, s.SidePlatLength, s.MidPlatLength,
                         s.SidePlatWidth, s.MidPlatWidth, s.RailWidth, s.RailToSide, s.RailMid, s.RailSide, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native double three-run stair settings");
        if (s.Tread <= 0 || s.Steps1 < 2 || s.Steps2 < 2 || s.Steps3 < 2 || s.Floor < 0 || s.Floor > 2 || s.SideWidth <= 0 || s.MidWidth <= 0)
            return fail("invalid native double three-run stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对，均为两边起步）：
    // 两边起步梯段向上（+y）到侧平台，第二跑横向走向中间平台，中梯段自中间平台向下（−y）；
    // U 形扶手沿起步梯段内侧、第二跑下边、中梯段外侧，两端分别伸出 RailOutDistSide、RailOutDist
    inline TchGraphics BuildDoubleMulStair(const TchDoubleMulStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const double T = s.Tread, rw = s.RailWidth, rd = s.RailToSide;
        const double L1 = (s.Steps1 - 1) * T, L2 = (s.Steps2 - 1) * T, L3 = (s.Steps3 - 1) * T;
        const double total = 2 * (s.SidePlatWidth + L2) + s.MidPlatWidth;
        const double xm0 = (total - s.MidWidth) / 2, xm1 = xm0 + s.MidWidth;
        const double sideBottom = -s.SidePlatLength - L1, midBottom = -s.MidPlatLength - L3;
        const double w2 = s.SecondWidth;
        const bool sidesStart = s.StartSide != 0;
        const int startBreak = floor == Detail::StairFloor::Middle ? 2 : (floor == Detail::StairFloor::First ? 1 : 0);
        auto spec = [&](P origin, double vx, double vy, double width, int steps) {
            Detail::FlightSpec f;
            f.Origin = origin; f.Vx = vx; f.Vy = vy; f.Width = width; f.Tread = T; f.Steps = steps;
            f.BreakSteps = s.BreakSteps; f.BreakAngle = s.BreakAngle;
            return f;
        };
        // 各梯段的局部坐标见 FlightSpec：u 轴为上行方向顺时针转 90°
        Detail::FlightSpec sideL = spec({0, sideBottom}, 0, 1, s.SideWidth, s.Steps1);                    // u → +x
        Detail::FlightSpec sideR = spec({total - s.SideWidth, sideBottom}, 0, 1, s.SideWidth, s.Steps1);   // u → +x
        Detail::FlightSpec secL = spec({s.SidePlatWidth, 0}, 1, 0, w2, s.Steps2);                          // u → −y
        Detail::FlightSpec secR = spec({total - s.SidePlatWidth, -w2}, -1, 0, w2, s.Steps2);              // u → +y
        Detail::FlightSpec mid = spec({xm1, -s.MidPlatLength}, 0, -1, s.MidWidth, s.Steps3);              // u → −x
        sideL.RailBands = {{s.SideWidth - rd - rw, s.SideWidth - rd}};
        sideR.RailBands = {{rd, rd + rw}};
        secL.RailBands = {{w2 - rd - rw, w2 - rd}};
        secR.RailBands = {{rd, rd + rw}};
        mid.RailBands = {{rd, rd + rw}, {s.MidWidth - rd - rw, s.MidWidth - rd}};
        for (auto* f : {&sideL, &sideR}) f->BreakKind = sidesStart ? startBreak : 0;
        mid.BreakKind = sidesStart ? 0 : startBreak;
        const bool first = floor == Detail::StairFloor::First;
        if (!first || sidesStart) { Detail::DrawFlight(k, sideL); Detail::DrawFlight(k, sideR); }
        if (!first) { Detail::DrawFlight(k, secL); Detail::DrawFlight(k, secR); }
        if (!first || !sidesStart) Detail::DrawFlight(k, mid);
        if (!first)
        {
            k.Line(0, 0, total, 0);
            for (const bool isLeft : {true, false})
            {
                auto mx = [&](double x) { return isLeft ? x : total - x; };
                // 侧平台
                k.Line(mx(0), 0, mx(0), -s.SidePlatLength);
                k.Line(mx(0), -s.SidePlatLength, mx(s.SideWidth), -s.SidePlatLength);
                k.Line(mx(s.SideWidth), -s.SidePlatLength, mx(s.SideWidth), -w2);
                k.Line(mx(s.SideWidth), -w2, mx(s.SidePlatWidth), -w2);
                // 第二跑两端与平台的分界线（整宽，踏步线只画到扶手）
                k.Line(mx(s.SidePlatWidth), 0, mx(s.SidePlatWidth), -w2);
                k.Line(mx(s.SidePlatWidth + L2), 0, mx(s.SidePlatWidth + L2), -w2);
                // 中间平台在本侧的部分
                const double xe = s.SidePlatWidth + L2;
                k.Line(mx(xe), -w2, mx(xm0), -w2);
                k.Line(mx(xm0), -w2, mx(xm0), -s.MidPlatLength);
            }
            k.Line(xm0, -s.MidPlatLength, xm1, -s.MidPlatLength);
        }
        // U 形扶手（外轮廓贴梯井一侧）
        for (const bool isLeft : {true, false})
        {
            auto mx = [&](double x) { return isLeft ? x : total - x; };
            const double xo = s.SideWidth - rd, xi = xo - rw, yo = -w2 + rd, yi = yo + rw, mo = xm0 + rd, mi = mo + rw;
            const double y0 = sideBottom - s.RailSide, y1 = midBottom - s.RailMid;
            Detail::DrawRail(k, {{mx(xo), y0}, {mx(xo), yo}, {mx(mo), yo}, {mx(mo), y1}},
                                {{mx(xi), y0}, {mx(xi), yi}, {mx(mi), yi}, {mx(mi), y1}});
        }
        // 箭头
        const double cvSide = (s.BreakSteps - 0.5) * T;
        const double xs = s.SideWidth / 2, xc = (xm0 + xm1) / 2, yTurn = -w2 / 2;
        if (sidesStart)
        {
            if (floor != Detail::StairFloor::Top)
                for (double x : {xs, total - xs}) k.Arrows.push_back({{P{x, sideBottom - T / 2}, P{x, sideBottom + cvSide - T / 2}}, Detail::kUp});
            if (!first)
            {
                const double endY = sideBottom + (floor == Detail::StairFloor::Top ? T / 2 : cvSide + T / 2);
                for (double x : {xs, total - xs})
                    k.Arrows.push_back({{P{xc, midBottom - T / 2}, P{xc, yTurn}, P{x, yTurn}, P{x, endY}}, Detail::kDown});
            }
        }
        else
        {
            // 自中梯段起步（未见样例）
            if (floor != Detail::StairFloor::Top)
                k.Arrows.push_back({{P{xc, midBottom - T / 2}, P{xc, midBottom + cvSide - T / 2}}, Detail::kUp});
            if (!first)
            {
                const double endY = midBottom + (floor == Detail::StairFloor::Top ? T / 2 : cvSide + T / 2);
                for (double x : {xs, total - xs})
                    k.Arrows.push_back({{P{x, sideBottom - T / 2}, P{x, yTurn}, P{xc, yTurn}, P{xc, endY}}, Detail::kDown});
            }
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_SCISSORSSTAIR 剪刀楼梯 ──
    struct TchScissorsStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：中间平台左边线的中点
        double StepHeight = 150, Tread = 270;
        double Width = 1200, Landing = 1200;   // 梯段宽、中间平台进深（SlabSize）
        int    Steps1 = 10, Steps2 = 10;
        double SpaceWidth = 2600;
        int    Floor = 1, StartSide = 1;       // StartSide：1 右边，0 左边
        double RailWidth = 60, RailToSide = 0, RailLanding = 60, RailEnd = 60;   // 扶手向平台端、梯段端伸出
        int    BreakSteps = 5;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 梯段宽、BD 平台进深、
    // 2 个 RC 步数、BD 梯间宽、BD 扶手高、RC 楼层、RC 起步位置、RC、BD 扶手宽、BD 扶手距边、BD 向平台伸出、BD 向梯段端伸出、
    // BS 剖断步数、BD 剖断角、BD 字高、BL 标志、3 个 BD、RC
    inline bool DecodeScissorsStair(const MiniDWG::UnknownEntity& entity, TchScissorsStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SCISSORSSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_SCISSORSSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native scissors stair version");
        TchScissorsStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.Width = r.ReadBitDouble();
        s.Landing = r.ReadBitDouble();
        s.Steps1 = r.ReadByte(); s.Steps2 = r.ReadByte();
        s.SpaceWidth = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailLanding = r.ReadBitDouble();
        s.RailEnd = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native scissors stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.Width, s.Landing, s.SpaceWidth, s.RailWidth, s.RailToSide, s.RailLanding,
                         s.RailEnd, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native scissors stair settings");
        if (s.Tread <= 0 || s.Steps1 < 2 || s.Steps2 < 2 || s.Floor < 0 || s.Floor > 2 || s.Width <= 0)
            return fail("invalid native scissors stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对，均为右边起步）：
    // 中间平台（y ±平台进深 / 2）两侧各有两跑并列梯段；下半部右侧梯段自下端起步向上，左侧梯段向下到楼层，
    // 上半部与下半部关于平台中心成 180° 对称；梯井中的扶手两端分别伸出 RotationDist（平台端）、LayerDist（梯段端）
    inline TchGraphics BuildScissorsStair(const TchScissorsStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const double T = s.Tread, rw = s.RailWidth, rd = s.RailToSide, W = s.Width, total = s.SpaceWidth;
        const double half = s.Landing / 2, L = (s.Steps1 - 1) * T, bottom = -half - L;
        const int startBreak = floor == Detail::StairFloor::Middle ? 2 : (floor == Detail::StairFloor::First ? 1 : 0);
        // 下半部：右侧为起步梯段（u 自左边量起），左侧为另一梯段
        Detail::FlightSpec start, other;
        start.Origin = {total - W, bottom}; start.Vx = 0; start.Vy = 1;
        other.Origin = {0, bottom}; other.Vx = 0; other.Vy = 1;
        for (auto* f : {&start, &other}) { f->Width = W; f->Tread = T; f->Steps = s.Steps1; f->BreakSteps = s.BreakSteps; f->BreakAngle = s.BreakAngle; }
        start.RailBands = {{rd, rd + rw}};
        other.RailBands = {{W - rd - rw, W - rd}};
        start.BreakKind = startBreak;
        const bool first = floor == Detail::StairFloor::First;
        Detail::DrawFlight(k, start);
        if (!first) Detail::DrawFlight(k, other);
        // 梯井中的扶手
        const double o0 = W - rd - rw, o1 = total - W + rd + rw, i0 = W - rd, i1 = total - W + rd;
        const double yo = -half + s.RailLanding, yb = bottom - s.RailEnd;
        k.Line(o0, yo, o1, yo);                      // 平台端外、内封口
        k.Line(i0, yo - rw, i1, yo - rw);
        if (floor == Detail::StairFloor::Middle)
        {
            // 两侧扶手在梯段端连通，另有一条横跨梯井的起步端线
            k.Line(o0, yo, o0, yb); k.Line(o1, yo, o1, yb); k.Line(o0, yb, o1, yb);
            k.Line(i0, yo - rw, i0, yb + rw); k.Line(i1, yo - rw, i1, yb + rw); k.Line(i0, yb + rw, i1, yb + rw);
            k.Line(W, bottom, total - W, bottom);
        }
        else if (first)
        {
            // 首层：两侧扶手在梯段端各自封口（伸出半个扶手宽）
            const double ye = bottom - rw / 2;
            k.Line(o0, yo, o0, ye); k.Line(i0, yo - rw, i0, ye); k.Line(o0, ye, i0, ye);
            k.Line(o1, yo, o1, ye); k.Line(i1, yo - rw, i1, ye); k.Line(i1, ye, o1, ye);
        }
        else
        {
            // 顶层：扶手沿起步梯段端头延伸到梯间外边
            k.Line(o0, yo, o0, yb); k.Line(o0, yb, total, yb); k.Line(total, yb, total, bottom);
            k.Line(o1, yo, o1, bottom); k.Line(i1, bottom, o1, bottom);
            k.Line(i0, yo - rw, i0, bottom); k.Line(i1, yo - rw, i1, bottom);
            k.Line(i0, bottom, total, bottom);
        }
        if (!first)
        {
            // 平台（下半部的一半）
            k.Line(0, -half, total, -half);
            k.Line(0, -half, 0, 0); k.Line(total, -half, total, 0);
        }
        // 箭头
        const double cv = bottom + (s.BreakSteps - 0.5) * T;
        const double xs = total - W / 2, xo = W / 2;
        if (floor != Detail::StairFloor::Top)
            k.Arrows.push_back({{P{xs, bottom - T / 2}, P{xs, cv - T / 2}}, Detail::kUp});
        if (!first)
        {
            const double endY = floor == Detail::StairFloor::Top ? bottom + T / 2 : cv + T / 2;
            k.Arrows.push_back({{P{xo, bottom - T / 2}, P{xo, 0}, P{xs, 0}, P{xs, endY}}, Detail::kDown});
        }
        // 上半部：关于平台中心 (total / 2, 0) 旋转 180°
        auto turn = [&](const P& p) { return P{total - p.X, -p.Y}; };
        const auto lines = k.G.Lines;
        for (const auto& l : lines) k.G.Lines.push_back({turn(l.A), turn(l.B)});
        const auto arrows = k.Arrows;
        for (const auto& a : arrows)
        {
            Detail::StairSketch::ArrowPath t{{}, a.Text};
            for (const auto& p : a.Points) t.Points.push_back(turn(p));
            k.Arrows.push_back(std::move(t));
        }
        // 起步在左边时左右镜像（未见样例）
        if (s.StartSide == 0)
        {
            for (auto& l : k.G.Lines) { l.A.X = total - l.A.X; l.B.X = total - l.B.X; }
            for (auto& a : k.Arrows) for (auto& p : a.Points) p.X = total - p.X;
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_CROSSSTAIR 交叉楼梯 ──
    struct TchCrossStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：左梯段上端平台外侧左上角
        double StepHeight = 150, Tread = 270;
        double Width = 1200;                   // 梯段宽
        double LeftTop = 1200, RightTop = 1200, RightBottom = 1200;   // 左段上端平台长、右段上端平台长、右段下端平台长
        int    Steps = 20;                     // 每段踏步数（StepSumNum）
        double SpaceWidth = 2560;
        int    Floor = 1, StartSide = 1;
        double RailWidth = 60, RailToSide = 0, RailOut = 0;
        int    BreakSteps = 10;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 梯段宽、3 个 BD 平台长、RC 踏步数、
    // BD 梯间宽、BD 扶手高、RC 楼层、RC 起步位置、RC、BD 扶手宽、BD 扶手距边、BD 扶手伸出、BS 剖断步数、BD 剖断角、BD 字高、
    // BL 标志、3 个 BD、RC
    inline bool DecodeCrossStair(const MiniDWG::UnknownEntity& entity, TchCrossStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CROSSSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_CROSSSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native cross stair version");
        TchCrossStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.Width = r.ReadBitDouble();
        s.LeftTop = r.ReadBitDouble();
        s.RightTop = r.ReadBitDouble();
        s.RightBottom = r.ReadBitDouble();
        s.Steps = r.ReadByte();
        s.SpaceWidth = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailOut = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native cross stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.Width, s.LeftTop, s.RightTop, s.RightBottom, s.SpaceWidth, s.RailWidth,
                         s.RailToSide, s.RailOut, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native cross stair settings");
        if (s.Tread <= 0 || s.Steps < 2 || s.Floor < 0 || s.Floor > 2 || s.Width <= 0)
            return fail("invalid native cross stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对）：两段等长梯段并列，
    // 左段自上端（距基点 LeftTop）起步向下、右段自下端起步向上（上端距基点 RightTop），各自在距起步端 (SectStepNum − 0.5) 级处剖断；
    // 扶手在各段梯井一侧；每段起步端有上行箭头、另一端有下行箭头（都指向剖断线），顶层下行箭头到起步端前一级为止
    inline TchGraphics BuildCrossStair(const TchCrossStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const double T = s.Tread, rw = s.RailWidth, rd = s.RailToSide, W = s.Width;
        const double L = (s.Steps - 1) * T;
        const int brk = floor == Detail::StairFloor::Middle ? 2 : (floor == Detail::StairFloor::First ? 1 : 0);
        Detail::FlightSpec left, right;
        left.Origin = {W, -s.LeftTop}; left.Vx = 0; left.Vy = -1;                       // u → −x（u 0 在梯井一侧）
        right.Origin = {s.SpaceWidth - W, -s.RightTop - L}; right.Vx = 0; right.Vy = 1;  // u → +x（u 0 在梯井一侧）
        for (auto* f : {&left, &right})
        {
            f->Width = W; f->Tread = T; f->Steps = s.Steps; f->BreakSteps = s.BreakSteps; f->BreakAngle = s.BreakAngle;
            f->BreakKind = brk;
            f->RailBands = {{rd, rd + rw}};
        }
        Detail::DrawFlight(k, left);
        Detail::DrawFlight(k, right);
        // 扶手：梯井一侧的矩形
        auto rail = [&](double x0, double x1, double top, double bottom) {
            k.Line(x0, top, x1, top); k.Line(x0, bottom, x1, bottom); k.Line(x0, top, x0, bottom); k.Line(x1, top, x1, bottom);
        };
        rail(W - rd - rw, W - rd, -s.LeftTop + s.RailOut, -s.LeftTop - L - s.RailOut);
        rail(s.SpaceWidth - W + rd, s.SpaceWidth - W + rd + rw, -s.RightTop + s.RailOut, -s.RightTop - L - s.RailOut);
        // 箭头
        const double cv = (s.BreakSteps - 0.5) * T;
        const double xl = W / 2, xr = s.SpaceWidth - W / 2;
        const double lt = -s.LeftTop, lb = lt - L, rt = -s.RightTop, rb = rt - L;
        if (floor != Detail::StairFloor::Top)
        {
            k.Arrows.push_back({{P{xl, lt + T / 2}, P{xl, lt - cv + T / 2}}, Detail::kUp});
            k.Arrows.push_back({{P{xr, rb - T / 2}, P{xr, rb + cv - T / 2}}, Detail::kUp});
        }
        if (floor != Detail::StairFloor::First)
        {
            const bool top = floor == Detail::StairFloor::Top;
            k.Arrows.push_back({{P{xl, lb - T / 2}, P{xl, top ? lt - T : lt - cv - T / 2}}, Detail::kDown});
            k.Arrows.push_back({{P{xr, rt + T / 2}, P{xr, top ? rb + T : rb + cv + T / 2}}, Detail::kDown});
        }
        if (s.StartSide == 0)
        {
            for (auto& l : k.G.Lines) { l.A.X = s.SpaceWidth - l.A.X; l.B.X = s.SpaceWidth - l.B.X; }
            for (auto& a : k.Arrows) for (auto& p : a.Points) p.X = s.SpaceWidth - p.X;
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_TRIANGLESTAIR 三角楼梯 ──
    struct TchTriangleStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：第一跑起步端外侧角点
        double StepHeight = 150, Tread = 270;
        double Width = 1200, Landing = 1200;   // 梯段宽、平台外边长（SlabSize）
        int    Steps = 6;                      // 每跑踏步数
        double SpaceWidth = 300;               // 梯井切角边长
        int    Floor = 1, StartSide = 0;
        double RailWidth = 60, RailToSide = 0, RailOut = 60;   // 扶手中心线在每跑两端伸出 RailOut
        int    BreakSteps = 5;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、BD 梯段宽、BD 平台边长、RC 每跑步数、
    // BD 梯井宽、BD 扶手高、RC 楼层、RC 起步方向、BD 扶手宽、BD 扶手距边、BD 扶手伸出、BS 剖断步数、BD 剖断角、BD 字高、
    // BL 标志、3 个 BD、RC
    inline bool DecodeTriangleStair(const MiniDWG::UnknownEntity& entity, TchTriangleStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_TRIANGLESTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_TRIANGLESTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native triangle stair version");
        TchTriangleStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.Width = r.ReadBitDouble();
        s.Landing = r.ReadBitDouble();
        s.Steps = r.ReadByte();
        s.SpaceWidth = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailOut = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.BreakAngle = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native triangle stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.Width, s.Landing, s.SpaceWidth, s.RailWidth, s.RailToSide, s.RailOut, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native triangle stair settings");
        if (s.Tread <= 0 || s.Steps < 2 || s.Floor < 0 || s.Floor > 2 || s.Width <= 0 || s.SpaceWidth < 0)
            return fail("invalid native triangle stair settings");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对）：三跑梯段围绕切角三角形梯井，
    // 第一跑沿 +y 上行（x 0 ～ 梯段宽），之后每跑依次顺时针转 120°（绕梯井中心）；每跑之后为菱形平台（外边长 SlabSize）；
    // 梯井三角形边长 = 梯段长 + 2 × 梯井宽，中心在 (梯段宽 + 内切圆半径, 梯段长 / 2)；
    // 剖断线放不下时垂直于梯段；扶手中心线在每跑两端伸出 RailOut，转角处以垂直于角平分线的斜线相接
    inline TchGraphics BuildTriangleStair(const TchTriangleStair& s)
    {
        using P = TchGraphics::Point;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const bool first = floor == Detail::StairFloor::First;
        const double T = s.Tread, W = s.Width, L = (s.Steps - 1) * T, sp = s.SpaceWidth, rw = s.RailWidth, rd = s.RailToSide;
        const double side = L + 2 * sp, rin = side / (2 * std::sqrt(3.0));
        const P c{W + rin, L / 2};
        const double c120 = -0.5, s120 = -std::sqrt(3.0) / 2;       // 顺时针 120°
        auto turn = [&](const P& p, int times) {
            P q = p;
            for (int i = 0; i < times; ++i)
            {
                const double dx = q.X - c.X, dy = q.Y - c.Y;
                q = {c.X + dx * c120 - dy * s120, c.Y + dx * s120 + dy * c120};
            }
            return q;
        };
        const double cos30 = std::sqrt(3.0) / 2;
        // 第一跑：带剖断（只有第一跑剖断）与不带剖断两种画法；两端与平台的分界线为整宽
        Detail::FlightSpec f;
        f.Origin = {0, 0}; f.Vx = 0; f.Vy = 1; f.Width = W; f.Tread = T; f.Steps = s.Steps;
        f.RailBands = {{W - rd - rw, W - rd}};
        f.BreakSteps = s.BreakSteps; f.BreakAngle = s.BreakAngle;
        Detail::StairSketch plain, broken, landing, rail;
        Detail::DrawFlight(plain, f);
        f.BreakKind = floor == Detail::StairFloor::Middle ? 2 : (first ? 1 : 0);
        Detail::DrawFlight(broken, f);
        plain.Line(0, 0, W, 0); plain.Line(0, L, W, L);
        if (!first) { broken.Line(0, 0, W, 0); broken.Line(0, L, W, L); }
        // 平台：外边沿 +y 长 SlabSize，再沿 30° 方向到外顶点，然后沿 −30° 方向长 SlabSize 到下一跑起步端外角；内侧为梯井切角
        const P nextOuter = turn({0, 0}, 1), nextInner = turn({W, 0}, 1);
        const P a{0, L + s.Landing};
        const P apex{nextOuter.X - s.Landing * cos30, nextOuter.Y + s.Landing / 2};
        landing.Line({0, L}, a);
        landing.Line(a, apex);
        landing.Line(apex, nextOuter);
        landing.Line({W, L}, nextInner);
        // 扶手：中心线伸出 RailOut，转角斜线与梯井切角平行；内外轮廓各偏半个扶手宽
        const double xc = W - rd - rw / 2;
        const double nx = -0.5, ny = cos30;            // 切角线法向（指向三角形顶点）
        const double vc = nx * xc + ny * (L + s.RailOut);
        // 首层：第一跑扶手在起步端平头封口（伸出 RailOut），最后一跑扶手不再与第一跑相接；
        // 顶层：第一跑扶手自起步端起，最后一跑扶手转角后沿起步端横向连到外边
        Detail::StairSketch railFirst, railLast;
        for (int side = 0; side < 2; ++side)              // 0 内轮廓（靠梯井）、1 外轮廓
        {
            const double off = side == 0 ? rw / 2 : -rw / 2;
            const double x = xc + off, v = vc - off;
            const double yTop = (v - nx * x) / ny, yBottom = L - yTop;
            rail.Line(x, yBottom, x, yTop);
            rail.Line({x, yTop}, turn({x, yBottom}, 1));
            const double y0 = first ? -s.RailOut : (floor == Detail::StairFloor::Top ? 0 : yBottom);
            railFirst.Line(x, y0, x, yTop);
            railFirst.Line({x, yTop}, turn({x, yBottom}, 1));
            railLast.Line(x, yBottom, x, yTop);
            if (floor == Detail::StairFloor::Middle) railLast.Line({x, yTop}, turn({x, yBottom}, 1));
        }
        if (first) railFirst.Line(xc - rw / 2, -s.RailOut, xc + rw / 2, -s.RailOut);
        // 三个单元：第一跑、其后平台与扶手，依次旋转；首层只画带剖断的第一跑与其扶手
        Detail::StairSketch k;
        auto add = [&](const Detail::StairSketch& src, int times) {
            for (const auto& l : src.G.Lines) k.G.Lines.push_back({turn(l.A, times), turn(l.B, times)});
        };
        add(broken, 0);
        add(railFirst, 0);
        add(rail, 1);
        add(railLast, 2);
        if (!first)
            for (int i = 0; i < 3; ++i)
            {
                if (i > 0) add(plain, i);
                add(landing, i);
            }
        if (floor == Detail::StairFloor::Top)
            for (int side = 0; side < 2; ++side)
            {
                // 最后一跑扶手的末端（转回第一跑起步端一侧）沿 x 方向连到梯段外边
                const double off = side == 0 ? rw / 2 : -rw / 2;
                const double x = xc + off, v = vc - off, yTop = (v - nx * x) / ny;
                const P end = turn({x, yTop}, 2);
                k.Line(end, {0, end.Y});
            }
        // 箭头
        const double xm = W / 2, cv = (s.BreakSteps - 0.5) * T;
        if (floor != Detail::StairFloor::Top)
            k.Arrows.push_back({{P{xm, -T / 2}, P{xm, cv - T / 2}}, Detail::kUp});
        if (!first)
        {
            const double endV = floor == Detail::StairFloor::Top ? T / 2 : cv + T / 2;
            k.Arrows.push_back({{turn({xm, L + T / 2}, 2), turn({xm, -T}, 2), turn({xm, L + T}, 1), turn({xm, -T}, 1), P{xm, L + T}, P{xm, endV}},
                                Detail::kDown});
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }

    // ── TCH_RECTCORNERSTAIR 矩形转角楼梯 ──
    struct TchRectCornerStair
    {
        double X = 0, Y = 0, Rotation = 0;     // 基点：第一跑之后平台的外角
        double StepHeight = 150, Tread = 270;
        double Width[4] = {1200, 1200, 1200, 1200};          // 各跑梯段宽
        double SlabAcross[3] = {1440, 1200, 1200};           // 各平台沿下一跑方向的长（SlabSize）
        double SlabAlong[3] = {1440, 1200, 1200};            // 各平台沿上一跑方向的长（SlabWidth）
        int    Steps[4] = {9, 9, 2, 2};
        int    Runs = 2;                                     // 跑数（StairNum）
        int    Floor = 1, StartSide = 0;
        double RailWidth = 60, RailToSide = 0, RailEnd = 60, RailStart = 60;   // 扶手外轮廓在末跑端（RailOutDistDown）、起步端（RailOutDist）伸出
        int    BreakSteps = 5;
        double BreakAngle = std::numbers::pi / 6;
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC、RC 版本（0）、3BD 基点、BD 转角、BD 踏步高、BD 踏步宽、4 个 BD 梯段宽、3 个 BD 平台横向长、
    // 3 个 BD 平台纵向长（SlabWidth）、3 个 BD 平台横向长（SlabSize）、4 个 RC 各跑步数、BD 末端伸出、BD 扶手高、RC 楼层、RC 起步方向、RC 跑数、BD 扶手宽、BD 扶手距边、
    // BD 起步端伸出、BD 剖断角、BS 剖断步数、BD 字高、BL 标志、3 个 BD、RC
    inline bool DecodeRectCornerStair(const MiniDWG::UnknownEntity& entity, TchRectCornerStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_RECTCORNERSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_RECTCORNERSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native rectangular corner stair version");
        TchRectCornerStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        s.StepHeight = r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        for (double& w : s.Width) w = r.ReadBitDouble();
        for (double& w : s.SlabAlong) w = r.ReadBitDouble();
        for (double& w : s.SlabAcross) w = r.ReadBitDouble();
        for (int& n : s.Steps) n = r.ReadByte();
        s.RailEnd = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Floor = r.ReadByte();
        s.StartSide = r.ReadByte();
        s.Runs = r.ReadByte();
        s.RailWidth = r.ReadBitDouble();
        s.RailToSide = r.ReadBitDouble();
        s.RailStart = r.ReadBitDouble();
        s.BreakAngle = r.ReadBitDouble();
        s.BreakSteps = r.ReadBitShort();
        s.TextHeight = r.ReadBitDouble();
        r.ReadBitLong();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native rectangular corner stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.RailWidth, s.RailToSide, s.RailEnd, s.RailStart, s.BreakAngle, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native rectangular corner stair settings");
        for (int i = 0; i < 4; ++i)
            if (!Detail::Finite(s.Width[i]) || s.Width[i] <= 0) return fail("invalid native rectangular corner stair settings");
        for (int i = 0; i < 3; ++i)
            if (!Detail::Finite(s.SlabAcross[i]) || !Detail::Finite(s.SlabAlong[i])) return fail("invalid native rectangular corner stair settings");
        if (s.Tread <= 0 || s.Runs < 2 || s.Runs > 4 || s.Floor < 0 || s.Floor > 2) return fail("invalid native rectangular corner stair settings");
        for (int i = 0; i < s.Runs; ++i)
            if (s.Steps[i] < 2) return fail("invalid native rectangular corner stair steps");
        Detail::ReadStairHandles(entity, base, s.TextStyle, s.ArrowLayer);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（两跑：中间层 / 首层 / 顶层、属性取不同值共 4 个样例核对；三、四跑按同一规律推出，未核对）：
    // 第一跑沿 +y 上行，之后每跑顺时针转 90°；第 i 跑之后的平台沿第 i 跑方向长 SlabWidth、沿下一跑方向长 SlabSize；
    // 扶手沿各跑内侧（梯井侧），中间层在第一跑起步端、末跑末端外伸后封闭成环；
    // 首层为开口折线（起步端伸出 RailOutDistDown、末端伸出半个扶手宽，两端封口）；顶层的封闭边延伸到第一跑外边
    inline TchGraphics BuildRectCornerStair(const TchRectCornerStair& s)
    {
        using P = TchGraphics::Point;
        Detail::StairSketch k;
        const auto floor = static_cast<Detail::StairFloor>(s.Floor);
        const bool first = floor == Detail::StairFloor::First;
        const double T = s.Tread, rw = s.RailWidth, rd = s.RailToSide;
        const int n = std::clamp(s.Runs, 2, 4);
        const P dirs[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
        auto right = [](const P& d) { return P{d.Y, -d.X}; };
        auto add = [](P a, P d, double t) { return P{a.X + d.X * t, a.Y + d.Y * t}; };
        // 各跑起步端外角：第一跑之后平台的外角在原点
        double len[4];
        for (int i = 0; i < 4; ++i) len[i] = (s.Steps[i] - 1) * T;
        P start[4];
        start[0] = {0, -(s.SlabAlong[0] + len[0])};
        for (int i = 0; i + 1 < n; ++i)
        {
            const P end = add(start[i], dirs[i], len[i]);
            const P outer = add(end, dirs[i], s.SlabAlong[i]);
            start[i + 1] = add(outer, dirs[i + 1], s.SlabAcross[i]);
        }
        const int brk = floor == Detail::StairFloor::Middle ? 2 : (first ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            if (first && i > 0) break;
            Detail::FlightSpec f;
            f.Origin = start[i]; f.Vx = dirs[i].X; f.Vy = dirs[i].Y;
            f.Width = s.Width[i]; f.Tread = T; f.Steps = s.Steps[i];
            f.RailBands = {{s.Width[i] - rd - rw, s.Width[i] - rd}};
            f.BreakSteps = s.BreakSteps; f.BreakAngle = s.BreakAngle;
            f.BreakKind = i == 0 ? brk : 0;
            Detail::DrawFlight(k, f);
        }
        if (!first)
            for (int i = 0; i + 1 < n; ++i)
            {
                // 平台：外边两段、两端整宽分界线、内角两段
                const P d = dirs[i], dn = dirs[i + 1], r = right(d), rn = right(dn);
                const P end = add(start[i], d, len[i]), outer = add(end, d, s.SlabAlong[i]);
                k.Line(end, outer);
                k.Line(outer, start[i + 1]);
                const P endIn = add(end, r, s.Width[i]), nextIn = add(start[i + 1], rn, s.Width[i + 1]);
                k.Line(end, endIn);
                k.Line(start[i + 1], nextIn);
                const P corner = add(endIn, d, s.SlabAlong[i] - s.Width[i + 1]);
                k.Line(endIn, corner);
                k.Line(corner, nextIn);
            }
        // 扶手环：内轮廓沿各跑内侧边（距边 RailToSide），在第一跑起步端、末跑末端封闭；外轮廓再向外偏扶手宽、两端各伸出
        auto ring = [&](double inset, double startOut, double endOut) {
            std::vector<P> pts;
            if (first) { startOut = s.RailStart; endOut = rw / 2; }
            if (floor == Detail::StairFloor::Top) startOut = rw / 2;
            // 各跑内侧线：起点 start[i] + r·(Width − inset)，方向 dirs[i]
            auto lineAt = [&](int i) { return add(start[i], right(dirs[i]), s.Width[i] - inset); };
            auto meet = [&](int i) {
                // 第 i 跑与第 i + 1 跑内侧线的交点
                const P a = lineAt(i), b = lineAt(i + 1);
                const P d = dirs[i];
                const double t = d.X != 0 ? (b.X - a.X) / d.X : (b.Y - a.Y) / d.Y;
                return add(a, d, t);
            };
            const P s0 = add(lineAt(0), dirs[0], -startOut);
            const P e = add(lineAt(n - 1), dirs[n - 1], len[n - 1] + endOut);
            pts.push_back(s0);
            for (int i = 0; i + 1 < n; ++i) pts.push_back(meet(i));
            pts.push_back(e);
            if (!first)
            {
                // 封闭：末端沿末跑的右侧方向到第一跑起步端外伸线，中间层回到起点，顶层延伸到第一跑外边
                const P rr = right(dirs[n - 1]);
                const double closeOut = floor == Detail::StairFloor::Top ? (inset > rd ? s.RailStart : std::max(s.RailStart - rw, 0.0)) : startOut;
                const P s1 = add(lineAt(0), dirs[0], -closeOut);
                const double t = rr.X != 0 ? (s1.X - e.X) / rr.X : (s1.Y - e.Y) / rr.Y;
                const P c = add(e, rr, t);
                pts.push_back(c);
                if (floor == Detail::StairFloor::Top) k.Line(c, add(start[0], dirs[0], -closeOut));
                else pts.push_back(s0);
            }
            for (std::size_t j = 0; j + 1 < pts.size(); ++j) k.Line(pts[j], pts[j + 1]);
            return std::pair{pts.front(), first ? pts.back() : pts.front()};
        };
        const auto [i0, i1] = ring(rd, std::max(s.RailStart - rw, 0.0), std::max(s.RailEnd - rw, 0.0));
        const auto [o0, o1] = ring(rd + rw, s.RailStart, s.RailEnd);
        if (floor != Detail::StairFloor::Middle) k.Line(i0, o0);       // 起步端封口
        if (first) k.Line(i1, o1);                                      // 首层末端封口
        // 箭头：上行箭头在第一跑；下行箭头自末跑末端外 T/2 沿各跑中线（平台处取两跑中线交点）到第一跑剖断线中心以上 T/2
        const double cv = (s.BreakSteps - 0.5) * T;
        auto center = [&](int i, double v) { return add(add(start[i], right(dirs[i]), s.Width[i] / 2), dirs[i], v); };
        if (floor != Detail::StairFloor::Top)
            k.Arrows.push_back({{center(0, -T / 2), center(0, cv - T / 2)}, Detail::kUp});
        if (!first)
        {
            std::vector<P> path{center(n - 1, len[n - 1] + T / 2)};
            for (int i = n - 2; i >= 0; --i)
            {
                // 第 i 跑与第 i + 1 跑中线的交点
                const P a = center(i, 0), b = center(i + 1, 0);
                const P d = dirs[i];
                const double t = d.X != 0 ? (b.X - a.X) / d.X : (b.Y - a.Y) / d.Y;
                path.push_back(add(a, d, t));
            }
            path.push_back(center(0, floor == Detail::StairFloor::Top ? T / 2 : cv + T / 2));
            k.Arrows.push_back({std::move(path), Detail::kDown});
        }
        return k.Finish(s.X, s.Y, s.Rotation, s.Scale, s.TextHeight);
    }
}
