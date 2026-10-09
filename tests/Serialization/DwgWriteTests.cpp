#include "Codec/CodePage.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Dwg/Read/DwgDecompress.h"
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgCompress.h"
#include "Dwg/Write/DwgWriter.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Write/DxfWriter.h"
#include "CompareDatabases.h"
#include "TestData.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace MiniDWG;
using namespace MiniDWG::Test;

// 位流写入与读取一一对应
TEST(DwgWrite_BitWriter_RoundTrip)
{
    for (CadVersion version : { CadVersion::AC1015, CadVersion::AC1018, CadVersion::AC1024 })
    {
        DwgBitWriter w(version, Codec::CodePage::Gbk);
        const std::int16_t shorts[] = { 0, 1, 255, 256, 257, -1, 32767, -32768 };
        const std::int32_t longs[] = { 0, 1, 255, 256, -1, 0x7FFFFFFF, -0x7FFFFFFF - 1 };
        const double doubles[] = { 0.0, -0.0, 1.0, -1.0, 0.1, 1e300, 3.14159 };
        w.WriteBit(true);       // 打乱字节对齐
        for (std::int16_t v : shorts)
            w.WriteBitShort(v);
        for (std::int32_t v : longs)
            w.WriteBitLong(v);
        for (std::int64_t v : { std::int64_t(0), std::int64_t(1), std::int64_t(0x123456789A) })
            w.WriteBitLongLong(v);
        for (double v : doubles)
            w.WriteBitDouble(v);
        // DD：各种与默认值部分相同的情况
        const std::pair<double, double> dd[] = { { 1.0, 1.0 }, { 1.0, 1.0000001 }, { 100.0, 100.5 }, { 1.0, -7e10 }, { 0.0, 2.0 } };
        for (const auto& [def, v] : dd)
            w.WriteBitDoubleWithDefault(def, v);
        for (std::uint64_t v : { 0ull, 127ull, 128ull, 0x123456789ull })
            w.WriteModularChar(v);
        for (std::int64_t v : { std::int64_t(0), std::int64_t(63), std::int64_t(64), std::int64_t(-1), std::int64_t(-1000000) })
            w.WriteSignedModularChar(v);
        for (std::uint32_t v : { 0u, 0x7FFFu, 0x8000u, 0x12345678u })
            w.WriteModularShort(v);
        for (Handle h : { Handle(0), Handle(0x1F), Handle(0x1234), Handle(0x123456789A) })
            w.WriteHandle(DwgRef::HardPointer, h);
        w.WriteVariableText("图层 A");
        w.WriteVariableText("");
        w.WriteTextUnicode("扩展");
        const Color colors[] = { Color::ByLayer(), Color::ByBlock(), Color(std::int16_t(7)), Color::FromRgb(10, 20, 30) };
        for (const Color& c : colors)
            w.WriteCmColor(c);
        for (const Color& c : colors)
            w.WriteEnColor(c, Transparency(std::int16_t(40)));
        w.WriteEnColor(Color::ByBlock(), Transparency::ByLayer());
        for (std::int16_t t : { std::int16_t(1), std::int16_t(0x1F3), std::int16_t(0x52), std::int16_t(510), std::int16_t(800) })
            w.WriteObjectType(t);
        w.WriteJulianDate(2459632.5);
        w.WriteBitExtrusion({ 0, 0, 1 });
        w.WriteBitExtrusion({ 0, 1, 0 });
        w.WriteBitThickness(0.0);
        w.WriteBitThickness(2.5);

        const std::vector<std::uint8_t> data = w.Data();
        DwgBitReader r(data, version, Codec::CodePage::Gbk);
        CHECK(r.ReadBit());
        for (std::int16_t v : shorts)
            CHECK(r.ReadBitShort() == v);
        for (std::int32_t v : longs)
            CHECK(r.ReadBitLong() == v);
        for (std::int64_t v : { std::int64_t(0), std::int64_t(1), std::int64_t(0x123456789A) })
            CHECK(r.ReadBitLongLong() == v);
        for (double v : doubles)
        {
            const double x = r.ReadBitDouble();
            CHECK(x == v && std::signbit(x) == std::signbit(v));
        }
        for (const auto& [def, v] : dd)
            CHECK(r.ReadBitDoubleWithDefault(def) == v);
        for (std::uint64_t v : { 0ull, 127ull, 128ull, 0x123456789ull })
            CHECK(r.ReadModularChar() == v);
        for (std::int64_t v : { std::int64_t(0), std::int64_t(63), std::int64_t(64), std::int64_t(-1), std::int64_t(-1000000) })
            CHECK(r.ReadSignedModularChar() == v);
        for (std::uint32_t v : { 0u, 0x7FFFu, 0x8000u, 0x12345678u })
            CHECK(r.ReadModularShort() == v);
        for (Handle h : { Handle(0), Handle(0x1F), Handle(0x1234), Handle(0x123456789A) })
            CHECK(r.ReadHandle() == h);
        CHECK(r.ReadVariableText() == "图层 A");
        CHECK(r.ReadVariableText().empty());
        CHECK(r.ReadTextUnicode() == "扩展");
        for (const Color& c : colors)
        {
            const Color x = r.ReadCmColor(r);
            CHECK(version >= CadVersion::AC1018 ? x == c : x == Color(c.ApproxIndex()));
        }
        for (const Color& c : colors)
        {
            Transparency t;
            bool book = false;
            const Color x = r.ReadEnColor(t, book);
            CHECK(!book);
            if (version >= CadVersion::AC1018)
                CHECK(x == c && t == Transparency(std::int16_t(40)));
            else
                CHECK(x == Color(c.ApproxIndex()));
        }
        {
            Transparency t;
            bool book = false;
            CHECK(r.ReadEnColor(t, book).IsByBlock() && t.IsByLayer());
        }
        for (std::int16_t t : { std::int16_t(1), std::int16_t(0x1F3), std::int16_t(0x52), std::int16_t(510), std::int16_t(800) })
            CHECK(r.ReadObjectType() == t);
        CHECK(r.ReadJulianDate() == 2459632.5);
        CHECK(r.ReadBitExtrusion() == XYZ::AxisZ());
        CHECK((r.ReadBitExtrusion() == XYZ{ 0, 1, 0 }));
        CHECK(r.ReadBitThickness() == 0.0);
        CHECK(r.ReadBitThickness() == 2.5);
        CHECK(!r.Failed());
    }
}

