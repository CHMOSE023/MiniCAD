#include "Database/UnknownObjects.h"
#include "Database/DxfMeta.h"
#include "Database/CadDatabase.h"
#include "Dwg/Read/DwgBitReader.h"
#include <cstring>

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

    bool IsAcisEntity(std::string_view n)
    {
        return n == "3DSOLID" || n == "REGION" || n == "BODY"
            || (n.size() >= 7 && n.substr(n.size() - 7) == "SURFACE");
    }

    // AcDs 布局（与 libredwg 的 acds 说明一致）：文件头 0x18 段索引位置、0x20 段数、0x28 datidx 所在段；
    // 每段 48 字节段头（+36 objdata 对齐偏移，×16）；datidx 段：数量、保留，再每项 {段号, 偏移, 模式号}；
    // 指向的 _data_ 项 {0x14, 1, 句柄, 0, 数据偏移}，数据 = 长度 RL + 内容
    AcisDataStore ReadAcisDataStore(const CadDatabase& db)
    {
        AcisDataStore out;
        for (const RawSection& s : db.RawSections)
        {
            if (s.Name != "AcDb:AcDsPrototype_1b")
                continue;
            const std::vector<std::uint8_t>& d = s.Data;
            auto u32 = [&](std::uint64_t p, std::uint32_t& v) {
                if (p + 4 > d.size())
                    return false;
                std::memcpy(&v, d.data() + p, 4);
                return true;
            };
            auto u64 = [&](std::uint64_t p, std::uint64_t& v) {
                if (p + 8 > d.size())
                    return false;
                std::memcpy(&v, d.data() + p, 8);
                return true;
            };
            std::uint32_t segIndexAt = 0, segCount = 0, datIndexSeg = 0;
            if (!u32(0x18, segIndexAt) || !u32(0x20, segCount) || !u32(0x28, datIndexSeg) || segCount > 1u << 20)
                continue;
            std::vector<std::uint64_t> segs(segCount, 0);
            for (std::uint32_t i = 0; i < segCount; ++i)
                u64(segIndexAt + 48 + 12ull * i, segs[i]);
            if (datIndexSeg >= segCount)
                continue;
            const std::uint64_t di = segs[datIndexSeg];
            std::uint32_t count = 0;
            if (!u32(di + 48, count))
                continue;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t seg = 0, off = 0;
                if (!u32(di + 56 + 12ull * i, seg) || !u32(di + 60 + 12ull * i, off) || seg >= segCount)
                    break;
                const std::uint64_t so = segs[seg];
                std::uint32_t handle = 0, dataOffset = 0, align = 0, length = 0;
                if (!u32(so + 48 + off + 8, handle) || !u32(so + 48 + off + 16, dataOffset) || !u32(so + 36, align))
                    continue;
                const std::uint64_t p = so + 16ull * align + dataOffset;
                if (!u32(p, length) || p + 4 + length > d.size() || length < 16)
                    continue;
                const std::uint8_t* q = d.data() + p + 4;
                if (std::memcmp(q, "ASM ", 4) != 0 && std::memcmp(q, "ACIS", 4) != 0)
                    continue;
                out[handle].assign(q, q + length);
            }
        }
        return out;
    }

    bool DecodeAcisData(const UnknownEntity& entity, const AcisDataStore& store, std::vector<std::uint8_t>& out)
    {
        out.clear();
        if (!IsAcisEntity(entity.Raw.DxfName))
            return false;
        const RawDwgData& dwg = entity.Raw.Dwg;
        if (dwg.Version != CadVersion::Unknown && !dwg.Main.empty())
        {
            DwgBitReader r(dwg.Main, dwg.Version, Codec::CodePage::Utf8);
            if (r.ReadBit())
            {
                const auto it = store.find(entity.ObjectHandle);
                if (it == store.end())
                    return false;
                out = it->second;
                return true;
            }
            r.ReadBit();
            const std::int16_t version = r.ReadBitShort();
            if (version == 1)
            {
                for (int guard = 0; guard < 1 << 20; ++guard)
                {
                    const std::int32_t size = r.ReadBitLong();
                    if (r.Failed() || size <= 0 || static_cast<std::uint64_t>(size) * 8 > r.RemainingBits())
                        break;
                    for (std::uint8_t c : r.ReadBytes(static_cast<std::size_t>(size)))
                        out.push_back(c <= 32 ? c : static_cast<std::uint8_t>(159 - c));
                }
            }
            else if (version == 2)
                out = r.ReadBytes(static_cast<std::size_t>(r.RemainingBits() / 8));
            return !r.Failed() && !out.empty();
        }
        if (entity.Raw.Dxf.Version != CadVersion::Unknown)
        {
            // 1 开始新的一行，3 接在上一行后面；逐字符解密
            for (const RawDxfGroup& g : entity.Raw.Dxf.Groups)
            {
                if (g.Code != 1 && g.Code != 3)
                    continue;
                if (g.Code == 1 && !out.empty())
                    out.push_back('\n');
                for (unsigned char c : g.Value.AsString())
                    out.push_back(c <= 32 ? c : static_cast<std::uint8_t>(159 - c));
            }
            return !out.empty();
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
