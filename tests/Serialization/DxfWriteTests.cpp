#include "Codec/CodePage.h"
#include "Database/DxfMeta.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Write/DxfStreamWriter.h"
#include "Dxf/Write/DxfWriter.h"
#include "CompareDatabases.h"
#include "TestData.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace MiniDWG;
using namespace MiniDWG::Test;

namespace
{
    const char* kVersions[] = { "AC1009", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" };

    struct ReadResult
    {
        std::unique_ptr<CadDatabase> Db;
        std::vector<std::string>     Warnings;
    };

    ReadResult ReadBytes(std::span<const std::uint8_t> data)
    {
        ReadResult result;
        DxfReadOptions options;
        options.Notify = [&](NotificationType type, std::string_view message) {
            if (type != NotificationType::Info)
                result.Warnings.emplace_back(message);
        };
        result.Db = ReadDxf(data, options);
        return result;
    }

    ReadResult ReadSample(const std::string& name)
    {
        const auto data = ReadFileAll(SamplePath(name));
        return ReadBytes(data);
    }

    std::vector<std::uint8_t> Write(const CadDatabase& db, bool binary, std::vector<std::string>* warnings = nullptr,
                                    CadVersion version = CadVersion::Unknown)
    {
        DxfWriteOptions options;
        options.Binary = binary;
        options.Version = version;
        options.WriteAllHeaderVariables = true;
        options.Notify = [&](NotificationType type, std::string_view message) {
            if (type != NotificationType::Info && warnings != nullptr)
                warnings->emplace_back(message);
        };
        return WriteDxf(db, options);
    }

    std::string Text(const std::vector<std::uint8_t>& data)
    {
        return std::string(data.begin(), data.end());
    }

    // 读样例 → 写出 → 读回，逐对象比较
    void RoundTrip(const std::string& sample, bool binary)
    {
        ReadResult original = ReadSample(sample);
        CHECK(original.Db != nullptr);
        if (!original.Db)
            return;

        std::vector<std::string> writeWarnings;
        const auto bytes = Write(*original.Db, binary, &writeWarnings);
        ReadResult back = ReadBytes(bytes);
        CHECK(back.Db != nullptr);
        if (!back.Db)
            return;

        const std::string tag = sample + (binary ? "（二进制）" : "（ASCII）");
        CHECK(writeWarnings.empty());
        for (const std::string& w : writeWarnings)
            std::printf("    %s 写出：%s\n", tag.c_str(), w.c_str());
        CHECK(back.Warnings.empty());
        for (const std::string& w : back.Warnings)
            std::printf("    %s 读回：%s\n", tag.c_str(), w.c_str());

        const CadVersion expected = original.Db->GetVersion() < CadVersion::AC1015 ? CadVersion::AC1015 : original.Db->GetVersion();
        CHECK(back.Db->GetVersion() == expected);

        Comparer cmp{ *original.Db, *back.Db };
        const bool sourceIsR12 = original.Db->GetVersion() == CadVersion::AC1009;
        cmp.RawNotWritten = sourceIsR12;    // R12 写为 R2000：原样保留的对象不写出
        cmp.SkipRule = [sourceIsR12](const CadObject& a, const DxfPropertyInfo& p) {
            return SkipNotRoundTripped(a, p, sourceIsR12);
        };
        cmp.Run();
        CHECK(cmp.Diffs.empty());
        for (const std::string& d : cmp.Diffs)
            std::printf("    %s 差异：%s\n", tag.c_str(), d.c_str());

        // 没有写出的只能是形（SHAPE）和不在根字典之下的对象
        for (const auto& [cls, n] : cmp.Missing)
        {
            const bool allowed = cls == "Shape";
            CHECK(allowed);
            if (!allowed)
                std::printf("    %s 丢失：%s × %d\n", tag.c_str(), cls.c_str(), n);
        }

        // 读回的句柄都小于 HANDSEED
        for (const auto& [h, obj] : back.Db->Objects())
            CHECK(h < back.Db->GetHandleSeed());
    }
}

TEST(DxfWrite_FormatDouble)
{
    CHECK(FormatDxfDouble(0.0) == "0.0");
    CHECK(FormatDxfDouble(-0.0) == "0.0");
    CHECK(FormatDxfDouble(1.0) == "1.0");
    CHECK(FormatDxfDouble(-2.5) == "-2.5");
    CHECK(FormatDxfDouble(0.1) == "0.1");
    CHECK(FormatDxfDouble(1e20) == "1.0E+20");
    CHECK(FormatDxfDouble(1.5e-9) == "1.5E-09");
    // 最短可往返表示
    const double x = 0.1 + 0.2;
    CHECK(std::stod(FormatDxfDouble(x)) == x);
}

TEST(DxfWrite_RoundTrip_AllSamples)
{
    for (const char* v : kVersions)
    {
        for (const char* kind : { "_ascii.dxf", "_binary.dxf" })
        {
            const std::string name = std::string("sample_") + v + kind;
            RoundTrip(name, false);
            RoundTrip(name, true);
        }
    }
}

TEST(DxfWrite_SecondRoundTrip_IsStable)
{
    // 写出 → 读回 → 再写出：两次写出的内容完全相同
    for (const char* v : { "AC1009", "AC1018", "AC1032" })
    {
        ReadResult r = ReadSample(std::string("sample_") + v + "_ascii.dxf");
        CHECK(r.Db != nullptr);
        if (!r.Db)
            continue;
        const auto first = Write(*r.Db, false);
        ReadResult back = ReadBytes(first);
        CHECK(back.Db != nullptr);
        if (!back.Db)
            continue;
        const auto second = Write(*back.Db, false);
        CHECK(first == second);
    }
}

TEST(DxfWrite_NewDatabase)
{
    // R2007 起为 UTF-8，中文原样写出（R2004 及以前见 DxfWrite_CodePage_Gbk）
    CadDatabase db;
    db.SetVersion(CadVersion::AC1024);
    db.CreateDefaults();

    auto layer = std::make_unique<Layer>();
    layer->Name = "墙体";
    layer->Color = Color::FromRgb(10, 20, 30);
    Layer* wall = db.AddTableEntry(db.Layers(), std::move(layer));

    auto line = std::make_unique<Line>();
    line->StartPoint = { 1, 2, 0 };
    line->EndPoint = { 3.5, -4, 0 };
    line->LayerHandle = wall->ObjectHandle;
    line->MaterialHandle = 0xFFFF0;      // 悬空引用：不能写出
    db.AddEntity(db.ModelSpace(), std::move(line));

    auto text = std::make_unique<TextEntity>();
    text->Value = "a^b\nc\t中文";
    text->Rotation = kPi / 2;
    db.AddEntity(db.ModelSpace(), std::move(text));

    // 多段线没有 SEQEND：写出时补一个新句柄
    auto pl = std::make_unique<Polyline2D>();
    Polyline2D* polyline = db.AddEntity(db.ModelSpace(), std::move(pl));
    for (int i = 0; i < 3; ++i)
    {
        auto vertex = std::make_unique<Vertex2D>();
        vertex->Location = { double(i), double(i * i), 0 };
        Vertex2D* added = db.Add(std::move(vertex));
        added->OwnerHandle = polyline->ObjectHandle;
        added->LayerHandle = polyline->LayerHandle;
        added->LineTypeHandle = polyline->LineTypeHandle;
        polyline->Vertices.push_back(added->ObjectHandle);
    }

    // 超过 250 字节的多行文字，按 UTF-8 字符边界分块
    auto mtext = std::make_unique<MText>();
    for (int i = 0; i < 120; ++i)
        mtext->Value += "文字";
    db.AddEntity(db.ModelSpace(), std::move(mtext));

    const Handle seedBefore = db.GetHandleSeed();
    for (bool binary : { false, true })
    {
        std::vector<std::string> warnings;
        const auto bytes = Write(db, binary, &warnings);
        CHECK(warnings.empty());
        if (!binary)
        {
            const std::string s = Text(bytes);
            CHECK(s.find("\n347\r\n") == std::string::npos);
            CHECK(s.find("a^ b^Jc^I") != std::string::npos);
        }

        ReadResult back = ReadBytes(bytes);
        CHECK(back.Db != nullptr);
        if (!back.Db)
            continue;
        CHECK(back.Warnings.empty());
        CHECK(back.Db->GetVersion() == CadVersion::AC1024);

        auto* readLayer = back.Db->FindTableEntry<Layer>(back.Db->Layers(), "墙体");
        CHECK(readLayer != nullptr && readLayer->Color == Color::FromRgb(10, 20, 30));

        const BlockRecord* model = back.Db->ModelSpace();
        CHECK(model->Entities.size() == 4);
        auto* readLine = back.Db->FindAs<Line>(model->Entities[0]);
        CHECK(readLine != nullptr && readLine->EndPoint == (XYZ{ 3.5, -4, 0 }));
        CHECK(readLine != nullptr && readLine->LayerHandle == wall->ObjectHandle);
        CHECK(readLine != nullptr && readLine->MaterialHandle == kNullHandle);

        auto* readText = back.Db->FindAs<TextEntity>(model->Entities[1]);
        CHECK(readText != nullptr && readText->Value == "a^b\nc\t中文");
        CHECK(readText != nullptr && std::abs(readText->Rotation - kPi / 2) < 1e-12);

        auto* readPl = back.Db->FindAs<Polyline2D>(model->Entities[2]);
        CHECK(readPl != nullptr && readPl->Vertices.size() == 3);
        CHECK(readPl != nullptr && back.Db->FindAs<Seqend>(readPl->SeqendHandle) != nullptr);
        CHECK(readPl != nullptr && readPl->SeqendHandle >= seedBefore);

        auto* readMText = back.Db->FindAs<MText>(model->Entities[3]);
        CHECK(readMText != nullptr && readMText->Value.size() == 120 * 6);

        CHECK(back.Db->GetHandleSeed() > readPl->SeqendHandle);
        for (const auto& [h, obj] : back.Db->Objects())
            CHECK(h < back.Db->GetHandleSeed());
    }
    // 写出不修改数据库
    CHECK(db.GetHandleSeed() == seedBefore);
}

TEST(DxfWrite_CodePage_Gbk)
{
    // R2004 及以前按 $DWGCODEPAGE 编码；代码页外的字符写成 \U+XXXX
    CadDatabase db;
    db.SetVersion(CadVersion::AC1018);
    db.Header.CodePage = "ANSI_936";
    db.CreateDefaults();
    auto text = std::make_unique<TextEntity>();
    text->Value = "中文\xE0\xB8\x81";   // "中文ก"：泰文字母不在 GBK 中
    db.AddEntity(db.ModelSpace(), std::move(text));

    const auto bytes = Write(db, false);
    const std::string s = Text(bytes);
    CHECK(s.find("ANSI_936") != std::string::npos);
    CHECK(s.find("\xD6\xD0\xCE\xC4") != std::string::npos);    // GBK 的"中文"

    ReadResult back = ReadBytes(bytes);
    CHECK(back.Db != nullptr);
    if (!back.Db)
        return;
    auto* t = back.Db->FindAs<TextEntity>(back.Db->ModelSpace()->Entities.front());
    CHECK(t != nullptr);
    if (t != nullptr)
        CHECK(Codec::DecodeUnicodeEscapes(t->Value) == "中文\xE0\xB8\x81");

    // R2007 起是 UTF-8
    const auto utf8 = Write(db, false, nullptr, CadVersion::AC1021);
    CHECK(Text(utf8).find("中文") != std::string::npos);
}

TEST(DxfWrite_R12Source_WrittenAsR2000)
{
    ReadResult r = ReadSample("sample_AC1009_ascii.dxf");
    CHECK(r.Db != nullptr);
    if (!r.Db)
        return;
    const std::string s = Text(Write(*r.Db, false));
    CHECK(s.find("$ACADVER\r\n  1\r\nAC1015") != std::string::npos);

    // 指定 R14 也改写为 R2000（R14 及以前的降级转换尚未实现）
    const std::string r14 = Text(Write(*r.Db, false, nullptr, CadVersion::AC1014));
    CHECK(r14.find("$ACADVER\r\n  1\r\nAC1015") != std::string::npos);

    // R12 标注没有测量值，写出时由定义点计算
    ReadResult back = ReadBytes(std::vector<std::uint8_t>(s.begin(), s.end()));
    CHECK(back.Db != nullptr);
    if (!back.Db)
        return;
    int dims = 0;
    for (const auto& [h, obj] : back.Db->Objects())
    {
        if (auto* dim = dynamic_cast<const DimensionLinear*>(obj.get()))
        {
            ++dims;
            const XYZ d{ dim->SecondPoint.X - dim->FirstPoint.X, dim->SecondPoint.Y - dim->FirstPoint.Y, 0 };
            const double expected = std::abs(d.X * std::cos(dim->Rotation) + d.Y * std::sin(dim->Rotation));
            CHECK(std::abs(dim->Measurement - expected) < 1e-9);
        }
    }
    CHECK(dims > 0);
}