// R2004 的 LZ77：压缩后能解压回原数据
TEST(DwgWrite_Compress_RoundTrip)
{
    std::mt19937 rng(12345);
    std::vector<std::vector<std::uint8_t>> inputs;
    inputs.push_back({});
    inputs.push_back({ 1, 2, 3 });
    inputs.push_back(std::vector<std::uint8_t>(0x7400, 0));
    {
        std::vector<std::uint8_t> v(50000);
        for (auto& b : v)
            b = static_cast<std::uint8_t>(rng());
        inputs.push_back(std::move(v));
    }
    {
        // 重复片段多、距离远近不一（覆盖各种操作码）
        std::vector<std::uint8_t> v;
        for (int i = 0; i < 0x7400; ++i)
            v.push_back(static_cast<std::uint8_t>((i % 37) * ((i / 1000) % 5) + (rng() % 3 == 0 ? rng() % 4 : 0)));
        inputs.push_back(std::move(v));
    }
    const auto sample = ReadFileAll(SamplePath("sample_AC1015.dwg"));
    inputs.emplace_back(sample.begin(), sample.begin() + std::min<std::size_t>(sample.size(), 0x7400 * 3));

    for (const auto& input : inputs)
    {
        std::vector<std::uint8_t> compressed;
        DwgCodec::CompressAC18(input, compressed);
        std::vector<std::uint8_t> out;
        std::size_t pos = 0;
        CHECK(DwgCodec::DecompressAC18(compressed, pos, out));
        CHECK(out == input);
        // 与 AutoCAD 一致，结束操作码 0x11 之后还有两个 0 字节（计入压缩大小）
        CHECK(pos + 2 == compressed.size());
    }
}

// AC18 优化后的复制仍要支持重叠回溯，并拒绝截断/越界输入。
TEST(DwgRead_AC18_OverlappingMatchAndTruncatedInput)
{
    // 一个字面量 A，之后距离为 1 的三个字节回溯，必须使用刚写出的字节。
    const std::vector<std::uint8_t> compressed{ 0x12, 'A', 0x21, 0, 0, 0x11, 0, 0 };
    std::vector<std::uint8_t> out{ 9, 8 };
    std::size_t pos = 0;
    CHECK(DwgCodec::DecompressAC18(compressed, pos, out));
    CHECK(out == (std::vector<std::uint8_t>{ 9, 8, 'A', 'A', 'A', 'A' }));
    CHECK(pos == 6);

    for (const std::vector<std::uint8_t>& invalid : {
             std::vector<std::uint8_t>{ 0x15, 1, 2 },           // 字面量不足
             std::vector<std::uint8_t>{ 0x12, 'A', 0x21, 0 },   // 回溯距离被截断
             std::vector<std::uint8_t>{ 0x12, 'A', 0x21, 8, 0 } // 回溯越界
         })
    {
        out.clear();
        pos = 0;
        CHECK(!DwgCodec::DecompressAC18(invalid, pos, out));
    }
}

