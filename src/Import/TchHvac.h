#pragma once
#include "Import/TchElec.h"
#include "Import/TchPipe.h"
#include "Import/TchSheet.h"
#include <cmath>
#include <numbers>
#include <string>
#include <vector>
 
namespace MiniCAD::Tch
{ 
    struct TchHvacDuct
    {
        MiniDWG::XYZ Start, End;
        double Width = 0;
    };

    inline bool DecodeHvacDuct(const MiniDWG::UnknownEntity& entity, TchHvacDuct& duct, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_DBHVACDUCT") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        TchXDataMap map;
        if (!ReadCableTrayMap(entity, map, diagnostic)) return false;
        const auto start = map.Point("Start"), end = map.Point("End");
        const auto width = map.Number("R2");
        if (!start || !end || !width || !Detail::FinitePoint(*start) || !Detail::FinitePoint(*end) || !(*width > 0) || !Detail::Finite(*width))
            return fail("invalid duct geometry");
        duct = {*start, *end, *width};
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
    inline TchGraphics BuildHvacDuct(const TchHvacDuct& d)
    {
        TchGraphics g;
        const double dx = d.End.X - d.Start.X, dy = d.End.Y - d.Start.Y, len = std::hypot(dx, dy);
        if (len < 1e-9) return g;
        const double nx = -dy / len * d.Width / 2, ny = dx / len * d.Width / 2;
        TchGraphics::Path p;
        for (const auto& q : {TchGraphics::Point{d.Start.X + nx, d.Start.Y + ny}, TchGraphics::Point{d.End.X + nx, d.End.Y + ny},
                              TchGraphics::Point{d.End.X - nx, d.End.Y - ny}, TchGraphics::Point{d.Start.X - nx, d.Start.Y - ny},
                              TchGraphics::Point{d.Start.X + nx, d.Start.Y + ny}})
            p.Points.push_back({q});
        g.Paths.push_back(std::move(p));
        TchGraphics::Path c;
        c.Layer = 1;
        c.Points = {{{d.Start.X, d.Start.Y}}, {{d.End.X, d.End.Y}}};
        g.Paths.push_back(std::move(c));
        return g;
    }
 
    struct TchHvacFitting
    {
        enum class Kind { Flange, Reducer, Elbow, Tee, Cross, Offset };
        Kind Type = Kind::Flange;
        MiniDWG::XYZ Pt0, Pt1, Pt2, Pt3, Pt5;
        double Width0 = 0, Width2 = 0;      // Pt0、Pt2 端宽度（法兰：风管宽度）
        double Width3 = 0, Width5 = 0;      // 三通支管宽度 R9；四通两侧支管 Pt4（存为 Pt3）宽 R11、Pt5 宽 R13
        int TeeStyle = 1;                   // 三通 I4：1 直支管，3 弧形支管，5 Y 形（主管也转弯），7 Y 形而对侧为直边
        double Straight = 50;               // 三通、四通、乙字弯两端的直段 R0
        double Bend = 0;                    // 乙字弯每段圆弧的圆心角 R34
        double Rise = 0;                    // 乙字弯的偏移量 R2
        double Extra = 0;                   // 法兰边宽 R22；弯头直段 R0
        double Ratio = 0;                   // 弯头 R21
        int Pieces = 0;                     // 分节弯头（虾米弯）的边线段数：I5 ≥ 2 时为 I5，否则 I4 − 1；0 为圆弧弯头
        bool RoundStart = false, RoundEnd = false;   // 变径：Pt0（I0）、Pt2（I7）端为圆口
        MiniDWG::XYZ Axis;                  // 变径：端面法向 Vt0（偏心变径的两端中心不在同一轴线上）
    };

    inline bool DecodeHvacFitting(const MiniDWG::UnknownEntity& entity, TchHvacFitting& fitting, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_DBHFITTING") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        TchXDataMap map;
        TchBase base;
        if (!ReadCableFittingMap(entity, map, diagnostic, &base)) return false;
        TchHvacFitting f;
        const auto p0 = map.Point("Pt0"), p1 = map.Point("Pt1"), p2 = map.Point("Pt2");
        if (!p0 || !p2 || !Detail::FinitePoint(*p0) || !Detail::FinitePoint(*p2)) return fail("duct fitting end points are missing");
        f.Pt0 = *p0; f.Pt2 = *p2;
        auto positive = [](std::optional<double> v) { return v && Detail::Finite(*v) && *v > 0; };
        if (map.Find("R22"))
        {
            const auto width = map.Number("R2"), rim = map.Number("R22");
            if (!positive(width) || !rim || !Detail::Finite(*rim) || *rim < 0) return fail("invalid duct flange size");
            f.Type = TchHvacFitting::Kind::Flange;
            f.Width0 = f.Width2 = *width;
            f.Extra = *rim * (base.Scale > 0 ? base.Scale : 100) / 100;      // 边宽按 1:100 的图面尺寸给出（暖通样图 1:50 时减半）
        }
        else if (map.Find("R21") && p1)
        {
            const auto w0 = map.Number("R5"), w2 = map.Number("R7"), ratio = map.Number("R21"), straight = map.Number("R0");
            if (!positive(w0) || !positive(ratio) || !Detail::FinitePoint(*p1)) return fail("invalid duct elbow size");
            if (w2 && std::abs(*w2 - *w0) > 1e-6) return fail("reducing duct elbows are not supported");
            f.Type = TchHvacFitting::Kind::Elbow;
            f.Pt1 = *p1;
            f.Width0 = f.Width2 = *w0;
            f.Ratio = *ratio;
            f.Extra = straight && Detail::Finite(*straight) && *straight > 0 ? *straight : 0;
            if (const auto style = map.Number("I4"); style && *style >= 3 && *style < 64)
            {
                const auto pieces = map.Number("I5");       // I5 ≥ 2 时就是节数
                f.Pieces = pieces && *pieces >= 2 && *pieces < 64 ? static_cast<int>(*pieces) : static_cast<int>(*style) - 1;
            }
        }
        else if (map.Find("R11") && map.Find("R13") && map.Point("Pt4") && map.Point("Pt5"))
        {
            const auto w0 = map.Number("R5"), w2 = map.Number("R7"), w4 = map.Number("R11"), w5 = map.Number("R13");
            const auto p4 = map.Point("Pt4"), p5 = map.Point("Pt5");
            if (!positive(w0) || !positive(w2) || !positive(w4) || !positive(w5) || !Detail::FinitePoint(*p4) || !Detail::FinitePoint(*p5))
                return fail("invalid duct cross size");
            f.Type = TchHvacFitting::Kind::Cross;
            f.Pt3 = *p4; f.Pt5 = *p5;
            f.Width0 = *w0; f.Width2 = *w2; f.Width3 = *w4; f.Width5 = *w5;
            if (const auto r0 = map.Number("R0"); r0 && Detail::Finite(*r0) && *r0 >= 0) f.Straight = *r0;
        }
        else if (map.Find("R9") && map.Point("Pt3"))
        {
            const auto w0 = map.Number("R5"), w2 = map.Number("R7"), w3 = map.Number("R9"), style = map.Number("I4");
            const auto p3 = map.Point("Pt3");
            if (!positive(w0) || !positive(w2) || !positive(w3) || !Detail::FinitePoint(*p3)) return fail("invalid duct tee size");
            if (!style || (*style != 1 && *style != 3 && *style != 5 && *style != 7)) return fail("unsupported duct tee style");
            f.Type = TchHvacFitting::Kind::Tee;
            f.TeeStyle = static_cast<int>(*style);
            if (const auto r0 = map.Number("R0"); r0 && Detail::Finite(*r0) && *r0 >= 0) f.Straight = *r0;
            f.Pt3 = *p3;
            f.Width0 = *w0; f.Width2 = *w2; f.Width3 = *w3;
            const double dx = f.Pt2.X - f.Pt0.X, dy = f.Pt2.Y - f.Pt0.Y, len = std::hypot(dx, dy);
            const double along = ((f.Pt3.X - f.Pt0.X) * dx + (f.Pt3.Y - f.Pt0.Y) * dy) / len;
            const double side = std::abs((f.Pt3.X - f.Pt0.X) * dy - (f.Pt3.Y - f.Pt0.Y) * dx) / len;
            if (len < 1e-9 || side <= std::max(f.Width0, f.Width2) / 2 || along - f.Width3 / 2 < 0 || along + f.Width3 / 2 > len)
                return fail("invalid duct tee geometry");
        }
        else if (map.Find("R28") && map.Find("R34") && !map.Find("Vt6"))
        {
            const auto w = map.Number("R5"), depth = map.Number("R3"), rise = map.Number("R2"), bend = map.Number("R34");
            if (!positive(w) || !positive(rise) || !positive(bend) || *bend >= std::numbers::pi / 2 + 1e-9) return fail("invalid duct offset size");
            f.Type = TchHvacFitting::Kind::Offset;
            f.Width0 = f.Width2 = *w;
            f.Width3 = positive(depth) ? *depth : *w;
            f.Rise = *rise;
            f.Bend = *bend;
            if (const auto r0 = map.Number("R0"); r0 && Detail::Finite(*r0) && *r0 >= 0) f.Straight = *r0;
            if (const auto axis = map.Point("Vt0"); axis && Detail::FinitePoint(*axis) && std::hypot(axis->X, axis->Y) > 1e-9) f.Axis = *axis;
        }
        else if (map.Find("R28") && map.Find("Vt6"))
        {
            const auto w0 = map.Number("R5"), w2 = map.Number("R7"), round0 = map.Number("I0"), round2 = map.Number("I7");
            if (!positive(w0) || !positive(w2)) return fail("invalid duct reducer size");
            if ((round0 && *round0 != 0 && *round0 != 1) || (round2 && *round2 != 0 && *round2 != 1)) return fail("unsupported duct transition type");
            f.Type = TchHvacFitting::Kind::Reducer;
            f.Width0 = *w0; f.Width2 = *w2;
            f.RoundStart = round0 && *round0 == 1;
            f.RoundEnd = round2 && *round2 == 1;
            if (const auto axis = map.Point("Vt0"); axis && Detail::FinitePoint(*axis) && std::hypot(axis->X, axis->Y) > 1e-9) f.Axis = *axis;
        }
        else
            return fail("unsupported duct fitting type");
        if (std::hypot(f.Pt2.X - f.Pt0.X, f.Pt2.Y - f.Pt0.Y) < 1e-9) return fail("degenerate duct fitting");
        fitting = f;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline TchGraphics::Path HvacPath(int layer, std::initializer_list<TchGraphics::PathPoint> points)
        {
            TchGraphics::Path p;
            p.Layer = layer;
            p.Points = points;
            return p;
        }
    }

    
    inline TchGraphics BuildHvacFitting(const TchHvacFitting& f)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        auto unit = [](double x, double y) { const double n = std::hypot(x, y); return P{x / n, y / n}; };
        auto at = [](const MiniDWG::XYZ& o, P d, double t, P n, double s) { return P{o.X + d.X * t + n.X * s, o.Y + d.Y * t + n.Y * s}; };
        const P d = unit(f.Pt2.X - f.Pt0.X, f.Pt2.Y - f.Pt0.Y), n{-d.Y, d.X};
        if (f.Type == TchHvacFitting::Kind::Flange)
        {
            const MiniDWG::XYZ mid{(f.Pt0.X + f.Pt2.X) / 2, (f.Pt0.Y + f.Pt2.Y) / 2, 0};
            const double half = f.Width0 / 2 + f.Extra;
            g.Paths.push_back(Detail::HvacPath(0, {{at(mid, d, 0, n, -half)}, {at(mid, d, 0, n, half)}}));
            return g;
        }
        if (f.Type == TchHvacFitting::Kind::Reducer)
        {
            const double w0 = f.Width0 / 2, w2 = f.Width2 / 2;
            const P axis = std::hypot(f.Axis.X, f.Axis.Y) > 1e-9 ? unit(f.Axis.X, f.Axis.Y) : d, m{-axis.Y, axis.X};
            const P a0 = at(f.Pt0, axis, 0, m, w0), b0 = at(f.Pt0, axis, 0, m, -w0), a2 = at(f.Pt2, axis, 0, m, w2), b2 = at(f.Pt2, axis, 0, m, -w2);
            g.Paths.push_back(Detail::HvacPath(0, {{a0}, {a2}}));
            g.Paths.push_back(Detail::HvacPath(0, {{b0}, {b2}}));
            g.Paths.push_back(Detail::HvacPath(2, {{a0}, {b0}}));
            g.Paths.push_back(Detail::HvacPath(2, {{a2}, {b2}}));
            g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt0.X, f.Pt0.Y}}, {{f.Pt2.X, f.Pt2.Y}}}));
            if (f.RoundEnd && !f.RoundStart)
            {
                g.Paths.push_back(Detail::HvacPath(0, {{{f.Pt2.X, f.Pt2.Y}}, {a0}}));
                g.Paths.push_back(Detail::HvacPath(0, {{{f.Pt2.X, f.Pt2.Y}}, {b0}}));
            }
            else if (f.RoundStart && !f.RoundEnd)
            {
                g.Paths.push_back(Detail::HvacPath(0, {{{f.Pt0.X, f.Pt0.Y}}, {a2}}));
                g.Paths.push_back(Detail::HvacPath(0, {{{f.Pt0.X, f.Pt0.Y}}, {b2}}));
            }
            return g;
        }
        // 圆弧（不超过半圆）的凸度：圆心 c，起点 a 到终点 b 取劣弧
        auto arcBulge = [](P c, P a, P b) {
            const double ax = a.X - c.X, ay = a.Y - c.Y, bx = b.X - c.X, by = b.Y - c.Y, cr = ax * by - ay * bx;
            return std::tan(std::atan2(std::abs(cr), ax * bx + ay * by) / 4) * (cr >= 0 ? 1 : -1);
        };
        if (f.Type == TchHvacFitting::Kind::Offset)
        {
            // 乙字弯：两段圆心角 R34 的反向圆弧，半径 = 偏移 / (2 (1 − cos θ))，两端各有直段 R0；
            // 偏移只在竖向（平面上首尾同轴）时画直管加 Z 形符号
            const P axis = std::hypot(f.Axis.X, f.Axis.Y) > 1e-9 ? unit(-f.Axis.X, -f.Axis.Y) : d, m{-axis.Y, axis.X};
            const double rx = f.Pt2.X - f.Pt0.X, ry = f.Pt2.Y - f.Pt0.Y;
            const double run = rx * axis.X + ry * axis.Y, lateral = rx * m.X + ry * m.Y, dir = run < 0 ? -1.0 : 1.0;
            const P u{axis.X * dir, axis.Y * dir}, v{-u.Y, u.X};
            const double offset = lateral * dir, length = std::abs(run);
            if (std::abs(offset) < 1e-6)
            {
                // Z 形：首段在左侧 −w/3（朝 −Y 的管在 +w/3），末端回勾 L/12、斜 7.5°，升高时向外、降低时向内
                const double w = f.Width3 / 2, k = (u.Y < -1e-6 ? -1.0 : 1.0) * w / 3, tick = length / 12;
                const double lean = (f.Pt2.Z > f.Pt0.Z ? 1.0 : -1.0) * (k > 0 ? 1.0 : -1.0) * tick * std::tan(std::numbers::pi / 24);
                g.Paths.push_back(Detail::HvacPath(0, {{at(f.Pt0, u, 0, v, w)}, {at(f.Pt0, u, length, v, w)}}));
                g.Paths.push_back(Detail::HvacPath(0, {{at(f.Pt0, u, 0, v, -w)}, {at(f.Pt0, u, length, v, -w)}}));
                g.Paths.push_back(Detail::HvacPath(0, {{at(f.Pt0, u, length / 4, v, -k)}, {at(f.Pt0, u, length * 5 / 12, v, -k)},
                                                       {at(f.Pt0, u, length * 7 / 12, v, k)}, {at(f.Pt0, u, length * 3 / 4, v, k)},
                                                       {at(f.Pt0, u, length * 3 / 4 - tick, v, k + lean)}}));
                g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, u, 0, v, w)}, {at(f.Pt0, u, 0, v, -w)}}));
                g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, u, length, v, w)}, {at(f.Pt0, u, length, v, -w)}}));
                g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt0.X, f.Pt0.Y}}, {at(f.Pt0, u, length, v, 0)}}));
                return g;
            }
            const double side = offset > 0 ? 1.0 : -1.0, theta = f.Bend, radius = std::abs(offset) / (2 * (1 - std::cos(theta)));
            const double straight = std::max(0.0, (length - 2 * radius * std::sin(theta)) / 2), w = f.Width0 / 2;
            const MiniDWG::XYZ a{f.Pt0.X + u.X * straight, f.Pt0.Y + u.Y * straight, 0}, b{f.Pt2.X - u.X * straight, f.Pt2.Y - u.Y * straight, 0};
            const P mid{(a.X + b.X) / 2, (a.Y + b.Y) / 2};
            const P t{u.X * std::cos(theta) - u.Y * std::sin(theta) * side, u.Y * std::cos(theta) + u.X * std::sin(theta) * side}, tn{-t.Y, t.X};
            const double k = std::tan(theta / 4) * side;
            auto edge = [&](int layer, double s) {
                return Detail::HvacPath(layer, {{at(f.Pt0, u, 0, v, s)}, {at(a, u, 0, v, s), k}, {P{mid.X + tn.X * s, mid.Y + tn.Y * s}, -k},
                                                {at(b, u, 0, v, s)}, {at(f.Pt2, u, 0, v, s)}});
            };
            g.Paths.push_back(edge(0, w));
            g.Paths.push_back(edge(0, -w));
            g.Paths.push_back(edge(1, 0));
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, u, 0, v, w)}, {at(f.Pt0, u, 0, v, -w)}}));
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt2, u, 0, v, w)}, {at(f.Pt2, u, 0, v, -w)}}));
            return g;
        }
        if ((f.Type == TchHvacFitting::Kind::Tee && f.TeeStyle != 1) || f.Type == TchHvacFitting::Kind::Cross)
        {
            // 弧形支管：主管边线直段 R0 后以 1/4 圆弧转入支管内侧边线（半径 = 支管内侧距 Pt0 − R0）；
            // 支管外侧边线：样式 3 / 四通沿同心圆弧回到主管边线，样式 5 / 7 以 1/4 圆弧转入出口边线（出口前留 R0 直段）；
            // 对侧边线：样式 5 为分别与两段圆弧同心的两段圆弧相交，其余为直边
            const double len = std::hypot(f.Pt2.X - f.Pt0.X, f.Pt2.Y - f.Pt0.Y), r0 = f.Straight, h0 = f.Width0 / 2, h2 = f.Width2 / 2;
            const int style = f.Type == TchHvacFitting::Kind::Cross ? 3 : f.TeeStyle;
            auto local = [&](P b, double t, double s) { return P{f.Pt0.X + d.X * t + b.X * s, f.Pt0.Y + d.Y * t + b.Y * s}; };
            P inner{}; double innerR = 0;
            auto branch = [&](const MiniDWG::XYZ& pt, double width) {
                const double along = (pt.X - f.Pt0.X) * d.X + (pt.Y - f.Pt0.Y) * d.Y;
                const double toward = (pt.X - f.Pt0.X) * n.X + (pt.Y - f.Pt0.Y) * n.Y;
                const P b = toward >= 0 ? n : P{-n.X, -n.Y};
                const double depth = std::abs(toward), h3 = width / 2, rIn = along - h3 - r0;
                const P c1 = local(b, r0, h0 + rIn);
                TchGraphics::Path near;
                near.Points.push_back({local(b, 0, h0)});
                if (rIn > 1e-9)
                {
                    near.Points.push_back({local(b, r0, h0), arcBulge(c1, local(b, r0, h0), local(b, along - h3, h0 + rIn))});
                    near.Points.push_back({local(b, along - h3, h0 + rIn)});
                }
                else
                    near.Points.push_back({local(b, along - h3, h0)});
                near.Points.push_back({local(b, along - h3, depth)});
                g.Paths.push_back(std::move(near));
                TchGraphics::Path far;
                far.Points.push_back({local(b, along + h3, depth)});
                const double r2 = len - r0 - (along + h3);
                if (style == 3 && rIn > 1e-9)
                {
                    const double rOut = rIn + 2 * h3, lift = h0 + rIn - h2;
                    const P from = local(b, along + h3, h0 + rIn);
                    far.Points.push_back({from});
                    if (rOut > std::abs(lift))
                    {
                        const P to = local(b, r0 + std::sqrt(rOut * rOut - lift * lift), h2);
                        far.Points.back().Bulge = arcBulge(c1, from, to);
                        far.Points.push_back({to});
                    }
                }
                else if (style != 3 && r2 > 1e-9)
                {
                    const P c2 = local(b, len - r0, h2 + r2), from = local(b, along + h3, h2 + r2), to = local(b, len - r0, h2);
                    far.Points.push_back({from, arcBulge(c2, from, to)});
                    far.Points.push_back({to});
                }
                far.Points.push_back({local(b, len, h2)});
                g.Paths.push_back(std::move(far));
                g.Paths.push_back(Detail::HvacPath(2, {{local(b, along - h3, depth)}, {local(b, along + h3, depth)}}));
                g.Paths.push_back(Detail::HvacPath(1, {{{pt.X, pt.Y}}, {local(b, along, 0)}}));
                inner = c1; innerR = rIn;
                return b;
            };
            const P b = branch(f.Pt3, f.Width3);
            if (f.Type == TchHvacFitting::Kind::Cross)
                branch(f.Pt5, f.Width5);
            else
            {
                const P o{-b.X, -b.Y};
                const double along = (f.Pt3.X - f.Pt0.X) * d.X + (f.Pt3.Y - f.Pt0.Y) * d.Y, r2 = len - r0 - (along + f.Width3 / 2);
                TchGraphics::Path far;
                far.Points.push_back({local(o, 0, h0)});
                far.Points.push_back({local(o, r0, h0)});
                if (style == 5 && innerR > 1e-9 && r2 > 1e-9)
                {
                    // 两圆交点：圆心 c1（半径 rIn + 2 h0）与 c2（半径 r2 + 2 h2），取远离支管一侧
                    const P c2 = local(b, len - r0, h2 + r2);
                    const double ra = innerR + 2 * h0, rb = r2 + 2 * h2;
                    const double dx = c2.X - inner.X, dy = c2.Y - inner.Y, dist = std::hypot(dx, dy);
                    if (dist > 1e-9 && dist < ra + rb && dist > std::abs(ra - rb))
                    {
                        const double t = (ra * ra - rb * rb + dist * dist) / (2 * dist), hgt = std::sqrt(std::max(0.0, ra * ra - t * t));
                        const P base{inner.X + dx / dist * t, inner.Y + dy / dist * t};
                        P x1{base.X - dy / dist * hgt, base.Y + dx / dist * hgt}, x2{base.X + dy / dist * hgt, base.Y - dx / dist * hgt};
                        auto away = [&](P q) { return (q.X - f.Pt0.X) * o.X + (q.Y - f.Pt0.Y) * o.Y; };
                        const P x = away(x1) > away(x2) ? x1 : x2;
                        far.Points.back().Bulge = arcBulge(inner, local(o, r0, h0), x);
                        far.Points.push_back({x, arcBulge(c2, x, local(o, len - r0, h2))});
                    }
                    far.Points.push_back({local(o, len - r0, h2)});
                }
                else if (style == 7)
                    far.Points.push_back({local(o, len - r0, h2)});
                far.Points.push_back({local(o, len, h2)});
                g.Paths.push_back(std::move(far));
            }
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, d, 0, n, h0)}, {at(f.Pt0, d, 0, n, -h0)}}));
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt2, d, 0, n, h2)}, {at(f.Pt2, d, 0, n, -h2)}}));
            g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt0.X, f.Pt0.Y}}, {{f.Pt2.X, f.Pt2.Y}}}));
            return g;
        }
        if (f.Type == TchHvacFitting::Kind::Tee)
        {
            // 主管两侧边线 Pt0 ± R5/2 → Pt2 ± R7/2；支管两边线（距支管中心线 ± R9/2）自主管边线伸到 Pt3；
            // 开口处一段圆弧过两个交点与支管中心线上自主管边线向主管内 0.15 × R9 的一点（暖通样图 80 个三通）
            const double len = std::hypot(f.Pt2.X - f.Pt0.X, f.Pt2.Y - f.Pt0.Y);
            const double along = ((f.Pt3.X - f.Pt0.X) * d.X + (f.Pt3.Y - f.Pt0.Y) * d.Y);
            const double toward = ((f.Pt3.X - f.Pt0.X) * n.X + (f.Pt3.Y - f.Pt0.Y) * n.Y) > 0 ? 1.0 : -1.0;
            const P b{n.X * toward, n.Y * toward};                     // 支管方向
            const double w0 = f.Width0 / 2, w2 = f.Width2 / 2, w3 = f.Width3 / 2;
            auto edge = [&](double t) { return w0 + (w2 - w0) * t / len; };    // 主管在 t 处的半宽
            const P c{f.Pt0.X + d.X * along, f.Pt0.Y + d.Y * along};    // 支管中心线与主管轴线的交点
            const P ia = at(f.Pt0, d, along - w3, b, edge(along - w3)), ib = at(f.Pt0, d, along + w3, b, edge(along + w3));
            const P ea = at(f.Pt3, d, -w3, b, 0), eb = at(f.Pt3, d, w3, b, 0);
            g.Paths.push_back(Detail::HvacPath(0, {{at(f.Pt0, d, 0, b, -w0)}, {at(f.Pt2, d, 0, b, -w2)}}));
            g.Paths.push_back(Detail::HvacPath(0, {{at(f.Pt0, d, 0, b, w0)}, {ia}}));
            g.Paths.push_back(Detail::HvacPath(0, {{ib}, {at(f.Pt2, d, 0, b, w2)}}));
            g.Paths.push_back(Detail::HvacPath(0, {{ia}, {ea}}));
            g.Paths.push_back(Detail::HvacPath(0, {{ib}, {eb}}));
            // 开口圆弧：A → B 过 M，凸度 = tan((π − ∠AMB) / 2)，M 在 A → B 右侧时为正
            const P m = at(f.Pt0, d, along, b, edge(along) - 0.15 * f.Width3);
            const double ax = ia.X - m.X, ay = ia.Y - m.Y, bx = ib.X - m.X, by = ib.Y - m.Y;
            const double angle = std::atan2(std::abs(ax * by - ay * bx), ax * bx + ay * by);
            const double right = (ib.X - ia.X) * (m.Y - ia.Y) - (ib.Y - ia.Y) * (m.X - ia.X);
            g.Paths.push_back(Detail::HvacPath(0, {{ia, std::tan((std::numbers::pi - angle) / 2) * (right < 0 ? 1 : -1)}, {ib}}));
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, d, 0, b, w0)}, {at(f.Pt0, d, 0, b, -w0)}}));
            g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt2, d, 0, b, w2)}, {at(f.Pt2, d, 0, b, -w2)}}));
            g.Paths.push_back(Detail::HvacPath(2, {{ea}, {eb}}));
            g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt0.X, f.Pt0.Y}}, {c}}));
            g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt2.X, f.Pt2.Y}}, {c}}));
            g.Paths.push_back(Detail::HvacPath(1, {{{f.Pt3.X, f.Pt3.Y}}, {c}}));
            return g;
        }
        // 弯头
        const double l0 = std::hypot(f.Pt1.X - f.Pt0.X, f.Pt1.Y - f.Pt0.Y), l2 = std::hypot(f.Pt2.X - f.Pt1.X, f.Pt2.Y - f.Pt1.Y);
        if (l0 < 1e-9 || l2 < 1e-9) return g;
        const P u = unit(f.Pt1.X - f.Pt0.X, f.Pt1.Y - f.Pt0.Y), v = unit(f.Pt2.X - f.Pt1.X, f.Pt2.Y - f.Pt1.Y);
        const double cross = u.X * v.Y - u.Y * v.X, turn = std::atan2(std::abs(cross), u.X * v.X + u.Y * v.Y);
        if (turn < 1e-6 || turn > std::numbers::pi - 1e-6) return g;
        const double straight = std::abs(turn - std::numbers::pi / 2) < 1e-4 ? f.Extra : 0.1;
        const double tangent = std::max(0.0, l0 - straight);
        const double w = f.Width0 / 2, bulge = std::tan(turn / 4) * (cross > 0 ? 1 : -1);
        const P nu{-u.Y, u.X}, nv{-v.Y, v.X};
        const MiniDWG::XYZ a{f.Pt1.X - u.X * tangent, f.Pt1.Y - u.Y * tangent, 0}, b{f.Pt1.X + v.X * tangent, f.Pt1.Y + v.Y * tangent, 0};
        auto edge = [&](int layer, double s) {
            return Detail::HvacPath(layer, {{at(f.Pt0, u, 0, nu, s)}, {at(a, u, 0, nu, s), bulge}, {at(b, v, 0, nv, s)}, {at(f.Pt2, v, 0, nv, s)}});
        };
        if (f.Pieces > 0)
        {
            // 分节弯头：边线为圆弧上均分点之间的弦，节间分隔线（管端线图层）沿半径方向；中心线仍为圆弧
            const double side = cross > 0 ? 1.0 : -1.0, radius = tangent / std::tan(turn / 2);
            const P center{a.X + nu.X * side * radius, a.Y + nu.Y * side * radius};
            auto onArc = [&](int k, double s) {     // 第 k 个分点，偏离中心线 s（左侧为正）
                const double phi = side * turn * k / f.Pieces;
                const double rx = a.X - center.X, ry = a.Y - center.Y, c = std::cos(phi), sn = std::sin(phi);
                const P q{center.X + rx * c - ry * sn, center.Y + rx * sn + ry * c};
                const double len = std::hypot(q.X - center.X, q.Y - center.Y), k2 = (len - side * s) / len;
                return P{center.X + (q.X - center.X) * k2, center.Y + (q.Y - center.Y) * k2};
            };
            for (const double s : {w, -w})
            {
                TchGraphics::Path p;
                p.Points.push_back({at(f.Pt0, u, 0, nu, s)});
                for (int k = 0; k <= f.Pieces; ++k) p.Points.push_back({onArc(k, s)});
                p.Points.push_back({at(f.Pt2, v, 0, nv, s)});
                g.Paths.push_back(std::move(p));
            }
            for (int k = 1; k < f.Pieces; ++k) g.Paths.push_back(Detail::HvacPath(2, {{onArc(k, w)}, {onArc(k, -w)}}));
        }
        else
        {
            g.Paths.push_back(edge(0, w));
            g.Paths.push_back(edge(0, -w));
        }
        g.Paths.push_back(edge(1, 0));
        g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt0, u, 0, nu, w)}, {at(f.Pt0, u, 0, nu, -w)}}));
        g.Paths.push_back(Detail::HvacPath(2, {{at(f.Pt2, v, 0, nv, w)}, {at(f.Pt2, v, 0, nv, -w)}}));
        return g;
    }

    // 风管标注 TCH_HVACDIMENSION：TCH基类 + BS 版本 1 + 属性表。S1 为以“|”分隔的显示文字（S0 是生成它的模板），
    // Pt6 标注点、Pt7 引线折点、R24 字高、R25 方向、I4 样式（0 单行文字，4 引线加两行两列的表格）、Id1 文字样式
    struct TchHvacDimension
    {
        MiniDWG::XYZ Anchor, Corner;
        double Angle = 0, Height = 3, Scale = 100;
        int Style = 0;
        std::vector<std::string> Values;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
    };

    inline bool DecodeHvacDimension(const MiniDWG::UnknownEntity& entity, TchHvacDimension& dim, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        if (entity.Raw.DxfName != "TCH_HVACDIMENSION") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        const auto& data = entity.Raw.Dwg;
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for duct dimension");
        if (Detail::IsCipherObject(data)) return fail("encrypted duct dimensions are not supported");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
        if (r.ReadBitShort() != 1) return fail("unsupported duct dimension version");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        TchXDataMap map;
        int handles = base.HandleCount;
        if (!Detail::ReadXDataMap(r, text, map, handles)) return fail("invalid duct dimension property map");
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native duct dimension fields do not consume the payload exactly");
        TchHvacDimension d;
        const auto anchor = map.Point("Pt6"), corner = map.Point("Pt7");
        if (!anchor || !Detail::FinitePoint(*anchor)) return fail("duct dimension point is missing");
        d.Anchor = *anchor;
        d.Corner = corner && Detail::FinitePoint(*corner) ? *corner : *anchor;
        if (const auto a = map.Number("R25"); a && Detail::Finite(*a)) d.Angle = *a;
        if (const auto h = map.Number("R24"); h && Detail::Finite(*h) && *h > 0) d.Height = *h;
        d.Scale = base.Scale > 0 ? base.Scale : 100;
        if (const auto s = map.Number("I4")) d.Style = static_cast<int>(*s);
        if (d.Style != 0 && d.Style != 4) return fail("unsupported duct dimension style");
        const std::string all = Codec::DecodeUnicodeEscapes(map.Text("S1"));
        for (std::size_t from = 0;;)
        {
            const auto bar = all.find('|', from);
            d.Values.push_back(all.substr(from, bar == std::string::npos ? std::string::npos : bar - from));
            if (bar == std::string::npos) break;
            from = bar + 1;
        }
        d.Values.resize(4);
        if (const auto* v = map.Find("Id1")) d.TextStyle = Detail::HandleAt(entity, *v);
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 单行：文字中心在 Pt6，方向 R25 转正。表格：引线 Pt6 → Pt7，表格左上 / 左下两行以 Pt7 所在水平线为中线、行高 1.6 倍字高，
    // 按 cos R25 的正负伸向 Pt7 的右侧或左侧；列宽为该列最宽文字加两侧各 0.3 倍字高，文字居中
    inline TchGraphics BuildHvacDimension(const TchHvacDimension& d, const TchTextStyleRule& rule)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        auto text = [&](double x, double y, double rotation, const std::string& value) {
            if (!value.empty())
                g.Texts.push_back({{x, y}, d.Height * d.Scale * rule.HeightFactor(), rotation, value, 5, 0, 256, 0, rule.WidthFactor()});
        };
        if (d.Style == 0)
        {
            text(d.Anchor.X, d.Anchor.Y, Detail::Upright(d.Angle), d.Values[0]);
            return g;
        }
        const double h = d.Height * d.Scale, row = 1.6 * h, pad = 0.3 * h;
        // 天正按字形实际宽度排表格；这里用估算的步进，gbenor 等再乘样式宽度因子
        const double k = !rule.TrueType && !rule.Symbol() ? rule.StyleWidth : 1.0;
        auto width = [&](int a, int b) {
            return std::max(Detail::SheetTextWidth(d.Values[a], h, rule), Detail::SheetTextWidth(d.Values[b], h, rule)) * k + 2 * pad;
        };
        const double c1 = width(0, 2), c2 = width(1, 3), total = c1 + c2;
        const double left = std::cos(d.Angle) < 0 ? d.Corner.X - total : d.Corner.X, mid = left + c1, right = left + total;
        const double y = d.Corner.Y, top = y + row, bottom = y - row;
        g.Lines.push_back({{d.Anchor.X, d.Anchor.Y}, {d.Corner.X, d.Corner.Y}});
        g.Lines.push_back({{left, y}, {right, y}});
        TchGraphics::Path box;
        for (const P& q : {P{left, y}, P{left, top}, P{right, top}, P{right, bottom}, P{left, bottom}, P{left, y}}) box.Points.push_back({q});
        g.Paths.push_back(std::move(box));
        g.Lines.push_back({{mid, top}, {mid, bottom}});
        text((left + mid) / 2, y + row / 2, 0, d.Values[0]);
        text((mid + right) / 2, y + row / 2, 0, d.Values[1]);
        text((left + mid) / 2, y - row / 2, 0, d.Values[2]);
        text((mid + right) / 2, y - row / 2, 0, d.Values[3]);
        return g;
    }
}
