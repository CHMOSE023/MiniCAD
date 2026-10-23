// ── DWG / DXF 交换测试：场景 → 导出 → 导入 → 比对，以及读取 MiniDWG 的样例图 ──────────
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

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

    bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }
    constexpr double kHalfPi = 1.57079632679489661923;

    template <class T>
    const T* FindOne(const Scene& scene)
    {
        const T* found = nullptr;
        scene.ForEachObject([&](const Object& o) { if (o.IsKindOf<T>()) found = static_cast<const T*>(&o); });
        return found;
    }

    int CountAll(const Scene& scene)
    {
        int n = 0;
        scene.ForEachObject([&](const Object&) { ++n; });
        return n;
    }

    void BuildScene(Scene& scene)
    {
        LayerID walls = scene.GetLayerManager().AddLayer("Walls");
        scene.GetLayerManager().GetLayer(walls)->SetColor({ 1.0, 0.0, 0.0, 1.0 });

        auto line = std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 10, 5, 0 });
        EntityAttr a = line->GetAttr();
        a.LayerId = walls;
        a.Color   = EntityColor::FromAci(3);
        line->SetAttr(a);
        scene.AddEntity(std::move(line));

        scene.AddEntity(std::make_unique<CircleEntity>(scene.NextObjectID(), Math::Point3{ 20, 20, 0 }, 4.0));
        scene.AddEntity(std::make_unique<ArcEntity>(scene.NextObjectID(), Math::Point3{ 0, 30, 0 }, 7.0, 0.5, 2.0));
        scene.AddEntity(std::make_unique<EllipseEntity>(scene.NextObjectID(), Math::Point3{ 50, 0, 0 }, 6.0, 3.0, 0.3));
        auto text = std::make_unique<TextEntity>(scene.NextObjectID(), Math::Point3{ 1, 2, 0 }, "Hello 你好", 2.5f, 0.25f);
        text->SetAlignment(1, 2);   // 居中、竖直居中：位置为对齐点
        text->SetWidthFactor(0.55); // 文字自身的宽度因子（与样式不同）
        scene.AddEntity(std::move(text));
        scene.AddEntity(std::make_unique<MTextEntity>(scene.NextObjectID(), 0u, "line1\Pline2", Math::Point3{ 5, 5, 0 }, 3.0, 0.0, 40.0));

        // 闭合多段线（末点 = 起点）+ 圆弧段
        auto pl = std::make_unique<PolylineEntity>(scene.NextObjectID(),
            std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 0, 0 } },
            std::vector<double>{ 0.0, 0.5, 0.0 });
        pl->SetWidth(0.0);
        scene.AddEntity(std::move(pl));

        // 块与插入
        BlockID blk = scene.GetBlockTable().Create("BOX", { 1, 1, 0 });
        scene.GetBlockTable().Find(blk)->AddEntity(
            std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 2, 0, 0 }));
        auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), blk, "BOX", Math::Point3{ 100, 100, 0 });
        ins->SetRotation(0.5);
        ins->SetScale({ 2, 2, 1 });
        scene.AddEntity(std::move(ins));
    }

    // expected：读回的版本，默认与写出的版本相同（2007 DWG 写不出，改写为 2010）
    void CheckRoundTrip(CadFileKind kind, const char* label, CadSaveVersion version = CadSaveVersion::R2018,
                        std::optional<CadSaveVersion> expected = std::nullopt)
    {
        Scene src;
        BuildScene(src);

        std::string err;
        const auto bytes = ExportCad(src, kind, version, &err);
        Check(!bytes.empty(), (std::string(label) + "：导出成功").c_str());
        if (bytes.empty())
            return;

        Scene dst;
        CadSaveVersion readVersion = CadSaveVersion::R2018;
        Check(ImportCad(bytes, dst, &err, &readVersion), (std::string(label) + "：导入成功").c_str());
        Check(readVersion == expected.value_or(version), (std::string(label) + "：读回的版本与写出的版本一致").c_str());

        Check(CountAll(dst) == CountAll(src), (std::string(label) + "：实体数量一致").c_str());

        const auto* line = FindOne<LineEntity>(dst);
        Check(line && Near(line->GetLine().End.x, 10) && Near(line->GetLine().End.y, 5), "直线端点一致");
        Check(line && line->GetAttr().Color.Method == ColorMethod::ByAci && line->GetAttr().Color.Aci == 3, "直线 ACI 颜色保留");
        const Layer* layer = line ? dst.GetLayerManager().GetLayer(line->GetAttr().LayerId) : nullptr;
        Check(layer && layer->GetName() == "Walls" && Near(layer->GetColor().r, 1.0), "图层名与颜色保留");
        Check(layer && layer->IsVisible(), "真彩图层读回后仍可见（-1 索引不是关闭标志）");

        const auto* circle = FindOne<CircleEntity>(dst);
        Check(circle && Near(circle->GetCircle().Radius, 4.0) && Near(circle->GetCircle().Center.x, 20), "圆一致");

        const auto* arc = FindOne<ArcEntity>(dst);
        Check(arc && Near(arc->GetArc().StartAngle, 0.5) && Near(arc->GetArc().EndAngle, 2.0), "圆弧角度一致");

        const auto* el = FindOne<EllipseEntity>(dst);
        Check(el && Near(el->GetEllipse().RadiusX, 6.0) && Near(el->GetEllipse().RadiusY, 3.0)
                 && Near(el->GetEllipse().Rotation, 0.3), "椭圆半轴与旋转一致");

        const auto* text = FindOne<TextEntity>(dst);
        Check(text && text->GetText() == "Hello 你好" && Near(text->GetHeight(), 2.5), "单行文字（含中文）一致");
        Check(text && text->GetHAlign() == 1 && text->GetVAlign() == 2 && Near(text->GetPosition().x, 1) && Near(text->GetPosition().y, 2),
              "单行文字的对齐方式与对齐点一致");
        Check(text && Near(text->GetWidthFactor(), 0.55), "单行文字自身的宽度因子一致");

        const auto* mtext = FindOne<MTextEntity>(dst);
        Check(mtext && mtext->GetText() == "line1\Pline2" && Near(mtext->GetBoxWidth(), 40.0), "多行文字一致");

        const auto* pl = FindOne<PolylineEntity>(dst);
        Check(pl && pl->GetPolyline().Points.size() == 4 && Near(pl->GetPolyline().Bulges[1], 0.5), "闭合多段线（含弧段）一致");

        const auto* ins = FindOne<InsertEntity>(dst);
        Check(ins && ins->GetBlock() && ins->GetBlock()->GetName() == "BOX" && ins->GetBlock()->EntityCount() == 1
                  && Near(ins->GetScale().x, 2) && Near(ins->GetRotation(), 0.5), "块定义与插入一致");
        Check(ins && ins->GetBlock() && Near(ins->GetBlock()->GetBasePoint().x, 1), "块基点一致");
    }

    // 统计填充画出的线段总长与三角形面积，用来比较两种表示（比例 / 角度 vs. 换算后的线族）画出的图形是否相同
    class MeasureSink : public IDrawSink
    {
    public:
        double length = 0.0, area = 0.0;
        int    lines = 0;
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override
        {
            length += std::hypot(b.x - a.x, b.y - a.y);
            ++lines;
        }
        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
        {
            area += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
        }
    };

    MeasureSink Measure(const Entity& e)
    {
        MeasureSink s;
        e.Draw(s, false, false);
        return s;
    }

    bool NearRel(double a, double b, double tol = 1e-3) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

    template <class T>
    std::vector<const T*> FindAll(const Scene& scene)
    {
        std::vector<const T*> all;
        scene.ForEachObject([&](const Object& o) { if (o.IsKindOf<T>()) all.push_back(static_cast<const T*>(&o)); });
        return all;
    }

    // 填充、区域覆盖、样条：导出再导入后外观一致
    void CheckHatchWipeoutSpline(CadFileKind kind, const char* label)
    {
        Scene src;

        // 实心填充：外环带一段圆弧（凸度），内环为孔
        Polyline outer({ { 0, 0, 0 }, { 20, 0, 0 }, { 20, 10, 0 }, { 0, 10, 0 }, { 0, 0, 0 } }, { 0.0, 0.4, 0.0, 0.0 });
        Polyline hole({ { 5, 3, 0 }, { 8, 3, 0 }, { 8, 6, 0 }, { 5, 6, 0 }, { 5, 3, 0 } });
        auto solid = std::make_unique<HatchEntity>(src.NextObjectID(), std::vector<Polyline>{ outer, hole }, HatchPattern::MakeSolid());
        EntityAttr attr = solid->GetAttr();
        attr.Color = EntityColor::FromAci(8);
        solid->SetAttr(attr);
        const MeasureSink solidRef = Measure(*solid);
        src.AddEntity(std::move(solid));

        // 图案填充：两族带虚线与错位的线，比例 2、角度 30°；第二个环由多段线边 + 椭圆弧边组成（导出为逐条边）
        HatchPattern pat;
        pat.Name  = "CUSTOM_TEST";
        pat.Solid = false;
        pat.Families.push_back({ 45.0, 3.0, 1.0, 0.5, 1.5, { 4.0, -2.0, 1.0, -2.0 } });
        pat.Families.push_back({ 135.0, 3.0, 0.0, 0.0, 0.0, {} });
        HatchLoop mixed;
        mixed.Edges.push_back(HatchEdge::MakeEllipseArc(Ellipse({ 60, 5, 0 }, 3.0, 5.0, 0.0), -kHalfPi, kHalfPi));
        mixed.Edges.push_back(HatchEdge::MakePoly(Polyline({ { 60, 10, 0 }, { 40, 10, 0 }, { 40, 0, 0 } }, { 0.0, 0.3 })));
        mixed.Edges.push_back(HatchEdge::MakePoly(Polyline({ { 40, 0, 0 }, { 60, 0, 0 } }, { 0.0 })));
        auto patterned = std::make_unique<HatchEntity>(src.NextObjectID(), std::vector<HatchLoop>{ mixed }, pat);
        patterned->SetScale(2.0);
        patterned->SetAngle(30.0);
        const MeasureSink patRef = Measure(*patterned);
        src.AddEntity(std::move(patterned));

        // 区域覆盖：不对齐坐标轴的四边形
        const std::vector<Math::Point3> quad = { { 100, 0, 0 }, { 110, 2, 0 }, { 108, 9, 0 }, { 99, 6, 0 } };
        src.AddEntity(std::make_unique<WipeoutEntity>(src.NextObjectID(), quad));

        // 样条：三次有理 NURBS
        auto spline = std::make_unique<SplineEntity>(src.NextObjectID(), std::vector<Math::Point3>{});
        spline->SetControlData({ { 0, 50, 0 }, { 5, 60, 0 }, { 10, 45, 0 }, { 15, 55, 0 }, { 20, 50, 0 } },
                               { 0, 0, 0, 0, 0.5, 1, 1, 1, 1 }, 3, { 1, 2, 1, 0.5, 1 });
        const auto splineRef = spline->GetSpline().Tessellate(8);
        src.AddEntity(std::move(spline));

        std::string err;
        const auto bytes = ExportCad(src, kind, CadSaveVersion::R2018, &err);
        Scene dst;
        const std::string prefix = std::string(label) + "：";
        Check(!bytes.empty() && ImportCad(bytes, dst, &err), (prefix + "填充 / 区域覆盖 / 样条导出再导入").c_str());
        std::printf("       实体 %d（导出前 %d）\n", CountAll(dst), CountAll(src));
        Check(CountAll(dst) == CountAll(src), (prefix + "实体数一致（没有被跳过）").c_str());

        const auto hatches = FindAll<HatchEntity>(dst);
        const HatchEntity* solidBack = nullptr;
        const HatchEntity* patBack = nullptr;
        for (const auto* h : hatches)
            (h->GetPattern().Solid ? solidBack : patBack) = h;
        Check(solidBack && solidBack->GetLoops().size() == 2 && solidBack->GetAttr().Color.Aci == 8, (prefix + "实心填充：两个环与颜色").c_str());
        if (solidBack)
        {
            const MeasureSink m = Measure(*solidBack);
            Check(NearRel(m.area, solidRef.area, 1e-6), (prefix + "实心填充：填充面积一致（圆弧方向保持）").c_str());
            const Polyline& pl = solidBack->GetLoops()[0].Edges[0].Poly;
            Check(pl.Points.size() >= 4 && Near(pl.SegBulge(1), 0.4), (prefix + "实心填充：凸度原样读回").c_str());
        }
        Check(patBack && patBack->GetPattern().Name == "CUSTOM_TEST" && patBack->GetPattern().Families.size() == 2,
              (prefix + "图案填充：名称与线族数").c_str());
        if (patBack)
        {
            const MeasureSink m = Measure(*patBack);
            std::printf("       图案线 %d 段 / 总长 %.4f（导出前 %d 段 / %.4f）\n", m.lines, m.length, patRef.lines, patRef.length);
            Check(m.lines > 0 && NearRel(m.length, patRef.length) && std::abs(m.lines - patRef.lines) <= 2,
                  (prefix + "图案填充：比例、角度换算后画出的图案线一致").c_str());
            const auto box = patBack->GetBoundingBox();
            // 边界多段线的第二段是 {40,10} → {40,0}、bulge = 0.3（向下行进）。
            // 按 DXF 组码 42 的规范，正凸度的弧向行进方向**右侧**鼓出 ⇒ 向 −X 鼓到 x = 40 − 1.5 = 38.5
            // （sagitta：θ = 4·atan(0.3)、r = 10/(2·sin(θ/2))、r − r·cos(θ/2) = 1.5）。
            // 旧期望写的 40 是按镜像方向（弧朝 +X 凸）算的，凸度方向修正后应为 38.5。
            Check(NearRel(box.Min.x, 38.5) && NearRel(box.Max.x, 63) && NearRel(box.Min.y, 0) && NearRel(box.Max.y, 10),
                  (prefix + "图案填充：直线、圆弧、椭圆弧边界范围一致").c_str());
        }

        const auto* wipe = FindOne<WipeoutEntity>(dst);
        bool wipeOk = wipe != nullptr && wipe->GetPolyline().Points.size() == quad.size() + 1;
        for (std::size_t i = 0; wipeOk && i < quad.size(); ++i)
            wipeOk = Near(wipe->GetPolyline().Points[i].x, quad[i].x) && Near(wipe->GetPolyline().Points[i].y, quad[i].y);
        Check(wipeOk, (prefix + "区域覆盖：边界顶点一致").c_str());

        const auto* sp = FindOne<SplineEntity>(dst);
        bool splineOk = sp != nullptr && sp->HasControlData() && sp->Degree() == 3 && sp->GetSpline().IsRational();
        if (splineOk)
        {
            const auto pts = sp->GetSpline().Tessellate(8);
            splineOk = pts.size() == splineRef.size();
            for (std::size_t i = 0; splineOk && i < pts.size(); ++i)
                splineOk = Near(pts[i].x, splineRef[i].x) && Near(pts[i].y, splineRef[i].y);
        }
        Check(splineOk, (prefix + "样条：控制点、节点、权重一致").c_str());
    }

    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }

    // 光栅图像：四个顶点与路径在导出再导入后不变；图像文件不在磁盘上时只画占位框，
    // 但实体本身必须存在（MiniDWG 只保存路径，不内嵌像素）
    void CheckImage(CadFileKind kind, const char* label)
    {
        Scene src;
        const Math::Point3 p1{ 10, 20, 0 }, p2{ 40, 20, 0 }, p3{ 40, 35, 0 }, p4{ 10, 35, 0 };
        src.AddEntity(std::make_unique<ImageEntity>(src.NextObjectID(), "missing_image.png", p1, p2, p3, p4));

        const auto data = ExportCad(src, kind);
        Scene dst;
        Check(!data.empty() && ImportCad(data, dst), (std::string(label) + "：图像导出再导入成功").c_str());

        const auto* img = FindOne<ImageEntity>(dst);
        if (!img)
        {
            Check(false, (std::string(label) + "：读回 ImageEntity").c_str());
            return;
        }
        const auto& r = img->GetRectangle();
        Check(img->GetPath() == "missing_image.png", (std::string(label) + "：图像路径保留").c_str());
        Check(Near(r.P1.x, p1.x) && Near(r.P1.y, p1.y) && Near(r.P2.x, p2.x) && Near(r.P2.y, p2.y)
              && Near(r.P3.x, p3.x) && Near(r.P3.y, p3.y) && Near(r.P4.x, p4.x) && Near(r.P4.y, p4.y),
              (std::string(label) + "：图像四个顶点一致").c_str());

        // 找不到图像文件：画四周边框 + 两条对角线作占位（6 条线）
        const MeasureSink m = Measure(*img);
        Check(m.lines == 6, (std::string(label) + "：图像不可用时画占位框与对角线").c_str());
    }
}