TEST(DwgRead_ModelSpaceEntityOrder)
{
    CadDatabase db;
    db.CreateDefaults();
    auto* model = db.ModelSpace();
    CHECK(model != nullptr);
    if (!model)
        return;
    for (int i = 0; i < 256; ++i)
    {
        auto line = std::make_unique<Line>();
        line->StartPoint = { static_cast<double>(i), 0, 0 };
        line->EndPoint = { static_cast<double>(i), 1, 0 };
        db.AddEntity(model, std::move(line));
    }
    for (CadVersion version : { CadVersion::AC1015, CadVersion::AC1018, CadVersion::AC1021, CadVersion::AC1032 })
    {
        DwgWriteOptions options;
        options.Version = version;
        const auto bytes = WriteDwg(db, options);
        auto read = ReadDwg(bytes);
        CHECK(read != nullptr);
        if (!read)
            continue;
        const auto* result = read->ModelSpace();
        CHECK(result != nullptr);
        if (!result)
            continue;
        CHECK(result->Entities.size() == 256);
        for (std::size_t i = 0; i < result->Entities.size(); ++i)
        {
            const auto* line = read->FindAs<Line>(result->Entities[i]);
            CHECK(line != nullptr);
            if (line)
                CHECK(line->StartPoint.X == static_cast<double>(i));
        }
    }
}

