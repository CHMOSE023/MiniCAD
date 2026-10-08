// DWG 写入：对象段的组织、对象外壳与公共数据、符号表、字典与其他非图形对象
// （对应 ACadSharp DwgObjectWriter；字段顺序与 DwgReadObjects.cpp 逐项对应）
#include "Dwg/Write/DwgWriterImpl.h"
#include "Dwg/Write/DwgCompress.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MiniDWG::DwgWrite
{
    namespace
    {
        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                char x = a[i], y = b[i];
                if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
                if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
                if (x != y)
                    return false;
            }
            return true;
        }

        std::string ToUpper(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
            {
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            }
            return out;
        }

        // 扩展数据、XRECORD 中的大端 8 字节句柄
        void PutBigEndianHandle(std::vector<std::uint8_t>& out, Handle h)
        {
            for (int i = 7; i >= 0; --i)
                out.push_back(static_cast<std::uint8_t>(h >> (8 * i)));
        }

        template <class E>
        int Flags(E e) { return static_cast<int>(e); }
    }

    // ── 对象段的组织 ───────────────────────────────────────────────

    void Writer::WriteObjects()
    {
        m_objectData.clear();
        m_map.clear();
        // 对象段以 RL 0x0DCA 开头（含义未知；AutoCAD 写的 R2000 文件也有）
        m_objectData = { 0xCA, 0x0D, 0x00, 0x00 };

        for (const CadTable* table : m_tables)
        {
            WriteTableControl(*table);
            for (Handle h : table->Entries)
            {
                if (const auto* entry = m_db.FindAs<TableEntry>(h); entry != nullptr && Written(h))
                    WriteTableEntry(*entry, table->ObjectHandle);
            }
        }
        if (R2004Pre())
            WriteVEntityControl();

        if (const CadTable* records = m_db.BlockRecords())
        {
            for (Handle h : records->Entries)
            {
                if (const auto* record = m_db.FindAs<BlockRecord>(h); record != nullptr && Written(h))
                    WriteBlockEntities(*record);
            }
        }

        for (Handle h : m_objectOrder)
        {
            if (const CadObject* object = Find(h))
                WriteNonGraphical(*object);
        }
    }

    std::int16_t Writer::TypeCodeOf(const CadObject& object)
    {
        CadObjectType type = object.GetObjectType();
        if (const auto* insert = dynamic_cast<const Insert*>(&object);
            insert != nullptr && (insert->ColumnCount > 1 || insert->RowCount > 1))
            type = CadObjectType::MINSERT;
        // R2004 之前 LAYOUT、ACDBPLACEHOLDER 按类名注册（与 AutoCAD 写出的一致）
        const bool byClass = type == CadObjectType::UNLISTED
            || (R2004Pre() && (type == CadObjectType::LAYOUT || type == CadObjectType::ACDBPLACEHOLDER));
        if (!byClass)
            return static_cast<std::int16_t>(type);
        auto it = m_classNumbers.find(ToUpper(object.GetDxfName()));
        if (it == m_classNumbers.end())
        {
            NotifyOnce("class:" + std::string(object.GetDxfName()), NotificationType::Warning,
                       "找不到 " + std::string(object.GetDxfName()) + " 的类定义，未写出");
            return -1;
        }
        return it->second;
    }

    // 对象开头：类型、R2000～R2007 的数据位数（占位，结束时回填）、句柄、扩展数据
    bool Writer::BeginObject(const CadObject& object)
    {
        const std::int16_t type = TypeCodeOf(object);
        if (type < 0)
            return false;
        m_main = DwgBitWriter(m_version, m_codePage);
        m_text = DwgBitWriter(m_version, m_codePage);
        m_hnd = DwgBitWriter(m_version, m_codePage);
        m_main.WriteObjectType(type);
        if (m_version >= CadVersion::AC1015 && m_version < CadVersion::AC1024)
        {
            m_sizePos = m_main.PositionInBits();
            m_main.WriteRawLong(0);
        }
        m_main.WriteHandle(DwgRef::Undefined, object.ObjectHandle);
        WriteExtendedData(object);
        m_objectOpen = true;
        return true;
    }

    // 对象结尾：拼接数据、字符串流与句柄流，加上 MS 大小（R2010 起另有 MC 句柄流位数）与 CRC
    void Writer::EndObject(Handle handle)
    {
        if (!m_objectOpen)
            return;
        m_objectOpen = false;

        DwgBitWriter all = std::move(m_main);
        if (R2010Plus())
        {
            const std::uint64_t textBits = m_text.PositionInBits();
            if (textBits > 0)
            {
                all.AppendBits(m_text, textBits);
                if (textBits >= 0x8000)
                {
                    all.WriteRawShort(static_cast<std::int16_t>(textBits >> 15));
                    all.WriteRawShort(static_cast<std::int16_t>((textBits & 0x7FFF) | 0x8000));
                }
                else
                {
                    all.WriteRawShort(static_cast<std::int16_t>(textBits));
                }
                all.WriteBit(true);
            }
            else
            {
                all.WriteBit(false);
            }
        }
        else
        {
            // RL：从类型码开始到句柄流之前的位数
            all.PatchRawLong(m_sizePos, static_cast<std::uint32_t>(all.PositionInBits()));
        }
        const std::uint64_t handleStart = all.PositionInBits();
        all.AppendBits(m_hnd, m_hnd.PositionInBits());
        all.AlignToByte();
        const std::uint64_t handleBits = all.PositionInBits() - handleStart;
        const std::vector<std::uint8_t> data = all.Take();

        DwgBitWriter head;
        head.WriteModularShort(static_cast<std::uint32_t>(data.size()));
        if (R2010Plus())
            head.WriteModularChar(handleBits);

        const std::int64_t offset = static_cast<std::int64_t>(m_objectData.size());
        m_objectData.insert(m_objectData.end(), head.Data().begin(), head.Data().end());
        m_objectData.insert(m_objectData.end(), data.begin(), data.end());
        const std::uint16_t crc = DwgCodec::Crc16(
            0xC0C1, std::span<const std::uint8_t>(m_objectData).subspan(static_cast<std::size_t>(offset)));
        m_objectData.push_back(static_cast<std::uint8_t>(crc));
        m_objectData.push_back(static_cast<std::uint8_t>(crc >> 8));
        m_map[handle] = offset;
    }

    // ── 公共数据 ───────────────────────────────────────────────────

    void Writer::WriteCommonNonEntityData(const CadObject& object, Handle owner)
    {
        H(DwgRef::SoftPointer, owner);
        WriteReactorsAndXDictionary(object);
    }

    void Writer::WriteReactorsAndXDictionary(const CadObject& object)
    {
        std::vector<Handle> reactors;
        for (Handle r : object.Reactors)
        {
            if (Written(r) && std::find(reactors.begin(), reactors.end(), r) == reactors.end())
                reactors.push_back(r);
        }
        M().WriteBitLong(static_cast<std::int32_t>(reactors.size()));
        for (Handle r : reactors)
            H(DwgRef::SoftPointer, r);

        const bool hasXDictionary = Written(object.XDictionaryHandle) && FindAs<CadDictionary>(object.XDictionaryHandle) != nullptr;
        if (R2004Plus())
        {
            M().WriteBit(!hasXDictionary);
            if (hasXDictionary)
                H(DwgRef::HardOwnership, object.XDictionaryHandle);
        }
        else
        {
            H(DwgRef::HardOwnership, hasXDictionary ? object.XDictionaryHandle : kNullHandle);
        }
        if (R2013Plus())
            M().WriteBit(m_dataStoreBit);   // 数据存储中有该对象的二进制数据（只有原样保留的三维实体等）
    }

    // 扩展数据：每个应用程序一段，BS 字节数 + H 应用程序句柄 + 记录（1 字节组码 - 1000 + 值），最后 BS 0
    void Writer::WriteExtendedData(const CadObject& object)
    {
        for (const ExtendedData& data : object.ExtendedDataList)
        {
            if (!Written(data.AppIdHandle) || FindAs<AppId>(data.AppIdHandle) == nullptr)
            {
                NotifyOnce("xdata-app", NotificationType::Warning, "扩展数据的应用程序（APPID）不存在，已跳过");
                continue;
            }
            // 记录写到字节缓冲：字符串用位流写入器编码（R2007 起 UTF-16）
            DwgBitWriter bytes(m_version, m_codePage);
            for (const ExtendedDataRecord& record : data.Records)
            {
                const int code = record.Code;
                if (code >= 1020 && code <= 1033)
                {
                    NotifyOnce("xdata:lone", NotificationType::Info, "扩展数据中单独的 Y/Z 坐标分量（1020～1033）未写出");
                    continue;
                }
                std::vector<std::uint8_t> raw;
                bool ok = true;
                switch (code)
                {
                case 1000:
                case 1001:
                    if (const auto* s = std::get_if<std::string>(&record.Value))
                    {
                        bytes.WriteByte(static_cast<std::uint8_t>(code - 1000));
                        bytes.WriteTextUnicode(*s);
                    }
                    continue;
                case 1002:
                {
                    const auto* s = std::get_if<std::string>(&record.Value);
                    raw.push_back(s != nullptr && *s == "}" ? 1 : 0);
                    break;
                }
                case 1003:
                {
                    // 图层名 → 图层句柄
                    Handle layer = kNullHandle;
                    if (const auto* s = std::get_if<std::string>(&record.Value))
                        layer = Ref(TableEntryByName(m_db.Layers(), *s));
                    else if (const auto* h = std::get_if<Handle>(&record.Value))
                        layer = Ref(*h);
                    PutBigEndianHandle(raw, layer);
                    break;
                }
                case 1004:
                    if (const auto* b = std::get_if<std::vector<std::uint8_t>>(&record.Value); b != nullptr && b->size() < 256)
                    {
                        raw.push_back(static_cast<std::uint8_t>(b->size()));
                        raw.insert(raw.end(), b->begin(), b->end());
                    }
                    else
                    {
                        ok = false;
                    }
                    break;
                case 1005:
                {
                    const auto* h = std::get_if<Handle>(&record.Value);
                    // 与 ACadSharp 一致：不写出的对象（找不到的、只能写回原版本的未建模对象）写 0
                    PutBigEndianHandle(raw, h != nullptr ? Ref(*h) : kNullHandle);
                    break;
                }
                case 1010: case 1011: case 1012: case 1013:
                {
                    const auto* p = std::get_if<XYZ>(&record.Value);
                    const XYZ v = p != nullptr ? *p : XYZ{};
                    for (double d : { v.X, v.Y, v.Z })
                    {
                        std::uint64_t bits;
                        std::memcpy(&bits, &d, 8);
                        for (int i = 0; i < 8; ++i)
                            raw.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
                    }
                    break;
                }
                case 1040: case 1041: case 1042:
                {
                    double d = 0.0;
                    std::visit([&](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_arithmetic_v<T>)
                            d = static_cast<double>(v);
                    }, record.Value);
                    std::uint64_t bits;
                    std::memcpy(&bits, &d, 8);
                    for (int i = 0; i < 8; ++i)
                        raw.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
                    break;
                }
                case 1070:
                case 1071:
                {
                    std::int64_t v = 0;
                    std::visit([&](const auto& x) {
                        using T = std::decay_t<decltype(x)>;
                        if constexpr (std::is_arithmetic_v<T>)
                            v = static_cast<std::int64_t>(x);
                    }, record.Value);
                    const int n = code == 1070 ? 2 : 4;
                    for (int i = 0; i < n; ++i)
                        raw.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> (8 * i)));
                    break;
                }
                default:
                    ok = false;
                    break;
                }
                if (!ok)
                {
                    NotifyOnce("xdata:" + std::to_string(code), NotificationType::Warning,
                               "扩展数据中无法写出的组码 " + std::to_string(code));
                    continue;
                }
                bytes.WriteByte(static_cast<std::uint8_t>(code - 1000));
                bytes.WriteBytes(raw);
            }
            const std::vector<std::uint8_t>& buffer = bytes.Data();
            if (buffer.empty() || buffer.size() > 0x7FFF)
                continue;
            M().WriteBitShort(static_cast<std::int16_t>(buffer.size()));
            M().WriteHandle(DwgRef::HardPointer, data.AppIdHandle);
            M().WriteBytes(buffer);
        }
        M().WriteBitShort(0);
    }

    void Writer::WriteXrefDependantBit(const TableEntry& entry)
    {
        if (R2007Plus())
        {
            M().WriteBitShort(HasFlag(entry.Flags, StandardFlags::XrefDependent) ? 0x100 : 0);
            return;
        }
        M().WriteBit(HasFlag(entry.Flags, StandardFlags::Referenced));
        M().WriteBitShort(0);   // xrefindex + 1
        M().WriteBit(HasFlag(entry.Flags, StandardFlags::XrefDependent));
    }

    // ── 符号表 ─────────────────────────────────────────────────────

    void Writer::WriteTableControl(const CadTable& table)
    {
        if (!BeginObject(table))
            return;
        WriteCommonNonEntityData(table, kNullHandle);

        const CadObjectType type = table.GetObjectType();
        std::vector<Handle> entries;
        Handle special[2] = { kNullHandle, kNullHandle };
        for (Handle h : table.Entries)
        {
            const auto* entry = m_db.FindAs<TableEntry>(h);
            if (entry == nullptr || !Written(h))
                continue;
            // 块表的 *MODEL_SPACE、*PAPER_SPACE，线型表的 ByBlock、ByLayer 不计入个数，在最后单独列出
            if (type == CadObjectType::BLOCK_CONTROL_OBJ)
            {
                if (m_db.ModelSpace() != nullptr && h == m_db.ModelSpace()->ObjectHandle)
                {
                    special[0] = h;
                    continue;
                }
                if (m_db.PaperSpace() != nullptr && h == m_db.PaperSpace()->ObjectHandle)
                {
                    special[1] = h;
                    continue;
                }
            }
            if (type == CadObjectType::LTYPE_CONTROL_OBJ)
            {
                if (EqualsIgnoreCase(entry->Name, "ByBlock"))
                {
                    special[0] = h;
                    continue;
                }
                if (EqualsIgnoreCase(entry->Name, "ByLayer"))
                {
                    special[1] = h;
                    continue;
                }
            }
            entries.push_back(h);
        }
        M().WriteBitLong(static_cast<std::int32_t>(entries.size()));
        if (type == CadObjectType::DIMSTYLE_CONTROL_OBJ && R2000Plus())
            M().WriteByte(0);   // 未公开的 71 组码：后面的硬指针个数
        for (Handle h : entries)
            H(DwgRef::SoftOwnership, h);
        if (type == CadObjectType::BLOCK_CONTROL_OBJ || type == CadObjectType::LTYPE_CONTROL_OBJ)
        {
            H(DwgRef::HardOwnership, special[0]);
            H(DwgRef::HardOwnership, special[1]);
        }
        if (type == CadObjectType::LTYPE_CONTROL_OBJ)
            m_lineTypeIndex = entries;
        EndObject(table.ObjectHandle);
    }

    // R2004 之前的视口实体头表（VX，R14 的遗留）：当前布局（*Paper_Space）中的每个视口一项，
    // 记录视口的开关；AutoCAD 读 R2000 时按它确定当前布局视口的状态，没有表项时视口都按关闭处理
    void Writer::WriteVEntityControl()
    {
        CadTable control(CadObjectType::VP_ENT_HDR_CTRL_OBJ, "VP_ENT_HDR");
        control.ObjectHandle = m_vEntityControl;
        if (!BeginObject(control))
            return;
        WriteCommonNonEntityData(control, kNullHandle);
        M().WriteBitLong(static_cast<std::int32_t>(m_vxEntries.size()));
        for (const auto& [vx, viewport] : m_vxEntries)
            H(DwgRef::SoftOwnership, vx);
        EndObject(m_vEntityControl);

        // 表项：名称（与 AutoCAD 一样，总视口为 "1"，其余为空）、标志、是否打开；
        // 句柄：所有者、扩展字典、外部参照块、视口实体、下一项
        for (std::size_t i = 0; i < m_vxEntries.size(); ++i)
        {
            const auto [vx, viewport] = m_vxEntries[i];
            CadTable entry(CadObjectType::VP_ENT_HDR, "VP_ENT_HDR");
            entry.ObjectHandle = vx;
            if (!BeginObject(entry))
                continue;
            WriteCommonNonEntityData(entry, m_vEntityControl);
            Text(i == 0 ? "1" : "");
            M().WriteBit(true);     // 64：被引用
            M().WriteBitShort(0);   // xrefindex + 1
            M().WriteBit(false);    // 依赖外部参照
            const auto* vp = FindAs<Viewport>(viewport);
            M().WriteBit(vp != nullptr && !HasFlag(vp->Status, ViewportStatusFlags::ViewportOff));
            H(DwgRef::HardPointer, kNullHandle);
            H(DwgRef::SoftPointer, viewport);
            H(DwgRef::HardPointer, i + 1 < m_vxEntries.size() ? m_vxEntries[i + 1].first : kNullHandle);
            EndObject(vx);
        }
    }

    void Writer::WriteTableEntry(const TableEntry& entry, Handle table)
    {
        if (!BeginObject(entry))
            return;
        WriteCommonNonEntityData(entry, table);
        if (const auto* record = dynamic_cast<const BlockRecord*>(&entry))
            WriteBlockHeader(*record);
        else if (const auto* layer = dynamic_cast<const Layer*>(&entry))
            WriteLayer(*layer);
        else if (const auto* style = dynamic_cast<const TextStyle*>(&entry))
            WriteTextStyle(*style);
        else if (const auto* lineType = dynamic_cast<const LineType*>(&entry))
            WriteLineType(*lineType);
        else if (const auto* view = dynamic_cast<const View*>(&entry))
            WriteView(*view);
        else if (const auto* ucs = dynamic_cast<const UCS*>(&entry))
            WriteUcs(*ucs);
        else if (const auto* vport = dynamic_cast<const VPort*>(&entry))
            WriteVPort(*vport);
        else if (const auto* appId = dynamic_cast<const AppId*>(&entry))
            WriteAppId(*appId);
        else if (const auto* dimStyle = dynamic_cast<const DimensionStyle*>(&entry))
            WriteDimStyle(*dimStyle);
        EndObject(entry.ObjectHandle);
    }

    void Writer::WriteBlockHeader(const BlockRecord& record)
    {
        const Block* block = FindAs<Block>(BlockBeginOf(record));
        const Block empty;
        if (block == nullptr)
            block = &empty;
        const int flags = Flags(block->Flags);
        const bool anonymous = (flags & Flags(BlockTypeFlags::Anonymous)) != 0;
        const bool isXref = (flags & (Flags(BlockTypeFlags::XRef) | Flags(BlockTypeFlags::XRefOverlay))) != 0;
        const std::vector<Handle>& entities = m_blockEntities[record.ObjectHandle];

        // 名称：匿名块只存 *{类型字符}，布局块不带编号（*Paper_Space0 → *Paper_Space），与 AutoCAD 一致
        std::string name = record.Name;
        if (anonymous && name.size() >= 2 && name[0] == '*')
            name = name.substr(0, 2);
        else if (FindAs<Layout>(record.LayoutHandle) != nullptr && !name.empty() && name[0] == '*')
            name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }), name.end());
        Text(name);
        WriteXrefDependantBit(record);

        bool hasAttributes = (flags & Flags(BlockTypeFlags::NonConstantAttributeDefinitions)) != 0;
        for (Handle e : entities)
            hasAttributes |= FindAs<AttributeDefinition>(e) != nullptr;
        M().WriteBit(anonymous);
        M().WriteBit(hasAttributes);
        M().WriteBit((flags & Flags(BlockTypeFlags::XRef)) != 0);
        M().WriteBit((flags & Flags(BlockTypeFlags::XRefOverlay)) != 0);
        if (R2000Plus())
            M().WriteBit(block->IsUnloaded);
        if (R2004Plus() && !isXref)
            M().WriteBitLong(static_cast<std::int32_t>(entities.size()));
        M().Write3BitDouble(block->BasePoint);
        Text(block->XRefPath);

        const std::vector<Handle>& inserts = m_inserts[record.ObjectHandle];
        if (R2000Plus())
        {
            for (std::size_t i = 0; i < inserts.size(); ++i)
                M().WriteByte(1);
            M().WriteByte(0);
            Text(block->Comments);
            M().WriteBitLong(static_cast<std::int32_t>(record.Preview.size()));
            M().WriteBytes(record.Preview);
        }
        if (R2007Plus())
        {
            M().WriteBitShort(static_cast<std::int16_t>(record.Units));
            M().WriteBit(record.IsExplodable);
            M().WriteByte(record.CanScale ? 1 : 0);
        }

        H(DwgRef::HardPointer, kNullHandle);
        H(DwgRef::HardOwnership, BlockBeginOf(record));
        if (R2004Pre() && !isXref)
        {
            H(DwgRef::SoftPointer, entities.empty() ? kNullHandle : entities.front());
            H(DwgRef::SoftPointer, entities.empty() ? kNullHandle : entities.back());
        }
        if (R2004Plus() && !isXref)
        {
            for (Handle e : entities)
                H(DwgRef::HardOwnership, e);
        }
        H(DwgRef::HardOwnership, BlockEndOf(record));
        if (R2000Plus())
        {
            for (Handle i : inserts)
                H(DwgRef::SoftPointer, i);
            H(DwgRef::HardPointer, record.LayoutHandle);
        }
    }

    void Writer::WriteLayer(const Layer& layer)
    {
        Text(layer.Name);
        WriteXrefDependantBit(layer);
        const int flags = Flags(layer.Flags);
        std::int16_t values = static_cast<std::int16_t>(LineWeightToIndex(layer.LineWeight) << 5);
        if (flags & 1) values |= 0x1;           // 冻结
        if (!layer.IsOn) values |= 0x2;         // 关闭
        if (flags & 2) values |= 0x4;           // 新视口中冻结
        if (flags & 4) values |= 0x8;           // 锁定
        // DEFPOINTS 图层总是不打印（与 AutoCAD、DXF 写出一致；R12 文件没有打印标志）
        if (layer.PlotFlag && !EqualsIgnoreCase(layer.Name, "Defpoints")) values |= 0x10;
        M().WriteBitShort(values);
        Cmc(layer.Color);
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        if (R2000Plus())
            H(DwgRef::HardPointer, layer.PlotStyleName);
        if (R2007Plus())
            H(DwgRef::HardPointer, layer.MaterialHandle);
        H(DwgRef::HardPointer, layer.LineTypeHandle);
        if (R2013Plus())
            H(DwgRef::HardPointer, kNullHandle);
    }

    void Writer::WriteTextStyle(const TextStyle& style)
    {
        Text(style.Name);
        WriteXrefDependantBit(style);
        const int flags = Flags(style.Flags);
        M().WriteBit((flags & 1) != 0);         // 形文件
        M().WriteBit((flags & 4) != 0);         // 竖排
        M().WriteBitDouble(style.Height);
        M().WriteBitDouble(style.Width);
        M().WriteBitDouble(style.ObliqueAngle);
        M().WriteByte(static_cast<std::uint8_t>(style.MirrorFlag));
        M().WriteBitDouble(style.LastHeight);
        Text(style.Filename);
        Text(style.BigFontFilename);
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
    }

    void Writer::WriteLineType(const LineType& lineType)
    {
        Text(lineType.Name);
        WriteXrefDependantBit(lineType);
        Text(lineType.Description);

        double length = 0.0;
        bool hasText = false;
        for (const LineTypeSegment& s : lineType.Segments)
        {
            length += std::abs(s.Length);
            hasText |= HasFlag(s.Flags, LineTypeShapeFlags::Text);
        }
        M().WriteBitDouble(length);
        M().WriteByte(static_cast<std::uint8_t>(lineType.Alignment != 0 ? lineType.Alignment : 'A'));
        M().WriteByte(static_cast<std::uint8_t>(lineType.Segments.size()));

        // 文字分段的文字放在 256（R2007 起 512，UTF-16）字节的区域中，形号字段存文字在区域中的偏移
        std::vector<std::uint8_t> area;
        if (m_version <= CadVersion::AC1018)
            area.assign(256, 0);
        else if (hasText)
            area.assign(512, 0);
        std::size_t cursor = 0;
        for (const LineTypeSegment& s : lineType.Segments)
        {
            std::int16_t shape = s.ShapeNumber;
            if (HasFlag(s.Flags, LineTypeShapeFlags::Text))
            {
                shape = 0;
                std::vector<std::uint8_t> bytes;
                if (R2007Plus())
                {
                    for (char16_t c : Utf8ToUtf16(s.Text))
                    {
                        bytes.push_back(static_cast<std::uint8_t>(c));
                        bytes.push_back(static_cast<std::uint8_t>(c >> 8));
                    }
                    bytes.push_back(0);
                    bytes.push_back(0);
                }
                else
                {
                    const std::string encoded = Codec::FromUtf8(s.Text, m_codePage);
                    bytes.assign(encoded.begin(), encoded.end());
                    bytes.push_back(0);
                }
                if (!s.Text.empty() && cursor + bytes.size() <= area.size())
                {
                    shape = static_cast<std::int16_t>(cursor);
                    std::copy(bytes.begin(), bytes.end(), area.begin() + static_cast<std::ptrdiff_t>(cursor));
                    cursor += bytes.size();
                }
            }
            M().WriteBitDouble(s.Length);
            M().WriteBitShort(shape);
            M().WriteRawDouble(s.Offset.X);
            M().WriteRawDouble(s.Offset.Y);
            M().WriteBitDouble(s.Scale);
            M().WriteBitDouble(s.Rotation);
            M().WriteBitShort(static_cast<std::int16_t>(s.Flags));
        }
        M().WriteBytes(area);

        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        for (const LineTypeSegment& s : lineType.Segments)
            H(DwgRef::HardPointer, s.StyleHandle);
    }

    void Writer::WriteView(const View& view)
    {
        Text(view.Name);
        WriteXrefDependantBit(view);
        M().WriteBitDouble(view.Height);
        M().WriteBitDouble(view.Width);
        M().Write2RawDouble(view.Center);
        M().Write3BitDouble(view.Target);
        M().Write3BitDouble(view.Direction);
        M().WriteBitDouble(view.Angle);
        M().WriteBitDouble(view.LensLength);
        M().WriteBitDouble(view.FrontClipping);
        M().WriteBitDouble(view.BackClipping);
        M().WriteBit(HasFlag(view.ViewMode, ViewModeType::PerspectiveView));
        M().WriteBit(HasFlag(view.ViewMode, ViewModeType::FrontClipping));
        M().WriteBit(HasFlag(view.ViewMode, ViewModeType::BackClipping));
        // DWG 的第 4 位是"前裁剪面在视点处"，DXF 的 16 是"不在视点处"
        M().WriteBit(!HasFlag(view.ViewMode, ViewModeType::FrontClippingZ));
        if (R2000Plus())
            M().WriteByte(static_cast<std::uint8_t>(view.RenderMode));
        if (R2007Plus())
        {
            // 光照：未建模，AutoCAD 默认值
            M().WriteBit(true);
            M().WriteByte(1);
            M().WriteBitDouble(0.0);
            M().WriteBitDouble(0.0);
            Cmc(Color(std::int16_t(250)));
        }
        M().WriteBit((Flags(view.Flags) & 1) != 0);     // 图纸空间视图
        if (R2000Plus())
        {
            M().WriteBit(view.IsUcsAssociated);
            if (view.IsUcsAssociated)
            {
                M().Write3BitDouble(view.UcsOrigin);
                M().Write3BitDouble(view.UcsXAxis);
                M().Write3BitDouble(view.UcsYAxis);
                M().WriteBitDouble(view.UcsElevation);
                M().WriteBitShort(static_cast<std::int16_t>(view.UcsOrthographicType));
            }
        }
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        if (R2007Plus())
        {
            M().WriteBit(view.IsPlottable);
            H(DwgRef::SoftPointer, kNullHandle);        // 背景
            H(DwgRef::HardPointer, view.VisualStyleHandle);
            H(DwgRef::HardOwnership, kNullHandle);      // 太阳
        }
        if (R2000Plus() && view.IsUcsAssociated)
        {
            H(DwgRef::HardPointer, kNullHandle);        // 基准 UCS
            H(DwgRef::HardPointer, kNullHandle);        // 命名 UCS
        }
        if (R2007Plus())
            H(DwgRef::SoftPointer, kNullHandle);        // 实时剖切对象
    }

    void Writer::WriteUcs(const UCS& ucs)
    {
        Text(ucs.Name);
        WriteXrefDependantBit(ucs);
        M().Write3BitDouble(ucs.Origin);
        M().Write3BitDouble(ucs.XAxis);
        M().Write3BitDouble(ucs.YAxis);
        if (R2000Plus())
        {
            M().WriteBitDouble(ucs.Elevation);
            M().WriteBitShort(static_cast<std::int16_t>(ucs.OrthographicViewType));
            M().WriteBitShort(static_cast<std::int16_t>(ucs.OrthographicType));
        }
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        if (R2000Plus())
        {
            H(DwgRef::HardPointer, kNullHandle);
            H(DwgRef::HardPointer, kNullHandle);
        }
    }

    void Writer::WriteVPort(const VPort& vport)
    {
        Text(vport.Name);
        WriteXrefDependantBit(vport);
        M().WriteBitDouble(vport.ViewHeight);
        M().WriteBitDouble(vport.AspectRatio * vport.ViewHeight);
        M().Write2RawDouble(vport.Center);
        M().Write3BitDouble(vport.Target);
        M().Write3BitDouble(vport.Direction);
        M().WriteBitDouble(vport.TwistAngle);
        M().WriteBitDouble(vport.LensLength);
        M().WriteBitDouble(vport.FrontClippingPlane);
        M().WriteBitDouble(vport.BackClippingPlane);
        M().WriteBit(HasFlag(vport.ViewMode, ViewModeType::PerspectiveView));
        M().WriteBit(HasFlag(vport.ViewMode, ViewModeType::FrontClipping));
        M().WriteBit(HasFlag(vport.ViewMode, ViewModeType::BackClipping));
        M().WriteBit(!HasFlag(vport.ViewMode, ViewModeType::FrontClippingZ));
        if (R2000Plus())
            M().WriteByte(static_cast<std::uint8_t>(vport.RenderMode));
        if (R2007Plus())
        {
            M().WriteBit(vport.UseDefaultLighting);
            M().WriteByte(static_cast<std::uint8_t>(vport.DefaultLighting));
            M().WriteBitDouble(vport.Brightness);
            M().WriteBitDouble(vport.Contrast);
            Cmc(vport.AmbientColor);
        }
        M().Write2RawDouble(vport.BottomLeft);
        M().Write2RawDouble(vport.TopRight);
        M().WriteBit(HasFlag(vport.ViewMode, ViewModeType::Follow));
        M().WriteBitShort(vport.CircleZoomPercent);
        M().WriteBit(true);     // 快速缩放
        const int icon = static_cast<int>(vport.UcsIconDisplay);
        M().WriteBit((icon & 1) != 0);
        M().WriteBit((icon & 2) != 0);
        M().WriteBit(vport.ShowGrid);
        M().Write2RawDouble(vport.GridSpacing);
        M().WriteBit(vport.SnapOn);
        M().WriteBit(vport.IsometricSnap);
        M().WriteBitShort(vport.SnapIsoPair);
        M().WriteBitDouble(vport.SnapRotation);
        M().Write2RawDouble(vport.SnapBasePoint);
        M().Write2RawDouble(vport.SnapSpacing);
        if (R2000Plus())
        {
            M().WriteBit(false);
            M().WriteBit(true);     // 每个视口一个 UCS
            M().Write3BitDouble(vport.Origin);
            M().Write3BitDouble(vport.XAxis);
            M().Write3BitDouble(vport.YAxis);
            M().WriteBitDouble(vport.Elevation);
            M().WriteBitShort(static_cast<std::int16_t>(vport.OrthographicType));
        }
        if (R2007Plus())
        {
            M().WriteBitShort(static_cast<std::int16_t>(vport.GridFlags));
            M().WriteBitShort(vport.MinorGridLinesPerMajorGridLine);
        }
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        if (R2007Plus())
        {
            H(DwgRef::SoftPointer, kNullHandle);        // 背景
            H(DwgRef::HardPointer, vport.VisualStyleHandle);
            H(DwgRef::HardOwnership, kNullHandle);      // 太阳
        }
        if (R2000Plus())
        {
            H(DwgRef::HardPointer, vport.NamedUcsHandle);
            H(DwgRef::HardPointer, vport.BaseUcsHandle);
        }
    }

    void Writer::WriteAppId(const AppId& appId)
    {
        Text(appId.Name);
        WriteXrefDependantBit(appId);
        M().WriteByte(0);                       // 未公开的 71
        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
    }

    void Writer::WriteDimStyle(const DimensionStyle& d)
    {
        Text(d.Name);
        WriteXrefDependantBit(d);
        DwgBitWriter& m = M();
        if (R2000Plus())
        {
            Text(d.PostFix);
            Text(d.AlternateDimensioningSuffix);
            m.WriteBitDouble(d.ScaleFactor);
            m.WriteBitDouble(d.ArrowSize);
            m.WriteBitDouble(d.ExtensionLineOffset);
            m.WriteBitDouble(d.DimensionLineIncrement);
            m.WriteBitDouble(d.ExtensionLineExtension);
            m.WriteBitDouble(d.Rounding);
            m.WriteBitDouble(d.DimensionLineExtension);
            m.WriteBitDouble(d.PlusTolerance);
            m.WriteBitDouble(d.MinusTolerance);
        }
        if (R2007Plus())
        {
            m.WriteBitDouble(d.FixedExtensionLineLength);
            m.WriteBitDouble(d.JoggedRadiusDimensionTransverseSegmentAngle);
            m.WriteBitShort(static_cast<std::int16_t>(d.TextBackgroundFillMode));
            Cmc(d.TextBackgroundColor);
        }
        if (R2000Plus())
        {
            m.WriteBit(d.GenerateTolerances);
            m.WriteBit(d.LimitsGeneration);
            m.WriteBit(d.TextInsideHorizontal);
            m.WriteBit(d.TextOutsideHorizontal);
            m.WriteBit(d.SuppressFirstExtensionLine);
            m.WriteBit(d.SuppressSecondExtensionLine);
            m.WriteBitShort(static_cast<std::int16_t>(d.TextVerticalAlignment));
            m.WriteBitShort(static_cast<std::int16_t>(d.ZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(d.AngularZeroHandling));
        }
        if (R2007Plus())
            m.WriteBitShort(static_cast<std::int16_t>(d.ArcLengthSymbolPosition));
        if (R2000Plus())
        {
            m.WriteBitDouble(d.TextHeight);
            m.WriteBitDouble(d.CenterMarkSize);
            m.WriteBitDouble(d.TickSize);
            m.WriteBitDouble(d.AlternateUnitScaleFactor);
            m.WriteBitDouble(d.LinearScaleFactor);
            m.WriteBitDouble(d.TextVerticalPosition);
            m.WriteBitDouble(d.ToleranceScaleFactor);
            m.WriteBitDouble(d.DimensionLineGap);
            m.WriteBitDouble(d.AlternateUnitRounding);
            m.WriteBit(d.AlternateUnitDimensioning);
            m.WriteBitShort(static_cast<std::int16_t>(d.AlternateUnitDecimalPlaces));
            m.WriteBit(d.TextOutsideExtensions);
            m.WriteBit(d.SeparateArrowBlocks);
            m.WriteBit(d.TextInsideExtensions);
            m.WriteBit(d.SuppressOutsideExtensions);
            Cmc(d.DimensionLineColor);
            Cmc(d.ExtensionLineColor);
            Cmc(d.TextColor);
            m.WriteBitShort(static_cast<std::int16_t>(d.AngularDecimalPlaces));
            m.WriteBitShort(static_cast<std::int16_t>(d.DecimalPlaces));
            m.WriteBitShort(static_cast<std::int16_t>(d.ToleranceDecimalPlaces));
            m.WriteBitShort(static_cast<std::int16_t>(d.AlternateUnitFormat));
            m.WriteBitShort(static_cast<std::int16_t>(d.AlternateUnitToleranceDecimalPlaces));
            m.WriteBitShort(static_cast<std::int16_t>(d.AngularUnit));
            m.WriteBitShort(static_cast<std::int16_t>(d.FractionFormat));
            m.WriteBitShort(static_cast<std::int16_t>(d.LinearUnitFormat));
            m.WriteBitShort(static_cast<std::int16_t>(d.DecimalSeparator));
            m.WriteBitShort(static_cast<std::int16_t>(d.TextMovement));
            m.WriteBitShort(static_cast<std::int16_t>(d.TextHorizontalAlignment));
            m.WriteBit(d.SuppressFirstDimensionLine);
            m.WriteBit(d.SuppressSecondDimensionLine);
            m.WriteBitShort(static_cast<std::int16_t>(d.ToleranceAlignment));
            m.WriteBitShort(static_cast<std::int16_t>(d.ToleranceZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(d.AlternateUnitZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(d.AlternateUnitToleranceZeroHandling));
            m.WriteBit(d.CursorUpdate);
            m.WriteBitShort(static_cast<std::int16_t>(d.DimensionFit));
        }
        if (R2007Plus())
            m.WriteBit(d.IsExtensionLineLengthFixed);
        if (R2010Plus())
        {
            m.WriteBit(d.TextDirection == TextDirection::RightToLeft);
            m.WriteBitDouble(100.0);    // DIMALTMZF：未建模，AutoCAD 默认值
            Text("");                   // DIMALTMZS
            m.WriteBitDouble(100.0);    // DIMMZF
            Text("");                   // DIMMZS
        }
        if (R2000Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(d.DimensionLineWeight));
            m.WriteBitShort(static_cast<std::int16_t>(d.ExtensionLineWeight));
        }
        m.WriteBit(false);

        H(DwgRef::HardPointer, kNullHandle);    // 外部参照块
        H(DwgRef::HardPointer, d.StyleHandle);
        if (R2000Plus())
        {
            H(DwgRef::HardPointer, d.LeaderArrowHandle);
            H(DwgRef::HardPointer, d.ArrowBlockHandle);
            H(DwgRef::HardPointer, d.DimArrow1Handle);
            H(DwgRef::HardPointer, d.DimArrow2Handle);
        }
        if (R2007Plus())
        {
            H(DwgRef::HardPointer, d.LineTypeHandle);
            H(DwgRef::HardPointer, d.LineTypeExt1Handle);
            H(DwgRef::HardPointer, d.LineTypeExt2Handle);
        }
    }

    // ── 字典与对象 ─────────────────────────────────────────────────

    void Writer::WriteNonGraphical(const CadObject& object)
    {
        if (!BeginObject(object))
            return;
        const RawObjectData* raw = RawDataOf(object);
        m_dataStoreBit = raw != nullptr && raw->Dwg.HasDataStore && m_writeDataStore;
        WriteCommonNonEntityData(object, object.OwnerHandle);
        m_dataStoreBit = false;

        if (raw != nullptr)
            WriteRawData(raw->Dwg);
        else if (const auto* dict = dynamic_cast<const CadDictionary*>(&object))
            WriteDictionary(*dict);
        else if (const auto* assoc = dynamic_cast<const DimensionAssociation*>(&object))
            WriteDimensionAssociation(*assoc);
        else if (const auto* mlStyle = dynamic_cast<const MultiLeaderStyle*>(&object))
            WriteMultiLeaderStyle(*mlStyle);
        else if (const auto* var = dynamic_cast<const DictionaryVariable*>(&object))
            WriteDictionaryVariable(*var);
        else if (const auto* group = dynamic_cast<const Group*>(&object))
            WriteGroup(*group);
        else if (const auto* style = dynamic_cast<const MLineStyle*>(&object))
            WriteMLineStyle(*style);
        else if (const auto* record = dynamic_cast<const XRecord*>(&object))
            WriteXRecord(*record);
        else if (const auto* layout = dynamic_cast<const Layout*>(&object))
            WriteLayout(*layout);
        else if (const auto* plot = dynamic_cast<const PlotSettings*>(&object))
            WritePlotSettings(*plot);
        else if (const auto* def = dynamic_cast<const ImageDefinition*>(&object))
            WriteImageDefinition(*def);
        else if (const auto* reactor = dynamic_cast<const ImageDefinitionReactor*>(&object))
            WriteImageDefinitionReactor(*reactor);
        else if (const auto* vars = dynamic_cast<const RasterVariables*>(&object))
            WriteRasterVariables(*vars);
        else if (const auto* scale = dynamic_cast<const Scale*>(&object))
            WriteScale(*scale);
        else if (const auto* sortents = dynamic_cast<const SortEntitiesTable*>(&object))
            WriteSortEntitiesTable(*sortents);
        else if (dynamic_cast<const AcdbPlaceHolder*>(&object) != nullptr)
        {
            // 占位对象没有数据
        }
        else
        {
            NotifyOnce("object:" + std::string(object.GetDxfName()), NotificationType::Warning,
                       "未实现写出的对象 " + std::string(object.GetDxfName()));
        }
        EndObject(object.ObjectHandle);
    }

    // 字段顺序见 DwgReadObjects.cpp 的 ReadDimensionAssociation
    void Writer::WriteDimensionAssociation(const DimensionAssociation& assoc)
    {
        DwgBitWriter& m = M();
        H(DwgRef::SoftPointer, assoc.DimensionHandle);
        m.WriteBitLong(static_cast<std::int32_t>(assoc.AssociativityFlags));
        m.WriteBit(assoc.IsTransSpace);
        m.WriteByte(static_cast<std::uint8_t>(assoc.RotatedDimensionType));
        for (int i = 0; i < 4; ++i)
        {
            if ((static_cast<int>(assoc.AssociativityFlags) & (1 << i)) == 0)
                continue;
            const DimensionAssociationOsnapPointRef& ref = PointRefAt(assoc, i);
            Text("AcDbOsnapPointRef");
            m.WriteByte(static_cast<std::uint8_t>(ref.ObjectOsnapType));
            m.WriteBitLong(1);
            H(DwgRef::SoftPointer, ref.GeometryHandle);
            m.WriteBitLong(static_cast<std::int32_t>(ref.SubentType));
            m.WriteBitLong(ref.GsMarker);
            m.WriteBitLong(0);
            m.WriteBitDouble(ref.GeometryParameter);
            m.Write3BitDouble(ref.OsnapPoint);
            m.WriteBit(false);
        }
    }

    void Writer::WriteDictionary(const CadDictionary& dict)
    {
        std::vector<std::size_t> entries;
        const std::size_t n = std::min(dict.EntryNames.size(), dict.EntryHandles.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const Handle h = dict.EntryHandles[i];
            if (Written(h) && !dict.EntryNames[i].empty() && FindAs<Entity>(h) == nullptr)
                entries.push_back(i);
        }
        M().WriteBitLong(static_cast<std::int32_t>(entries.size()));
        if (R2000Plus())
        {
            M().WriteBitShort(static_cast<std::int16_t>(dict.ClonningFlags));
            M().WriteByte(dict.HardOwnerFlag ? 1 : 0);
        }
        for (std::size_t i : entries)
        {
            Text(dict.EntryNames[i]);
            H(dict.HardOwnerFlag ? DwgRef::HardOwnership : DwgRef::SoftOwnership, dict.EntryHandles[i]);
        }
        if (const auto* withDefault = dynamic_cast<const CadDictionaryWithDefault*>(&dict))
            H(DwgRef::HardPointer, withDefault->DefaultEntryHandle);
    }

    void Writer::WriteDictionaryVariable(const DictionaryVariable& var)
    {
        M().WriteByte(static_cast<std::uint8_t>(var.ObjectSchemaNumber));
        Text(var.Value);
    }

    void Writer::WriteGroup(const Group& group)
    {
        Text(group.Description);
        M().WriteBitShort(!group.Name.empty() && group.Name[0] == '*' ? 1 : 0);     // 匿名组
        M().WriteBitShort(group.Selectable ? 1 : 0);
        std::vector<Handle> entities;
        for (Handle h : group.Entities)
        {
            if (Written(h))
                entities.push_back(h);
        }
        M().WriteBitLong(static_cast<std::int32_t>(entities.size()));
        for (Handle h : entities)
            H(DwgRef::HardPointer, h);
    }

    void Writer::WriteMLineStyle(const MLineStyle& style)
    {
        Text(style.Name);
        Text(style.Description);
        // DWG 与 DXF 的标志位不同：1↔2、32↔64、512↔1024 对调（与读取相反）
        const int f = Flags(style.Flags);
        std::int16_t flags = 0;
        if (f & Flags(MLineStyleFlags::DisplayJoints)) flags |= 1;
        if (f & Flags(MLineStyleFlags::FillOn)) flags |= 2;
        if (f & Flags(MLineStyleFlags::StartSquareCap)) flags |= 16;
        if (f & Flags(MLineStyleFlags::StartRoundCap)) flags |= 32;
        if (f & Flags(MLineStyleFlags::StartInnerArcsCap)) flags |= 64;
        if (f & Flags(MLineStyleFlags::EndSquareCap)) flags |= 256;
        if (f & Flags(MLineStyleFlags::EndRoundCap)) flags |= 512;
        if (f & Flags(MLineStyleFlags::EndInnerArcsCap)) flags |= 1024;
        M().WriteBitShort(flags);
        Cmc(style.FillColor);
        M().WriteBitDouble(style.StartAngle);
        M().WriteBitDouble(style.EndAngle);
        M().WriteByte(static_cast<std::uint8_t>(style.Elements.size()));
        for (const MLineStyleElement& e : style.Elements)
        {
            M().WriteBitDouble(e.Offset);
            Cmc(e.Color);
            if (R2018Plus())
            {
                H(DwgRef::HardPointer, e.LineTypeHandle);
                continue;
            }
            // R2018 之前：线型在线型表中的序号（ByLayer 0x7FFF、ByBlock 0x7FFE）
            std::int16_t index = 0x7FFF;
            const auto* lineType = m_db.FindAs<LineType>(e.LineTypeHandle);
            if (lineType != nullptr && EqualsIgnoreCase(lineType->Name, "ByBlock"))
                index = 0x7FFE;
            else if (lineType != nullptr && !EqualsIgnoreCase(lineType->Name, "ByLayer"))
            {
                auto it = std::find(m_lineTypeIndex.begin(), m_lineTypeIndex.end(), e.LineTypeHandle);
                if (it != m_lineTypeIndex.end())
                    index = static_cast<std::int16_t>(it - m_lineTypeIndex.begin());
            }
            M().WriteBitShort(index);
        }
    }

    void Writer::WriteXRecord(const XRecord& record)
    {
        DwgBitWriter data(m_version, m_codePage);
        for (const XRecordEntry& e : record.Entries)
        {
            const int code = e.Code;
            const GroupCodeType type = GroupCodeTypeOf(code);
            if (type == GroupCodeType::None || type == GroupCodeType::Comment)
            {
                NotifyOnce("xrecord:" + std::to_string(code), NotificationType::Warning,
                           "XRECORD 中无法写出的组码 " + std::to_string(code));
                continue;
            }
            data.WriteRawShort(static_cast<std::int16_t>(code));
            switch (type)
            {
            case GroupCodeType::String:
                data.WriteTextUnicode(e.Value.AsString());
                break;
            case GroupCodeType::Point3D:
                data.Write3RawDouble(e.Value.AsXYZ());
                break;
            case GroupCodeType::Double:
                data.WriteRawDouble(e.Value.AsDouble());
                break;
            case GroupCodeType::Byte:
            case GroupCodeType::Bool:
                data.WriteByte(static_cast<std::uint8_t>(e.Value.AsInt()));
                break;
            case GroupCodeType::Int16:
                data.WriteRawShort(static_cast<std::int16_t>(e.Value.AsInt()));
                break;
            case GroupCodeType::Int32:
                data.WriteRawLong(static_cast<std::int32_t>(e.Value.AsInt()));
                break;
            case GroupCodeType::Int64:
                data.WriteRawLongLong(static_cast<std::uint64_t>(e.Value.AsInt()));
                break;
            case GroupCodeType::Chunk:
            {
                const std::vector<std::uint8_t>* bytes = e.Value.AsBytes();
                const std::size_t n = bytes != nullptr ? std::min<std::size_t>(bytes->size(), 255) : 0;
                data.WriteByte(static_cast<std::uint8_t>(n));
                if (n > 0)
                    data.WriteBytes(std::span<const std::uint8_t>(bytes->data(), n));
                break;
            }
            case GroupCodeType::Handle:
            {
                const Handle h = Ref(e.Value.AsHandle());
                if (code == 5 || code == 105)
                {
                    char hex[24];
                    std::snprintf(hex, sizeof(hex), "%llX", static_cast<unsigned long long>(h));
                    data.WriteTextUnicode(hex);
                }
                else
                {
                    data.WriteRawLongLong(h);
                }
                break;
            }
            default:
                break;
            }
        }
        M().WriteBitLong(static_cast<std::int32_t>(data.Data().size()));
        M().WriteBytes(data.Data());
        if (R2000Plus())
            M().WriteBitShort(static_cast<std::int16_t>(record.CloningFlags));
    }

    void Writer::WritePlotSettings(const PlotSettings& plot)
    {
        DwgBitWriter& m = M();
        Text(plot.PageName);
        Text(plot.SystemPrinterName);
        m.WriteBitShort(static_cast<std::int16_t>(plot.Flags));
        m.WriteBitDouble(plot.UnprintableMargin.Left);
        m.WriteBitDouble(plot.UnprintableMargin.Bottom);
        m.WriteBitDouble(plot.UnprintableMargin.Right);
        m.WriteBitDouble(plot.UnprintableMargin.Top);
        m.WriteBitDouble(plot.PaperWidth);
        m.WriteBitDouble(plot.PaperHeight);
        Text(plot.PaperSize);
        m.WriteBitDouble(plot.PlotOriginX);
        m.WriteBitDouble(plot.PlotOriginY);
        m.WriteBitShort(static_cast<std::int16_t>(plot.PaperUnits));
        m.WriteBitShort(static_cast<std::int16_t>(plot.PaperRotation));
        m.WriteBitShort(static_cast<std::int16_t>(plot.PlotType));
        m.WriteBitDouble(plot.WindowLowerLeftX);
        m.WriteBitDouble(plot.WindowLowerLeftY);
        m.WriteBitDouble(plot.WindowUpperLeftX);
        m.WriteBitDouble(plot.WindowUpperLeftY);
        if (R2004Pre())
            Text(plot.PlotViewName);
        m.WriteBitDouble(plot.NumeratorScale);
        m.WriteBitDouble(plot.DenominatorScale);
        Text(plot.StyleSheet);
        m.WriteBitShort(static_cast<std::int16_t>(plot.ScaledFit));
        m.WriteBitDouble(plot.StandardScale);
        m.Write2BitDouble({ plot.PaperImageOriginX, plot.PaperImageOriginY });
        if (R2004Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(plot.ShadePlotMode));
            m.WriteBitShort(static_cast<std::int16_t>(plot.ShadePlotResolutionMode));
            m.WriteBitShort(plot.ShadePlotDPI);
            H(DwgRef::HardPointer, kNullHandle);    // 打印视图
        }
        if (R2007Plus())
            H(DwgRef::SoftPointer, plot.ShadePlotIDHandle);
    }

    void Writer::WriteLayout(const Layout& layout)
    {
        WritePlotSettings(layout);
        DwgBitWriter& m = M();
        Text(layout.Name);
        m.WriteBitLong(layout.TabOrder);
        m.WriteBitShort(static_cast<std::int16_t>(layout.LayoutFlags));
        m.Write3BitDouble(layout.Origin);
        m.Write2RawDouble(layout.MinLimits);
        m.Write2RawDouble(layout.MaxLimits);
        m.Write3BitDouble(layout.InsertionBasePoint);
        m.Write3BitDouble(layout.XAxis);
        m.Write3BitDouble(layout.YAxis);
        m.WriteBitDouble(layout.Elevation);
        m.WriteBitShort(static_cast<std::int16_t>(layout.UcsOrthographicType));
        m.Write3BitDouble(layout.MinExtents);
        m.Write3BitDouble(layout.MaxExtents);

        // 布局中的视口：布局块中写出的视口实体
        std::vector<Handle> viewports;
        if (auto it = m_blockEntities.find(layout.AssociatedBlockHandle); it != m_blockEntities.end())
        {
            for (Handle e : it->second)
            {
                if (FindAs<Viewport>(e) != nullptr)
                    viewports.push_back(e);
            }
        }
        if (R2004Plus())
            m.WriteBitLong(static_cast<std::int32_t>(viewports.size()));

        H(DwgRef::SoftPointer, layout.AssociatedBlockHandle);
        H(DwgRef::SoftPointer, layout.LastActiveViewportHandle);
        H(DwgRef::HardPointer, layout.BaseUCSHandle);
        H(DwgRef::HardPointer, layout.UCSHandle);
        if (R2004Plus())
        {
            for (Handle v : viewports)
                H(DwgRef::SoftPointer, v);
        }
    }

    void Writer::WriteImageDefinition(const ImageDefinition& def)
    {
        M().WriteBitLong(def.ClassVersion);
        M().Write2RawDouble(def.Size);
        Text(def.FileName);
        M().WriteBit(def.IsLoaded);
        M().WriteByte(static_cast<std::uint8_t>(def.Units));
        M().Write2RawDouble(def.DefaultSize);
    }

    void Writer::WriteImageDefinitionReactor(const ImageDefinitionReactor& reactor)
    {
        M().WriteBitLong(reactor.ClassVersion);
    }

    void Writer::WriteRasterVariables(const RasterVariables& vars)
    {
        M().WriteBitLong(vars.ClassVersion);
        M().WriteBitShort(vars.IsDisplayFrameShown ? 1 : 0);
        M().WriteBitShort(static_cast<std::int16_t>(vars.DisplayQuality));
        M().WriteBitShort(static_cast<std::int16_t>(vars.Units));
    }

    void Writer::WriteScale(const Scale& scale)
    {
        M().WriteBitShort(0);
        Text(scale.Name);
        M().WriteBitDouble(scale.PaperUnits);
        M().WriteBitDouble(scale.DrawingUnits);
        M().WriteBit(scale.IsUnitScale);
    }

    void Writer::WriteSortEntitiesTable(const SortEntitiesTable& table)
    {
        H(DwgRef::SoftPointer, table.BlockOwnerHandle);
        std::vector<const SortEntsEntry*> entries;
        for (const SortEntsEntry& e : table.Entries)
        {
            if (Written(e.EntityHandle))
                entries.push_back(&e);
        }
        M().WriteBitLong(static_cast<std::int32_t>(entries.size()));
        for (const SortEntsEntry* e : entries)
        {
            // 排序句柄在数据流中，实体句柄在句柄流中
            M().WriteHandle(DwgRef::Undefined, e->SortHandle);
            H(DwgRef::SoftPointer, e->EntityHandle);
        }
    }
}
