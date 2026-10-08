// ── 多段线分段表示测试：参数化、子段、首尾相接、去掉一段、封闭环、作为参数曲线求交 ─────────
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "Core/GeomKernel/Curves.hpp"
#include "Core/GeomKernel/CurveIntersect.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
    bool NearP(const Math::Point3& p, double x, double y, double eps = 1e-9) { return Near(p.x, x, eps) && Near(p.y, y, eps); }
}

int RunPolylineOpsTests()
{
    g_failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    const Polyline L3({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } });          // 两段：0→1 横向，1→2 竖向

    // ── 参数化 ───────────────────────────────────────────────
    Check(Near(PolylineOps::ParamCount(L3), 2.0), "参数范围是 [0, 段数]");
    Check(NearP(PolylineOps::PointAt(L3, 0.5), 5, 0) && NearP(PolylineOps::PointAt(L3, 1.0), 10, 0) && NearP(PolylineOps::PointAt(L3, 1.5), 10, 5) && NearP(PolylineOps::PointAt(L3, 2.0), 10, 10),
          "PointAt：整数是顶点，小数是沿该段的进度");
    Check(NearP(PolylineOps::PointAt(L3, -1), 0, 0) && NearP(PolylineOps::PointAt(L3, 9), 10, 10), "PointAt：超出范围时夹到端点");
    Check(Near(PolylineOps::ParamOf(L3, { 5, 3, 0 }), 0.5) && Near(PolylineOps::ParamOf(L3, { 12, 5, 0 }), 1.5) && Near(PolylineOps::ParamOf(L3, { -3, -2, 0 }), 0.0),
          "ParamOf：投影到最近的段；落在外面取端点");
    const auto tangent = PolylineOps::TangentAt(L3, 1.5);
    Check(Near(tangent.x, 0) && Near(tangent.y, 10), "TangentAt：沿参数增大的方向");

    // ── 圆弧段 ───────────────────────────────────────────────
    const Polyline arc({ { 0, 0, 0 }, { 2, 0, 0 } }, { 1.0 });                // 半圆，向左凸，过 (1,1)
    Check(NearP(PolylineOps::PointAt(arc, 0.5), 1, 1) && NearP(PolylineOps::PointAt(arc, 1.0), 2, 0), "圆弧段：进度按角度，u = 0.5 是弧中点 (1,1)");
    Check(Near(PolylineOps::ParamOf(arc, { 1, 5, 0 }), 0.5, 1e-9), "圆弧段：ParamOf 按角度投影");
    const Polyline half = PolylineOps::Sub(arc, 0.0, 0.5);
    Check(half.Points.size() == 2 && NearP(half.Points[1], 1, 1) && Near(half.Bulges[0], std::tan(kPi / 8), 1e-9), "Sub：半个半圆是 1/4 圆，bulge = tan(π/8)");
    Check(Near(half.Length(), kPi / 2, 1e-9), "Sub：1/4 圆的弧长 π/2");
    const Polyline cw({ { 0, 0, 0 }, { 2, 0, 0 } }, { -1.0 });               // 向右凸，过 (1,-1)
    Check(NearP(PolylineOps::PointAt(cw, 0.5), 1, -1), "负 bulge：向右凸，u = 0.5 是 (1,-1)");
    Check(Near(PolylineOps::Sub(cw, 0.25, 0.75).Bulges[0], -std::tan(kPi / 8), 1e-9), "Sub：负 bulge 的符号保持");

    // ── 子段 / 相接 ──────────────────────────────────────────
    {
        const Polyline sub = PolylineOps::Sub(L3, 0.5, 1.5);
        Check(sub.Points.size() == 3 && NearP(sub.Points[0], 5, 0) && NearP(sub.Points[1], 10, 0) && NearP(sub.Points[2], 10, 5), "Sub：跨段时包含中间的顶点");
        Check(PolylineOps::Sub(L3, 1.0, 1.0).Points.empty() && PolylineOps::Sub(L3, 1.5, 1.2).Points.empty(), "Sub：区间退化返回空");
        const Polyline whole = PolylineOps::Sub(L3, 0.0, 2.0);
        Check(whole.Points.size() == 3 && Near(whole.Length(), 20.0), "Sub：整个范围等于原多段线");

        const Polyline joined = PolylineOps::Join(PolylineOps::Sub(L3, 0.0, 0.5), PolylineOps::Sub(L3, 0.5, 2.0));
        Check(joined.Points.size() == 4 && Near(joined.Length(), 20.0), "Join：两段首尾相接，总长不变");
        Check(joined.Bulges.size() == joined.Points.size() - 1, "Join：bulge 数量与顶点数一致");
    }

    // ── 去掉一段（开放多段线）────────────────────────────────
    {
        const auto pieces = PolylineOps::RemoveBetween(L3, 0.5, 1.5);
        Check(pieces.size() == 2 && Near(pieces[0].Length(), 5.0) && Near(pieces[1].Length(), 5.0), "RemoveBetween：中间去掉一段，剩两段各长 5");
        Check(NearP(pieces[0].Points.back(), 5, 0) && NearP(pieces[1].Points.front(), 10, 5), "RemoveBetween：两段的端点在去掉区间的两端");
        const auto swapped = PolylineOps::RemoveBetween(L3, 1.5, 0.5);
        Check(swapped.size() == 2 && Near(swapped[0].Length(), 5.0), "RemoveBetween：两个参数顺序无关");
        const auto head = PolylineOps::RemoveBetween(L3, 0.0, 1.0);
        Check(head.size() == 1 && Near(head[0].Length(), 10.0) && NearP(head[0].Points.front(), 10, 0), "RemoveBetween：去掉开头，剩后半");
        const auto split = PolylineOps::RemoveBetween(L3, 1.5, 1.5);
        Check(split.size() == 2 && Near(split[0].Length() + split[1].Length(), 20.0), "RemoveBetween：两点重合 = 在该点断开");
        Check(PolylineOps::RemoveBetween(L3, 0.0, 0.0).empty(), "RemoveBetween：在端点处断开没有意义，保持不变");
        Check(PolylineOps::RemoveBetween(L3, 0.0, 2.0).empty(), "RemoveBetween：整条都去掉，不返回任何东西");
    }

    // ── 封闭环 ───────────────────────────────────────────────
    {
        const Polyline sq({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } });
        Check(PolylineOps::IsRing(sq) && !PolylineOps::IsRing(L3), "IsRing：首尾重合才是封闭环");

        const auto a = PolylineOps::RemoveBetween(sq, 1.5, 2.5);
        Check(a.size() == 1 && Near(a[0].Length(), 30.0), "封闭环：去掉一段，剩一条长 30 的开放多段线");
        Check(NearP(a[0].Points.front(), 5, 10) && NearP(a[0].Points.back(), 10, 5) && !PolylineOps::IsRing(a[0]), "封闭环：从去掉区间的终点出发，绕过接缝，到起点结束");

        const auto b = PolylineOps::RemoveBetween(sq, 3.5, 0.5);        // 去掉的区间绕过接缝
        Check(b.size() == 1 && Near(b[0].Length(), 30.0) && NearP(b[0].Points.front(), 5, 0) && NearP(b[0].Points.back(), 0, 5), "封闭环：去掉的区间绕过接缝，剩 [0.5, 3.5]");

        const auto c = PolylineOps::RemoveBetween(sq, 1.5, 1.5);
        Check(c.size() == 1 && Near(c[0].Length(), 40.0) && NearP(c[0].Points.front(), 10, 5) && NearP(c[0].Points.back(), 10, 5), "封闭环：在一点断开，得到从该点出发绕一圈的开放多段线");
    }

    // ── 作为参数曲线 ─────────────────────────────────────────
    {
        PolylineEntity e(1, std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } });
        Check(e.AsCurveEntity() != nullptr, "PolylineEntity 是曲线实体");
        const auto curve = e.AsCurveEntity()->MakeCurve();
        Check(Near(curve->TMin(), 0) && Near(curve->TMax(), 2) && !curve->IsClosed(), "PolylineCurve：参数域 [0, 段数]");
        const LineCurve cut(Line({ 5, -5, 0 }, { 5, 5, 0 }));
        const auto xs = Geom::IntersectCurves(*curve, cut);
        Check(xs.size() == 1 && NearP(xs[0], 5, 0, 1e-6), "与直线求交：一个交点 (5,0)");
        Check(Near(curve->ProjectParam(xs[0]), 0.5, 1e-6), "交点投影回参数：u = 0.5");

        const Polyline wavy({ { 0, 0, 0 }, { 2, 0, 0 } }, { 1.0 });
        PolylineEntity w(2, wavy);
        const auto wc = w.AsCurveEntity()->MakeCurve();
        const LineCurve horizontal(Line({ -1, 0.5, 0 }, { 3, 0.5, 0 }));
        const auto ys = Geom::IntersectCurves(*wc, horizontal);
        Check(ys.size() == 2, "半圆弧段与水平线求交：两个交点");
        if (ys.size() == 2)
        {
            const double u0 = wc->ProjectParam(ys[0]), u1 = wc->ProjectParam(ys[1]);
            const double lo = std::min(u0, u1), hi = std::max(u0, u1);
            Check(Near(lo, 1.0 / 6.0, 1e-3) && Near(hi, 5.0 / 6.0, 1e-3), "弧上交点的参数：角度 30° 和 150° 对应 u = 1/6、5/6");
        }
    }

    // ── 偏移 ─────────────────────────────────────────────────
    {
        const Polyline line({ { 0, 0, 0 }, { 10, 0, 0 } });
        const Polyline o = PolylineOps::Offset(line, 2.0);
        Check(o.Points.size() == 2 && NearP(o.Points[0], 0, 2) && NearP(o.Points[1], 10, 2), "偏移：直线向左（前进方向左侧）偏移 2");
        const Polyline r = PolylineOps::Offset(line, -2.0);
        Check(r.Points.size() == 2 && NearP(r.Points[0], 0, -2), "偏移：负距离偏向右侧");
        Check(PolylineOps::Offset(line, 0.0).Points.empty(), "偏移：距离为 0 不生成");

        const Polyline inside = PolylineOps::Offset(L3, 2.0);                 // 向左 = 拐角内侧
        Check(inside.Points.size() == 3 && NearP(inside.Points[0], 0, 2) && NearP(inside.Points[1], 8, 2) && NearP(inside.Points[2], 8, 10), "偏移：拐角内侧，相邻两段求交成尖角 (8,2)");
        const Polyline outside = PolylineOps::Offset(L3, -2.0);
        Check(outside.Points.size() == 3 && NearP(outside.Points[0], 0, -2) && NearP(outside.Points[1], 12, -2) && NearP(outside.Points[2], 12, 10), "偏移：拐角外侧，尖角在 (12,-2)");
    }
    {
        // 半圆（CW，过 (1,1)，圆心 (1,0)）：向左偏移 0.5 是远离圆心，半径 1.5
        const Polyline out = PolylineOps::Offset(arc, 0.5);
        Check(out.Points.size() == 2 && NearP(out.Points[0], -0.5, 0) && NearP(out.Points[1], 2.5, 0), "偏移：弧段的端点沿半径方向移动");
        Check(NearP(PolylineOps::PointAt(out, 0.5), 1, 1.5, 1e-9) && Near(out.Length(), kPi * 1.5, 1e-9), "偏移：弧段变成半径 1.5 的同心半圆，方向不变");
        Check(PolylineOps::Offset(arc, -2.0).Points.empty(), "偏移：向圆心一侧偏移超过半径，这段没有了");
        const Polyline in = PolylineOps::Offset(arc, -0.5);
        Check(NearP(PolylineOps::PointAt(in, 0.5), 1, 0.5, 1e-9) && Near(in.Length(), kPi * 0.5, 1e-9), "偏移：向圆心一侧偏移 0.5，半径 0.5");
    }
    {
        // 直线 + 圆弧 + 直线：偏移后与圆弧相接的顶点在偏移曲线的交点上
        const Polyline mixed({ { 0, 0, 0 }, { 5, 0, 0 }, { 5, 2, 0 }, { 10, 2, 0 } }, { 0.0, 0.0, 0.0 });
        const Polyline m = PolylineOps::Offset(mixed, 1.0);
        Check(m.Points.size() == 4 && NearP(m.Points[1], 4, 1) && NearP(m.Points[2], 4, 3) && NearP(m.Points[3], 10, 3), "偏移：台阶形多段线向左偏移 1，每个拐角求交");
    }
    {
        const Polyline sq({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } });
        const Polyline in = PolylineOps::Offset(sq, 2.0);
        Check(PolylineOps::IsRing(in) && Near(in.Length(), 24.0), "偏移：封闭环向内偏移 2 得到 6 x 6 的封闭环（周长 24）");
        const Polyline out = PolylineOps::Offset(sq, -2.0);
        Check(PolylineOps::IsRing(out) && Near(out.Length(), 56.0), "偏移：封闭环向外偏移 2 得到 14 x 14 的封闭环（周长 56）");
    }
    {
        // 台阶很短，向台阶内侧偏移得比台阶高度还大：中间那段被挤反，去掉后两条横线相交
        const Polyline step({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 1, 0 }, { 20, 1, 0 } });
        const Polyline o = PolylineOps::Offset(step, -3.0);                   // 向右（朝下）偏移
        Check(o.Points.size() >= 2 && Near(o.Points.front().y, -3.0), "偏移：短台阶偏移后仍得到有效的多段线");
    }

    // ── 移动端点（延伸）─────────────────────────────────────
    {
        const Polyline line({ { 0, 0, 0 }, { 10, 0, 0 } });
        const Polyline e = PolylineOps::MoveEndpoint(line, true, { 15, 0, 0 });
        Check(e.Points.size() == 2 && NearP(e.Points[1], 15, 0), "MoveEndpoint：直线段终点延伸到 (15,0)");
        const Polyline s = PolylineOps::MoveEndpoint(line, false, { -5, 0, 0 });
        Check(NearP(s.Points[0], -5, 0) && NearP(s.Points[1], 10, 0), "MoveEndpoint：直线段起点延伸到 (-5,0)");

        // 半圆（CW 从 (0,0) 过 (1,1) 到 (2,0)，圆心 (1,0)）继续沿圆顺时针延伸 30°：终点移到 (1+cos(-30°), sin(-30°))
        const Math::Point3 p{ 1 + std::cos(-kPi / 6), std::sin(-kPi / 6), 0 };
        const Polyline ext = PolylineOps::MoveEndpoint(arc, true, p);
        Check(NearP(ext.Points[1], p.x, p.y, 1e-9) && Near(ext.Length(), kPi * 7.0 / 6.0, 1e-9), "MoveEndpoint：圆弧终点延伸 30°，包角 210°，半径不变");
        Check(NearP(PolylineOps::PointAt(ext, 0.5), 1 + std::cos(kPi - kPi * 7.0 / 12.0), std::sin(kPi - kPi * 7.0 / 12.0), 1e-9), "MoveEndpoint：延伸后的弧中点仍在同一个圆上");
        // 起点沿圆向外延伸 30°（CW 弧的起点向外是逆时针方向）：新起点在 210° 处
        const Math::Point3 q{ 1 + std::cos(kPi * 7.0 / 6.0), std::sin(kPi * 7.0 / 6.0), 0 };
        const Polyline ext2 = PolylineOps::MoveEndpoint(arc, false, q);
        Check(NearP(ext2.Points[0], q.x, q.y, 1e-9) && NearP(ext2.Points[1], 2, 0, 1e-9) && Near(ext2.Length(), kPi * 7.0 / 6.0, 1e-9), "MoveEndpoint：圆弧起点向外延伸 30°");
    }

    return g_failures;
}
