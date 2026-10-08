// ── 圆角测试：直线 / 圆弧 / 圆的圆角，半径 0，保留点选一侧，非法情况 ─────────────────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Document/Command/EntityFillet.h"
#include <cmath>
#include <cstdio>
#include <memory>

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

    const Line& L(const std::unique_ptr<Entity>& e) { return static_cast<const LineEntity&>(*e).GetLine(); }
}

int RunFilletTests()
{
    g_failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    // ── 直线与直线 ───────────────────────────────────────────
    {
        LineEntity a(1, { 0, 0, 0 }, { 10, 0, 0 }), b(2, { 0, 0, 0 }, { 0, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(a, { 5, 0.1, 0 }, b, { 0.1, 5, 0 }, 2.0, r), "直角：圆角成功");
        Check(r.first && r.second && r.arc, "两个对象都被修改，并生成圆角弧");
        Check(Near(L(r.first).Start.x, 2) && Near(L(r.first).End.x, 10), "第一条线：靠近点选位置的一端保留，另一端缩到切点 (2,0)");
        Check(Near(L(r.second).Start.y, 2) && Near(L(r.second).End.y, 10), "第二条线：缩到切点 (0,2)");
        const Arc& arc = r.arc->GetArc();
        Check(Near(arc.Center.x, 2) && Near(arc.Center.y, 2) && Near(arc.Radius, 2.0), "圆角弧：圆心 (2,2)，半径 2");
        Check(Near(arc.SweepAngle(), kPi / 2), "圆角弧：包角 90°");
        const auto mid = arc.MidPoint();
        Check(mid.x < 2 && mid.y < 2, "圆角弧：凸向直角的顶点一侧（取较短的弧）");
        Check(r.first->GetID() == 1 && r.second->GetID() == 2, "修改后的对象保持原 ID");
    }
    {
        // 两条线没有相交（有缺口）：圆角会把它们延伸到切点
        LineEntity a(3, { 6, 0, 0 }, { 10, 0, 0 }), b(4, { 0, 6, 0 }, { 0, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(a, { 8, 0, 0 }, b, { 0, 8, 0 }, 2.0, r), "有缺口的两条线：圆角成功");
        Check(Near(L(r.first).Start.x, 2) && Near(L(r.second).Start.y, 2), "两条线都被延伸到切点");
    }
    {
        // 十字相交：点选哪一侧就保留哪一侧
        LineEntity a(5, { -5, 0, 0 }, { 10, 0, 0 }), b(6, { 0, -5, 0 }, { 0, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(a, { 6, 0, 0 }, b, { 0, 6, 0 }, 2.0, r), "十字相交：圆角成功");
        Check(Near(L(r.first).End.x, 10) && Near(L(r.first).Start.x, 2) && Near(L(r.second).End.y, 10) && Near(L(r.second).Start.y, 2),
              "十字相交：保留点选的右 / 上两侧，左 / 下两段被去掉");
        FilletResult r2;
        ComputeFillet(a, { -4, 0, 0 }, b, { 0, -4, 0 }, 2.0, r2);
        Check(Near(L(r2.first).Start.x, -5) && Near(L(r2.first).End.x, -2) && Near(L(r2.second).End.y, -2), "十字相交：点选左 / 下两侧则保留左 / 下");
        FilletResult r3;
        ComputeFillet(a, { 6, 0, 0 }, b, { 0, -4, 0 }, 2.0, r3);
        Check(Near(r3.arc->GetArc().Center.x, 2) && Near(r3.arc->GetArc().Center.y, -2), "十字相交：一右一下，圆角落在右下象限");
    }

    // ── 半径 0 ───────────────────────────────────────────────
    {
        LineEntity a(7, { 0, 0, 0 }, { 5, 0, 0 }), b(8, { 8, -3, 0 }, { 8, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(a, { 2, 0, 0 }, b, { 8, 5, 0 }, 0.0, r) && !r.arc, "半径 0：成功且不生成圆弧");
        Check(Near(L(r.first).End.x, 8) && Near(L(r.first).Start.x, 0), "半径 0：第一条线延伸到交点");
        Check(Near(L(r.second).Start.y, 0) && Near(L(r.second).End.y, 10), "半径 0：第二条线修剪到交点，保留点选的一侧");
    }
    {
        CircleEntity c(9, { 0, 0, 0 }, 5.0);
        LineEntity l(10, { 0, 0, 0 }, { 1, 0, 0 });
        FilletResult r;
        Check(!ComputeFillet(c, { 5, 0, 0 }, l, { 0.5, 0, 0 }, 0.0, r), "半径 0 只支持两条直线");
    }

    // ── 非法情况 ─────────────────────────────────────────────
    {
        LineEntity a(11, { 0, 0, 0 }, { 10, 0, 0 }), b(12, { 0, 5, 0 }, { 10, 5, 0 });
        FilletResult r;
        Check(!ComputeFillet(a, { 5, 0, 0 }, b, { 5, 5, 0 }, 1.0, r) && !r.arc, "两条平行线：失败");
        Check(!ComputeFillet(a, { 5, 0, 0 }, a, { 6, 0, 0 }, 1.0, r), "同一个对象：失败");
        LineEntity c(13, { 0, 0, 0 }, { 0, 10, 0 });
        Check(!ComputeFillet(a, { 5, 0, 0 }, c, { 0, 5, 0 }, -1.0, r), "半径为负：失败");
        PointEntity p(14, { 0, 0, 0 });
        Check(!ComputeFillet(a, { 5, 0, 0 }, p, { 0, 0, 0 }, 1.0, r), "不支持的类型（点）：失败");
        LineEntity zero(15, { 1, 1, 0 }, { 1, 1, 0 });
        Check(!ComputeFillet(a, { 5, 0, 0 }, zero, { 1, 1, 0 }, 1.0, r), "零长度的线：失败");
    }

    // ── 直线与圆 ─────────────────────────────────────────────
    {
        CircleEntity c(20, { 0, 0, 0 }, 5.0);
        LineEntity l(21, { 6, -10, 0 }, { 6, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(c, { 4.9, 3.0, 0 }, l, { 6, 6.0, 0 }, 1.0, r), "圆与直线：圆角成功");
        Check(!r.first, "圆不被修剪");
        Check(r.second && Near(L(r.second).Start.y, std::sqrt(11.0), 1e-9) && Near(L(r.second).End.y, 10) && Near(L(r.second).Start.x, 6), "直线缩到切点，保留点选的一侧");
        Check(Near(r.arc->GetArc().Center.x, 5, 1e-9) && Near(r.arc->GetArc().Center.y, std::sqrt(11.0), 1e-9) && Near(r.arc->GetArc().Radius, 1.0), "圆角弧：圆心 (5, √11)，与圆外切、与直线相切");
        FilletResult lower;
        ComputeFillet(c, { 4.9, -3.0, 0 }, l, { 6, -6.0, 0 }, 1.0, lower);
        Check(Near(lower.arc->GetArc().Center.y, -std::sqrt(11.0), 1e-9), "点选下半部分：圆角落在下方");
    }
    {
        // 半径太大：圆与线之间放不下
        CircleEntity c(22, { 0, 0, 0 }, 5.0);
        LineEntity l(23, { 20, -10, 0 }, { 20, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(c, { 5, 0, 0 }, l, { 20, 0, 0 }, 1.0, r) == false, "间隙大于 2 倍半径的圆与线，没有外切圆角：失败");
    }

    // ── 圆弧与直线 ───────────────────────────────────────────
    {
        ArcEntity a(30, { 0, 0, 0 }, 5.0, 0.0, kPi / 2);
        LineEntity l(31, { 8, -10, 0 }, { 8, 10, 0 });
        FilletResult r;
        Check(ComputeFillet(a, { 4.5, 2.18, 0 }, l, { 8, 8.0, 0 }, 2.0, r), "圆弧与直线：圆角成功");
        const double expectEnd = std::atan2(std::sqrt(13.0), 6.0);
        const Arc& na = static_cast<const ArcEntity&>(*r.first).GetArc();
        Check(Near(na.StartAngle, 0.0) && Near(na.EndAngle, expectEnd, 1e-9), "圆弧：保留靠近点选位置的起点，终点缩到切点所在角度");
        Check(Near(L(r.second).Start.x, 8) && Near(L(r.second).Start.y, std::sqrt(13.0), 1e-9), "直线：缩到切点");
    }

    return g_failures;
}
