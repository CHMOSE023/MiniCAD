// ── 面域布尔运算测试：并 / 交 / 差、孔、相切 / 重合、圆的精确保留、多个操作数 ─────────────
#include "Core/Entity/RegionEntity.hpp"
#include "Document/RegionBoolean.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace MiniCAD;
using Op = RegionBoolean::Op;

namespace
{
    int g_failures = 0;
    const double kPi = 3.14159265358979323846;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }

    HatchLoop Poly(std::vector<Math::Point3> p)
    {
        p.push_back(p.front());
        return HatchLoop::FromPolyline(Polyline(std::move(p)));
    }
    HatchLoop Rect(double x0, double y0, double x1, double y1) { return Poly({ { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 } }); }
    HatchLoop Circ(double cx, double cy, double r)
    {
        HatchLoop lp;
        lp.Edges.push_back(HatchEdge::MakeEllipseArc(Ellipse({ cx, cy, 0 }, r, r), 0.0, 2 * kPi));
        return lp;
    }

    double Area(const std::vector<HatchLoop>& loops)
    {
        double a, p;
        LoopMeasure::Measure(loops, a, p);
        return a;
    }

    size_t VertexCount(const HatchLoop& l) { return l.Edges.size() == 1 ? l.Edges[0].Poly.Points.size() : 0; }
    bool HasExactCircle(const std::vector<HatchLoop>& loops)
    {
        for (const auto& l : loops)
            if (l.Edges.size() == 1 && l.Edges[0].Type == HatchEdge::Kind::EllipseArc) return true;
        return false;
    }
}