int RunCadExchangeTests()
{
    g_failures = 0;

    CheckRoundTrip(CadFileKind::Dxf, "DXF");
    CheckRoundTrip(CadFileKind::Dwg, "DWG");
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2013", CadSaveVersion::R2013);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2013", CadSaveVersion::R2013);
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2010", CadSaveVersion::R2010);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2010", CadSaveVersion::R2010);
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2007", CadSaveVersion::R2007);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2007（改写为 2010）", CadSaveVersion::R2007, CadSaveVersion::R2010);
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2004", CadSaveVersion::R2004);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2004", CadSaveVersion::R2004);
    CheckRoundTrip(CadFileKind::Dxf, "DXF 2000", CadSaveVersion::R2000);
    CheckRoundTrip(CadFileKind::Dwg, "DWG 2000", CadSaveVersion::R2000);
    CheckHatchWipeoutSpline(CadFileKind::Dwg, "DWG");
    CheckHatchWipeoutSpline(CadFileKind::Dxf, "DXF");
    CheckImage(CadFileKind::Dwg, "DWG");
    CheckImage(CadFileKind::Dxf, "DXF");

    for (const auto kind : { CadFileKind::Dwg, CadFileKind::Dxf })
    {
        Scene hidden;
        BuildScene(hidden);
        hidden.GetLayerManager().GetLayer(Layer::DefaultLayerID)->SetVisible(false);
        const auto data = ExportCad(hidden, kind);
        Scene restored;
        Check(!data.empty() && ImportCad(data, restored) &&
              !restored.GetLayerManager().GetLayer(Layer::DefaultLayerID)->IsVisible(),
              "关闭的真彩图层读回后保持关闭");
    }

    // 法向为 -Z 的二维填充（SOLID）：角点是 OCS 坐标，读入后换到世界坐标（左右镜像）
    for (const auto kind : { CadFileKind::Dwg, CadFileKind::Dxf })
    {
        Scene src;
        auto solid = std::make_unique<SolidEntity>(src.NextObjectID(), Math::Point3{ 10, 0, 0 }, Math::Point3{ 20, 0, 0 },
                                                   Math::Point3{ 20, 5, 0 }, Math::Point3{ 10, 5, 0 });
        solid->SetExtrusion({ 0, 0, -1 });
        src.AddEntity(std::move(solid));
        const auto data = ExportCad(src, kind);
        Scene restored;
        AABB box = AABB::Empty();
        Check(!data.empty() && ImportCad(data, restored) && restored.GetExtents(box) &&
              std::abs(box.Min.x + 20) < 1e-9 && std::abs(box.Max.x + 10) < 1e-9 && std::abs(box.Max.y - 5) < 1e-9,
              kind == CadFileKind::Dwg ? "DWG：法向 -Z 的 SOLID 换到世界坐标" : "DXF：法向 -Z 的 SOLID 换到世界坐标");
    }

    // MiniDWG 样例图：能读入，且读入后能再导出、再读回，实体数不变
    const char* samples[] = { "sample_AC1015.dwg", "sample_AC1032.dwg", "sample_AC1015_ascii.dxf", "sample_AC1032_binary.dxf" };
    for (const char* name : samples)
    {
        const auto data = ReadFile(std::string(MINIDWG_SAMPLE_DIR) + "/" + name);
        if (data.empty())
        {
            std::printf("[跳过] 没有找到样例 %s\n", name);
            continue;
        }
        Scene scene;
        Check(ImportCad(data, scene, nullptr), (std::string("读取样例 ") + name).c_str());
        std::printf("       %s：%d 个实体\n", name, CountAll(scene));

        const auto out = ExportCad(scene, CadFileKind::Dwg);
        Scene again;
        Check(!out.empty() && ImportCad(out, again, nullptr) && CountAll(again) == CountAll(scene),
              (std::string("样例 ") + name + " 导出 DWG 后再读回，实体数不变").c_str());
    }

