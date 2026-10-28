// ── OLE 对象（OLE2FRAME）、复杂线型、文字样式导入与延迟虚线相位测试 ─────────────────────
// 测哪些：
//   · Core/Ole/OleObject：OLE2FRAME 头（版本 0x5581 + 四个角点）、图像路径 "ole:<哈希>"、DIB 解码（合成数据，不依赖图纸）
//   · DrawContext::LayPattern 的起点相位：布局页视口裁掉一段延迟虚线的前半段后，后半段从原相位接着铺
//   · OleFrameEntity 经 JSON 往返（数据 base64）
//   · tests/Data/0525最终改总平面图.dwg（用户图纸，不在版本库里，没有就跳过）：
//       5 个 OLE2FRAME 导入为图纸空间的 OleFrameEntity，能从复合文档取出 WMF / EMF 展示图；
//       文字样式表没有 "_W70" 之类的派生样式、预置的 Simplex 被删、GB2312 / Standard 按图纸的参数；
//       含形的线型 1161 保留 0 长度的形段与 1.6 的间隔（以前整条线消失）；$PSLTSCALE = 0
#include "Core/Ole/OleObject.h"
#include "Core/Entity/OleFrameEntity.hpp"
#include "Core/Entity/ViewportEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Document/Document.h"
#include "Document/DrawContext.hpp"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
    bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

    void PutDouble(std::vector<uint8_t>& v, double d)
    {
        uint8_t b[8];
        std::memcpy(b, &d, 8);
        v.insert(v.end(), b, b + 8);
    }
    void PutU32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
    void PutU16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }

    // AutoCAD 的 OLE2FRAME 头：版本 + 四个角点（左上、右上、右下、左下）+ 30 字节（末尾 4 字节是复合文档长度）
    std::vector<uint8_t> MakeFrameHeader(double x0, double y0, double x1, double y1)
    {
        std::vector<uint8_t> v;
        PutU16(v, 0x5581);
        const double pts[4][2] = { { x0, y1 }, { x1, y1 }, { x1, y0 }, { x0, y0 } };
        for (const auto& p : pts) { PutDouble(v, p[0]); PutDouble(v, p[1]); PutDouble(v, 0.0); }
        v.insert(v.end(), 26, 0);
        PutU32(v, 0);
        return v;
    }

    void TestFrameHeader()
    {
        const std::vector<uint8_t> data = MakeFrameHeader(10, -30, 50, -20);
        Ole::Frame f;
        Check(Ole::ParseFrame(data, f), "OLE2FRAME 头：解析成功");
        Check(Near(f.Corners[0].x, 10) && Near(f.Corners[0].y, -20) && Near(f.Corners[2].x, 50) && Near(f.Corners[2].y, -30),
              "OLE2FRAME 头：四个角点（左上 / 右下）");
        Check(f.CompoundOffset == 128 && f.CompoundSize == 0, "OLE2FRAME 头：复合文档起点 128、长度 0");
        Ole::Presentation pres;
        Check(!Ole::ExtractPresentation(data, pres), "没有复合文档：取不到展示图");

        const uint64_t key = Ole::HashData(data);
        uint64_t parsed = 0;
        Check(key != 0 && Ole::ParseImagePath(Ole::ImagePath(key), parsed) && parsed == key, "图像路径 ole:<哈希> 往返");
        Check(!Ole::ParseImagePath("ole:xyz", parsed) && !Ole::ParseImagePath("a.png", parsed), "非 OLE 路径不解析");
    }

    void TestDib()
    {
        // 2×2 的 24 位 DIB（自下而上，行按 4 字节对齐）：下行 红、绿，上行 蓝、白
        std::vector<uint8_t> dib;
        PutU32(dib, 40); PutU32(dib, 2); PutU32(dib, 2); PutU16(dib, 1); PutU16(dib, 24); PutU32(dib, 0);
        PutU32(dib, 0); PutU32(dib, 0); PutU32(dib, 0); PutU32(dib, 0); PutU32(dib, 0);
        const uint8_t bottom[8] = { 0, 0, 255, 0, 255, 0, 0, 0 };     // BGR：红、绿 + 2 字节填充
        const uint8_t top[8]    = { 255, 0, 0, 255, 255, 255, 0, 0 };  // 蓝、白
        dib.insert(dib.end(), bottom, bottom + 8);
        dib.insert(dib.end(), top, top + 8);
        auto img = Ole::DecodeDib(dib, 7);
        Check(img && img->Width == 2 && img->Height == 2 && img->Key == 7, "DIB 解码：尺寸与键");
        if (img)
        {
            const auto& p = img->Rgba;
            Check(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255, "DIB 解码：第一行第一个像素是蓝（自下而上翻转）");
            Check(p[8] == 255 && p[9] == 0 && p[10] == 0, "DIB 解码：第二行第一个像素是红");
        }
        std::vector<uint8_t> bad(dib.begin(), dib.begin() + 30);
        Check(Ole::DecodeDib(bad, 1) == nullptr, "DIB 解码：数据不完整返回空");
    }

    // UTF-8 文字 ↔ 路径：窄字符串直接构造 path 会按系统代码页解释（英文系统遇到中文抛异常），
    // 反过来 path::string() 也一样 —— 见 AGENTS.md「路径与编码」
    std::filesystem::path Utf8Path(const char* utf8)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8)));
    }

    std::string Utf8Name(const std::filesystem::path& p)
    {
        const std::u8string u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    // 沿 (0,0)→(10,0) 按 dash 2 / gap 2 铺，返回实段的起止 x
    std::vector<std::pair<double, double>> Lay(double startOffset)
    {
        LineTypeRecord rec;
        rec.Name = "T";
        rec.Pattern = { 2.0, -2.0 };
        rec.PatternLength = 4.0;
        std::vector<std::pair<double, double>> segs;
        DrawContext::LayPattern({ 0, 0, 0 }, { 10, 0, 0 }, rec, 1.0, 0.0,
                                [&](const Math::Point3& a, const Math::Point3& b) { segs.emplace_back(a.x, b.x); },
                                [](const Math::Point3&) {}, startOffset);
        return segs;
    }

    void TestLayPatternPhase()
    {
        auto s0 = Lay(0.0);
        Check(s0.size() == 3 && Near(s0[0].first, 0) && Near(s0[0].second, 2) && Near(s0[2].first, 8) && Near(s0[2].second, 10),
              "LayPattern 相位 0：[0,2] [4,6] [8,10]");
        auto s1 = Lay(1.0);
        Check(s1.size() == 3 && Near(s1[0].first, 0) && Near(s1[0].second, 1) && Near(s1[1].first, 3) && Near(s1[1].second, 5) &&
              Near(s1[2].first, 7) && Near(s1[2].second, 9),
              "LayPattern 相位 1：首段只剩 1，之后 [3,5] [7,9]");
        auto s3 = Lay(3.0);
        Check(s3.size() == 3 && Near(s3[0].first, 1) && Near(s3[0].second, 3) && Near(s3[2].first, 9) && Near(s3[2].second, 10),
              "LayPattern 相位 3：起点落在间隔里，首段从 1 开始");
        auto s5 = Lay(5.0);
        Check(s5.size() == s1.size() && Near(s5[0].second, s1[0].second), "LayPattern 相位 5 与相位 1 相同（对周期取模）");
        // 与裁剪的配合：整条 (-3,0)→(10,0) 的相位 0 铺法，裁掉前 3 后等于从 0 起、相位 3 的铺法
        LineTypeRecord rec;
        rec.Pattern = { 2.0, -2.0 };
        rec.PatternLength = 4.0;
        std::vector<double> full;
        DrawContext::LayPattern({ -3, 0, 0 }, { 10, 0, 0 }, rec, 1.0, 0.0,
                                [&](const Math::Point3& a, const Math::Point3& b) { if (b.x > 0) full.push_back(std::max(a.x, 0.0)); },
                                [](const Math::Point3&) {});
        Check(full.size() == s3.size() && Near(full[0], s3[0].first) && Near(full[1], s3[1].first), "裁剪后按相位接着铺与整条铺的结果一致");
    }

    void TestJsonRoundTrip()
    {
        Document doc;
        auto& scene = doc.GetScene();
        auto e = std::make_unique<OleFrameEntity>(scene.NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 4, 0, 0 },
                                                  Math::Point3{ 4, 3, 0 }, Math::Point3{ 0, 3, 0 });
        auto data = std::make_shared<const std::vector<uint8_t>>(MakeFrameHeader(0, 0, 4, 3));
        e->SetData(data);
        e->SetOleType(3);
        const uint64_t key = e->GetDataKey();
        scene.AddEntity(std::move(e));

        const std::string path = Utf8Name(std::filesystem::current_path() / "ole_roundtrip.json");
        Check(doc.SaveAs(path), "OLE 对象写 JSON");
        Document back;
        std::string err;
        Check(back.LoadFromFile(path, &err), "OLE 对象读 JSON");
        const OleFrameEntity* found = nullptr;
        back.GetScene().ForEachObject([&](const Object& o) { if (!found) found = dynamic_cast<const OleFrameEntity*>(&o); });
        Check(found != nullptr, "读回的是 OleFrameEntity");
        if (found)
        {
            Check(found->GetData() && *found->GetData() == *data && found->GetDataKey() == key, "数据与哈希一致");
            Check(found->GetOleType() == 3 && Near(found->GetRectangle().P3.x, 4) && Near(found->GetRectangle().P3.y, 3), "类型与顶点一致");
            Check(Ole::DataStore::Instance().Get(key) != nullptr, "读回后数据已登记（Editor 据此光栅化）");
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // 视口：DCS 视图中心按目标点换算；冻结图层随 JSON 往返
    void TestViewportFrozenLayers()
    {
        std::printf("-- 视口：目标点与冻结图层\n");
        ViewportEntity vp(1, Math::Point3{ 100, 50, 0 }, 200, 100);
        vp.SetViewCenterDcs(Math::Point3{ -80, -5, 0 }, 0.0, Math::Point3{ 64000, 86000, 0 });
        Check(Near(vp.GetViewCenter().x, 63920) && Near(vp.GetViewCenter().y, 85995), "视图中心 = 目标点 + DCS 偏移");
        vp.SetFrozenLayers({ 7, 3, 7 });
        Check(vp.GetFrozenLayers().size() == 2 && vp.IsLayerFrozen(3) && vp.IsLayerFrozen(7) && !vp.IsLayerFrozen(1), "冻结图层去重，查询正确");
        auto clone = vp.Clone(2);
        Check(static_cast<ViewportEntity*>(clone.get())->IsLayerFrozen(7), "克隆带着冻结图层");

        Document doc;
        auto& scene = doc.GetScene();
        auto e = std::make_unique<ViewportEntity>(scene.NextObjectID(), Math::Point3{ 100, 50, 0 }, 200, 100);
        e->SetViewCenter(Math::Point3{ 500, 600, 0 });
        e->SetViewHeight(1000);
        e->SetFrozenLayers({ 5, 9 });
        scene.AddEntity(std::move(e));
        const std::string path = Utf8Name(std::filesystem::current_path() / "viewport_roundtrip.json");
        Check(doc.SaveAs(path), "视口写 JSON");
        Document back;
        std::string err;
        Check(back.LoadFromFile(path, &err), "视口读 JSON");
        const ViewportEntity* found = nullptr;
        back.GetScene().ForEachObject([&](const Object& o) { if (!found) found = dynamic_cast<const ViewportEntity*>(&o); });
        Check(found && found->GetFrozenLayers() == std::vector<LayerID>{ 5, 9 } && Near(found->GetViewCenter().x, 500), "读回的视口冻结图层与视图中心一致");
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void TestSiteDrawing()
    {
        const std::filesystem::path path = Utf8Path(MINIDWG_SAMPLE_DIR).parent_path() / u8"0525最终改总平面图.dwg";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            std::printf("[跳过] 没有 tests/Data/0525最终改总平面图.dwg（用户图纸，不在版本库里）\n");
            return;
        }
        Document doc;
        std::string err;
        Check(doc.LoadFromFile(Utf8Name(path), &err), "打开 0525 总平面图");
        auto& scene = doc.GetScene();

        std::vector<const OleFrameEntity*> oles;
        scene.ForEachObject([&](const Object& o) { if (auto* e = dynamic_cast<const OleFrameEntity*>(&o)) oles.push_back(e); });
        Check(oles.size() == 5, "5 个 OLE2FRAME 导入为 OleFrameEntity");
        bool allPaper = !oles.empty(), presentations = !oles.empty(), matched = false;
        for (const auto* e : oles)
        {
            allPaper = allPaper && e->GetAttr().InPaperSpace;
            Ole::Presentation pres;
            presentations = presentations && e->GetData() && Ole::ExtractPresentation(*e->GetData(), pres) &&
                            (pres.Format == Ole::PictureFormat::Wmf || pres.Format == Ole::PictureFormat::Emf) &&
                            !pres.Data.empty() && pres.WidthHimetric > 0 && pres.HeightHimetric > 0;
            const auto& r = e->GetRectangle();
            if (Near(r.P1.x, 5500.39, 0.01) && Near(r.P1.y, -754.59, 0.01) && Near(r.P3.x, 5833.88, 0.01) && Near(r.P3.y, -694.72, 0.01))
                matched = true;
        }
        Check(allPaper, "OLE 对象都在图纸空间");
        Check(presentations, "每个 OLE 对象都能从复合文档取出 WMF / EMF 展示图");
        Check(matched, "其中一个的角点是 (5500.39,-754.59)～(5833.88,-694.72)（左下 / 右上）");

        const TextStyleTable& styles = scene.GetTextStyleTable();
        bool derived = false;
        for (const auto& rec : styles.Records())
            derived = derived || rec.Name.find("_W") != std::string::npos;
        Check(!derived, "文字样式表里没有 _W 派生样式");
        Check(styles.FindByName("Simplex") == TextStyleTable::InvalidID, "图纸里没有的预置样式 Simplex 已删");
        if (const TextStyleID id = styles.FindByName("GB2312"); id != TextStyleTable::InvalidID)
        {
            const TextStyleRecord& r = *styles.Find(id);
            Check(Near(r.WidthFactor, 0.7) && Near(r.Height, 5.0) && r.FontFile == "simkai.ttf", "GB2312 按图纸：宽 0.7、高 5、KaiTi_GB2312 → simkai.ttf");
        }
        else
            Check(false, "图纸的 GB2312 样式存在");
        Check(styles.Resolve(TextStyleTable::StandardID).FontFile == "simsun.ttc", "Standard 的字体按扩展数据的 SimSun → simsun.ttc");
        if (const TextStyleID id = styles.FindByName("宋体"); id != TextStyleTable::InvalidID)
            Check(styles.Find(id)->FontFile == "simhei.ttf" && Near(styles.Find(id)->WidthFactor, 0.7), "宋体样式：simhei.ttf、宽 0.7");

        const LineTypeTable& lts = scene.GetLineTypeTable();
        auto pattern = [&](const char* name) -> std::vector<double>
        {
            for (const auto& r : lts.Records())
                if (r.Name == name) return r.Pattern;
            return {};
        };
        const auto p1161 = pattern("1161");
        Check(p1161.size() == 2 && Near(p1161[0], 0.0) && Near(p1161[1], -1.6), "线型 1161：0 长度形段 + 1.6 间隔（不再只剩间隔）");
        const auto p444 = pattern("444");
        Check(p444.size() == 4 && Near(p444[0], 4.5) && Near(p444[1], 4.5) && Near(p444[2], -0.5) && Near(p444[3], -0.5), "线型 444：形段的长度保留");
        Check(!scene.GetPaperSpaceLineTypeScaling(), "$PSLTSCALE = 0 读入");
        Check(scene.Layouts().size() == 2, "两个布局（模型 + 总图）");
        if (scene.Layouts().size() == 2)
        {
            const DrawingLayout& sheet = scene.Layouts()[1];
            Check(sheet.ViewHeight > 0.0, "布局带着保存的图纸空间视图（主视口的中心与高度）");
            std::printf("    视图中心 (%.1f, %.1f) 高 %.1f；纸张 (%.1f, %.1f)～(%.1f, %.1f)\n", sheet.ViewCenterX, sheet.ViewCenterY, sheet.ViewHeight,
                        sheet.MinX, sheet.MinY, sheet.MaxX, sheet.MaxY);
        }

        // 视口 effb9（id=5）：目标点 (64093.1,86195.3) + DCS 偏移 (-80.7,-5.2)，冻结 3 个图层；id=2 的视口冻结 4 个
        const ViewportEntity* vp5 = nullptr;
        const ViewportEntity* vp2 = nullptr;
        scene.ForEachObject([&](const Object& o)
        {
            if (auto* v = dynamic_cast<const ViewportEntity*>(&o))
            {
                if (v->GetViewportId() == 5) vp5 = v;
                if (v->GetViewportId() == 2) vp2 = v;
            }
        });
        Check(vp5 != nullptr && vp2 != nullptr, "找到视口 id=5（effb9）与 id=2");
        if (vp5)
        {
            Check(std::abs(vp5->GetViewCenter().x - 64012.4) < 0.1 && std::abs(vp5->GetViewCenter().y - 86190.1) < 0.1,
                  "effb9 的视图中心 = 目标点 + DCS 偏移 ≈ (64012.4, 86190.1)");
            bool fire = false;
            for (LayerID lid : vp5->GetFrozenLayers())
                if (const Layer* l = scene.GetLayerManager().GetLayer(lid); l && l->GetName() == "消防环线") fire = true;
            Check(vp5->GetFrozenLayers().size() == 3 && fire, "effb9 冻结 3 个图层（含 消防环线）");
        }
        if (vp2)
            Check(vp2->GetFrozenLayers().size() == 4, "id=2 的视口冻结 4 个图层");

        // TCH_COORD F656B（= 1009003）：对象线型 __DASH，天正画坐标标注不用它，引线应是实线
        if (const BlockEntity* coord = scene.GetBlockTable().Get("*U_TCH_1009003"))
        {
            bool any = false, continuous = true;
            for (const auto& e : coord->GetEntities())
                if (e && e->IsKindOf<LineEntity>())
                {
                    any = true;
                    continuous = continuous && e->GetAttr().LineType == LineTypeTable::ContinuousID;
                }
            Check(any && continuous, "坐标标注 F656B（线型 __DASH）的引线按实线组装");
        }
        else
            Check(false, "坐标标注 F656B 已组装成 *U_TCH_1009003");
    }
}

int RunOleFrameTests()
{
    g_failures = 0;
    std::printf("\n=== OLE 对象 / 复杂线型 / 文字样式导入 ===\n");
    TestFrameHeader();
    TestDib();
    TestLayPatternPhase();
    TestJsonRoundTrip();
    TestViewportFrozenLayers();
    TestSiteDrawing();
    return g_failures;
}
