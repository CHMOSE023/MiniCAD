// ── 拉伸测试：窗口内的定义点位移，窗口外不动；圆弧端点拉伸保持包角 ───────────────────
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Document/Command/EntityStretch.h"
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

    bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int RunStretchTests()
{
    g_failures = 0;
    constexpr double kPi = 3.14159265358979323846;
    // 窗口 x∈[8,20]，y∈[-5,5]：只框住 x 方向较大的一侧
    const StretchWindow win = StretchWindow::FromCorners({ 20, 5, 0 }, { 8, -5, 0 });
    Check(Near(win.MinX, 8) && Near(win.MaxX, 20) && Near(win.MinY, -5) && Near(win.MaxY, 5), "窗口：两个对角点任意顺序都规范化");

    {
        LineEntity e(1, { 0, 0, 0 }, { 10, 0, 0 });
        Check(StretchEntityInPlace(e, win, 5, 3), "直线：一端在窗口内，拉伸成功");
        const Line& l = e.GetLine();
        Check(Near(l.Start.x, 0) && Near(l.Start.y, 0) && Near(l.End.x, 15) && Near(l.End.y, 3), "直线：窗口外的端点不动，窗口内的端点位移");
    }
    {
        LineEntity e(2, { 10, 0, 0 }, { 15, 2, 0 });
        StretchEntityInPlace(e, win, 5, 3);
        Check(Near(e.GetLine().Start.x, 15) && Near(e.GetLine().End.x, 20) && Near(e.GetLine().End.y, 5), "直线：两端都在窗口内 = 整体平移");
    }
    {
        LineEntity e(3, { 0, 0, 0 }, { 5, 0, 0 });
        Check(!StretchEntityInPlace(e, win, 5, 3) && Near(e.GetLine().End.x, 5), "直线：没有端点在窗口内，不变，返回 false");
    }
    {
        LineEntity e(4, { 10, 0, 0 }, { 15, 0, 0 });
        Check(!StretchEntityInPlace(e, win, 0, 0), "位移为零：不修改，返回 false");
    }
    {
        // 窗口边界上的点算在窗口内
        LineEntity e(5, { 0, 0, 0 }, { 8, 0, 0 });
        StretchEntityInPlace(e, win, 2, 0);
        Check(Near(e.GetLine().End.x, 10), "边界上的端点算在窗口内");
    }

    {
        PointEntity in(10, { 10, 0, 0 }), out(11, { 0, 0, 0 });
        Check(StretchEntityInPlace(in, win, 1, 1) && Near(in.GetPoint().Position.x, 11), "点：在窗口内则位移");
        Check(!StretchEntityInPlace(out, win, 1, 1), "点：在窗口外不变");
    }
    {
        CircleEntity in(12, { 10, 0, 0 }, 3), out(13, { 0, 0, 0 }, 30);
        Check(StretchEntityInPlace(in, win, 4, 0) && Near(in.GetCircle().Center.x, 14) && Near(in.GetCircle().Radius, 3.0), "圆：圆心在窗口内则整体平移，半径不变");
        Check(!StretchEntityInPlace(out, win, 4, 0), "圆：圆心在窗口外，即使圆周穿过窗口也不变");
    }

    // ── 圆弧 ─────────────────────────────────────────────────
    {
        // 半圆：圆心 (0,0)，半径 10，从 0° 到 180°；起点 (10,0) 在窗口内，终点 (-10,0) 在窗口外
        ArcEntity e(20, { 0, 0, 0 }, 10.0, 0.0, kPi);
        const Arc before = e.GetArc();
        Check(StretchEntityInPlace(e, win, 0, 6), "圆弧：一个端点在窗口内，拉伸成功");
        const Arc& a = e.GetArc();
        const auto s = a.StartPoint(), t = a.EndPoint();
        // FromThreePoints 保证弧仍从原起点走到原终点；检查两个端点：一个不动一个位移
        const bool endsOk = (Near(s.x, -10) && Near(s.y, 0) && Near(t.x, 10) && Near(t.y, 6))
                         || (Near(t.x, -10) && Near(t.y, 0) && Near(s.x, 10) && Near(s.y, 6));
        Check(endsOk, "圆弧：窗口外的端点不动，窗口内的端点位移");
        Check(Near(a.SweepAngle(), before.SweepAngle()) || Near(a.SweepAngle(), 2 * kPi - before.SweepAngle()), "圆弧：包角保持不变");
    }
    {
        ArcEntity e(21, { 0, 0, 0 }, 10.0, 1.2, 1.8);
        Check(!StretchEntityInPlace(e, win, 3, 3), "圆弧：窗口内没有圆心和端点，不变");
    }
    {
        ArcEntity e(22, { 12, 0, 0 }, 3.0, 0.5, 1.0);
        StretchEntityInPlace(e, win, 4, 0);
        Check(Near(e.GetArc().Center.x, 16) && Near(e.GetArc().Radius, 3.0), "圆弧：圆心在窗口内 = 整体平移");
    }

    // ── 矩形 / 多段线 ────────────────────────────────────────
    {
        RectangleEntity e(30, { 0, -2, 0 }, { 10, 2, 0 });     // 右边两个角点 (10,-2) (10,2) 在窗口内
        StretchEntityInPlace(e, win, 5, 0);
        const Rectangle& r = e.GetRectangle();
        Check(Near(r.P1.x, 0) && Near(r.P2.x, 15) && Near(r.P3.x, 15) && Near(r.P4.x, 0), "矩形：右侧两个角点位移，矩形被拉长");
    }
    {
        PolylineEntity e(31, { { 0, 0, 0 }, { 10, 0, 0 }, { 12, 3, 0 }, { 30, 3, 0 } }, { 0.0, 0.0, 0.0 });
        StretchEntityInPlace(e, win, 0, 2);
        const auto& pts = e.GetPolyline().Points;
        Check(Near(pts[0].y, 0) && Near(pts[1].y, 2) && Near(pts[2].y, 5) && Near(pts[3].y, 3), "多段线：只有窗口内的顶点位移，窗口外的不动");
    }

    // ── 文字 / 块插入 / 标注 ─────────────────────────────────
    {
        TextEntity in(40, { 10, 0, 0 }, "a"), out(41, { 0, 0, 0 }, "b");
        Check(StretchEntityInPlace(in, win, 2, 0) && Near(in.GetPosition().x, 12) && !StretchEntityInPlace(out, win, 2, 0), "文字：插入点在窗口内才平移");
    }
    {
        InsertEntity in(42, 1, "B", { 10, 0, 0 });
        in.SetScale({ 2, 2, 2 });
        Check(StretchEntityInPlace(in, win, 2, 0) && Near(in.GetPosition().x, 12) && Near(in.GetScale().x, 2.0), "块插入：插入点在窗口内平移，缩放不变");
    }
    {
        DimensionEntity e(43, { 0, 0, 0 }, { 10, 0, 0 }, { 5, 4, 0 });
        StretchEntityInPlace(e, win, 6, 0);
        Check(Near(e.GetP1().x, 0) && Near(e.GetP2().x, 16) && Near(e.GetDimLinePoint().x, 5), "标注：只有窗口内的定义点位移（测量值随之变化）");
    }

    // ── 填充：整个包围盒在窗口内才平移 ────────────────────────
    {
        auto makeHatch = [](double x0, double x1)
        {
            return HatchLoop::FromPolyline(Polyline({ { x0, 0, 0 }, { x1, 0, 0 }, { x1, 3, 0 }, { x0, 3, 0 }, { x0, 0, 0 } }));
        };
        HatchEntity inside(50, std::vector<HatchLoop>{ makeHatch(10, 15) });
        HatchEntity straddle(51, std::vector<HatchLoop>{ makeHatch(5, 15) });
        Check(StretchEntityInPlace(inside, win, 2, 0) && Near(inside.GetBoundingBox().Min.x, 12), "填充：包围盒整个在窗口内，整体平移");
        Check(!StretchEntityInPlace(straddle, win, 2, 0) && Near(straddle.GetBoundingBox().Min.x, 5), "填充：只部分在窗口内，不拉伸（避免边界变形）");
    }

    return g_failures;
}
