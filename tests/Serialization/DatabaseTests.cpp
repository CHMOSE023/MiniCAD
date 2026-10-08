#include "Database/CadDatabase.h"
#include "TestFramework.h"
#include <set>

using namespace MiniDWG;

TEST(Database_AllocateHandle_AdvancesSeed)
{
    CadDatabase db;
    const Handle a = db.AllocateHandle();
    const Handle b = db.AllocateHandle();
    CHECK(a != kNullHandle);
    CHECK(b == a + 1);
    CHECK(db.GetHandleSeed() == b + 1);
}

TEST(Database_SetHandleSeed_NeverZero)
{
    CadDatabase db;
    db.SetHandleSeed(0x2A0);
    CHECK(db.AllocateHandle() == 0x2A0);

    db.SetHandleSeed(kNullHandle);
    CHECK(db.AllocateHandle() != kNullHandle);
}

TEST(Database_AddObject_KeepsExistingHandle)
{
    // 读文件时对象带着原句柄加入：沿用句柄并推进 HANDSEED；重复句柄被拒绝
    CadDatabase db;
    auto line = std::make_unique<Line>();
    line->ObjectHandle = 0x100;
    CHECK(db.Add(std::move(line)) != nullptr);
    CHECK(db.GetHandleSeed() == 0x101);
    CHECK(db.FindAs<Line>(0x100) != nullptr);
    CHECK(db.FindAs<Circle>(0x100) == nullptr);

    auto dup = std::make_unique<Circle>();
    dup->ObjectHandle = 0x100;
    CHECK(db.Add(std::move(dup)) == nullptr);
}

TEST(Database_CreateDefaults_Tables)
{
    CadDatabase db;
    db.CreateDefaults();

    CHECK(db.Layers() && db.LineTypes() && db.TextStyles() && db.DimensionStyles() && db.BlockRecords()
          && db.Views() && db.UCSs() && db.VPorts() && db.AppIds());
    CHECK(db.Layers()->GetObjectType() == CadObjectType::LAYER_CONTROL_OBJ);
    CHECK(db.Layers()->GetEntryDxfName() == "LAYER");

    CHECK(db.LineTypes()->Entries.size() == 3);
    CHECK(db.FindTableEntry(db.LineTypes(), "bylayer") != nullptr);     // 名称不区分大小写

    auto* layer0 = db.FindTableEntry<Layer>(db.Layers(), "0");
    CHECK(layer0 != nullptr);
    CHECK(layer0 && layer0->LineTypeHandle == db.FindTableEntry(db.LineTypes(), "Continuous")->ObjectHandle);
    CHECK(layer0 && layer0->OwnerHandle == db.Layers()->ObjectHandle);

    auto* dimStyle = db.FindTableEntry<DimensionStyle>(db.DimensionStyles(), "Standard");
    CHECK(dimStyle && dimStyle->StyleHandle == db.FindTableEntry(db.TextStyles(), "Standard")->ObjectHandle);
    CHECK(db.FindTableEntry(db.AppIds(), "ACAD") != nullptr);
    CHECK(db.FindTableEntry(db.VPorts(), "*Active") != nullptr);
}

TEST(Database_CreateDefaults_Dictionaries)
{
    CadDatabase db;
    db.CreateDefaults();

    CadDictionary* root = db.RootDictionary();
    CHECK(root != nullptr && root->EntryNames.size() == 14);
    CHECK(db.FindNamedDictionary("ACAD_GROUP") != nullptr);

    CadDictionary* scales = db.FindNamedDictionary("ACAD_SCALELIST");
    CHECK(scales && scales->EntryNames.size() == 17);
    auto* unit = dynamic_cast<Scale*>(db.FindDictionaryEntry(scales, "1:1"));
    CHECK(unit && unit->IsUnitScale && unit->Name == "1:1");

    auto* annoScale = dynamic_cast<DictionaryVariable*>(
        db.FindDictionaryEntry(db.FindNamedDictionary("AcDbVariableDictionary"), "CANNOSCALE"));
    CHECK(annoScale && annoScale->Value == "1:1");

    auto* mline = dynamic_cast<MLineStyle*>(db.FindDictionaryEntry(db.FindNamedDictionary("ACAD_MLINESTYLE"), "Standard"));
    CHECK(mline && mline->Elements.size() == 2);
}

TEST(Database_CreateDefaults_ModelAndPaperSpace)
{
    CadDatabase db;
    db.CreateDefaults();

    BlockRecord* model = db.ModelSpace();
    BlockRecord* paper = db.PaperSpace();
    CHECK(model != nullptr && paper != nullptr);

    auto* begin = db.FindAs<Block>(model->BlockEntityHandle);
    CHECK(begin && begin->Name == "*Model_Space" && begin->OwnerHandle == model->ObjectHandle);
    CHECK(db.FindAs<BlockEnd>(model->BlockEndHandle) != nullptr);

    auto* modelLayout = db.FindAs<Layout>(model->LayoutHandle);
    CHECK(modelLayout && modelLayout->Name == "Model" && modelLayout->AssociatedBlockHandle == model->ObjectHandle);
    auto* paperLayout = db.FindAs<Layout>(paper->LayoutHandle);
    CHECK(paperLayout && paperLayout->Name == "Layout1" && paperLayout->TabOrder == 1);

    // 图纸空间自带一个整页视口
    CHECK(model->Entities.empty());
    CHECK(paper->Entities.size() == 1);
    auto* vp = paper->Entities.empty() ? nullptr : db.FindAs<Viewport>(paper->Entities[0]);
    CHECK(vp && vp->OwnerHandle == paper->ObjectHandle);
    CHECK(vp && vp->Width == paperLayout->PaperWidth);
}

TEST(Database_CreateDefaults_ReferencesValid)
{
    CadDatabase db;
    db.CreateDefaults();

    // 所有所有者句柄都指向存在的对象，句柄唯一且小于 HANDSEED
    for (const auto& [handle, obj] : db.Objects())
    {
        CHECK(handle == obj->ObjectHandle);
        CHECK(handle < db.GetHandleSeed());
        if (obj->OwnerHandle != kNullHandle)
            CHECK(db.Find(obj->OwnerHandle) != nullptr);
    }
    CHECK(db.Header.HandleSeed == db.GetHandleSeed());

    // 重复调用不会再加对象
    const std::size_t count = db.Objects().size();
    db.CreateDefaults();
    CHECK(db.Objects().size() == count);
}

TEST(Database_AddEntity_DefaultsLayerAndLineType)
{
    CadDatabase db;
    db.CreateDefaults();

    auto line = std::make_unique<Line>();
    line->EndPoint = XYZ{ 10, 0, 0 };
    Line* added = db.AddEntity(db.ModelSpace(), std::move(line));

    CHECK(added != nullptr);
    CHECK(added->OwnerHandle == db.ModelSpace()->ObjectHandle);
    CHECK(added->LayerHandle == db.FindTableEntry(db.Layers(), "0")->ObjectHandle);
    CHECK(added->LineTypeHandle == db.FindTableEntry(db.LineTypes(), "ByLayer")->ObjectHandle);
    CHECK(db.ModelSpace()->Entities.size() == 1);
}
