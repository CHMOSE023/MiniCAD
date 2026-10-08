// ── 缩放测试：各类实体按基点等比缩放，尺寸类参数同步缩放，方向 / 角度不变 ──────────────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Document/Command/EntityScale.h"
#include <cmath>
#include <cstdio>
#include <memory>
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

int RunScaleTests()
{
    g_failures = 0;
    const Math::Point3 base{ 10, 10, 0 };

    {
        LineEntity e(1, { 10, 10, 0 }, { 14, 13, 0 });
        Check(ScaleEntityInPlace(e, base, 2.0), "直线：缩放成功");
        const Line& l = e.GetLine();
        Check(Near(l.Start.x, 10) && Near(l.Start.y, 10) && Near(l.End.x, 18) && Near(l.End.y, 16), "直线：基点上的端点不动，另一端离基点远 2 倍");
    }
    {
        LineEntity e(2, { 0, 0, 0 }, { 10, 0, 0 });
        ScaleEntityInPlace(e, { 5, 0, 0 }, 0.5);
        Check(Near(e.GetLine().Start.x, 2.5) && Near(e.GetLine().End.x, 7.5), "直线：以中点为基点缩小一半，两端向中点靠拢");
    }
    {
        CircleEntity e(3, { 12, 10, 0 }, 3.0);
        ScaleEntityInPlace(e, base, 3.0);
        Check(Near(e.GetCircle().Center.x, 16) && Near(e.GetCircle().Radius, 9.0), "圆：圆心按基点缩放，半径同比放大");
    }
    {
        ArcEntity e(4, { 12, 10, 0 }, 2.0, 0.3, 1.2);
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetArc().Radius, 4.0) && Near(e.GetArc().StartAngle, 0.3) && Near(e.GetArc().EndAngle, 1.2), "圆弧：半径放大，起止角不变");
    }
    {
        EllipseEntity e(5, { 12, 10, 0 }, 5.0, 3.0, 0.5);
        ScaleEntityInPlace(e, base, 2.0);
        const Ellipse& el = e.GetEllipse();
        Check(Near(el.RadiusX, 10) && Near(el.RadiusY, 6) && Near(el.Rotation, 0.5) && Near(el.Center.x, 14), "椭圆：两个半轴放大，旋转角不变");
    }
    {
        RectangleEntity e(6, { 10, 10, 0 }, { 14, 13, 0 });
        ScaleEntityInPlace(e, base, 2.0);
        const Rectangle& r = e.GetRectangle();
        Check(Near(r.P1.x, 10) && Near(r.P3.x, 18) && Near(r.P3.y, 16), "矩形：四个角点按基点缩放");
    }
    {
        PolylineEntity e(7, { { 10, 10, 0 }, { 12, 10, 0 }, { 12, 12, 0 } }, { 0.0, 0.5 });
        e.SetWidth(1.0);
        ScaleEntityInPlace(e, base, 2.0);
        const auto& pl = e.GetPolyline();
        Check(Near(pl.Points[2].x, 14) && Near(pl.Points[2].y, 14) && Near(pl.Bulges[1], 0.5) && Near(e.GetWidth(), 2.0),
              "多段线：顶点缩放、bulge 不变、线宽同比放大");
    }
    {
        TextEntity e(8, { 12, 10, 0 }, "abc", 2.5f, 0.4f);
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetPosition().x, 14) && Near(e.GetHeight(), 5.0) && Near(e.GetRotation(), 0.4f), "文字：位置缩放、字高放大、旋转不变");
    }
    {
        MTextEntity e(9, 0, "abc", { 12, 10, 0 }, 2.5, 0.0, 30.0);
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetHeight(), 5.0) && Near(e.GetBoxWidth(), 60.0) && Near(e.GetPosition().x, 14), "多行文字：字高、框宽同比放大");
    }
    {
        TableEntity e(10, { 10, 10, 0 }, std::vector<double>{ 10, 20 }, std::vector<double>{ 5, 5 }, 2.0);
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.ColWidths()[1], 40) && Near(e.RowHeights()[0], 10) && Near(e.GetHeight(), 4.0), "表格：行高列宽字高一起放大");
    }
    {
        InsertEntity e(11, 1, "BLK", { 12, 10, 0 });
        e.SetScale({ 2.0, -1.5, 1.0 });
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetPosition().x, 14) && Near(e.GetScale().x, 4.0) && Near(e.GetScale().y, -3.0), "块插入：位置缩放，缩放系数相乘（保留镜像符号）");
    }
    {
        RayEntity e(12, { 12, 10, 0 }, { 0, 3, 0 });
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetOrigin().x, 14) && Near(e.GetDirection().y, 3.0), "射线：基点缩放，方向不变");
    }
    {
        HatchLoop loop = HatchLoop::FromPolyline(Polyline({ { 10, 10, 0 }, { 20, 10, 0 }, { 20, 15, 0 }, { 10, 15, 0 }, { 10, 10, 0 } }));
        HatchEntity h(13, std::vector<HatchLoop>{ loop });
        RegionEntity r(14, std::vector<HatchLoop>{ loop });
        ScaleEntityInPlace(h, base, 2.0);
        ScaleEntityInPlace(r, base, 2.0);
        Check(Near(h.GetScale(), 2.0), "填充：图案比例同步放大");
        Check(Near(r.Area(), 50.0 * 4.0) && Near(r.Perimeter(), 30.0 * 2.0), "面域：面积放大 k² 倍，周长放大 k 倍");
    }
    {
        MLineEntity e(15, { { 10, 10, 0 }, { 20, 10, 0 } }, MLineStyleRecord{});
        ScaleEntityInPlace(e, base, 3.0);
        Check(Near(e.GetVertices()[1].x, 40) && Near(e.GetScale(), 3.0), "多线：顶点缩放，多线比例相乘");
    }
    {
        DimensionEntity e(16, { 10, 10, 0 }, { 20, 10, 0 }, { 15, 14, 0 });
        ScaleEntityInPlace(e, base, 2.0);
        Check(Near(e.GetP2().x, 30) && Near(e.GetDimLinePoint().y, 18), "标注：几何点缩放");
    }

    // ── 非法比例 ─────────────────────────────────────────────
    {
        LineEntity e(20, { 0, 0, 0 }, { 1, 0, 0 });
        Check(!ScaleEntityInPlace(e, base, 0.0) && !ScaleEntityInPlace(e, base, -2.0) && !ScaleEntityInPlace(e, base, std::nan("")),
              "比例 ≤ 0 或不是数：拒绝");
        Check(Near(e.GetLine().End.x, 1.0), "拒绝时实体不被修改");
    }

    return g_failures;
}
