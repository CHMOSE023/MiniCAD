// ── 面域测试：面积 / 周长（直线、圆弧段、圆、椭圆、孔）、由对象生成（含线段接环）、绘制、移动、序列化 ──
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Document/Command/EntityTranslate.h"
#include "Document/RegionBuilder.hpp"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace MiniCAD;

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

    class LineCount : public IDrawSink
    {
    public:
        int lines = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
    };

    HatchLoop Rect(double x0, double y0, double x1, double y1, bool ccw = true)
    {
        std::vector<Math::Point3> p = { { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, 0 } };
        if (!ccw) std::reverse(p.begin(), p.end());
        return HatchLoop::FromPolyline(Polyline(p));
    }

    HatchLoop Circ(double cx, double cy, double r)
    {
        HatchLoop lp;
        lp.Edges.push_back(HatchEdge::MakeEllipseArc(Ellipse({ cx, cy, 0 }, r, r), 0.0, 2 * kPi));
        return lp;
    }
}

int RunRegionTests()
{
    g_failures = 0;
    double a, p;

    // ── 面积 / 周长 ────────────────────────────────────────────────────
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 5) }, a, p);
    Check(Near(a, 50) && Near(p, 30), "矩形：面积 50，周长 30");
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 5, false) }, a, p);
    Check(Near(a, 50), "顺时针的环面积也为正");

    LoopMeasure::Measure(std::vector<HatchLoop>{ Circ(3, 4, 5) }, a, p);
    Check(Near(a, kPi * 25, 1e-9) && Near(p, 2 * kPi * 5, 0.02), "圆：面积 πr² 精确，周长 2πr");

    {
        HatchLoop e;
        e.Edges.push_back(HatchEdge::MakeEllipseArc(Ellipse({ 1, 2, 0 }, 6, 3, 0.7), 0.0, 2 * kPi));
        LoopMeasure::Measure(std::vector<HatchLoop>{ e }, a, p);
        Check(Near(a, kPi * 6 * 3, 1e-9), "旋转椭圆：面积 πab 精确");
    }
    {
        // 半圆：直径 (0,0)→(10,0) 的下半圆弧（bulge=1，凸向行进方向右侧），再用直线闭合
        HatchLoop lp = HatchLoop::FromPolyline(Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 0, 0, 0 } }, { -1.0, 0.0 }));
        LoopMeasure::Measure(std::vector<HatchLoop>{ lp }, a, p);
        Check(Near(a, kPi * 25 / 2, 1e-9) && Near(p, kPi * 5 + 10, 1e-9), "bulge 半圆：面积 πr²/2，周长 πr + 直径");
    }
    {
        // 10×10 逆时针正方形，右边（自下向上）改成半圆弧：bulge = +1 凸向行进方向右侧（外），-1 凸向内
        Polyline out({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }, { 0.0, 1.0, 0.0, 0.0 });
        LoopMeasure::Measure(std::vector<HatchLoop>{ HatchLoop::FromPolyline(out) }, a, p);
        Check(Near(a, 100 + kPi * 25 / 2, 1e-9), "bulge=+1 向外鼓：面积加上半圆");
        Polyline in = out;
        in.Bulges[1] = -1.0;
        LoopMeasure::Measure(std::vector<HatchLoop>{ HatchLoop::FromPolyline(in) }, a, p);
        Check(Near(a, 100 - kPi * 25 / 2, 1e-9), "bulge=-1 向内凹：面积减去半圆");
    }

    // ── 孔与多个岛 ─────────────────────────────────────────────────────
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 10), Rect(2, 2, 4, 4) }, a, p);
    Check(Near(a, 100 - 4) && Near(p, 40 + 8), "矩形里挖矩形孔：面积 96，周长含孔 48");
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 10), Circ(5, 5, 2) }, a, p);
    Check(Near(a, 100 - kPi * 4, 1e-9), "矩形里挖圆孔：面积 100 − 4π");
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 10), Rect(2, 2, 8, 8), Rect(4, 4, 6, 6) }, a, p);
    Check(Near(a, 100 - 36 + 4), "三层嵌套：外 + 内 − 中，岛中岛重新计入");
    LoopMeasure::Measure(std::vector<HatchLoop>{ Rect(0, 0, 10, 10), Rect(20, 0, 25, 5) }, a, p);
    Check(Near(a, 125), "两个互不包含的环：面积相加");

    // ── 由对象生成 ─────────────────────────────────────────────────────
    {
        CircleEntity circle(1, { 0, 0, 0 }, 3.0);
        RectangleEntity rect(2, { 10, 10, 0 }, { 14, 12, 0 });
        PolylineEntity closedPl(3, std::vector<Math::Point3>{ { 20, 0, 0 }, { 24, 0, 0 }, { 24, 3, 0 }, { 20, 3, 0 }, { 20, 0, 0 } });
        EllipseEntity arcOnly(4, Ellipse({ 0, 0, 0 }, 5, 3, 0.0, 0.0, kPi));     // 半个椭圆弧：忽略
        auto r = RegionBuilder::Build({ &circle, &rect, &closedPl, &arcOnly });
        Check(r.Loops.size() == 3 && r.Used == 3 && r.Skipped == 1, "圆 / 矩形 / 封闭多段线成环，椭圆弧被忽略");
    }
    {
        // 四条首尾相接的直线（乱序、方向不一）→ 一个矩形环
        LineEntity l1(1, { 0, 0, 0 }, { 10, 0, 0 });
        LineEntity l2(2, { 10, 5, 0 }, { 10, 0, 0 });       // 反向
        LineEntity l3(3, { 10, 5, 0 }, { 0, 5, 0 });
        LineEntity l4(4, { 0, 5, 0 }, { 0, 0, 0 });
        auto r = RegionBuilder::Build({ &l1, &l2, &l3, &l4 });
        Check(r.Loops.size() == 1 && r.Used == 4 && r.Skipped == 0, "四条线接成一个环");
        LoopMeasure::Measure(r.Loops, a, p);
        Check(Near(a, 50) && Near(p, 30), "接环后的面积 50、周长 30");
    }
    {
        // D 形：直线 + 半圆弧
        LineEntity base(1, { 0, 0, 0 }, { 10, 0, 0 });
        ArcEntity  arc(2, { 5, 0, 0 }, 5.0, 0.0, kPi);       // 上半圆，逆时针，从 (10,0) 到 (0,0)
        auto r = RegionBuilder::Build({ &base, &arc });
        Check(r.Loops.size() == 1, "直线 + 圆弧接成 D 形");
        LoopMeasure::Measure(r.Loops, a, p);
        Check(Near(a, kPi * 25 / 2, 1e-9) && Near(p, kPi * 5 + 10, 1e-9), "D 形：面积 πr²/2，周长 πr + 直径");
    }
    {
        // 三条线接不成环：全部忽略
        LineEntity l1(1, { 0, 0, 0 }, { 10, 0, 0 });
        LineEntity l2(2, { 10, 0, 0 }, { 10, 5, 0 });
        LineEntity l3(3, { 10, 5, 0 }, { 0, 5, 0 });
        auto r = RegionBuilder::Build({ &l1, &l2, &l3 });
        Check(r.Loops.empty() && r.Skipped == 3, "缺一边的线段接不成环，全部计入忽略");
    }
    {
        CircleEntity circle(1, { 0, 0, 0 }, 2.0);
        double ar, pe;
        Check(RegionMeasure::Measure(circle, ar, pe) && Near(ar, kPi * 4, 1e-9), "AREA 查询：圆");
        LineEntity line(2, { 0, 0, 0 }, { 1, 1, 0 });
        Check(!RegionMeasure::Measure(line, ar, pe), "AREA 查询：直线不能围成区域");
    }

    // ── 分组：嵌套的归一个面域，互不包含 / 互相交叠的各成一个 ───────────────
    {
        auto groups = RegionBuilder::GroupIntoRegions({ Rect(0, 0, 10, 10), Rect(5, 5, 15, 15) });
        Check(groups.size() == 2, "两个互相交叠的环：各成一个面域");
        groups = RegionBuilder::GroupIntoRegions({ Rect(0, 0, 10, 10), Rect(20, 0, 30, 10) });
        Check(groups.size() == 2, "两个分离的环：各成一个面域");
        groups = RegionBuilder::GroupIntoRegions({ Rect(2, 2, 4, 4), Rect(0, 0, 10, 10) });
        Check(groups.size() == 1 && groups[0].size() == 2, "外环 + 孔：一个面域（与输入顺序无关）");
        groups = RegionBuilder::GroupIntoRegions({ Rect(0, 0, 10, 10), Rect(2, 2, 8, 8), Rect(4, 4, 6, 6) });
        Check(groups.size() == 2, "三层嵌套：外环 + 孔 为一个面域，孔里的岛为另一个");
        size_t sizes[2] = { groups[0].size(), groups[1].size() };
        Check((sizes[0] == 2 && sizes[1] == 1) || (sizes[0] == 1 && sizes[1] == 2), "三层嵌套的分组大小为 2 和 1");
        groups = RegionBuilder::GroupIntoRegions({ Rect(0, 0, 10, 10), Rect(2, 2, 4, 4), Rect(6, 6, 8, 8) });
        Check(groups.size() == 1 && groups[0].size() == 3, "一个外环里两个孔：一个面域");
    }

    // ── 实体 ───────────────────────────────────────────────────────────
    RegionEntity region(1, { Rect(0, 0, 10, 10), Rect(2, 2, 4, 4) });
    Check(Near(region.Area(), 96) && Near(region.Perimeter(), 48), "实体面积 / 周长");

    LineCount sink;
    region.Draw(sink, false, false);
    Check(sink.lines == 8, "线框显示：两个环共 8 条边，闭合环不重复首点");

    TranslateEntityInPlace(region, { 5, 5, 0 });
    const auto bb = region.GetBoundingBox();
    Check(Near(bb.Min.x, 5) && Near(bb.Max.y, 15) && Near(region.Area(), 96), "沿用填充的移动：平移后面积不变");

    auto clone = region.Clone(2);
    Check(clone && clone->IsKindOf<RegionEntity>() && Near(static_cast<RegionEntity*>(clone.get())->Area(), 96), "克隆保持类型与边界");

    region.SetShowFill(true);
    JsonSerializer w;
    EntityIO::Write(w, region);
    JsonSerializer rd;
    auto back = rd.Parse(w.Dump()) ? EntityIO::Read(rd) : nullptr;
    Check(back && back->IsKindOf<RegionEntity>(), "序列化往返保持 RegionEntity 类型（而不是 HatchEntity）");
    if (back && back->IsKindOf<RegionEntity>())
    {
        auto* b = static_cast<RegionEntity*>(back.get());
        Check(Near(b->Area(), 96) && b->GetShowFill() && b->GetLoops().size() == 2, "往返：边界、孔、填充开关一致");
    }

    return g_failures;
}
