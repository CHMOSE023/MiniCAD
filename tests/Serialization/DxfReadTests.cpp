#include "Codec/CodePage.h"
#include "Database/DxfMeta.h"
#include "Dxf/Read/DxfReader.h"
#include "TestData.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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

    ReadResult ReadText(const std::string& text)
    {
        return ReadBytes({ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
    }

    std::map<std::string, int> CountByClass(const CadDatabase& db)
    {
        std::map<std::string, int> counts;
        for (const auto& [h, obj] : db.Objects())
            ++counts[std::string(obj->GetClassInfo().ClassName)];
        return counts;
    }

    template <class T>
    std::vector<const T*> EntitiesOf(const CadDatabase& db, const BlockRecord* block)
    {
        std::vector<const T*> out;
        for (Handle h : block->Entities)
        {
            if (auto* e = db.FindAs<T>(h))
                out.push_back(e);
        }
        return out;
    }
}

TEST(DxfRead_AllSamples_NoWarnings)
{
    for (const char* v : kVersions)
    {
        for (const char* kind : { "_ascii.dxf", "_binary.dxf" })
        {
            const std::string name = std::string("sample_") + v + kind;
            ReadResult r = ReadSample(name);
            CHECK(r.Db != nullptr);
            if (!r.Db)
                continue;
            CHECK(r.Db->GetVersion() == ParseVersionString(v));
            CHECK(r.Db->ModelSpace() != nullptr && !r.Db->ModelSpace()->Entities.empty());
            CHECK(r.Warnings.empty());
            for (const std::string& w : r.Warnings)
                std::printf("    %s：%s\n", name.c_str(), w.c_str());
        }
    }
}

TEST(DxfRead_AsciiAndBinary_SameContent)
{
    for (const char* v : kVersions)
    {
        ReadResult a = ReadSample(std::string("sample_") + v + "_ascii.dxf");
        ReadResult b = ReadSample(std::string("sample_") + v + "_binary.dxf");
        if (!a.Db || !b.Db)
        {
            CHECK(false);
            continue;
        }
        // 两份样例是 AutoCAD 分别保存的：句柄不同，会话变量（DictionaryVariable）个数也不同
        auto ca = CountByClass(*a.Db);
        auto cb = CountByClass(*b.Db);
        ca.erase("DictionaryVariable");
        cb.erase("DictionaryVariable");
        CHECK(ca == cb);

        // 模型空间实体的类型顺序一致
        auto typesOf = [](const CadDatabase& db) {
            std::vector<std::string_view> types;
            for (Handle h : db.ModelSpace()->Entities)
                types.push_back(db.Find(h)->GetClassInfo().ClassName);
            return types;
        };
        CHECK(typesOf(*a.Db) == typesOf(*b.Db));

        // ASCII 中的浮点数是 16 位有效数字的十进制文本，按容差比较
        auto near = [](const XYZ& p, const XYZ& q) {
            auto close = [](double x, double y) { return std::abs(x - y) <= 1e-9 * std::max(1.0, std::abs(x)); };
            return close(p.X, q.X) && close(p.Y, q.Y) && close(p.Z, q.Z);
        };
        auto la = EntitiesOf<Line>(*a.Db, a.Db->ModelSpace());
        auto lb = EntitiesOf<Line>(*b.Db, b.Db->ModelSpace());
        CHECK(la.size() == lb.size());
        for (std::size_t i = 0; i < la.size() && i < lb.size(); ++i)
            CHECK(near(la[i]->StartPoint, lb[i]->StartPoint) && near(la[i]->EndPoint, lb[i]->EndPoint));

        auto ta = EntitiesOf<MText>(*a.Db, a.Db->ModelSpace());
        auto tb = EntitiesOf<MText>(*b.Db, b.Db->ModelSpace());
        for (std::size_t i = 0; i < ta.size() && i < tb.size(); ++i)
            CHECK(ta[i]->Value == tb[i]->Value);
    }
}

TEST(DxfRead_Sample_ReferencesResolved)
{
    ReadResult r = ReadSample("sample_AC1018_ascii.dxf");
    CHECK(r.Db != nullptr);
    if (!r.Db)
        return;
    const CadDatabase& db = *r.Db;

    int inserts = 0;
    for (const auto& [h, obj] : db.Objects())
    {
        if (auto* e = dynamic_cast<const Entity*>(obj.get()))
        {
            CHECK(db.FindAs<Layer>(e->LayerHandle) != nullptr);
            CHECK(db.FindAs<LineType>(e->LineTypeHandle) != nullptr);
            CHECK(db.Find(e->OwnerHandle) != nullptr);
        }
        if (auto* insert = dynamic_cast<const Insert*>(obj.get()))
        {
            ++inserts;
            CHECK(db.FindAs<BlockRecord>(insert->BlockHandle) != nullptr);
            for (Handle a : insert->Attributes)
                CHECK(db.FindAs<AttributeEntity>(a) != nullptr);
        }
        if (auto* text = dynamic_cast<const TextEntity*>(obj.get()))
            CHECK(db.FindAs<TextStyle>(text->StyleHandle) != nullptr);
        if (auto* dim = dynamic_cast<const Dimension*>(obj.get()))
        {
            CHECK(db.FindAs<DimensionStyle>(dim->StyleHandle) != nullptr);
            CHECK(db.FindAs<BlockRecord>(dim->BlockHandle) != nullptr);
        }
    }
    CHECK(inserts > 0);

    // 块：每个块记录都有 BLOCK / ENDBLK，布局与块记录互相引用
    for (Handle h : db.BlockRecords()->Entries)
    {
        auto* record = db.FindAs<BlockRecord>(h);
        CHECK(record != nullptr);
        if (!record)
            continue;
        auto* begin = db.FindAs<Block>(record->BlockEntityHandle);
        CHECK(begin != nullptr && begin->Name == record->Name);
        CHECK(db.FindAs<BlockEnd>(record->BlockEndHandle) != nullptr);
    }
    auto* modelLayout = db.FindAs<Layout>(db.ModelSpace()->LayoutHandle);
    CHECK(modelLayout != nullptr && modelLayout->AssociatedBlockHandle == db.ModelSpace()->ObjectHandle);
    CHECK(modelLayout != nullptr && modelLayout->Name == "Model");

    // 根字典与命名字典
    CHECK(db.RootDictionary() != nullptr);
    CHECK(db.FindNamedDictionary("ACAD_LAYOUT") != nullptr);
}

TEST(DxfRead_Minimal_R12)
{
    // 没有 HEADER、TABLES、句柄：图层按名称新建，实体放入模型空间，其余结构由 CreateDefaults 补齐
    ReadResult r = ReadText(
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\nWALL\n10\n1.5\n20\n2\n30\n0\n11\n10\n21\n20\n31\n0\n"
        "0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n5\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n2.5\n1\nHello^JWorld\n"
        "0\nENDSEC\n0\nEOF\n");
    CHECK(r.Db != nullptr);
    if (!r.Db)
        return;
    const CadDatabase& db = *r.Db;
    CHECK(db.ModelSpace()->Entities.size() == 3);

    auto lines = EntitiesOf<Line>(db, db.ModelSpace());
    CHECK(lines.size() == 1);
    if (!lines.empty())
    {
        CHECK(lines[0]->StartPoint == (XYZ{ 1.5, 2, 0 }));
        CHECK(lines[0]->EndPoint == (XYZ{ 10, 20, 0 }));
        auto* layer = db.FindAs<Layer>(lines[0]->LayerHandle);
        CHECK(layer != nullptr && layer->Name == "WALL");
        CHECK(lines[0]->ObjectHandle != kNullHandle);
    }
    auto circles = EntitiesOf<Circle>(db, db.ModelSpace());
    CHECK(circles.size() == 1 && circles[0]->Radius == 5.0);
    auto texts = EntitiesOf<TextEntity>(db, db.ModelSpace());
    CHECK(texts.size() == 1 && texts[0]->Value == "Hello\nWorld");    // ^J 是换行

    CHECK(db.FindTableEntry(db.Layers(), "0") != nullptr);
    CHECK(db.FindAs<Layout>(db.ModelSpace()->LayoutHandle) != nullptr);
    for (const auto& [h, obj] : db.Objects())
        CHECK(h < db.GetHandleSeed());
}

TEST(DxfRead_CodePage_Gbk_And_UnicodeEscape)
{
    // R2004 及以前按 $DWGCODEPAGE 解码；\U+XXXX 是代码页外字符的转义
    std::string dxf =
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1018\n9\n$DWGCODEPAGE\n3\nANSI_936\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n1\n1\n\xD6\xD0\xCE\xC4\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n1\n1\nA\\U+4E2DB\n"
        "0\nENDSEC\n0\nEOF\n";
    ReadResult r = ReadText(dxf);
    CHECK(r.Db != nullptr);
    if (!r.Db)
        return;
    auto texts = EntitiesOf<TextEntity>(*r.Db, r.Db->ModelSpace());
    CHECK(texts.size() == 2);
    if (texts.size() == 2)
    {
        CHECK(texts[0]->Value == "\xE4\xB8\xAD\xE6\x96\x87");     // "中文"
        // \U+XXXX 保留原文，显示时再解码
        CHECK(texts[1]->Value == "A\\U+4E2DB");
        CHECK(Codec::DecodeUnicodeEscapes(texts[1]->Value) == "A\xE4\xB8\xAD" "B");     // "A中B"
    }
}

TEST(DxfRead_NotDxf_ReturnsNull)
{
    const std::string json = "{\"entities\": []}";
    CHECK(ReadText(json).Db == nullptr);
}