int RunRegionBooleanTests()
{
    g_failures = 0;
    using L = std::vector<HatchLoop>;

    // ── 两个交叠的正方形 ───────────────────────────────────────────────
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(5, 5, 15, 15) };
        const auto u = RegionBoolean::Apply(a, b, Op::Union);
        const auto i = RegionBoolean::Apply(a, b, Op::Intersect);
        const auto s = RegionBoolean::Apply(a, b, Op::Subtract);
        Check(u.size() == 1 && Near(Area(u), 175) && VertexCount(u[0]) == 9, "并集：一个环（8 个顶点 + 闭合点），面积 175");
        Check(i.size() == 1 && Near(Area(i), 25) && VertexCount(i[0]) == 5, "交集：面积 25，一个正方形");
        Check(s.size() == 1 && Near(Area(s), 75) && VertexCount(s[0]) == 7, "差集 A−B：L 形，面积 75");
        Check(Near(Area(RegionBoolean::Apply(b, a, Op::Subtract)), 75), "差集 B−A：面积 75");
    }

    // ── 旋转 45° 的菱形 ────────────────────────────────────────────────
    {
        const L a{ Rect(0, 0, 10, 10) };
        const L b{ Poly({ { 5, -3, 0 }, { 13, 5, 0 }, { 5, 13, 0 }, { -3, 5, 0 } }) };
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Intersect)), 92), "正方形 ∩ 菱形：面积 92（切掉四个角）");
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Union)), 136), "正方形 ∪ 菱形：面积 136");
    }

    // ── 不相交 ─────────────────────────────────────────────────────────
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(20, 0, 25, 5) };
        const auto u = RegionBoolean::Apply(a, b, Op::Union);
        Check(u.size() == 2 && Near(Area(u), 125), "不相交的并集：两个环原样保留");
        Check(RegionBoolean::Apply(a, b, Op::Intersect).empty(), "不相交的交集为空");
        const auto s = RegionBoolean::Apply(a, b, Op::Subtract);
        Check(s.size() == 1 && Near(Area(s), 100), "不相交的差集：A 不变");
    }

    // ── 一个在另一个里面（产生孔）──────────────────────────────────────
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(2, 2, 4, 4) };
        const auto s = RegionBoolean::Apply(a, b, Op::Subtract);
        Check(s.size() == 2 && Near(Area(s), 96), "A−B（B 在 A 内）：外环 + 孔，面积 96");
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Union)), 100), "B 在 A 内的并集：就是 A");
        const auto i = RegionBoolean::Apply(a, b, Op::Intersect);
        Check(i.size() == 1 && Near(Area(i), 4), "B 在 A 内的交集：就是 B");
        Check(RegionBoolean::Apply(b, a, Op::Subtract).empty(), "B−A（B 被 A 完全盖住）为空");

        // 带孔的区域再减去一个落在孔里的块：孔里的块不在区域里，不影响
        const auto ring = s;
        Check(Near(Area(RegionBoolean::Apply(ring, L{ Rect(2.5, 2.5, 3.5, 3.5) }, Op::Subtract)), 96), "减去落在孔里的块：面积不变");
        Check(Near(Area(RegionBoolean::Apply(ring, L{ Rect(2.5, 2.5, 3.5, 3.5) }, Op::Union)), 97), "并上落在孔里的块：面积 + 1（岛中岛）");
    }

    // ── 相切 / 重合 ────────────────────────────────────────────────────
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(10, 0, 20, 10) };           // 共用一条边
        const auto u = RegionBoolean::Apply(a, b, Op::Union);
        Check(u.size() == 1 && Near(Area(u), 200) && VertexCount(u[0]) == 5, "共边的并集：合成一个矩形，共线顶点被合并");
        const auto i = RegionBoolean::Apply(a, b, Op::Intersect);
        Check(i.empty() || Near(Area(i), 0.0), "共边的交集没有面积");
    }
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(0, 0, 10, 10) };            // 完全重合
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Union)), 100), "重合的并集：面积 100");
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Intersect)), 100), "重合的交集：面积 100");
        Check(RegionBoolean::Apply(a, b, Op::Subtract).empty(), "重合的差集为空");
    }
    {
        const L a{ Rect(0, 0, 10, 10) }, b{ Rect(0, 0, 5, 10) };             // 共用两条边的一部分
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Subtract)), 50), "半个矩形的差集：面积 50");
        Check(Near(Area(RegionBoolean::Apply(a, b, Op::Union)), 100), "半个矩形的并集：面积 100");
    }

    // ── 圆 ─────────────────────────────────────────────────────────────
    {
        const L c1{ Circ(0, 0, 5) }, c2{ Circ(6, 0, 5) };
        const double lens = 50.0 * std::acos(0.6) - 3.0 * 8.0;                 // 两圆交叠的透镜面积
        const auto i = RegionBoolean::Apply(c1, c2, Op::Intersect);
        const auto u = RegionBoolean::Apply(c1, c2, Op::Union);
        Check(i.size() == 1 && Near(Area(i), lens, 0.05), "两圆交集（透镜）：面积与解析值一致（离散误差 < 0.05）");
        Check(u.size() == 1 && Near(Area(u), 2 * kPi * 25 - lens, 0.2), "两圆并集：面积与解析值一致");
        Check(Near(Area(RegionBoolean::Apply(c1, c2, Op::Subtract)), kPi * 25 - lens, 0.2), "两圆差集：面积与解析值一致");
    }
    {
        // 圆完全在矩形里：圆没有被切开，保留精确的椭圆弧边，不会变成折线
        const L r{ Rect(-10, -10, 10, 10) }, c{ Circ(0, 0, 3) };
        const auto s = RegionBoolean::Apply(r, c, Op::Subtract);
        Check(s.size() == 2 && HasExactCircle(s) && Near(Area(s), 400 - kPi * 9, 1e-9), "矩形挖圆孔：圆孔保持精确的圆，面积 400 − 9π");
        const auto i = RegionBoolean::Apply(r, c, Op::Intersect);
        Check(i.size() == 1 && HasExactCircle(i) && Near(Area(i), kPi * 9, 1e-9), "矩形 ∩ 里面的圆：结果仍是精确的圆");
    }

    // ── 多个操作数 ─────────────────────────────────────────────────────
    {
        const std::vector<L> ops = { { Rect(0, 0, 10, 10) }, { Rect(8, 0, 18, 10) }, { Rect(16, 0, 26, 10) } };
        Check(Near(Area(RegionBoolean::Combine(ops, Op::Union)), 260), "三个依次交叠的矩形并集：宽 26，面积 260");
        Check(Near(Area(RegionBoolean::Combine(ops, Op::Subtract)), 80), "依次减去后两个：面积 80");
        Check(RegionBoolean::Combine(ops, Op::Intersect).empty(), "三个矩形没有共同部分：交集为空");
    }

    return g_failures;
}
