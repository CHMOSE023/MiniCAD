#include "Database/DxfMeta.h"
#include "Database/Generated/CadHeader.g.h"
#include "Database/Generated/Model.g.h"
#include "TestFramework.h"
#include <set>
#include <string>
#include <vector>

using namespace MiniDWG;

namespace
{
    const DxfPropertyInfo* FindProperty(const DxfClassInfo& info, std::string_view subclass, std::string_view name)
    {
        for (const DxfSubclassInfo& sub : info.Subclasses)
        {
            if (sub.Marker != subclass)
                continue;
            for (const DxfPropertyInfo& p : sub.Properties)
            {
                if (p.Name == name)
                    return &p;
            }
        }
        return nullptr;
    }

    std::vector<std::string_view> Markers(const DxfClassInfo& info)
    {
        std::vector<std::string_view> out;
        for (const DxfSubclassInfo& sub : info.Subclasses)
            out.push_back(sub.Marker);
        return out;
    }
}

TEST(Model_AllClasses_CreateAndIdentify)
{
    CHECK(AllDxfClasses().size() > 60);

    std::set<std::string_view> names;
    for (const DxfClassInfo* info : AllDxfClasses())
    {
        CHECK(info != nullptr && info->Create != nullptr);
        CHECK(!info->DxfName.empty());
        CHECK(names.insert(info->ClassName).second);    // 类名不重复

        auto obj = info->Create();
        CHECK(obj != nullptr);
        CHECK(&obj->GetClassInfo() == info);
        CHECK(obj->GetDxfName() == info->DxfName);
        CHECK(!obj->GetSubclassMarker().empty());
        CHECK(obj->ObjectHandle == kNullHandle);
    }
}

TEST(Model_Line_SubclassesAndCodes)
{
    const DxfClassInfo& info = Line::ClassInfo();
    CHECK(info.DxfName == "LINE");
    CHECK((Markers(info) == std::vector<std::string_view>{ "AcDbEntity", "AcDbLine" }));

    const DxfPropertyInfo* start = FindProperty(info, "AcDbLine", "StartPoint");
    CHECK(start != nullptr && start->CodeCount == 3 && start->Codes[0] == 10 && start->Codes[2] == 30);
    CHECK(start != nullptr && start->Kind == DxfValueKind::XYZ);

    const DxfPropertyInfo* layer = FindProperty(info, "AcDbEntity", "Layer");
    CHECK(layer != nullptr && layer->Codes[0] == 8 && layer->Kind == DxfValueKind::Handle);
    CHECK(layer != nullptr && layer->Reference == DxfReferenceType::Name);
}

TEST(Model_TableEntries_Subclasses)
{
    // 表项的通用子类 AcDbSymbolTableRecord 在前；它是"空"子类（ACadSharp 中 IsEmpty），
    // 名称 2 与标志 70 在 DXF 里出现在具体子类标记之后，所以归入 AcDbLayerTableRecord
    const DxfClassInfo& info = Layer::ClassInfo();
    CHECK((Markers(info) == std::vector<std::string_view>{ "AcDbSymbolTableRecord", "AcDbLayerTableRecord" }));
    CHECK(info.Subclasses[0].Properties.empty());
    CHECK(FindProperty(info, "AcDbLayerTableRecord", "Name") != nullptr);
    CHECK(FindProperty(info, "AcDbLayerTableRecord", "Flags") != nullptr);
    CHECK(info.DxfName == "LAYER");
    CHECK(BlockRecord::ClassInfo().DxfName == "BLOCK_RECORD");
}

TEST(Model_Defaults_FromAcadSharp)
{
    Line line;
    CHECK(line.Color.IsByLayer());
    CHECK(line.Transparency.IsByLayer());
    CHECK(line.LineWeight == LineWeightType::ByLayer);
    CHECK_NEAR(line.LineTypeScale, 1.0);
    CHECK(line.Normal == XYZ::AxisZ());

    Circle circle;
    CHECK_NEAR(circle.Radius, 1.0);     // 默认值来自 backing 字段 _radius

    Layer layer;
    CHECK(layer.IsOn);

    MLineStyle style;
    CHECK_NEAR(style.StartAngle, kPi / 2);
}

TEST(Model_DimensionTypes_DistinctObjectTypes)
{
    CHECK(DimensionLinear().GetObjectType() == CadObjectType::DIMENSION_LINEAR);
    CHECK(DimensionRadius().GetObjectType() == CadObjectType::DIMENSION_RADIUS);
    CHECK(Insert().GetObjectType() == CadObjectType::INSERT);
    CHECK(Scale().GetObjectType() == CadObjectType::UNLISTED);
    CHECK(DimensionLinear::kDxfName == "DIMENSION");
}

TEST(Model_Header_Variables)
{
    CHECK(AllHeaderVariables().size() > 240);

    bool hasAcadVer = false, hasClayer = false;
    for (const HeaderVariableInfo& v : AllHeaderVariables())
    {
        if (v.Name == "$ACADVER")
            hasAcadVer = v.Codes[0] == 1 && v.Kind == DxfValueKind::String;
        if (v.Name == "$CLAYER")
            hasClayer = v.IsName && v.Codes[0] == 8;
    }
    CHECK(hasAcadVer);
    CHECK(hasClayer);

    CadHeader header;
    CHECK(header.CurrentLayerName == "0");
    CHECK(header.CurrentLineTypeName == "ByLayer");
    CHECK(header.CurrentTextStyleName == "Standard");
    CHECK_NEAR(header.DimensionArrowSize, 0.18);    // 转发到 DimensionStyle.ArrowSize 的默认值
}
