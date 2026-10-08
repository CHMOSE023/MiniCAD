#include "Database/UnknownObjects.h"
#include "Database/DxfMeta.h"

namespace MiniDWG
{
    // 未建模的实体只按 AcDbEntity 的组码读写公共属性
    const DxfClassInfo& UnknownEntity::GetClassInfo() const
    {
        static const DxfClassInfo info{ "UnknownEntity", "", Line::ClassInfo().Subclasses.first(1), nullptr };
        return info;
    }

    const DxfClassInfo& UnknownObject::GetClassInfo() const
    {
        static const DxfClassInfo info{ "UnknownObject", "", {}, nullptr };
        return info;
    }

    std::unique_ptr<Insert> MakeTableInsert(const TableEntity& table)
    {
        auto insert = std::make_unique<Insert>();
        insert->ObjectHandle = table.ObjectHandle;
        insert->OwnerHandle = table.OwnerHandle;
        insert->LayerHandle = table.LayerHandle;
        insert->LineTypeHandle = table.LineTypeHandle;
        insert->Color = table.Color;
        insert->Transparency = table.Transparency;
        insert->LineWeight = table.LineWeight;
        insert->LineTypeScale = table.LineTypeScale;
        insert->IsInvisible = table.IsInvisible;
        insert->MaterialHandle = table.MaterialHandle;
        insert->ExtendedDataList = table.ExtendedDataList;
        insert->BlockHandle = table.BlockHandle;
        insert->InsertPoint = table.InsertPoint;
        insert->XScale = table.XScale;
        insert->YScale = table.YScale;
        insert->ZScale = table.ZScale;
        insert->Rotation = table.Rotation;
        insert->Normal = table.Normal;
        return insert;
    }

    const RawObjectData* RawDataOf(const CadObject& object)
    {
        if (const auto* e = dynamic_cast<const UnknownEntity*>(&object))
            return &e->Raw;
        if (const auto* o = dynamic_cast<const UnknownObject*>(&object))
            return &o->Raw;
        return nullptr;
    }
}
