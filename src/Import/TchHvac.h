#pragma once
#include "Import/TchElec.h"
#include <cmath>
#include <numbers>
#include <string>

// 天正暖通的风管（TCH_DBHVACDUCT，TDbHvacDuct）与风管管件（TCH_DBHFITTING，TDbHvacFitting）。
// 两者与天正电气的桥架、桥架配件同属 TDbPipeBase / TDbFittingBase（tch_PipeBase.arx），数据布局相同，按属性表取几何。
// 显示与天正暖通（AutoCAD 2014，THvac20V6）分解 CK-T 暖通平面图的结果核对：图元都随层；
// 边线在对象自身的图层，中心线、管端线在自身图层名去掉末尾“管”后加“中心线”“管端线”的图层（TchGraphics 的 Layer 1、2）
namespace MiniCAD::Tch
{
    // ── 风管：Start / End 中心线端点，R2 宽度（R3 为高度，平面图不用）──
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

    // 沿中心线、宽 R2 的矩形，另加中心线（Layer 1）；长度为 0 时天正不显示
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

    // ── 风管管件：按属性表中的键区分类型 ──
    // 法兰（有 R22）：Pt0、Pt2 为法兰两面的中心，R2 风管宽度（圆管为直径），R22 法兰边宽（1:100 图面尺寸，按出图比例缩放）；
    // 变径 / 天圆地方（有 R28、Vt6）：Pt0 端宽 R5、Pt2 端宽 R7，I0 / I7 为 1 时 Pt0 / Pt2 端为圆口，端面垂直于 Vt0；
    // 弯头（有 R21、Pt1）：Pt0、Pt2 两端中心，Pt1 两支中心线交点，R5 宽度，R21 中心线半径与宽度之比，R0 两端直段长，
    // I4 ≥ 3 时为分节弯头（1#楼暖通的圆管弯头 I4 = 3：每侧 2 段弦线）；
    // 三通（有 R9、Pt3，I4 为 1）：主管 Pt0（宽 R5）→ Pt2（宽 R7），支管自主管垂直伸到 Pt3，宽 R9。
    // 来回弯、弧形三通（I4 为 5）等其他管件样例很少，未解析
    struct TchHvacFitting
    {
        enum class Kind { Flange, Reducer, Elbow, Tee };
        Kind Type = Kind::Flange;
        MiniDWG::XYZ Pt0, Pt1, Pt2, Pt3;
        double Width0 = 0, Width2 = 0;      // Pt0、Pt2 端宽度（法兰：风管宽度）
        double Width3 = 0;                  // 三通支管宽度 R9
        double Extra = 0;                   // 法兰边宽 R22；弯头直段 R0
        double Ratio = 0;                   // 弯头 R21
        int Pieces = 0;                     // 分节弯头（虾米弯）的边线段数 I4 − 1；0 为圆弧弯头
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
            f.Extra = *rim * (base.Scale > 0 ? base.Scale : 100) / 100;      // 边宽按 1:100 的图面尺寸给出（1#楼 1:50 时减半）
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
            if (const auto style = map.Number("I4"); style && *style >= 3 && *style < 64) f.Pieces = static_cast<int>(*style) - 1;
        }
        else if (map.Find("R9") && map.Point("Pt3"))
        {
            const auto w0 = map.Number("R5"), w2 = map.Number("R7"), w3 = map.Number("R9"), style = map.Number("I4");
            const auto p3 = map.Point("Pt3");
            if (!positive(w0) || !positive(w2) || !positive(w3) || !Detail::FinitePoint(*p3)) return fail("invalid duct tee size");
            if (!style || *style != 1) return fail("only straight-branch duct tees are supported");
            f.Type = TchHvacFitting::Kind::Tee;
            f.Pt3 = *p3;
            f.Width0 = *w0; f.Width2 = *w2; f.Width3 = *w3;
            const double dx = f.Pt2.X - f.Pt0.X, dy = f.Pt2.Y - f.Pt0.Y, len = std::hypot(dx, dy);
            const double along = ((f.Pt3.X - f.Pt0.X) * dx + (f.Pt3.Y - f.Pt0.Y) * dy) / len;
            const double side = std::abs((f.Pt3.X - f.Pt0.X) * dy - (f.Pt3.Y - f.Pt0.Y) * dx) / len;
            if (len < 1e-9 || side <= std::max(f.Width0, f.Width2) / 2 || along - f.Width3 / 2 < 0 || along + f.Width3 / 2 > len)
                return fail("invalid duct tee geometry");
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

    // 法兰：Pt0、Pt2 中点处垂直于 Pt0 → Pt2 的一条线，半长 R2 / 2 + R22（自身图层）。
    // 变径：两端端面垂直于 Vt0，两条斜边（Pt0 ± 半宽 R5 → Pt2 ± 半宽 R7），两端管端线（Layer 2），中心线 Pt0 → Pt2（Layer 1）；
    // 天圆地方（只有一端为圆口）另自圆口中心连到方口两角（CK-T 暖通平面图 55 个变径核对）。
    // 三通：见上方 TchHvacFitting 的说明与函数内注释。
    // 弯头：中心线自 Pt0 直行、沿圆弧转过两支夹角、再直行到 Pt2，两侧边线为其偏移 ± 半宽，两端管端线。
    // 90° 弯头直段长 R0、中心线半径 R21 × 宽度；非 90° 弯头直段只有 0.1，半径由臂长反推（Pt1 按 R21 × 宽度、R0 + 0.1 定位，分解时圆弧放大）
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
        if (f.Type == TchHvacFitting::Kind::Tee)
        {
            // 主管两侧边线 Pt0 ± R5/2 → Pt2 ± R7/2；支管两边线（距支管中心线 ± R9/2）自主管边线伸到 Pt3；
            // 开口处一段圆弧过两个交点与支管中心线上自主管边线向主管内 0.15 × R9 的一点（1#楼暖通 80 个三通）
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
}
