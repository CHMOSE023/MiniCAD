// ── TransformDrawSink：块参照 / 视口合成时的绘制转发完整性 ──────────────
//
// 用户报「B地块地下室平面」图纸切到布局页后，图框能看到但里面内容缺了
// （模型页正常）—— 布局页的内容全部经 `TransformDrawSink` 施加视口变换，
// 所以问题出在这个转发 sink 漏掉了几个方法。
//
// `IDrawSink` 的所有虚函数都有**空实现**，漏转发不会编译报错，只是静默丢内容。
// 实测这张图纸：
//   · `DrawPoint` 没转发 ⇒ 布局页的 3 万个 POINT 一个都不显示（ScenePoints = 0）
//   · `GetWorldPerPixel` / `GetViewportHeightPx` 没转发 ⇒ 返回 0，
//     点退化成 1 像素小点、线宽与虚线判定失效
//   · `EmitImage` 没转发 ⇒ 视口/块内的光栅图像不显示
//
// 本文件守住：TransformDrawSink 对**每一个** IDrawSink 方法都要转发。
#include "Core/Draw/TransformDrawSink.hpp"
#include "Core/Math/Mat4.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    // 记录所有被调用的绘制方法 —— 用来验证转发是否真的发生
    struct RecSink : IDrawSink
    {
        int lines = 0, styledLines = 0, points = 0, fills = 0, wipes = 0;
        int texts = 0, mtexts = 0, alignedTexts = 0, images = 0, beginEntity = 0;
        double worldPerPixel = 0.0, viewportHeightPx = 0.0;
        int markViewDependent = 0;
        mutable int pointStyleCalls = 0;
        mutable int linetypeQueries = 0;
        Math::Point3 lastPoint{};
        std::array<Math::Point3, 4> lastImageCorners{};

        void BeginEntity(const EntityAttr*) override { ++beginEntity; }
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void DrawStyledLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, double) override { ++styledLines; }
        void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override { ++points; lastPoint = p; }
        void FillTriangle(const Math::Point3&, const Math::Point3&, const Math::Point3&, const Math::Color4&) override { ++fills; }
        void Wipe(const std::vector<Math::Point3>&) override { ++wipes; }
        void EmitText(const Math::Point3&, const std::string&, float, float, const Math::Color4&) override { ++texts; }
        void EmitMText(const Math::Point3&, const std::string&, uint32_t, double, double, double, const Math::Color4&) override { ++mtexts; }
        void EmitAlignedText(const Math::Point3&, const std::string&, uint32_t, double, double, int, int, double, const Math::Color4&) override { ++alignedTexts; }
        bool EmitImage(const std::string&, const std::array<Math::Point3, 4>& c) override
        {
            ++images;
            lastImageCorners = c;
            return true;
        }
        bool GetPointStyle(int& mode, double& size) const override
        {
            ++pointStyleCalls;
            mode = 34;
            size = 2.5;
            return true;
        }
        bool IsCurrentLinetypeDashed() const override { ++linetypeQueries; return true; }
        double GetWorldPerPixel() const override { return worldPerPixel; }
        double GetViewportHeightPx() const override { return viewportHeightPx; }
        void MarkViewDependent() override { ++markViewDependent; }
        Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
        bool IsLayerVisible(LayerID) const override { return true; }
    };
}

int RunTransformSinkTests()
{
    g_failures = 0;

    // 变换：平移 (100, 200)，缩放 2
    const Math::Mat4 m = Math::Mat4::Scale(Math::Vec3{ 2.0, 2.0, 1.0 }) *
                         Math::Mat4::Translation(Math::Vec3{ 100.0, 200.0, 0.0 });

    RecSink target;
    target.worldPerPixel = 0.05;
    target.viewportHeightPx = 900.0;
    TransformDrawSink sink(target, m);

    const Math::Color4 white = Math::Color4::White();

    // ══ ① 每个绘制方法都必须转发到目标 sink ══════════════════════════════
    sink.BeginEntity(nullptr);
    sink.DrawLine({ 0, 0, 0 }, { 1, 0, 0 }, white, false);
    sink.DrawStyledLine({ 0, 0, 0 }, { 1, 0, 0 }, white, 0.5);
    // 关键：点是布局页丢失的重灾区
    sink.DrawPoint({ 3, 4, 0 }, white, false);
    sink.FillTriangle({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, white);
    sink.Wipe({ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 } });
    sink.EmitText({ 0, 0, 0 }, "A", 10.0f, 0.0f, white);
    sink.EmitMText({ 0, 0, 0 }, "B", 0, 10.0, 0.0, 100.0, white);
    sink.EmitAlignedText({ 0, 0, 0 }, "C", 0, 10.0, 0.0, 0, 0, 1.0, white);
    const std::array<Math::Point3, 4> imgCorners{
        Math::Point3{ 0, 0, 0 }, Math::Point3{ 1, 0, 0 },
        Math::Point3{ 1, 1, 0 }, Math::Point3{ 0, 1, 0 } };
    sink.EmitImage("x.png", imgCorners);

    Check(target.beginEntity == 1, "变换 sink：BeginEntity 转发");
    Check(target.lines == 1,       "变换 sink：DrawLine 转发");
    Check(target.styledLines == 1, "变换 sink：DrawStyledLine 转发");
    Check(target.points == 1,      "变换 sink：DrawPoint 转发（布局页点丢失就出在这里）");
    Check(target.fills == 1,       "变换 sink：FillTriangle 转发（填充）");
    Check(target.wipes == 1,       "变换 sink：Wipe 转发（区域覆盖）");
    Check(target.texts == 1,       "变换 sink：EmitText 转发");
    Check(target.mtexts == 1,      "变换 sink：EmitMText 转发");
    Check(target.alignedTexts == 1,"变换 sink：EmitAlignedText 转发（标注文字）");
    Check(target.images == 1,      "变换 sink：EmitImage 转发（光栅图像）");

    // ══ ② 点坐标确实经过变换 ══════════════════════════════════════════════
    // (3,4) → 缩放2 → (6,8) → 平移(100,200) → (106,208)
    std::printf("    点 (3,4) 变换后 (%.2f,%.2f)\n", target.lastPoint.x, target.lastPoint.y);
    Check(std::abs(target.lastPoint.x - 106.0) < 1e-9 &&
          std::abs(target.lastPoint.y - 208.0) < 1e-9,
          "变换 sink：点坐标已施加变换");

    // ══ ③ 像素尺度查询必须转发 —— 返回 0 会让点退化成 1px、线宽失效 ══════
    std::printf("    GetWorldPerPixel=%.4f  GetViewportHeightPx=%.1f\n",
                sink.GetWorldPerPixel(), sink.GetViewportHeightPx());
    Check(std::abs(sink.GetWorldPerPixel() - 0.05) < 1e-12,
          "变换 sink：GetWorldPerPixel 转发（否则线宽/虚线判定失效）");
    Check(std::abs(sink.GetViewportHeightPx() - 900.0) < 1e-12,
          "变换 sink：GetViewportHeightPx 转发（否则点退化成 1 像素）");

    int mode = 0;
    double size = 0.0;
    Check(sink.GetPointStyle(mode, size) && mode == 34 && size == 2.5,
          "变换 sink：GetPointStyle 转发（点样式）");
    Check(sink.IsCurrentLinetypeDashed(), "变换 sink：IsCurrentLinetypeDashed 转发（虚线判定）");

    sink.MarkViewDependent();
    Check(target.markViewDependent == 1,
          "变换 sink：MarkViewDependent 转发（缩放时需要重建视口内实体）");

    std::printf("变换 sink 用例失败：%d\n", g_failures);
    return g_failures;
}

