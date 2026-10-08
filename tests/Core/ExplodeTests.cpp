// ── 分解测试：多段线 → 直线 / 圆弧，矩形 → 4 条直线，块插入 → 变换后的块内对象 ─────────────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Document/Command/EntityExplode.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
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

    const Line& L(const std::vector<std::unique_ptr<Entity>>& v, size_t i) { return static_cast<const LineEntity&>(*v[i]).GetLine(); }
}

int RunExplodeTests()
{
    g_failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    // ── 多段线 ───────────────────────────────────────────────
    {
        PolylineEntity pl(1, { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 } }, { 0.0, 0.0 });
        pl.GetAttr().LayerId = 3;
        std::vector<std::unique_ptr<Entity>> out;
        Check(ExplodeEntity(pl, out) && out.size() == 2, "多段线：两段直线分解成 2 条直线");
        Check(out[0]->IsKindOf<LineEntity>() && Near(L(out, 0).End.x, 10) && Near(L(out, 1).End.y, 5), "多段线：顶点依次相连");
        Check(out[0]->GetAttr().LayerId == 3 && out[0]->GetID() == 0, "分解出的对象继承图层，ID 为 0 由调用方分配");
    }
    {
        // bulge = 1：半圆。正 bulge 向左凸，弧过 (1,1)
        PolylineEntity pl(2, { { 0, 0, 0 }, { 2, 0, 0 } }, { 1.0 });
        std::vector<std::unique_ptr<Entity>> out;
        Check(ExplodeEntity(pl, out) && out.size() == 1 && out[0]->IsKindOf<ArcEntity>(), "多段线：带 bulge 的段分解成圆弧");
        const Arc& a = static_cast<const ArcEntity&>(*out[0]).GetArc();
        Check(Near(a.Radius, 1.0) && Near(a.Center.x, 1.0) && Near(a.Center.y, 0.0) && Near(a.SweepAngle(), kPi), "圆弧：半径 1、圆心 (1,0)、包角 180°");
        const auto mid = a.MidPoint();
        Check(Near(mid.x, 1.0) && Near(mid.y, 1.0), "圆弧：弧中点 (1,1)，与多段线的弧段方向一致");
    }
    {
        PolylineEntity pl(3, { { 0, 0, 0 }, { 2, 0, 0 } }, { -1.0 });
        std::vector<std::unique_ptr<Entity>> out;
        ExplodeEntity(pl, out);
        const auto mid = static_cast<const ArcEntity&>(*out[0]).GetArc().MidPoint();
        Check(Near(mid.x, 1.0) && Near(mid.y, -1.0), "负 bulge：弧向右凸，弧中点 (1,-1)");
    }
    {
        PolylineEntity pl(4, { { 0, 0, 0 } }, {});
        std::vector<std::unique_ptr<Entity>> out;
        Check(!ExplodeEntity(pl, out) && out.empty(), "只有一个顶点的多段线：不能分解");
    }

    // ── 矩形 ─────────────────────────────────────────────────
    {
        RectangleEntity r(5, { 0, 0, 0 }, { 4, 3, 0 });
        std::vector<std::unique_ptr<Entity>> out;
        Check(ExplodeEntity(r, out) && out.size() == 4, "矩形：分解成 4 条直线");
        bool closed = true;
        for (size_t i = 0; i < 4; ++i)
            closed = closed && Near(L(out, i).End.x, L(out, (i + 1) % 4).Start.x) && Near(L(out, i).End.y, L(out, (i + 1) % 4).Start.y);
        Check(closed, "矩形：4 条直线首尾相接成封闭环");
    }

    // ── 块插入 ───────────────────────────────────────────────
    {
        BlockEntity block(10, "B", { 0, 0, 0 });
        auto line = std::make_unique<LineEntity>(11, Math::Point3{ 0, 0, 0 }, Math::Point3{ 1, 0, 0 });
        block.AddEntity(std::move(line));

        InsertEntity ins(12, 10, "B", { 10, 10, 0 });
        ins.SetBlock(&block);
        ins.SetUniformScale(2.0);
        ins.SetRotation(kPi / 2);
        std::vector<std::unique_ptr<Entity>> out;
        std::string why;
        Check(ExplodeEntity(ins, out, &why) && out.size() == 1, "块插入：分解出块里的 1 个对象");
        Check(Near(L(out, 0).Start.x, 10) && Near(L(out, 0).Start.y, 10) && Near(L(out, 0).End.x, 10) && Near(L(out, 0).End.y, 12), "块插入：缩放 2、旋转 90°、平移后，线从 (10,10) 到 (10,12)");

        InsertEntity mirrored(13, 10, "B", { 10, 10, 0 });
        mirrored.SetBlock(&block);
        mirrored.SetScale({ -1.0, 1.0, 1.0 });
        std::vector<std::unique_ptr<Entity>> m;
        ExplodeEntity(mirrored, m);
        Check(Near(L(m, 0).Start.x, 10) && Near(L(m, 0).End.x, 9) && Near(L(m, 0).End.y, 10), "块插入：X 缩放为负（镜像），线朝左");

        InsertEntity arr(14, 10, "B", { 0, 0, 0 });
        arr.SetBlock(&block);
        arr.SetArray(2, 2, 5.0, 3.0);
        std::vector<std::unique_ptr<Entity>> a;
        Check(ExplodeEntity(arr, a) && a.size() == 4, "块插入：2 x 2 阵列分解成 4 份");
        Check(Near(L(a, 3).Start.x, 5) && Near(L(a, 3).Start.y, 3), "阵列：最后一个单元在 (5,3)");

        InsertEntity nonuniform(15, 10, "B", { 0, 0, 0 });
        nonuniform.SetBlock(&block);
        nonuniform.SetScale({ 2.0, 1.0, 1.0 });
        std::vector<std::unique_ptr<Entity>> n;
        std::string r2;
        Check(!ExplodeEntity(nonuniform, n, &r2) && n.empty() && !r2.empty(), "X / Y 缩放不相等：不能分解，并给出原因");

        InsertEntity noBlock(16, 99, "NOPE", { 0, 0, 0 });
        std::vector<std::unique_ptr<Entity>> nb;
        Check(!ExplodeEntity(noBlock, nb), "块定义不存在：不能分解");
    }
    {
        // BYBLOCK 的颜色继承插入对象的颜色
        BlockEntity block(20, "C", { 1, 0, 0 });
        auto line = std::make_unique<LineEntity>(21, Math::Point3{ 1, 0, 0 }, Math::Point3{ 2, 0, 0 });
        line->GetAttr().Color = EntityColor::ByBlock();
        block.AddEntity(std::move(line));
        InsertEntity ins(22, 20, "C", { 0, 0, 0 });
        ins.SetBlock(&block);
        ins.GetAttr().Color = EntityColor::FromAci(1);
        std::vector<std::unique_ptr<Entity>> out;
        ExplodeEntity(ins, out);
        Check(out[0]->GetAttr().Color.Method == ColorMethod::ByAci && out[0]->GetAttr().Color.Aci == 1, "BYBLOCK 颜色继承插入对象的颜色");
        Check(Near(L(out, 0).Start.x, 0) && Near(L(out, 0).End.x, 1), "块基点 (1,0)：插入点 (0,0) 处，线从 (0,0) 到 (1,0)");
    }

    // ── 不可分解 ─────────────────────────────────────────────
    {
        CircleEntity c(30, { 0, 0, 0 }, 5.0);
        LineEntity l(31, { 0, 0, 0 }, { 1, 0, 0 });
        SolidEntity s(32, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 });
        std::vector<std::unique_ptr<Entity>> out;
        std::string why;
        Check(!ExplodeEntity(c, out, &why) && !why.empty(), "圆：不能分解，并给出原因");
        Check(!ExplodeEntity(l, out) && !ExplodeEntity(s, out) && out.empty(), "直线、实心填充：不能分解（实心填充不当作矩形）");
    }

    return g_failures;
}