#ifdef MINICAD_TCH_SAMPLE_DIR
    // 天正 2014 用脚本画出的样例图（tests/Data/Tch）：每个天正对象都恢复为一个顶层实体（实体数 = 原图模型空间实体数），
    // 以原图为底另存后再读回实体数不变。样例覆盖墙（直墙、弧墙、带造型）、门窗（门、带形窗、组合门窗、门窗套）、
    // 各类楼梯与扶梯、台阶、坡道、阳台、散水、屋顶与注释符号
    struct TchSample { const char* Name; int Entities; int Tch; };
    const TchSample tchSamples[] = {
        {"p1.dwg", 14, 14}, {"p2.dwg", 15, 13}, {"p3.dwg", 15, 11}, {"p5.dwg", 11, 11}, {"q6.dwg", 13, 11},
        {"q7.dwg", 30, 30}, {"q8.dwg", 6, 6}, {"q9.dwg", 9, 6}, {"q_ban.dwg", 9, 9}, {"q_patch.dwg", 9, 9},
        {"q_step.dwg", 9, 9}, {"v1.dwg", 9, 9}, {"v2.dwg", 8, 8},
    };
    for (const auto& sample : tchSamples)
    {
        const auto data = ReadFile(std::string(MINICAD_TCH_SAMPLE_DIR) + "/" + sample.Name);
        if (data.empty())
        {
            std::printf("[跳过] 没有找到天正样例 %s\n", sample.Name);
            continue;
        }
        Scene scene;
        CadSaveVersion version = CadSaveVersion::R2018;
        std::shared_ptr<const CadSource> source;
        const bool ok = ImportCad(data, scene, nullptr, &version, &source);
        Check(ok && CountAll(scene) == sample.Entities && CadSourceTchCount(source.get()) == sample.Tch,
              (std::string("天正样例 ") + sample.Name + "：每个天正对象都恢复显示").c_str());
        if (!ok) continue;
        const auto out = ExportCad(scene, CadFileKind::Dwg, version, nullptr, source.get());
        Scene again;
        std::shared_ptr<const CadSource> againSource;
        Check(!out.empty() && ImportCad(out, again, nullptr, nullptr, &againSource) && CountAll(again) == sample.Entities &&
              CadSourceTchCount(againSource.get()) == sample.Tch,
              (std::string("天正样例 ") + sample.Name + " 以原图为底另存后再读回不变").c_str());
    }
#endif

    std::printf("DWG/DXF 交换测试：%d 项失败\n", g_failures);
    return g_failures;
}