// ══ ⑥ NURBS 节向量异常时的兜底 ══════════════════════════════════════════
//
// 「B地块地下室」图纸的图框 Hatch 边界样条，节向量存着 `[-1,-1,0,0,0,0,…]`
// —— 开头只重复 2 次，而 degree=3 的 NURBS 要求首尾各重复 4 次。
// 基函数在端点除到 0 长度区间，求值退化到**原点**：
//   整条样条从 (0,0) 起，包围盒横跨 1700 万，把图框块包围盒撑得比布局大 27 倍，
//   布局页内容因此被压到角落（用户截图就是这个现象）。
//
// 修法：求值结果跑出控制点包围盒就改用控制点折线。
int RunSplineKnotTests()
{
    int fail = 0;
    auto Check2 = [&](bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++fail;
    };

    // 复刻那张图的病态样条：degree=3，节向量开头只重复 2 次
    Spline bad;
    bad.Degree = 3;
    for (int i = 0; i < 8; ++i)
        bad.ControlPoints.push_back(Math::Point3{ 17337000.0 + i * 100.0, -3854000.0 + i * 50.0, 0.0 });
    bad.Knots = { -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 4.0, 4.0 };

    Check2(bad.HasControlData(), "NURBS 兜底：病态样条仍被视为有控制点数据");

    const auto pts = bad.Tessellate(24);
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const auto& p : pts)
    {
        x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
    }
    std::printf("    病态样条离散 %zu 点，范围 X[%.1f,%.1f] Y[%.1f,%.1f]\n",
                pts.size(), x0, x1, y0, y1);

    Check2(!pts.empty(), "NURBS 兜底：仍能产出点");
    // 关键：绝不能出现原点 —— 修复前首点是 (0,0)
    Check2(x0 > 1e6, "NURBS 兜底：结果不含原点（修复前会退化到 (0,0)）");
    // 结果必须落在控制点范围内
    Check2(x0 >= 17337000.0 - 1.0 && x1 <= 17337700.0 + 1.0,
          "NURBS 兜底：X 落在控制点范围内");
    Check2(y0 >= -3854000.0 - 1.0 && y1 <= -3853650.0 + 1.0,
          "NURBS 兜底：Y 落在控制点范围内");

    // 正常样条不受影响：规整节向量应走真正的 NURBS 求值
    Spline good;
    good.Degree = 3;
    for (int i = 0; i < 8; ++i)
        good.ControlPoints.push_back(Math::Point3{ static_cast<double>(i) * 10.0, 0.0, 0.0 });
    good.Knots = { 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 4 };
    // 10 个控制点时节向量要 10+3+1=14 个，这里补齐
    while (good.Knots.size() < good.ControlPoints.size() + 4)
        good.Knots.push_back(good.Knots.back());

    const auto gp = good.Tessellate(8);
    Check2(!gp.empty(), "NURBS 兜底：正常样条仍能求值");
    if (!gp.empty())
    {
        double gx0 = 1e300, gx1 = -1e300;
        for (const auto& p : gp) { gx0 = std::min(gx0, p.x); gx1 = std::max(gx1, p.x); }
        std::printf("    正常样条离散 %zu 点，范围 X[%.1f,%.1f]\n", gp.size(), gx0, gx1);
        Check2(gx0 >= -1.0 && gx1 <= 71.0, "NURBS 兜底：正常样条结果仍在控制点范围内");
    }

    std::printf("NURBS 兜底用例失败：%d\n", fail);
    return fail;
}