// CRC 与 AutoCAD 写出的文件一致（R2000 文件头的 CRC）
TEST(DwgWrite_Checksums)
{
    const auto data = ReadFileAll(SamplePath("sample_AC1015.dwg"));
    const std::size_t end = 0x19 + 9 * 6;
    const std::uint16_t stored = static_cast<std::uint16_t>(data[end] | (data[end + 1] << 8));
    CHECK(DwgCodec::Crc16(0xC0C1, std::span<const std::uint8_t>(data).first(end)) == stored);
    const std::uint8_t text[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    CHECK(DwgCodec::Crc32(0, text) == 0xCBF43926u);     // 标准 CRC-32 校验值
}

namespace
{
    std::vector<std::uint8_t> WriteDwgChecked(const CadDatabase& db, CadVersion version, std::vector<std::string>& warnings)
    {
        DwgWriteOptions options;
        options.Version = version;
        options.Notify = [&](NotificationType type, std::string_view message) {
            if (type != NotificationType::Info)
                warnings.emplace_back(message);
        };
        return WriteDwg(db, options);
    }

    std::unique_ptr<CadDatabase> ReadDwgChecked(std::span<const std::uint8_t> data, std::vector<std::string>& warnings)
    {
        DwgReadOptions options;
        options.Notify = [&](NotificationType type, std::string_view message) {
            if (type != NotificationType::Info)
                warnings.emplace_back(message);
        };
        return ReadDwg(data, options);
    }

    // 写 DWG 时有意不往返、或目标版本存不了的内容（a 为原数据库的对象，b 为读回的对象）
    bool KnownDwgWriteDiff(const CadObject& a, const CadObject& b, std::string_view what, CadVersion from, CadVersion to)
    {
        (void)from;
        auto starts = [&](std::string_view prefix) { return what.substr(0, prefix.size()) == prefix; };
        // 差异描述以属性名开头，后面是"（组码 ……）"或结束
        auto prop = [&](std::initializer_list<std::string_view> names) {
            for (std::string_view n : names)
            {
                if (what.substr(0, n.size()) == n
                    && (what.size() == n.size() || what.substr(n.size(), 3) == "\xEF\xBC\x88"))
                    return true;
            }
            return false;
        };

        // 符号表：DWG 中 ByBlock/ByLayer、*Model_Space/*Paper_Space 固定在前（R12 DXF 读入时补在最后）
        if (auto* ta = dynamic_cast<const CadTable*>(&a); ta && starts("Entries"))
        {
            auto x = ta->Entries;
            auto y = static_cast<const CadTable&>(b).Entries;
            std::sort(x.begin(), x.end());
            std::sort(y.begin(), y.end());
            return x == y;
        }
        // 多边形裁剪边界：DXF 中多一个与首点相同的闭合点，DWG 中不存
        if (auto* wa = dynamic_cast<const CadWipeoutBase*>(&a); wa && starts("ClipBoundaryVertices"))
        {
            const auto& va = wa->ClipBoundaryVertices;
            const auto& vb = static_cast<const CadWipeoutBase&>(b).ClipBoundaryVertices;
            return va.size() == vb.size() + 1 && vb.size() > 2 && va.front() == va.back()
                && std::equal(vb.begin(), vb.end(), va.begin());
        }
        // R2010 起 DWG 不存引线的文字框尺寸
        if (dynamic_cast<const Leader*>(&a) && to >= CadVersion::AC1024 && prop({ "TextHeight", "TextWidth" }))
            return true;
        // 只有控制点的样条（AutoCAD 写 DXF 时把拟合点样条写成算出的控制点）：DXF 组码 70 高位的
        // "按拟合点、用节点参数化"在 DWG 中表达不了（R2013 之前不存，之后与拟合数据绑定）
        if (auto* sa = dynamic_cast<const Spline*>(&a); sa && sa->FitPoints.empty() && prop({ "Flags" }))
            return (static_cast<int>(sa->Flags) & 0x7F) == (static_cast<int>(static_cast<const Spline&>(b).Flags) & 0x7F);

        // ── 写成更旧的版本时存不了的属性 ──
        if (to < CadVersion::AC1018)
        {
            // R2004 之前：没有真彩色（取最接近的索引色）与透明度
            auto approx = [](const Color& x, const Color& y) { return x.IsTrueColor() && y == Color(x.ApproxIndex()); };
            if (auto* ea = dynamic_cast<const Entity*>(&a))
            {
                if (prop({ "Transparency" }))
                    return true;
                if (prop({ "Color" }))
                    return approx(ea->Color, static_cast<const Entity&>(b).Color);
            }
            if (auto* la = dynamic_cast<const Layer*>(&a); la && prop({ "Color" }))
                return approx(la->Color, static_cast<const Layer&>(b).Color);
            if (auto* da = dynamic_cast<const DimensionStyle*>(&a))
            {
                auto* db = static_cast<const DimensionStyle*>(&b);
                if (prop({ "DimensionLineColor" }))
                    return approx(da->DimensionLineColor, db->DimensionLineColor);
                if (prop({ "ExtensionLineColor" }))
                    return approx(da->ExtensionLineColor, db->ExtensionLineColor);
                if (prop({ "TextColor" }))
                    return approx(da->TextColor, db->TextColor);
            }
            if (dynamic_cast<const PlotSettings*>(&a) && prop({ "ShadePlotMode", "ShadePlotResolutionMode", "ShadePlotDPI" }))
                return true;
            if (dynamic_cast<const MText*>(&a)
                && prop({ "BackgroundColor", "BackgroundFillFlags", "BackgroundScale", "BackgroundTransparency" }))
                return true;
            if (auto* ha = dynamic_cast<const Hatch*>(&a); ha && ha->GradientColor.Enabled && starts("Paths/Pattern/Gradient"))
                return true;
            if (dynamic_cast<const Viewport*>(&a) && prop({ "ShadePlotMode" }))
                return true;
            // 多重引线（含上下文数据）与样式中的颜色
            if (auto* ca = dynamic_cast<const MultiLeaderObjectContextData*>(&a))
            {
                auto* cb = static_cast<const MultiLeaderObjectContextData*>(&b);
                if (prop({ "BackgroundFillColor" }))
                    return approx(ca->BackgroundFillColor, cb->BackgroundFillColor);
                if (prop({ "TextColor" }))
                    return approx(ca->TextColor, cb->TextColor);
            }
            if (dynamic_cast<const MultiLeader*>(&a) || dynamic_cast<const MultiLeaderStyle*>(&a))
            {
                if (prop({ "LineColor", "TextColor", "BlockContentColor" }))
                    return true;
            }
        }
        if (to < CadVersion::AC1024)
        {
            // R2010 之前：多重引线没有上、下附着方式与附着方向（引线的替代属性也不存）
            if ((dynamic_cast<const MultiLeader*>(&a) || dynamic_cast<const MultiLeaderStyle*>(&a)
                 || dynamic_cast<const MultiLeaderObjectContextData*>(&a))
                && prop({ "TextTopAttachment", "TextBottomAttachment", "TextAttachmentDirection" }))
                return true;
            if (dynamic_cast<const MultiLeader*>(&a) && starts("LeaderRoots[")
                && (what.ends_with(".TextAttachmentDirection") || what.ends_with(".Override")))
                return true;
        }
        if (to < CadVersion::AC1027)
        {
            if (dynamic_cast<const MultiLeader*>(&a) && prop({ "ExtendedToText" }))
                return true;
            if (dynamic_cast<const MultiLeaderStyle*>(&a) && prop({ "UnknownFlag298" }))
                return true;
        }
        if (to < CadVersion::AC1021)
        {
            // R2007 之前：没有块的缩放/分解设置、光照、标注样式的线型与文字背景 ……
            if (dynamic_cast<const BlockRecord*>(&a) && prop({ "CanScale", "IsExplodable", "Units" }))
                return true;
            if (dynamic_cast<const VPort*>(&a)
                && prop({ "AmbientColor", "UseDefaultLighting", "DefaultLighting", "Brightness", "Contrast", "GridFlags",
                          "MinorGridLinesPerMajorGridLine" }))
                return true;
            if (dynamic_cast<const Viewport*>(&a)
                && prop({ "AmbientLightColor", "DefaultLightingType", "UseDefaultLighting", "Brightness", "Contrast",
                          "MajorGridLineFrequency" }))
                return true;
            if (dynamic_cast<const DimensionStyle*>(&a)
                && prop({ "LineType", "LineTypeExt1", "LineTypeExt2", "TextBackgroundFillMode", "TextBackgroundColor",
                          "FixedExtensionLineLength", "JoggedRadiusDimensionTransverseSegmentAngle",
                          "ArcLengthSymbolPosition", "IsExtensionLineLengthFixed" }))
                return true;
            if (dynamic_cast<const AttributeBase*>(&a) && prop({ "Version" }))
                return true;
            if (dynamic_cast<const Dimension*>(&a) && prop({ "FlipArrow1", "FlipArrow2" }))
                return true;
            if (dynamic_cast<const View*>(&a) && prop({ "IsPlottable" }))
                return true;
            if (dynamic_cast<const MText*>(&a) && prop({ "RectangleHeight" }))
                return true;
            // 代码页外的字符写成 \U+XXXX
            if (auto* ma = dynamic_cast<const MText*>(&a); ma && prop({ "Value" }))
                return Codec::DecodeUnicodeEscapes(static_cast<const MText&>(b).Value) == ma->Value;
            if (auto* ta = dynamic_cast<const TextEntity*>(&a); ta && prop({ "Value" }))
                return Codec::DecodeUnicodeEscapes(static_cast<const TextEntity&>(b).Value) == ta->Value;
        }
        if (to < CadVersion::AC1024)
        {
            if (dynamic_cast<const Dimension*>(&a) && prop({ "Version" }))
                return true;
            if (dynamic_cast<const DimensionStyle*>(&a) && prop({ "TextDirection" }))
                return true;
            if (dynamic_cast<const CadWipeoutBase*>(&a) && prop({ "ClipMode" }))
                return true;
        }
        if (to < CadVersion::AC1032)
        {
            // R2018 之前：MTEXT 没有分栏
            if (dynamic_cast<const MText*>(&a) && starts("ColumnData"))
                return true;
        }
        return false;
    }

    // 读入 → 写 DWG → 读回，逐对象比较
    // fromDwg：原图来自 DWG（写回同一版本时，原样保留的未建模对象也要写出且数据不变）
    void RoundTrip(const CadDatabase& original, CadVersion version, const std::string& tag, bool fromDwg)
    {
        std::vector<std::string> warnings;
        const auto bytes = WriteDwgChecked(original, version, warnings);
        auto back = ReadDwgChecked(bytes, warnings);
        CHECK(back != nullptr);
        if (!back)
            return;
        CHECK(warnings.empty());
        for (const std::string& w : warnings)
            std::printf("    %s 警告：%s\n", tag.c_str(), w.c_str());

        const CadVersion from = original.GetVersion();
        const CadVersion to = back->GetVersion();
        Comparer cmp{ original, *back };
        cmp.RawNotWritten = !fromDwg || from != to;
        cmp.IgnoreReferencedFlag = true;
        cmp.MaxDiffs = 60;
        cmp.SkipRule = [from](const CadObject& a, const DxfPropertyInfo& p) {
            return SkipNotRoundTripped(a, p, from < CadVersion::AC1015);
        };
        cmp.KnownDiff = [from, to](const CadObject& a, const CadObject& b, std::string_view what) {
            return KnownDwgWriteDiff(a, b, what, from, to);
        };
        cmp.Run();
        CHECK(cmp.Diffs.empty());
        for (const std::string& d : cmp.Diffs)
            std::printf("    %s 差异：%s\n", tag.c_str(), d.c_str());
        for (const auto& [cls, n] : cmp.Missing)
        {
            CHECK(cls == "Shape");
            if (cls != "Shape")
                std::printf("    %s 丢失：%s × %d\n", tag.c_str(), cls.c_str(), n);
        }
        for (const auto& [h, obj] : back->Objects())
            CHECK(h < back->GetHandleSeed());
    }
}

// DWG 样例读入 → 按原版本写 DWG（R14 写为 R2000，R2007 写为 R2010）→ 读回
TEST(DwgWrite_FromDwg_RoundTrip)
{
    for (const char* v : { "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" })
    {
        const auto data = ReadFileAll(SamplePath(std::string("sample_") + v + ".dwg"));
        auto db = ReadDwg(data);
        CHECK(db != nullptr);
        if (db)
            RoundTrip(*db, CadVersion::Unknown, std::string(v) + ".dwg", true);
    }
}

// DXF 样例读入 → 写 DWG → 读回
TEST(DwgWrite_FromDxf_RoundTrip)
{
    for (const char* v : { "AC1009", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" })
    {
        const auto data = ReadFileAll(SamplePath(std::string("sample_") + v + "_ascii.dxf"));
        auto db = ReadDxf(data);
        CHECK(db != nullptr);
        if (db)
            RoundTrip(*db, CadVersion::Unknown, std::string(v) + ".dxf", false);
    }
}

// 跨版本：R2018 的图写成各版本 DWG，R2000 的图写成 R2018
TEST(DwgWrite_CrossVersion)
{
    const auto data = ReadFileAll(SamplePath("sample_AC1032.dwg"));
    auto db = ReadDwg(data);
    CHECK(db != nullptr);
    if (!db)
        return;
    for (CadVersion version : { CadVersion::AC1015, CadVersion::AC1018, CadVersion::AC1024, CadVersion::AC1027 })
        RoundTrip(*db, version, "AC1032.dwg → " + std::string(VersionString(version)), true);

    const auto old = ReadFileAll(SamplePath("sample_AC1015.dwg"));
    auto db2 = ReadDwg(old);
    CHECK(db2 != nullptr);
    if (db2)
        RoundTrip(*db2, CadVersion::AC1032, "AC1015.dwg → AC1032", true);
}

// 缩略图：DWG 写回同一版本后不变（R2018 为 PNG、R2004 为 BMP）；BMP 缩略图经 DXF（THUMBNAILIMAGE）往返不变
TEST(DwgWrite_Preview_RoundTrip)
{
    for (const char* v : { "AC1015", "AC1018", "AC1032" })
    {
        const auto data = ReadFileAll(SamplePath(std::string("sample_") + v + ".dwg"));
        auto db = ReadDwg(data);
        CHECK(db != nullptr);
        if (!db)
            continue;
        CHECK(db->Preview.Type != CadPreview::ImageType::None && !db->Preview.Image.empty());

        DwgWriteOptions options;
        auto back = ReadDwg(WriteDwg(*db, options));
        CHECK(back != nullptr);
        if (back)
        {
            CHECK(back->Preview.Type == db->Preview.Type);
            CHECK(back->Preview.Header == db->Preview.Header);
            CHECK(back->Preview.Image == db->Preview.Image);
        }

        if (db->Preview.Type == CadPreview::ImageType::Bmp)
        {
            for (bool binary : { false, true })
            {
                DxfWriteOptions dxfOptions;
                dxfOptions.Binary = binary;
                auto fromDxf = ReadDxf(WriteDxf(*db, dxfOptions));
                CHECK(fromDxf != nullptr && fromDxf->Preview.Type == CadPreview::ImageType::Bmp
                      && fromDxf->Preview.Image == db->Preview.Image);
            }
        }
    }
}
