// ── 多段线逐段线宽（锥形段）：DXF 顶点组码 40/41 必须保留、能画、能往返 ─────────────
//
// 用户报「多段线不同线段宽度不同的情况需要修复」，并给出 `sample_AC1032.dwg` 的实例：
//
//   (0 . "LWPOLYLINE") (5 . "35B") (90 . 5) (70 . 0)
//   (10 12.1405 7.49932)  (40 . 0.2) (41 . 0.1)
//   (10 10.9464 10.173)   (40 . 0.0) (41 . 0.0)
//   (10 12.7818 11.1673)  (40 . 0.0) (41 . 0.0)
//   (10 15.6845 8.91633)  (40 . 0.0) (41 . 0.0)
//   (10 13.2741 8.43021)  (40 . 0.1) (41 . 0.5)
//
// 【根因】`PolylineEntity` 只有全局恒定宽度 `m_width`（组码 43）。每顶点的起止宽度
//   在导入时被丢弃、导出时也写不出来 —— 数据在往返中丢失，且渲染只能画成等宽。
//   实测该图 23 条 LWPOLYLINE 里多条带 40/41，全部画成等宽。
//
// 【修法】按**段**保存 `m_segStartWidth / m_segEndWidth`（与 DXF 语义一致：顶点 i 的宽度
//   作用于「i → i+1」这一段），渲染走梯形填充 + 按两侧半宽求交的 miter 尖角。
//
// 本测试自足：内嵌一份最小 DXF（含句柄 35B 的原值），不依赖外部图纸。
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Document/DrawContext.hpp"
#include "Editor/Overlay/Overlay.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_fail = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_fail;
    }

    // 记录填充三角形：用于判「粗线是否真的按逐段宽度画成了梯形」
    struct TriSink : IDrawSink
    {
        int triangles = 0;
        int lines = 0;
        double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
        // 每个三角形在「起点侧 / 终点侧」的宽度（按 X 排序后的最大边长，粗略但足以判梯形）
        std::vector<std::array<double, 6>> verts;
        Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
        {
            ++triangles;
            for (const auto* p : { &a, &b, &c })
            {
                minX = std::min(minX, p->x); maxX = std::max(maxX, p->x);
                minY = std::min(minY, p->y); maxY = std::max(maxY, p->y);
            }
            verts.push_back({ a.x, a.y, b.x, b.y, c.x, c.y });
        }
    };

    // 句柄 35B 的原始顶点与宽度（取自用户给的 entget）
    struct V { double x, y, sw, ew; };
    const V k35B[] = {
        { 12.14051619543163, 7.499316163680425, 0.2, 0.1 },
        { 10.94637760855856, 10.17295207995138, 0.0, 0.0 },
        { 12.7818129554314, 11.16727944910944, 0.0, 0.0 },
        { 15.68447712445625, 8.916329504396678, 0.0, 0.0 },
        { 13.27408603680844, 8.430213888255487, 0.1, 0.5 },
    };

    // 生成一份只含这一条 LWPOLYLINE 的 DXF
    std::string MakeDxf()
    {
        std::string s =
            "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n"
            "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
            "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n5\n35B\n8\n0\n"
            "100\nAcDbPolyline\n90\n" + std::to_string(sizeof(k35B) / sizeof(k35B[0])) + "\n70\n0\n";
        char buf[128];
        for (const V& v : k35B)
        {
            std::snprintf(buf, sizeof buf, "10\n%.17g\n20\n%.17g\n40\n%.17g\n41\n%.17g\n42\n0.0\n",
                          v.x, v.y, v.sw, v.ew);
            s += buf;
        }
        s += "210\n0.0\n220\n0.0\n230\n1.0\n0\nENDSEC\n0\nEOF\n";
        return s;
    }

    // 找场景里唯一的多段线
    const PolylineEntity* FindPolyline(const Scene& scene)
    {
        const PolylineEntity* found = nullptr;
        scene.ForEachObject([&](const Object& o)
        {
            if (const auto* p = dynamic_cast<const PolylineEntity*>(&o)) found = p;
        });
        return found;
    }
}

