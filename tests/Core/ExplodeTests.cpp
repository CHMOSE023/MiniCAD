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
#include <algorithm>
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
        // bulge = 1：半圆。按 DXF 组码 42 的规范，正 bulge 是逆时针弧、圆心在 A→B 左侧，
        // 弧向行进方向右侧鼓出 ⇒ 弧过 (1,-1)（判据见 tests/Core/BulgeDirectionTests.cpp）
        PolylineEntity pl(2, { { 0, 0, 0 }, { 2, 0, 0 } }, { 1.0 });
        std::vector<std::unique_ptr<Entity>> out;
        Check(ExplodeEntity(pl, out) && out.size() == 1 && out[0]->IsKindOf<ArcEntity>(), "多段线：带 bulge 的段分解成圆弧");
        const Arc& a = static_cast<const ArcEntity&>(*out[0]).GetArc();
        Check(Near(a.Radius, 1.0) && Near(a.Center.x, 1.0) && Near(a.Center.y, 0.0) && Near(a.SweepAngle(), kPi), "圆弧：半径 1、圆心 (1,0)、包角 180°");
        const auto mid = a.MidPoint();
        Check(Near(mid.x, 1.0) && Near(mid.y, -1.0), "圆弧：弧中点 (1,-1)，与多段线的弧段方向一致");
    }
    {
        PolylineEntity pl(3, { { 0, 0, 0 }, { 2, 0, 0 } }, { -1.0 });
        std::vector<std::unique_ptr<Entity>> out;
        ExplodeEntity(pl, out);
        const auto mid = static_cast<const ArcEntity&>(*out[0]).GetArc().MidPoint();
        Check(Near(mid.x, 1.0) && Near(mid.y, 1.0), "负 bulge：弧中点 (1,1)");
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

    {
        // 块内 0 层上的实体显示时随插入所在的图层，其他图层不变（与 AutoCAD 相同）
        struct LayerSink : IDrawSink
        {
            mutable std::vector<LayerID> Layers;
            // ByLayer 颜色在绘制时按实体图层向 sink 查询
            Math::Color4 GetLayerColor(LayerID layer) const override { Layers.push_back(layer); return Math::Color4::White(); }
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
        };
        BlockEntity block(40, "D", { 0, 0, 0 });
        block.AddEntity(std::make_unique<LineEntity>(41, Math::Point3{ 0, 0, 0 }, Math::Point3{ 1, 0, 0 }));
        auto other = std::make_unique<LineEntity>(42, Math::Point3{ 0, 1, 0 }, Math::Point3{ 1, 1, 0 });
        other->GetAttr().LayerId = 5;
        block.AddEntity(std::move(other));
        InsertEntity ins(43, 40, "D", { 0, 0, 0 });
        ins.SetBlock(&block);
        ins.GetAttr().LayerId = 7;
        LayerSink sink;
        ins.Draw(sink, false, false);
        Check(std::count(sink.Layers.begin(), sink.Layers.end(), LayerID(7)) == 1 && std::count(sink.Layers.begin(), sink.Layers.end(), LayerID(5)) == 1 &&
                  std::count(sink.Layers.begin(), sink.Layers.end(), LayerID(0)) == 0,
              "块内 0 层的实体随插入图层显示，其他图层的实体保持原图层");
    }
    {
        // 块内设置了不可见标志的子实体：不画，也不计入范围（如 DWG 里被隐藏的嵌套块参照）
        struct CountSink : IDrawSink
        {
            int Lines = 0;
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++Lines; }
        };
        BlockEntity block(50, "E", { 0, 0, 0 });
        block.AddEntity(std::make_unique<LineEntity>(51, Math::Point3{ 0, 0, 0 }, Math::Point3{ 1, 0, 0 }));
        auto hidden = std::make_unique<LineEntity>(52, Math::Point3{ 100, 0, 0 }, Math::Point3{ 200, 0, 0 });
        hidden->GetAttr().Visible = false;
        block.AddEntity(std::move(hidden));
        InsertEntity ins(53, 50, "E", { 0, 0, 0 });
        ins.SetBlock(&block);
        CountSink sink;
        ins.Draw(sink, false, false);
        Check(sink.Lines == 1, "块内不可见的子实体不画");
        Check(Near(ins.GetBoundingBox().Max.x, 1), "块内不可见的子实体不计入块参照的范围");
    }
    {
        // 块内关闭 / 冻结图层上的子实体不画；0 层上的子实体随插入所在的图层
        struct HiddenLayerSink : IDrawSink
        {
            LayerID Hidden = 0;
            int     Lines  = 0;
            bool IsLayerVisible(LayerID layer) const override { return layer != Hidden; }
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++Lines; }
        };
        BlockEntity block(60, "F", { 0, 0, 0 });
        block.AddEntity(std::make_unique<LineEntity>(61, Math::Point3{ 0, 0, 0 }, Math::Point3{ 1, 0, 0 }));   // 0 层
        auto onFive = std::make_unique<LineEntity>(62, Math::Point3{ 0, 1, 0 }, Math::Point3{ 1, 1, 0 });
        onFive->GetAttr().LayerId = 5;
        block.AddEntity(std::move(onFive));
        InsertEntity ins(63, 60, "F", { 0, 0, 0 });
        ins.SetBlock(&block);
        ins.GetAttr().LayerId = 7;

        HiddenLayerSink s5; s5.Hidden = 5;
        ins.Draw(s5, false, false);
        Check(s5.Lines == 1, "块内关闭图层上的子实体不画，其余照常");
        HiddenLayerSink s7; s7.Hidden = 7;
        ins.Draw(s7, false, false);
        Check(s7.Lines == 1, "块内 0 层的子实体随插入图层：插入图层关闭时不画");
        HiddenLayerSink sb; sb.Hidden = 5;
        block.Draw(sb, false, false);
        Check(sb.Lines == 1, "直接绘制块定义时同样跳过关闭图层上的子实体");
    }

    // ── 块参照的裁剪边界（XCLIP）──────────────────────────────
    {
        struct ClipSink : IDrawSink
        {
            std::vector<std::pair<Math::Point3, Math::Point3>> Lines;
            double Area = 0;
            int    Wipes = 0;
            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { Lines.push_back({ a, b }); }
            void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
            {
                Area += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
            }
            void Wipe(const std::vector<Math::Point3>& p) override { ++Wipes; Area += std::abs(ClipDrawSink::SignedArea(p)); }
        };
        // 块（基点 (100,0)）内一条横穿的线；裁剪边界为块坐标 x∈[100,104]、y∈[0,4] 的矩形，插入点 (0,0) 处
        BlockEntity block(70, "C", { 100, 0, 0 });
        block.AddEntity(std::make_unique<LineEntity>(71, Math::Point3{ 90, 2, 0 }, Math::Point3{ 110, 2, 0 }));
        block.AddEntity(std::make_unique<LineEntity>(72, Math::Point3{ 90, 9, 0 }, Math::Point3{ 110, 9, 0 }));
        InsertEntity ins(73, 70, "C", { 0, 0, 0 });
        ins.SetBlock(&block);
        ins.SetClipBoundary({ { 100, 0, 0 }, { 104, 0, 0 }, { 104, 4, 0 }, { 100, 4, 0 } });
        ClipSink sink;
        ins.Draw(sink, false, false);
        Check(sink.Lines.size() == 1 && Near(sink.Lines[0].first.x, 0) && Near(sink.Lines[0].second.x, 4) && Near(sink.Lines[0].first.y, 2),
              "裁剪边界：边界内的部分截断后照常变换，边界外的线不画");
        const AABB box = ins.GetBoundingBox();
        Check(Near(box.Min.x, 0) && Near(box.Max.x, 4) && Near(box.Max.y, 4), "裁剪边界：范围与边界求交");
        auto copy = ins.Clone(74);
        Check(static_cast<InsertEntity&>(*copy).GetClipBoundary().size() == 4, "裁剪边界：复制后保留");

        // 凹多边形（L 形）：穿过缺口的线分成两段
        std::vector<Math::Point3> ell = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 4, 0 }, { 4, 4, 0 }, { 4, 10, 0 }, { 0, 10, 0 } };
        ClipSink s2;
        ClipDrawSink clip(s2, ell);
        clip.DrawLine({ -5, 6, 0 }, { 15, 6, 0 }, Math::Color4::White(), false);
        clip.DrawLine({ 2, -5, 0 }, { 2, 15, 0 }, Math::Color4::White(), false);
        Check(s2.Lines.size() == 2 && Near(s2.Lines[0].second.x, 4) && Near(s2.Lines[1].second.y, 10), "凹裁剪边界：线只保留在边界内的部分");
        clip.FillTriangle({ -10, -10, 0 }, { 30, -10, 0 }, { -10, 30, 0 }, Math::Color4::White());
        Check(std::abs(s2.Area - 64.0) < 4.0, "凹裁剪边界：完全覆盖边界的三角形裁剪后面积约等于边界面积 64");
        ClipSink s3;
        ClipDrawSink rect(s3, { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 0, 4, 0 } });
        rect.FillTriangle({ -10, -10, 0 }, { 30, -10, 0 }, { -10, 30, 0 }, Math::Color4::White());
        rect.Wipe({ { 2, 2, 0 }, { 8, 2, 0 }, { 8, 8, 0 }, { 2, 8, 0 } });
        Check(std::abs(s3.Area - 16.0 - 4.0) < 1e-9 && s3.Wipes == 1, "凸裁剪边界：三角形与区域覆盖精确裁剪");
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
