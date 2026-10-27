#include "Database/UnknownObjects.h"
#include "Database/DxfMeta.h"
#include "Dwg/Read/DwgBitReader.h"

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

    bool DecodeOle2Frame(const UnknownEntity& entity, Ole2FrameData& out)
    {
        out = Ole2FrameData{};
        if (entity.Raw.DxfName != "OLE2FRAME")
            return false;
        if (entity.Raw.Dwg.Version != CadVersion::Unknown && !entity.Raw.Dwg.Main.empty())
        {
            DwgBitReader r(entity.Raw.Dwg.Main, entity.Raw.Dwg.Version, Codec::CodePage::Utf8);
            out.Type = r.ReadBitShort();
            out.Mode = r.ReadBitShort();
            // 规范里 R2000 起 Mode 后还有一个 BS（锁定纵横比），实测 AutoCAD 2013 写的文件里没有：长度紧跟在 Mode 之后。
            // 两种读法都试，长度不超过剩余字节数的那种是对的
            const std::uint64_t afterMode = r.PositionInBits();
            std::int32_t length = r.ReadBitLong();
            if (r.Failed() || length <= 0 || static_cast<std::uint64_t>(length) * 8 > r.RemainingBits())
            {
                r.SetPositionInBits(afterMode);
                r.ReadBitShort();
                length = r.ReadBitLong();
            }
            if (r.Failed() || length <= 0 || static_cast<std::uint64_t>(length) * 8 > r.RemainingBits())
                return false;
            out.Data = r.ReadBytes(static_cast<std::size_t>(length));
            return !r.Failed() && !out.Data.empty();
        }
        if (entity.Raw.Dxf.Version != CadVersion::Unknown)
        {
            for (const RawDxfGroup& g : entity.Raw.Dxf.Groups)
            {
                if (g.Code == 310)
                {
                    if (const auto* bytes = g.Value.AsBytes())
                        out.Data.insert(out.Data.end(), bytes->begin(), bytes->end());
                }
                else if (g.Code == 71) out.Type = static_cast<std::int16_t>(g.Value.AsInt());
                else if (g.Code == 72 || g.Code == 70) out.Mode = static_cast<std::int16_t>(g.Value.AsInt());
            }
            return !out.Data.empty();
        }
        return false;
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