int RunPolylineWidthTests()
{
    g_fail = 0;
    std::printf("── 多段线逐段线宽（锥形段）──\n");

    const std::string text = MakeDxf();
    Scene scene;
    std::string err;
    Check(ImportCad(std::vector<std::uint8_t>(text.begin(), text.end()), scene, &err, nullptr),
          "含 40/41 的 LWPOLYLINE 能导入");
    const PolylineEntity* pl = FindPolyline(scene);
    Check(pl != nullptr, "找到导入的多段线");
    if (!pl) { std::printf("多段线宽度用例失败：%d\n", g_fail); return g_fail; }

    // ── 1. 逐段起止宽度必须进场景（此前整批丢弃）─────────────────────────
    const int segs = pl->GetPolyline().SegCount();
    std::printf("    段数 = %d（顶点 %d）\n", segs, pl->PointCount());
    Check(segs == 4, "开放多段线：5 个顶点 = 4 段");
    Check(pl->HasSegmentWidths(), "**逐段宽度被导入（修前为 0 —— 整批丢失，整条画成等宽）**");

    std::printf("    每段起止宽度：");
    for (int i = 0; i < segs; ++i)
        std::printf("[%d] %.3f→%.3f  ", i, pl->SegmentStartWidth(i), pl->SegmentEndWidth(i));
    std::printf("\n");

    // 第 0 段 = 顶点 0 的宽度（0.2 → 0.1）；末段 = 顶点 3 的宽度（0,0 → 回退恒定宽度）
    Check(std::abs(pl->SegmentStartWidth(0) - 0.2) < 1e-9 && std::abs(pl->SegmentEndWidth(0) - 0.1) < 1e-9,
          "**第 0 段 0.2 → 0.1（锥形段起点侧）**");
    // 中间三段显式值为 0 ⇒ 回退到恒定宽度（组码 43 = 0）
    Check(pl->SegmentStartWidth(1) == 0.0 && pl->SegmentEndWidth(1) == 0.0,
          "第 1 段显式宽度为 0 ⇒ 回退恒定宽度（DXF 的「0 = 用默认值」）");
    // 末顶点（索引 4）的宽度作用于「4 → 0」这一段，开放多段线里它没有段 ⇒ 被丢弃（与 DXF 语义一致）
    std::printf("    末顶点宽度 (0.1→0.5)：开放多段线无对应段，按 DXF 语义丢弃\n");

    // ── 2. 恒定宽度仍是 43（此图没有 43，应为 0）─────────────────────────
    std::printf("    恒定宽度 = %.3f\n", pl->GetWidth());
    Check(pl->GetWidth() == 0.0, "组码 43（恒定宽度）为 0，与 DXF 一致");

    // ── 3. 最大实际宽度用逐段值（决定是否走填充路径）──────────────────────
    //   显式宽度都 < 1，恒定宽度 0 ⇒ MaxEffectiveWidth 应为 0.2（第 0 段起点侧）
    std::printf("    最大实际宽度 = %.3f\n", pl->MaxEffectiveWidth());
    Check(std::abs(pl->MaxEffectiveWidth() - 0.2) < 1e-9, "最大实际宽度取逐段值（0.2），不是恒定宽度 0");

    // ── 3b. 0.2 这种「小于 1」的真实宽度也必须画成带子 ────────────────────
    //   旧实现的判据是 `宽度 >= 1.0`，会把用户样例（0.2 → 0.1）当成细线整条丢掉宽度：
    //   数据保住了，画面上看不出任何差别 —— 等于没修。
    {
        TriSink sink;
        pl->Draw(sink, false, false);
        std::printf("    用户样例（段宽 0.2→0.1）绘制：三角形 %d 个，X 范围 [%.3f, %.3f]，Y 范围 [%.3f, %.3f]\n",
                    sink.triangles, sink.minX, sink.maxX, sink.minY, sink.maxY);
        Check(sink.triangles > 0, "**0.2 的宽度也走带子路径（旧判据 >= 1.0 会画成细线，宽度看不见）**");

        // 判据不用绝对坐标（这条多段线在 y≈7..11），改用**带宽**：
        // 沿中心线取一段，量它两侧的偏移量应约等于该段半宽（0.1 → 0.05）。
        // 看填充三角形的顶点到中心线的最大距离即可：等于最大半宽 0.1。
        const Polyline& pline = pl->GetPolyline();
        double maxOff = 0.0;
        for (const auto& v : sink.verts)
            for (int k = 0; k < 6; k += 2)
            {
                const Math::Point3 q{ v[k], v[k + 1], 0 };
                double best = 1e300;
                for (int s = 0; s < pline.SegCount(); ++s)
                {
                    const Math::Point3& A = pline.SegStart(s);
                    const Math::Point3& B = pline.SegEnd(s);
                    const double dx = B.x - A.x, dy = B.y - A.y;
                    const double len2 = dx * dx + dy * dy;
                    if (len2 < 1e-18) continue;
                    double t = ((q.x - A.x) * dx + (q.y - A.y) * dy) / len2;
                    t = std::clamp(t, 0.0, 1.0);
                    best = std::min(best, std::hypot(q.x - (A.x + dx * t), q.y - (A.y + dy * t)));
                }
                maxOff = std::max(maxOff, best);
            }
        std::printf("    填充顶点到中心线的最大距离（= 最大半宽）= %.4f（期望 0.1 = 0.2/2）\n", maxOff);
        Check(std::abs(maxOff - 0.1) < 1e-5, "**带子宽度 = 段宽 0.2 ⇒ 半宽 0.1（不是恒定的细线）**");
    }

    // ── 4. 锥形：两端半宽不同时必须画成梯形（逐段宽度真的进了渲染）──────────
    {
        // 直接构造一条粗的锥形多段线：2 个顶点，起端宽 4、末端宽 1
        auto thick = std::make_unique<PolylineEntity>(scene.NextObjectID(),
            std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 } },
            std::vector<double>{ 0.0 });
        thick->SetWidth(0.0);
        thick->SetSegmentWidths({ 4.0 }, { 1.0 });
        TriSink sink;
        thick->Draw(sink, false, false);
        std::printf("    锥形粗线：三角形 %d 个，X 范围 [%.2f, %.2f]，Y 范围 [%.2f, %.2f]\n",
                    sink.triangles, sink.minX, sink.maxX, sink.minY, sink.maxY);
        Check(sink.triangles > 0, "锥形段走填充路径");
        // 起点端半宽 2、终点端半宽 0.5 ⇒ Y 从 -2 到 +2（起点）收窄到 -0.5..+0.5（终点）
        Check(std::abs(sink.maxY - 2.0) < 1e-6 && std::abs(sink.minY + 2.0) < 1e-6,
              "**起点端半宽 = 4/2 = 2 ⇒ 梯形在 x=0 处张开到 ±2（不是等宽的 0.5）**");
        // 检查 x≈10 处的端点：找所有顶点里 x 最大者，其 |y| 应为 0.5
        double maxXy = 0.0;
        for (const auto& v : sink.verts)
            for (int k = 0; k < 6; k += 2)
                if (std::abs(v[k] - 10.0) < 1e-9) maxXy = std::max(maxXy, std::abs(v[k + 1]));
        std::printf("    终点端（x=10）最大 |y| = %.3f（应为半宽 0.5）\n", maxXy);
        Check(std::abs(maxXy - 0.5) < 1e-6, "**终点端半宽 = 1/2 = 0.5 ⇒ 梯形在 x=10 处收窄到 ±0.5**");
    }

    // ── 5. 与恒定宽度路径的几何一致性：恒宽时等价于矩形 ─────────────────────
    {
        auto rect = std::make_unique<PolylineEntity>(scene.NextObjectID(),
            std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 } },
            std::vector<double>{ 0.0 });
        rect->SetWidth(0.0);
        rect->SetSegmentWidths({ 2.0 }, { 2.0 });   // 两端半宽都是 1 ⇒ 矩形
        TriSink sink;
        rect->Draw(sink, false, false);
        Check(std::abs(sink.maxY - 1.0) < 1e-6 && std::abs(sink.minY + 1.0) < 1e-6,
              "两端等宽 ⇒ 退化为矩形（半宽 1），与旧的恒定宽度实现一致");
    }

    // ── 6. 导出再导入：逐段宽度必须原样回来（否则另存即退化）────────────────
    for (const auto kind : { CadFileKind::Dxf, CadFileKind::Dwg })
    {
        const char* label = kind == CadFileKind::Dxf ? "DXF" : "DWG";
        Scene src;
        Check(ImportCad(std::vector<std::uint8_t>(text.begin(), text.end()), src, nullptr, nullptr),
              (std::string("（") + label + " 往返）重新导入").c_str());
        const auto bytes = ExportCad(src, kind);
        Check(!bytes.empty(), (std::string("（") + label + " 往返）导出成功").c_str());
        Scene back;
        const bool ok = !bytes.empty() && ImportCad(bytes, back, nullptr, nullptr);
        Check(ok, (std::string("（") + label + " 往返）导出后再导入").c_str());
        if (!ok) continue;

        const PolylineEntity* b = FindPolyline(back);
        Check(b != nullptr, (std::string("（") + label + " 往返）找到多段线").c_str());
        if (!b) continue;
        std::printf("    %s 往返后：", label);
        for (int i = 0; i < b->GetPolyline().SegCount(); ++i)
            std::printf("[%d] %.3f→%.3f  ", i, b->SegmentStartWidth(i), b->SegmentEndWidth(i));
        std::printf("\n");
        Check(std::abs(b->SegmentStartWidth(0) - 0.2) < 1e-9 && std::abs(b->SegmentEndWidth(0) - 0.1) < 1e-9,
              (std::string("**") + label + " 往返后第 0 段仍是 0.2 → 0.1（与导入成对，否则另存后整条变等宽）**").c_str());
    }

    // ── 7. 恒定宽度多段线不受影响（不能被新逻辑改坏）───────────────────────
    {
        std::string dxf =
            "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n5\nCC\n8\n0\n"
            "100\nAcDbPolyline\n90\n2\n70\n0\n"
            "10\n0.0\n20\n0.0\n10\n10.0\n20\n0.0\n"
            "43\n5.0\n0\nENDSEC\n0\nEOF\n";
        Scene s;
        Check(ImportCad(std::vector<std::uint8_t>(dxf.begin(), dxf.end()), s, nullptr, nullptr),
              "（恒宽用例）含组码 43 的多段线能导入");
        const PolylineEntity* p = FindPolyline(s);
        Check(p != nullptr, "（恒宽用例）找到多段线");
        if (p)
        {
            std::printf("    （恒宽用例）恒定宽度 = %.3f，逐段宽度 = %s\n",
                        p->GetWidth(), p->HasSegmentWidths() ? "有" : "无");
            Check(std::abs(p->GetWidth() - 5.0) < 1e-9, "恒定宽度 43 读得对");
            Check(!p->HasSegmentWidths(), "没有 40/41 ⇒ 不算锥形段（HasSegmentWidths 为假）");
            Check(std::abs(p->SegmentStartWidth(0) - 5.0) < 1e-9, "锥形宽度回退到恒定宽度 5.0");
            TriSink sink;
            p->Draw(sink, false, false);
            Check(sink.triangles > 0 && std::abs(sink.maxY - 2.5) < 1e-6,
                  "恒宽 5.0 仍画成矩形带子（半宽 2.5），旧行为不变");
        }
    }

    // ── 8. 宽度为 0 的多段线仍走细线（不能被新判据拖慢）───────────────────
    {
        PolylineEntity thin(9999, std::vector<Math::Point3>{ { 0, 0, 0 }, { 10, 0, 0 } });
        thin.SetWidth(0.0);
        TriSink sink;
        thin.Draw(sink, false, false);
        std::printf("    宽度 0 的多段线：三角形 %d 个，线段 %d 条\n", sink.triangles, sink.lines);
        Check(sink.triangles == 0 && sink.lines > 0, "**宽度 0 ⇒ 走细线路径（不产生填充三角形）**");
    }

    std::printf("多段线宽度用例失败：%d\n", g_fail);
    return g_fail;
}
