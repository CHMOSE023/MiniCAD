// DWG 读取：对象的公共数据、类型分派、符号表与非图形对象（对应 ACadSharp DwgObjectReader）
#include "Dwg/Read/DwgReaderImpl.h"
#include <charconv>

namespace MiniDWG::DwgRead
{
    namespace
    {
        // 大端 8 字节句柄（扩展数据 1003/1005）
        Handle ReadBigEndianHandle(DwgBitReader& r)
        {
            Handle h = 0;
            for (int i = 0; i < 8; ++i)
                h = (h << 8) | r.ReadByte();
            return h;
        }

        std::string_view TableEntryName(std::int16_t controlType)
        {
            switch (static_cast<CadObjectType>(controlType))
            {
            case CadObjectType::BLOCK_CONTROL_OBJ: return "BLOCK_RECORD";
            case CadObjectType::LAYER_CONTROL_OBJ: return "LAYER";
            case CadObjectType::STYLE_CONTROL_OBJ: return "STYLE";
            case CadObjectType::LTYPE_CONTROL_OBJ: return "LTYPE";
            case CadObjectType::VIEW_CONTROL_OBJ: return "VIEW";
            case CadObjectType::UCS_CONTROL_OBJ: return "UCS";
            case CadObjectType::VPORT_CONTROL_OBJ: return "VPORT";
            case CadObjectType::APPID_CONTROL_OBJ: return "APPID";
            case CadObjectType::DIMSTYLE_CONTROL_OBJ: return "DIMSTYLE";
            default: return {};
            }
        }
    }

    // ── 对象的位置与三路读取器 ─────────────────────────────────────

    // 对象开头：MS 对象大小；R2010 起 MC 句柄流位数；之后是类型（OT）。返回类型，失败返回 -1
    std::int16_t Reader::BeginObject(std::uint64_t offset)
    {
        DwgBitReader crc(m_objectData, m_version, m_file.CodePage());
        crc.SetPosition(offset);
        m_objectSize = crc.ReadModularShort();
        if (m_objectSize == 0 || crc.Failed())
            return -1;
        const std::uint64_t sizeInBits = std::uint64_t(m_objectSize) << 3;

        m_objReader = DwgBitReader(m_objectData, m_version, m_file.CodePage());
        m_handleReader = m_objReader;
        m_textReader = m_objReader;
        if (R2010Plus())
        {
            const std::uint64_t handleSize = crc.ReadModularChar();
            const std::uint64_t handleOffset = crc.PositionInBits() + sizeInBits - handleSize;
            m_objReader.SetPositionInBits(crc.PositionInBits());
            m_objectInitialPos = m_objReader.PositionInBits();
            const std::int16_t type = m_objReader.ReadObjectType();
            m_handleReader.SetPositionInBits(handleOffset);
            m_textReader.SetPositionByFlag(handleOffset - 1);
            m_objectEnd = m_objectInitialPos + sizeInBits;
            m_mainEnd = m_textReader.IsEmpty() ? handleOffset - 1 : m_textReader.PositionInBits();
            m_s = DwgStreams{ &m_objReader, &m_textReader, &m_handleReader };
            return type;
        }

        m_objReader.SetPositionInBits(crc.PositionInBits());
        m_objectInitialPos = m_objReader.PositionInBits();
        m_objectEnd = m_objectInitialPos + sizeInBits;
        m_mainEnd = m_objectEnd;
        m_s = DwgStreams{ &m_objReader, &m_objReader, &m_handleReader };
        return m_objReader.ReadObjectType();
    }

    // R13～R2007：RL 数据部分的位数，句柄流从那里开始；R2007 的字符串流在数据部分末尾
    void Reader::UpdateHandleReader()
    {
        const std::uint64_t size = static_cast<std::uint32_t>(m_objReader.ReadRawLong());
        m_handleReader.SetPositionInBits(m_objectInitialPos + size);
        m_mainEnd = m_objectInitialPos + size;
        if (m_version == CadVersion::AC1021)
        {
            m_textReader = DwgBitReader(m_objectData, m_version, m_file.CodePage());
            m_textReader.SetPositionByFlag(m_objectInitialPos + size - 1);
            m_s.Text = &m_textReader;
            m_mainEnd = m_textReader.IsEmpty() ? m_objectInitialPos + size - 1 : m_textReader.PositionInBits();
        }
    }

    Handle Reader::HandleRef(Handle reference)
    {
        const Handle h = m_handleReader.ReadHandle(reference);
        if (h != kNullHandle && m_visited.count(h) == 0)
            m_queue.push_back(h);
        return h;
    }

    // ── 公共数据 ───────────────────────────────────────────────────

    void Reader::ReadCommonData(CadObject& object)
    {
        if (m_version >= CadVersion::AC1015 && m_version < CadVersion::AC1024)
            UpdateHandleReader();
        object.ObjectHandle = m_objReader.ReadHandle();
        ReadExtendedData(object);
    }

    void Reader::ReadCommonNonEntityData(CadObject& object)
    {
        ReadCommonData(object);
        if (R13_14Only())
            UpdateHandleReader();
        object.OwnerHandle = HandleRef(object.ObjectHandle);
        ReadReactorsAndXDictionary(object);
    }

    void Reader::ReadReactorsAndXDictionary(CadObject& object)
    {
        const std::int32_t count = m_objReader.ReadBitLong();
        if (!m_objReader.CheckCount(count))
            return;
        for (int i = 0; i < count; ++i)
            object.Reactors.push_back(HandleRef());
        bool missing = false;
        if (R2004Plus())
            missing = m_objReader.ReadBit();
        if (!missing)
            object.XDictionaryHandle = HandleRef();
        m_lastDataStore = false;
        if (R2013Plus())
            m_lastDataStore = m_objReader.ReadBit();    // 是否有数据存储中的二进制数据
    }

    // 扩展数据：BS 大小，H 应用程序句柄，之后是记录（1 字节组码 - 1000 + 值），直到大小为 0
    void Reader::ReadExtendedData(CadObject& object)
    {
        DwgBitReader& r = m_objReader;
        std::int16_t size = r.ReadBitShort();
        while (size != 0 && !r.Failed())
        {
            ExtendedData data;
            data.AppIdHandle = r.ReadHandle();
            if (data.AppIdHandle != kNullHandle && m_visited.count(data.AppIdHandle) == 0)
                m_queue.push_back(data.AppIdHandle);
            const std::uint64_t end = r.Position() + static_cast<std::uint16_t>(size);
            while (r.Position() < end && !r.Failed())
            {
                ExtendedDataRecord rec;
                rec.Code = static_cast<std::int16_t>(1000 + r.ReadByte());
                switch (rec.Code)
                {
                case 1000:
                case 1001:
                    rec.Value = r.ReadTextUnicode();
                    break;
                case 1002:
                    rec.Value = std::string(r.ReadByte() == 1 ? "}" : "{");
                    break;
                case 1003:
                    rec.Value = ReadBigEndianHandle(r);     // 图层句柄，建库时换成图层名
                    break;
                case 1004:
                    rec.Value = r.ReadBytes(r.ReadByte());
                    break;
                case 1005:
                    rec.Value = ReadBigEndianHandle(r);
                    break;
                case 1010: case 1011: case 1012: case 1013:
                    rec.Value = r.Read3RawDouble();
                    break;
                case 1040: case 1041: case 1042:
                    rec.Value = r.ReadRawDouble();
                    break;
                case 1070:
                    rec.Value = r.ReadRawShort();
                    break;
                case 1071:
                    rec.Value = r.ReadRawLong();
                    break;
                default:
                    NotifyOnce("xdata:" + std::to_string(rec.Code), NotificationType::Warning,
                               "扩展数据中未知的组码 " + std::to_string(rec.Code));
                    r.SetPosition(end);
                    continue;
                }
                data.Records.push_back(std::move(rec));
            }
            object.ExtendedDataList.push_back(std::move(data));
            size = r.ReadBitShort();
        }
    }

    void Reader::ReadXrefDependantBit(TableEntry& entry)
    {
        int flags = static_cast<int>(entry.Flags);
        if (R2007Plus())
        {
            const std::int16_t xrefIndex = m_objReader.ReadBitShort();
            if (xrefIndex & 0x100)
                flags |= static_cast<int>(StandardFlags::XrefDependent);
        }
        else
        {
            if (m_objReader.ReadBit())
                flags |= static_cast<int>(StandardFlags::Referenced);
            m_objReader.ReadBitShort();     // xrefindex + 1
            if (m_objReader.ReadBit())
                flags |= static_cast<int>(StandardFlags::XrefDependent);
        }
        entry.Flags = static_cast<StandardFlags>(flags);
    }

    // ── 分派 ───────────────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadObject(std::int16_t type, ObjectInfo& info)
    {
        using T = CadObjectType;
        switch (static_cast<T>(type))
        {
        case T::TEXT:
        case T::ATTRIB:
        case T::ATTDEF:
            return ReadText(type, info);
        case T::INSERT: return ReadInsert(false, info);
        case T::MINSERT: return ReadInsert(true, info);
        case T::VERTEX_2D: return ReadVertex2D(info);
        case T::VERTEX_3D:
        case T::VERTEX_MESH:
        case T::VERTEX_PFACE:
            return ReadVertex3D(type, info);
        case T::VERTEX_PFACE_FACE: return ReadFaceRecord(info);
        case T::POLYLINE_PFACE: return ReadPolyfaceMesh(info);
        case T::POLYLINE_MESH: return ReadPolygonMesh(info);
        case T::POLYLINE_2D: return ReadPolyline2D(info);
        case T::POLYLINE_3D: return ReadPolyline3D(info);
        case T::DIMENSION_ORDINATE:
        case T::DIMENSION_LINEAR:
        case T::DIMENSION_ALIGNED:
        case T::DIMENSION_ANG_3_Pt:
        case T::DIMENSION_ANG_2_Ln:
        case T::DIMENSION_RADIUS:
        case T::DIMENSION_DIAMETER:
            return ReadDimension(type, info);
        case T::BLOCK:
        case T::ENDBLK:
        case T::SEQEND:
        case T::ARC:
        case T::CIRCLE:
        case T::LINE:
        case T::POINT:
        case T::FACE3D:
        case T::SOLID:
        case T::TRACE:
        case T::SHAPE:
        case T::ELLIPSE:
        case T::RAY:
        case T::XLINE:
            return ReadSimpleEntity(type, info);
        case T::VIEWPORT: return ReadViewport(info);
        case T::SPLINE: return ReadSpline(info);
        case T::MTEXT: return ReadMText(info);
        case T::LEADER: return ReadLeader(info);
        case T::TOLERANCE: return ReadTolerance(info);
        case T::MLINE: return ReadMLine(info);
        case T::LWPOLYLINE: return ReadLwPolyline(info);
        case T::HATCH: return ReadHatch(info);

        case T::BLOCK_CONTROL_OBJ:
        case T::LAYER_CONTROL_OBJ:
        case T::STYLE_CONTROL_OBJ:
        case T::LTYPE_CONTROL_OBJ:
        case T::VIEW_CONTROL_OBJ:
        case T::UCS_CONTROL_OBJ:
        case T::VPORT_CONTROL_OBJ:
        case T::APPID_CONTROL_OBJ:
        case T::DIMSTYLE_CONTROL_OBJ:
            return ReadTableControl(type, info);
        case T::BLOCK_HEADER: return ReadBlockHeader(info);
        case T::LAYER: return ReadLayer();
        case T::STYLE: return ReadTextStyle();
        case T::LTYPE: return ReadLineType();
        case T::VIEW: return ReadView();
        case T::UCS: return ReadUcs();
        case T::VPORT: return ReadVPort();
        case T::APPID: return ReadAppId();
        case T::DIMSTYLE: return ReadDimStyle();
        case T::DICTIONARY: return ReadDictionary(false);
        case T::GROUP: return ReadGroup();
        case T::MLINESTYLE: return ReadMLineStyle(info);
        case T::XRECORD: return ReadXRecord();
        case T::ACDBPLACEHOLDER: return ReadPlaceHolder();
        case T::LAYOUT: return ReadLayout();
        default:
            break;
        }
        if (type >= 500)
            return ReadUnlisted(type, info);

        // 没有建模的固定类型：实体读公共数据（保留链接），对象读所有者与反应器（引用的对象照常读取）
        static const std::map<int, std::string_view> kNames = {
            { 0x0C, "VERTEX" }, { 0x0D, "VERTEX" }, { 0x0E, "VERTEX" }, { 0x1D, "POLYLINE" }, { 0x1E, "POLYLINE" },
            { 0x25, "REGION" }, { 0x26, "3DSOLID" }, { 0x27, "BODY" }, { 0x2B, "OLEFRAME" }, { 0x4A, "OLE2FRAME" },
            { 0x47, "VP_ENT_HDR" }, { 0x46, "VP_ENT_HDR_CTRL_OBJ" }, { 0x1F2, "ACAD_PROXY_ENTITY" },
            { 0x1F3, "ACAD_PROXY_OBJECT" }, { 0x4C, "LONG_TRANSACTION" }, { 0x51, "VBA_PROJECT" }, { 0x4B, "DUMMY" },
        };
        auto name = kNames.find(type);
        const std::string dxfName = name != kNames.end() ? std::string(name->second) : "类型 " + std::to_string(type);
        const bool isEntity = type < 0x2A || type == 0x2B || type == 0x4A || type == 0x1F2 || type == 0x4B;

        // 能独立原样保留的类型（不依赖由写入器维护的子实体与链接）：三维实体、OLE、代理对象等
        switch (type)
        {
        case 0x25: case 0x26: case 0x27: case 0x2B: case 0x4A: case 0x1F2: case 0x1F3: case 0x51:
            return ReadUnknown(dxfName, type, isEntity, info);
        default:
            break;
        }
        if (isEntity)
        {
            Line dummy;
            ReadCommonEntityData(dummy, info);
        }
        else
        {
            AcdbPlaceHolder dummy;
            ReadCommonNonEntityData(dummy);
        }
        ++m_skipped[dxfName];
        NotifyOnce("skip:" + dxfName, NotificationType::Info, "跳过未支持的 " + dxfName);
        return nullptr;
    }

    // 按类名注册的类型（类号 ≥ 500）
    std::unique_ptr<CadObject> Reader::ReadUnlisted(std::int16_t type, ObjectInfo& info)
    {
        auto it = m_classes.find(type);
        if (it == m_classes.end())
        {
            NotifyOnce("class:" + std::to_string(type), NotificationType::Warning,
                       "CLASSES 段中找不到类号 " + std::to_string(type));
            return nullptr;
        }
        const DxfClass& c = it->second;
        const std::string& n = c.DxfName;
        if (n == "ACDBDICTIONARYWDFLT" || n == "DICTIONARYWDFLT") return ReadDictionary(true);
        if (n == "ACDBPLACEHOLDER") return ReadPlaceHolder();
        if (n == "DICTIONARYVAR") return ReadDictionaryVariable();
        if (n == "GROUP") return ReadGroup();
        if (n == "HATCH") return ReadHatch(info);
        if (n == "IMAGE") return ReadImage(false, info);
        if (n == "WIPEOUT") return ReadImage(true, info);
        if (n == "IMAGEDEF") return ReadImageDefinition();
        if (n == "IMAGEDEF_REACTOR") return ReadImageDefinitionReactor();
        if (n == "LAYOUT") return ReadLayout();
        if (n == "PLOTSETTINGS") return ReadPlotSettingsObject();
        if (n == "LWPLINE" || n == "LWPOLYLINE") return ReadLwPolyline(info);
        if (n == "SCALE") return ReadScale();
        if (n == "SORTENTSTABLE") return ReadSortEntitiesTable();
        if (n == "RASTERVARIABLES") return ReadRasterVariables();
        if (n == "XRECORD") return ReadXRecord();
        if (n == "ARC_DIMENSION") return ReadDimensionArc(info);
        if (n == "MLEADERSTYLE") return ReadMultiLeaderStyle();
        if (n == "MULTILEADER")
        {
            if (auto ml = ReadMultiLeader(info))
                return ml;
            BeginObject(m_map[m_currentHandle]);
            info = ObjectInfo{};
            return ReadUnknown(n, 0, true, info);
        }
        if (n == "DIMASSOC")
        {
            // 遇到样例中没有出现过的结构时原样保留
            if (auto assoc = ReadDimensionAssociation())
                return assoc;
            BeginObject(m_map[m_currentHandle]);
            return ReadUnknown(n, 0, false, info);
        }
        if (n == "DBCOLOR")
        {
            // 颜色簿颜色：对象本身没有建模（原样保留），取出颜色，建库时填给引用它的实体
            auto object = ReadUnknown(n, 0, false, info);
            m_bookColors[m_currentHandle] = m_s.ReadCmColor();
            return object;
        }

        return ReadUnknown(n, 0, c.IsAnEntity, info);
    }

    // 未建模的类型：公共数据照常读入，之后的数据按位原样保存，并读出其中的句柄，引用的对象照常读取
    std::unique_ptr<CadObject> Reader::ReadUnknown(const std::string& dxfName, std::int16_t fixedType, bool isEntity,
                                                   ObjectInfo& info)
    {
        std::unique_ptr<CadObject> object;
        RawObjectData* raw = nullptr;
        if (isEntity)
        {
            std::unique_ptr<UnknownEntity> entity;
            if (dxfName == "ACAD_TABLE")
                entity = std::make_unique<TableEntity>();
            else
                entity = std::make_unique<UnknownEntity>();
            ReadCommonEntityData(*entity, info);
            entity->ProxyGraphics = std::move(m_lastProxyGraphics);
            raw = &entity->Raw;
            object = std::move(entity);
        }
        else
        {
            auto other = std::make_unique<UnknownObject>();
            ReadCommonNonEntityData(*other);
            raw = &other->Raw;
            object = std::move(other);
        }
        raw->DxfName = dxfName;
        raw->FixedType = fixedType;

        RawDwgData& d = raw->Dwg;
        d.Version = m_version;
        d.HasDataStore = m_lastDataStore;
        const std::uint64_t mainStart = m_objReader.PositionInBits();
        if (m_mainEnd > mainStart)
        {
            d.MainBits = m_mainEnd - mainStart;
            d.Main = m_objReader.CopyBits(mainStart, d.MainBits);
        }
        if (R2007Plus() && !m_textReader.IsEmpty() && m_textReader.StreamEndInBits() > m_textReader.PositionInBits())
        {
            d.TextBits = m_textReader.StreamEndInBits() - m_textReader.PositionInBits();
            d.Text = m_textReader.CopyBits(m_textReader.PositionInBits(), d.TextBits);
        }
        const std::uint64_t handleStart = m_handleReader.PositionInBits();
        if (m_objectEnd > handleStart)
        {
            d.HandleBits = m_objectEnd - handleStart;
            d.Handles = m_handleReader.CopyBits(handleStart, d.HandleBits);
        }
        if (m_objReader.Failed() || m_handleReader.Failed() || (d.MainBits > 0 && d.Main.empty())
            || (d.HandleBits > 0 && d.Handles.empty()))
        {
            m_objReader.Fail();
            return nullptr;
        }

        // 句柄流中其余的引用（末尾的填充位不足一个句柄）
        DwgBitReader refs = m_handleReader;
        while (refs.PositionInBits() + 8 <= m_objectEnd)
        {
            const Handle h = refs.ReadHandle(m_currentHandle);
            if (refs.Failed() || refs.PositionInBits() > m_objectEnd)
                break;
            if (h != kNullHandle && m_map.count(h) != 0)
            {
                d.References.push_back(h);
                if (m_visited.count(h) == 0)
                    m_queue.push_back(h);
            }
        }

        // 表格：数据开头与 INSERT 相同（插入点、比例、旋转、法向，句柄流中先是块记录）
        if (auto* table = dynamic_cast<TableEntity*>(object.get()))
        {
            DwgBitReader& r = m_objReader;
            table->InsertPoint = r.Read3BitDouble();
            if (R13_14Only())
            {
                const XYZ scale = r.Read3BitDouble();
                table->XScale = scale.X;
                table->YScale = scale.Y;
                table->ZScale = scale.Z;
            }
            else switch (r.Read2Bits())
            {
            case 0:
                table->XScale = r.ReadRawDouble();
                table->YScale = r.ReadBitDoubleWithDefault(table->XScale);
                table->ZScale = r.ReadBitDoubleWithDefault(table->XScale);
                break;
            case 1:
                table->YScale = r.ReadBitDoubleWithDefault(1.0);
                table->ZScale = r.ReadBitDoubleWithDefault(1.0);
                break;
            case 2:
                table->XScale = table->YScale = table->ZScale = r.ReadRawDouble();
                break;
            default:
                break;
            }
            table->Rotation = r.ReadBitDouble();
            table->Normal = r.Read3BitDouble();
            table->BlockHandle = HandleRef();
            if (r.Failed() || m_handleReader.Failed())
            {
                NotifyOnce("table:" + std::to_string(m_currentHandle), NotificationType::Warning,
                           "表格 " + DxfValue(HandleValue{ m_currentHandle }).AsString() + " 的块参照数据不完整");
                // 原始数据已保存，只放弃解析结果
                m_objReader = DwgBitReader();
                m_handleReader = DwgBitReader();
                table->BlockHandle = kNullHandle;
            }
        }

        ++m_preserved[dxfName];
        return object;
    }

    // 标注关联（ACadSharp 只读了一部分，以下字段由样例 DWG 与同一张图的 DXF 对照得出）：
    // H 标注、BL 关联标志（90）、B 跨空间（70）、RC 旋转标注类型（71），之后每个置位的点引用：
    // TV 类名、RC 捕捉类型（72）、BL 几何对象数 + H（331）、BL 子实体类型（73）、BL GS 标记（91）、
    // BL（样例中都是 0）、BD 参数（40）、3BD 点（10）、B 有末点引用（75）
    std::unique_ptr<CadObject> Reader::ReadDimensionAssociation()
    {
        auto assoc = std::make_unique<DimensionAssociation>();
        ReadCommonNonEntityData(*assoc);
        DwgBitReader& r = m_objReader;
        assoc->DimensionHandle = HandleRef();
        assoc->AssociativityFlags = static_cast<AssociativityFlags>(r.ReadBitLong());
        assoc->IsTransSpace = r.ReadBit();
        assoc->RotatedDimensionType = static_cast<RotatedDimensionType>(r.ReadByte());
        for (int i = 0; i < 4; ++i)
        {
            if ((static_cast<int>(assoc->AssociativityFlags) & (1 << i)) == 0)
                continue;
            DimensionAssociationOsnapPointRef& ref = PointRefAt(*assoc, i);
            if (m_s.ReadVariableText() != "AcDbOsnapPointRef")
                return nullptr;
            ref.ObjectOsnapType = static_cast<ObjectOsnapType>(r.ReadByte());
            if (r.ReadBitLong() != 1)
                return nullptr;
            ref.GeometryHandle = HandleRef();
            ref.SubentType = static_cast<SubentType>(r.ReadBitLong());
            ref.GsMarker = r.ReadBitLong();
            if (r.ReadBitLong() != 0)
                return nullptr;
            ref.GeometryParameter = r.ReadBitDouble();
            ref.OsnapPoint = r.Read3BitDouble();
            ref.HasLastPointRef = r.ReadBit();
            if (ref.HasLastPointRef)
                return nullptr;
        }
        if (m_s.Failed())
            return nullptr;
        return assoc;
    }

    // ── 符号表 ─────────────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadTableControl(std::int16_t type, ObjectInfo&)
    {
        auto table = std::make_unique<CadTable>(static_cast<CadObjectType>(type), TableEntryName(type));
        ReadCommonNonEntityData(*table);
        const std::int32_t count = m_objReader.ReadBitLong();
        if (!m_objReader.CheckCount(count))
            return table;

        // 块表另有 *MODEL_SPACE、*PAPER_SPACE，线型表另有 ByBlock、ByLayer，放在表项之前（与 DXF 中的顺序一致）
        std::vector<Handle> entries;
        for (int i = 0; i < count; ++i)
            entries.push_back(HandleRef());
        if (static_cast<CadObjectType>(type) == CadObjectType::LTYPE_CONTROL_OBJ)
            m_lineTypeIndex = entries;
        if (static_cast<CadObjectType>(type) == CadObjectType::BLOCK_CONTROL_OBJ
            || static_cast<CadObjectType>(type) == CadObjectType::LTYPE_CONTROL_OBJ)
        {
            const Handle a = HandleRef();
            const Handle b = HandleRef();
            for (Handle h : { a, b })
            {
                if (h != kNullHandle && std::find(entries.begin(), entries.end(), h) == entries.end())
                    table->Entries.push_back(h);
            }
        }
        for (Handle h : entries)
        {
            if (h != kNullHandle)
                table->Entries.push_back(h);
        }
        return table;
    }

    std::unique_ptr<CadObject> Reader::ReadBlockHeader(ObjectInfo& info)
    {
        auto record = std::make_unique<BlockRecord>();
        ReadCommonNonEntityData(*record);

        // 匿名块只存 *{类型字符}，完整名称以 BLOCK 实体为准（建库时设置）
        record->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*record);

        DwgBitReader& r = m_objReader;
        int blockFlags = 0;
        if (r.ReadBit()) blockFlags |= static_cast<int>(BlockTypeFlags::Anonymous);
        if (r.ReadBit()) blockFlags |= static_cast<int>(BlockTypeFlags::NonConstantAttributeDefinitions);
        if (r.ReadBit()) blockFlags |= static_cast<int>(BlockTypeFlags::XRef);
        if (r.ReadBit()) blockFlags |= static_cast<int>(BlockTypeFlags::XRefOverlay);
        const bool isXref = (blockFlags & (static_cast<int>(BlockTypeFlags::XRef) | static_cast<int>(BlockTypeFlags::XRefOverlay))) != 0;
        bool unloaded = false;
        if (R2000Plus())
            unloaded = r.ReadBit();

        std::int32_t owned = 0;
        if (R2004Plus() && !isXref)
            owned = r.ReadBitLong();

        // 块的基点、外部参照路径与说明存在块记录中，建库时写入 BLOCK 实体
        auto begin = std::make_unique<Block>();
        begin->Flags = static_cast<BlockTypeFlags>(blockFlags);
        begin->IsUnloaded = unloaded;
        begin->BasePoint = r.Read3BitDouble();
        begin->XRefPath = m_s.ReadVariableText();

        int insertCount = 0;
        if (R2000Plus())
        {
            for (std::uint8_t b = r.ReadByte(); b != 0 && !r.Failed(); b = r.ReadByte())
                ++insertCount;
            begin->Comments = m_s.ReadVariableText();
            const std::int32_t previewSize = r.ReadBitLong();
            if (r.CheckCount(previewSize, 8))
                record->Preview = r.ReadBytes(static_cast<std::size_t>(previewSize));
        }
        if (R2007Plus())
        {
            record->Units = static_cast<UnitsType>(r.ReadBitShort());
            record->IsExplodable = r.ReadBit();
            record->CanScale = r.ReadByte() > 0;
        }

        HandleRef();    // NULL
        record->BlockEntityHandle = HandleRef();
        if (R13_15Only() && !isXref)
        {
            info.FirstChild = HandleRef();
            info.LastChild = HandleRef();
        }
        if (R2004Plus() && r.CheckCount(owned))
        {
            for (int i = 0; i < owned; ++i)
                info.Owned.push_back(HandleRef());
        }
        record->BlockEndHandle = HandleRef();
        if (R2000Plus())
        {
            for (int i = 0; i < insertCount; ++i)
                HandleRef();
            record->LayoutHandle = HandleRef();
        }

        // 块记录中的块数据暂存，建库时合并到 BLOCK 实体
        m_blockData[record->ObjectHandle] = std::move(begin);
        return record;
    }

    std::unique_ptr<CadObject> Reader::ReadLayer()
    {
        auto layer = std::make_unique<Layer>();
        ReadCommonNonEntityData(*layer);
        layer->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*layer);

        DwgBitReader& r = m_objReader;
        int flags = static_cast<int>(layer->Flags);
        if (R13_14Only())
        {
            if (r.ReadBit()) flags |= 1;    // 冻结
            layer->IsOn = !r.ReadBit();
            if (r.ReadBit()) flags |= 2;    // 新视口中冻结
            if (r.ReadBit()) flags |= 4;    // 锁定
        }
        if (R2000Plus())
        {
            const std::int16_t values = r.ReadBitShort();
            if (values & 0x1) flags |= 1;
            layer->IsOn = (values & 0x2) == 0;
            if (values & 0x4) flags |= 2;
            if (values & 0x8) flags |= 4;
            layer->PlotFlag = (values & 0x10) != 0;
            layer->LineWeight = LineWeightFromIndex(static_cast<std::uint8_t>((values & 0x3E0) >> 5));
        }
        layer->Flags = static_cast<StandardFlags>(flags);

        const Color color = m_s.ReadCmColor();
        layer->Color = color.IsByBlock() || color.IsByLayer() ? Color(std::int16_t(7)) : color;

        HandleRef();    // 图层表
        if (R2000Plus())
            layer->PlotStyleName = HandleRef();
        if (R2007Plus())
            layer->MaterialHandle = HandleRef();
        layer->LineTypeHandle = HandleRef();
        if (R2013Plus())
            HandleRef();
        return layer;
    }

    std::unique_ptr<CadObject> Reader::ReadTextStyle()
    {
        auto style = std::make_unique<TextStyle>();
        ReadCommonNonEntityData(*style);
        style->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*style);

        DwgBitReader& r = m_objReader;
        int flags = static_cast<int>(style->Flags);
        if (r.ReadBit()) flags |= 1;    // 形文件
        if (r.ReadBit()) flags |= 4;    // 竖排
        style->Flags = static_cast<StandardFlags>(flags);
        style->Height = r.ReadBitDouble();
        style->Width = r.ReadBitDouble();
        style->ObliqueAngle = r.ReadBitDouble();
        style->MirrorFlag = static_cast<TextMirrorFlag>(r.ReadByte());
        style->LastHeight = r.ReadBitDouble();
        style->Filename = m_s.ReadVariableText();
        style->BigFontFilename = m_s.ReadVariableText();
        HandleRef();    // 样式表
        return style;
    }

    std::unique_ptr<CadObject> Reader::ReadLineType()
    {
        auto ltype = std::make_unique<LineType>();
        ReadCommonNonEntityData(*ltype);
        ltype->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*ltype);
        ltype->Description = m_s.ReadVariableText();

        DwgBitReader& r = m_objReader;
        r.ReadBitDouble();      // 总长：由分段计算
        ltype->Alignment = static_cast<char>(r.ReadByte());
        const int count = r.ReadByte();
        bool hasText = false;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            LineTypeSegment s;
            s.Length = r.ReadBitDouble();
            s.ShapeNumber = r.ReadBitShort();
            s.Offset.X = r.ReadRawDouble();
            s.Offset.Y = r.ReadRawDouble();
            s.Scale = r.ReadBitDouble();
            s.Rotation = r.ReadBitDouble();
            s.Flags = static_cast<LineTypeShapeFlags>(r.ReadBitShort());
            hasText |= HasFlag(s.Flags, LineTypeShapeFlags::Text);
            ltype->Segments.push_back(std::move(s));
        }

        // 文字分段的文字在 256（R2007 起 512，UTF-16）字节的区域中，分段的形号是文字在区域中的偏移
        std::vector<std::uint8_t> area;
        if (m_version <= CadVersion::AC1018)
            area = r.ReadBytes(256);
        else if (hasText)
            area = r.ReadBytes(512);
        if (hasText)
        {
            for (LineTypeSegment& s : ltype->Segments)
            {
                if (!HasFlag(s.Flags, LineTypeShapeFlags::Text))
                    continue;
                const std::size_t offset = static_cast<std::uint16_t>(s.ShapeNumber);
                s.ShapeNumber = 0;
                if (offset >= area.size())
                    continue;
                if (R2007Plus())
                {
                    std::string text;
                    for (std::size_t i = offset; i + 1 < area.size(); i += 2)
                    {
                        const char32_t c = area[i] | (char32_t(area[i + 1]) << 8);
                        if (c == 0)
                            break;
                        Codec::AppendUtf8(text, c);
                    }
                    s.Text = std::move(text);
                }
                else
                {
                    std::size_t end = offset;
                    while (end < area.size() && area[end] != 0)
                        ++end;
                    s.Text = Codec::ToUtf8(std::string(area.begin() + offset, area.begin() + end), m_file.CodePage());
                }
            }
        }

        HandleRef();    // 线型表
        for (LineTypeSegment& s : ltype->Segments)
            s.StyleHandle = HandleRef();
        return ltype;
    }

    std::unique_ptr<CadObject> Reader::ReadView()
    {
        auto view = std::make_unique<View>();
        ReadCommonNonEntityData(*view);
        view->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*view);

        DwgBitReader& r = m_objReader;
        view->Height = r.ReadBitDouble();
        view->Width = r.ReadBitDouble();
        view->Center = r.Read2RawDouble();
        view->Target = r.Read3BitDouble();
        view->Direction = r.Read3BitDouble();
        view->Angle = r.ReadBitDouble();
        view->LensLength = r.ReadBitDouble();
        view->FrontClipping = r.ReadBitDouble();
        view->BackClipping = r.ReadBitDouble();
        int mode = 0;
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::PerspectiveView);
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::FrontClipping);
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::BackClipping);
        // DWG 的第 4 位是"前裁剪面在视点处"，DXF 的 16 是"不在视点处"
        if (!r.ReadBit()) mode |= static_cast<int>(ViewModeType::FrontClippingZ);
        view->ViewMode = static_cast<ViewModeType>(mode);
        if (R2000Plus())
            view->RenderMode = static_cast<RenderMode>(r.ReadByte());
        if (R2007Plus())
        {
            r.ReadBit();
            r.ReadByte();
            r.ReadBitDouble();
            r.ReadBitDouble();
            m_s.ReadCmColor();
        }
        if (r.ReadBit())
            view->Flags = static_cast<StandardFlags>(static_cast<int>(view->Flags) | 1);   // 图纸空间视图
        if (R2000Plus())
        {
            view->IsUcsAssociated = r.ReadBit();
            if (view->IsUcsAssociated)
            {
                view->UcsOrigin = r.Read3BitDouble();
                view->UcsXAxis = r.Read3BitDouble();
                view->UcsYAxis = r.Read3BitDouble();
                view->UcsElevation = r.ReadBitDouble();
                view->UcsOrthographicType = static_cast<OrthographicType>(r.ReadBitShort());
            }
        }
        HandleRef();    // 视图表
        if (R2007Plus())
        {
            view->IsPlottable = r.ReadBit();
            HandleRef();    // 背景
            view->VisualStyleHandle = HandleRef();
            HandleRef();    // 太阳
        }
        if (R2000Plus() && view->IsUcsAssociated)
        {
            HandleRef();    // 基准 UCS
            HandleRef();    // 命名 UCS
        }
        if (R2007Plus())
            HandleRef();    // 实时剖切对象
        return view;
    }

    std::unique_ptr<CadObject> Reader::ReadUcs()
    {
        auto ucs = std::make_unique<UCS>();
        ReadCommonNonEntityData(*ucs);
        ucs->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*ucs);

        DwgBitReader& r = m_objReader;
        ucs->Origin = r.Read3BitDouble();
        ucs->XAxis = r.Read3BitDouble();
        ucs->YAxis = r.Read3BitDouble();
        if (R2000Plus())
        {
            ucs->Elevation = r.ReadBitDouble();
            ucs->OrthographicViewType = static_cast<OrthographicType>(r.ReadBitShort());
            ucs->OrthographicType = static_cast<OrthographicType>(r.ReadBitShort());
        }
        HandleRef();    // UCS 表
        if (R2000Plus())
        {
            HandleRef();
            HandleRef();
        }
        return ucs;
    }

    std::unique_ptr<CadObject> Reader::ReadVPort()
    {
        auto vport = std::make_unique<VPort>();
        ReadCommonNonEntityData(*vport);
        vport->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*vport);

        DwgBitReader& r = m_objReader;
        vport->ViewHeight = r.ReadBitDouble();
        const double width = r.ReadBitDouble();
        vport->AspectRatio = vport->ViewHeight != 0.0 ? width / vport->ViewHeight : 0.0;
        vport->Center = r.Read2RawDouble();
        vport->Target = r.Read3BitDouble();
        vport->Direction = r.Read3BitDouble();
        vport->TwistAngle = r.ReadBitDouble();
        vport->LensLength = r.ReadBitDouble();
        vport->FrontClippingPlane = r.ReadBitDouble();
        vport->BackClippingPlane = r.ReadBitDouble();
        int mode = 0;
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::PerspectiveView);
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::FrontClipping);
        if (r.ReadBit()) mode |= static_cast<int>(ViewModeType::BackClipping);
        // DWG 的第 4 位是"前裁剪面在视点处"，DXF 的 16 是"不在视点处"
        if (!r.ReadBit()) mode |= static_cast<int>(ViewModeType::FrontClippingZ);
        if (R2000Plus())
            vport->RenderMode = static_cast<RenderMode>(r.ReadByte());
        if (R2007Plus())
        {
            vport->UseDefaultLighting = r.ReadBit();
            vport->DefaultLighting = static_cast<DefaultLightingType>(r.ReadByte());
            vport->Brightness = r.ReadBitDouble();
            vport->Contrast = r.ReadBitDouble();
            vport->AmbientColor = m_s.ReadCmColor();
        }
        vport->BottomLeft = r.Read2RawDouble();
        vport->TopRight = r.Read2RawDouble();
        if (r.ReadBit())
            mode |= static_cast<int>(ViewModeType::Follow);
        vport->ViewMode = static_cast<ViewModeType>(mode);
        vport->CircleZoomPercent = r.ReadBitShort();
        r.ReadBit();    // 快速缩放
        if (r.ReadBit())
            vport->UcsIconDisplay = UscIconType::OnLower;
        if (r.ReadBit())
            vport->UcsIconDisplay = UscIconType::OnOrigin;
        vport->ShowGrid = r.ReadBit();
        vport->GridSpacing = r.Read2RawDouble();
        vport->SnapOn = r.ReadBit();
        vport->IsometricSnap = r.ReadBit();
        vport->SnapIsoPair = r.ReadBitShort();
        vport->SnapRotation = r.ReadBitDouble();
        vport->SnapBasePoint = r.Read2RawDouble();
        vport->SnapSpacing = r.Read2RawDouble();
        if (R2000Plus())
        {
            r.ReadBit();
            r.ReadBit();    // 每个视口一个 UCS
            vport->Origin = r.Read3BitDouble();
            vport->XAxis = r.Read3BitDouble();
            vport->YAxis = r.Read3BitDouble();
            vport->Elevation = r.ReadBitDouble();
            vport->OrthographicType = static_cast<OrthographicType>(r.ReadBitShort());
        }
        if (R2007Plus())
        {
            vport->GridFlags = static_cast<GridFlags>(r.ReadBitShort());
            vport->MinorGridLinesPerMajorGridLine = r.ReadBitShort();
        }
        HandleRef();    // 视口表
        if (R2007Plus())
        {
            HandleRef();    // 背景
            vport->VisualStyleHandle = HandleRef();
            HandleRef();    // 太阳
        }
        if (R2000Plus())
        {
            vport->NamedUcsHandle = HandleRef();
            vport->BaseUcsHandle = HandleRef();
        }
        return vport;
    }

    std::unique_ptr<CadObject> Reader::ReadAppId()
    {
        auto appId = std::make_unique<AppId>();
        ReadCommonNonEntityData(*appId);
        appId->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*appId);
        m_objReader.ReadByte();     // 未公开的 71
        HandleRef();                // 外部参照块
        return appId;
    }

    std::unique_ptr<CadObject> Reader::ReadDimStyle()
    {
        auto d = std::make_unique<DimensionStyle>();
        ReadCommonNonEntityData(*d);
        d->Name = m_s.ReadVariableText();
        ReadXrefDependantBit(*d);

        DwgBitReader& r = m_objReader;
        if (R13_14Only())
        {
            // 顺序按 ODA 规范（DIMTIH、DIMTOH、DIMSE1、DIMSE2），ACadSharp 中这四项错位
            d->GenerateTolerances = r.ReadBit();
            d->LimitsGeneration = r.ReadBit();
            d->TextInsideHorizontal = r.ReadBit();
            d->TextOutsideHorizontal = r.ReadBit();
            d->SuppressFirstExtensionLine = r.ReadBit();
            d->SuppressSecondExtensionLine = r.ReadBit();
            d->AlternateUnitDimensioning = r.ReadBit();
            d->TextOutsideExtensions = r.ReadBit();
            d->SeparateArrowBlocks = r.ReadBit();
            d->TextInsideExtensions = r.ReadBit();
            d->SuppressOutsideExtensions = r.ReadBit();
            d->AlternateUnitDecimalPlaces = r.ReadByte();
            d->ZeroHandling = static_cast<ZeroHandling>(r.ReadByte());
            d->SuppressFirstDimensionLine = r.ReadBit();
            d->SuppressSecondDimensionLine = r.ReadBit();
            d->ToleranceAlignment = static_cast<ToleranceAlignment>(r.ReadByte());
            d->TextHorizontalAlignment = static_cast<DimensionTextHorizontalAlignment>(r.ReadByte());
            d->DimensionFit = r.ReadByte();
            d->CursorUpdate = r.ReadBit();
            d->ToleranceZeroHandling = static_cast<ZeroHandling>(r.ReadByte());
            d->AlternateUnitZeroHandling = static_cast<ZeroHandling>(r.ReadByte());
            d->AlternateUnitToleranceZeroHandling = static_cast<ZeroHandling>(r.ReadByte());
            d->TextVerticalAlignment = static_cast<DimensionTextVerticalAlignment>(r.ReadByte());
            d->DimensionUnit = r.ReadBitShort();
            d->AngularUnit = static_cast<AngularUnitFormat>(r.ReadBitShort());
            d->DecimalPlaces = r.ReadBitShort();
            d->ToleranceDecimalPlaces = r.ReadBitShort();
            d->AlternateUnitFormat = static_cast<LinearUnitFormat>(r.ReadBitShort());
            d->AlternateUnitToleranceDecimalPlaces = r.ReadBitShort();
            d->ScaleFactor = r.ReadBitDouble();
            d->ArrowSize = r.ReadBitDouble();
            d->ExtensionLineOffset = r.ReadBitDouble();
            d->DimensionLineIncrement = r.ReadBitDouble();
            d->ExtensionLineExtension = r.ReadBitDouble();
            d->Rounding = r.ReadBitDouble();
            d->DimensionLineExtension = r.ReadBitDouble();
            d->PlusTolerance = r.ReadBitDouble();
            d->MinusTolerance = r.ReadBitDouble();
            d->TextHeight = r.ReadBitDouble();
            d->CenterMarkSize = r.ReadBitDouble();
            d->TickSize = r.ReadBitDouble();
            d->AlternateUnitScaleFactor = r.ReadBitDouble();
            d->LinearScaleFactor = r.ReadBitDouble();
            d->TextVerticalPosition = r.ReadBitDouble();
            d->ToleranceScaleFactor = r.ReadBitDouble();
            d->DimensionLineGap = r.ReadBitDouble();
            d->PostFix = m_s.ReadVariableText();
            d->AlternateDimensioningSuffix = m_s.ReadVariableText();
            m_s.ReadVariableText();     // DIMBLK 名称（R14 之后改为句柄）
            m_s.ReadVariableText();
            m_s.ReadVariableText();
            d->DimensionLineColor = Color(r.ReadBitShort());
            d->ExtensionLineColor = Color(r.ReadBitShort());
            d->TextColor = Color(r.ReadBitShort());
        }
        if (R2000Plus())
        {
            d->PostFix = m_s.ReadVariableText();
            d->AlternateDimensioningSuffix = m_s.ReadVariableText();
            d->ScaleFactor = r.ReadBitDouble();
            d->ArrowSize = r.ReadBitDouble();
            d->ExtensionLineOffset = r.ReadBitDouble();
            d->DimensionLineIncrement = r.ReadBitDouble();
            d->ExtensionLineExtension = r.ReadBitDouble();
            d->Rounding = r.ReadBitDouble();
            d->DimensionLineExtension = r.ReadBitDouble();
            d->PlusTolerance = r.ReadBitDouble();
            d->MinusTolerance = r.ReadBitDouble();
        }
        if (R2007Plus())
        {
            d->FixedExtensionLineLength = r.ReadBitDouble();
            d->JoggedRadiusDimensionTransverseSegmentAngle = r.ReadBitDouble();
            d->TextBackgroundFillMode = static_cast<DimensionTextBackgroundFillMode>(r.ReadBitShort());
            d->TextBackgroundColor = m_s.ReadCmColor();
        }
        if (R2000Plus())
        {
            d->GenerateTolerances = r.ReadBit();
            d->LimitsGeneration = r.ReadBit();
            d->TextInsideHorizontal = r.ReadBit();
            d->TextOutsideHorizontal = r.ReadBit();
            d->SuppressFirstExtensionLine = r.ReadBit();
            d->SuppressSecondExtensionLine = r.ReadBit();
            d->TextVerticalAlignment = static_cast<DimensionTextVerticalAlignment>(r.ReadBitShort());
            d->ZeroHandling = static_cast<ZeroHandling>(r.ReadBitShort());
            d->AngularZeroHandling = static_cast<AngularZeroHandling>(r.ReadBitShort());
        }
        if (R2007Plus())
            d->ArcLengthSymbolPosition = static_cast<ArcLengthSymbolPosition>(r.ReadBitShort());
        if (R2000Plus())
        {
            d->TextHeight = r.ReadBitDouble();
            d->CenterMarkSize = r.ReadBitDouble();
            d->TickSize = r.ReadBitDouble();
            d->AlternateUnitScaleFactor = r.ReadBitDouble();
            d->LinearScaleFactor = r.ReadBitDouble();
            d->TextVerticalPosition = r.ReadBitDouble();
            d->ToleranceScaleFactor = r.ReadBitDouble();
            d->DimensionLineGap = r.ReadBitDouble();
            d->AlternateUnitRounding = r.ReadBitDouble();
            d->AlternateUnitDimensioning = r.ReadBit();
            d->AlternateUnitDecimalPlaces = r.ReadBitShort();
            d->TextOutsideExtensions = r.ReadBit();
            d->SeparateArrowBlocks = r.ReadBit();
            d->TextInsideExtensions = r.ReadBit();
            d->SuppressOutsideExtensions = r.ReadBit();
            d->DimensionLineColor = m_s.ReadCmColor();
            d->ExtensionLineColor = m_s.ReadCmColor();
            d->TextColor = m_s.ReadCmColor();
            d->AngularDecimalPlaces = r.ReadBitShort();
            d->DecimalPlaces = r.ReadBitShort();
            d->ToleranceDecimalPlaces = r.ReadBitShort();
            d->AlternateUnitFormat = static_cast<LinearUnitFormat>(r.ReadBitShort());
            d->AlternateUnitToleranceDecimalPlaces = r.ReadBitShort();
            d->AngularUnit = static_cast<AngularUnitFormat>(r.ReadBitShort());
            d->FractionFormat = static_cast<FractionFormat>(r.ReadBitShort());
            d->LinearUnitFormat = static_cast<LinearUnitFormat>(r.ReadBitShort());
            d->DecimalSeparator = static_cast<char>(r.ReadBitShort());
            d->TextMovement = static_cast<TextMovement>(r.ReadBitShort());
            d->TextHorizontalAlignment = static_cast<DimensionTextHorizontalAlignment>(r.ReadBitShort());
            d->SuppressFirstDimensionLine = r.ReadBit();
            d->SuppressSecondDimensionLine = r.ReadBit();
            d->ToleranceAlignment = static_cast<ToleranceAlignment>(r.ReadBitShort());
            d->ToleranceZeroHandling = static_cast<ZeroHandling>(r.ReadBitShort());
            d->AlternateUnitZeroHandling = static_cast<ZeroHandling>(r.ReadBitShort());
            d->AlternateUnitToleranceZeroHandling = static_cast<ZeroHandling>(r.ReadBitShort());
            d->CursorUpdate = r.ReadBit();
            d->DimensionFit = r.ReadBitShort();
        }
        if (R2007Plus())
            d->IsExtensionLineLengthFixed = r.ReadBit();
        if (R2010Plus())
        {
            d->TextDirection = r.ReadBit() ? TextDirection::RightToLeft : TextDirection::LeftToRight;
            r.ReadBitDouble();          // DIMALTMZF：未建模
            m_s.ReadVariableText();     // DIMALTMZS
            r.ReadBitDouble();          // DIMMZF
            m_s.ReadVariableText();     // DIMMZS
        }
        if (R2000Plus())
        {
            d->DimensionLineWeight = static_cast<LineWeightType>(r.ReadBitShort());
            d->ExtensionLineWeight = static_cast<LineWeightType>(r.ReadBitShort());
        }
        r.ReadBit();    // 未知

        HandleRef();    // 标注样式表
        d->StyleHandle = HandleRef();
        if (R2000Plus())
        {
            d->LeaderArrowHandle = HandleRef();
            d->ArrowBlockHandle = HandleRef();
            d->DimArrow1Handle = HandleRef();
            d->DimArrow2Handle = HandleRef();
        }
        if (R2007Plus())
        {
            d->LineTypeHandle = HandleRef();
            d->LineTypeExt1Handle = HandleRef();
            d->LineTypeExt2Handle = HandleRef();
        }
        return d;
    }

    // ── 字典与对象 ─────────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadDictionary(bool withDefault)
    {
        std::unique_ptr<CadDictionary> dict = withDefault ? std::make_unique<CadDictionaryWithDefault>()
                                                          : std::make_unique<CadDictionary>();
        ReadCommonNonEntityData(*dict);

        DwgBitReader& r = m_objReader;
        const std::int32_t count = r.ReadBitLong();
        if (m_version == CadVersion::AC1014)
            r.ReadByte();
        if (R2000Plus())
        {
            dict->ClonningFlags = static_cast<DictionaryCloningFlags>(r.ReadBitShort());
            dict->HardOwnerFlag = r.ReadByte() > 0;
        }
        if (!r.CheckCount(count))
            return dict;
        for (int i = 0; i < count; ++i)
        {
            std::string name = m_s.ReadVariableText();
            const Handle h = HandleRef();
            if (h == kNullHandle || name.empty())
                continue;
            dict->EntryNames.push_back(std::move(name));
            dict->EntryHandles.push_back(h);
        }
        if (withDefault)
            static_cast<CadDictionaryWithDefault&>(*dict).DefaultEntryHandle = HandleRef();
        return dict;
    }

    std::unique_ptr<CadObject> Reader::ReadDictionaryVariable()
    {
        auto var = std::make_unique<DictionaryVariable>();
        ReadCommonNonEntityData(*var);
        var->ObjectSchemaNumber = m_objReader.ReadByte();
        var->Value = m_s.ReadVariableText();
        return var;
    }

    std::unique_ptr<CadObject> Reader::ReadGroup()
    {
        auto group = std::make_unique<Group>();
        ReadCommonNonEntityData(*group);
        group->Description = m_s.ReadVariableText();
        m_objReader.ReadBitShort();     // 匿名：由名称决定
        group->Selectable = m_objReader.ReadBitShort() > 0;
        const std::int32_t count = m_objReader.ReadBitLong();
        if (m_objReader.CheckCount(count))
        {
            for (int i = 0; i < count; ++i)
                group->Entities.push_back(HandleRef());
        }
        return group;
    }

    std::unique_ptr<CadObject> Reader::ReadMLineStyle(ObjectInfo& info)
    {
        auto style = std::make_unique<MLineStyle>();
        ReadCommonNonEntityData(*style);
        style->Name = m_s.ReadVariableText();
        style->Description = m_s.ReadVariableText();

        // DWG 与 DXF 的标志位不同：1↔2、32↔64、512↔1024 对调
        DwgBitReader& r = m_objReader;
        const std::int16_t flags = r.ReadBitShort();
        int f = 0;
        if (flags & 1) f |= static_cast<int>(MLineStyleFlags::DisplayJoints);
        if (flags & 2) f |= static_cast<int>(MLineStyleFlags::FillOn);
        if (flags & 16) f |= static_cast<int>(MLineStyleFlags::StartSquareCap);
        if (flags & 32) f |= static_cast<int>(MLineStyleFlags::StartRoundCap);
        if (flags & 64) f |= static_cast<int>(MLineStyleFlags::StartInnerArcsCap);
        if (flags & 256) f |= static_cast<int>(MLineStyleFlags::EndSquareCap);
        if (flags & 512) f |= static_cast<int>(MLineStyleFlags::EndRoundCap);
        if (flags & 1024) f |= static_cast<int>(MLineStyleFlags::EndInnerArcsCap);
        style->Flags = static_cast<MLineStyleFlags>(f);
        style->FillColor = m_s.ReadCmColor();
        style->StartAngle = r.ReadBitDouble();
        style->EndAngle = r.ReadBitDouble();
        const int count = r.ReadByte();
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            MLineStyleElement e;
            e.Offset = r.ReadBitDouble();
            e.Color = m_s.ReadCmColor();
            if (R2018Plus())
            {
                e.LineTypeHandle = HandleRef();
                info.ElementLineTypeIndex.push_back(-1);
            }
            else
            {
                info.ElementLineTypeIndex.push_back(r.ReadBitShort());
            }
            style->Elements.push_back(e);
        }
        return style;
    }

    std::unique_ptr<CadObject> Reader::ReadXRecord()
    {
        auto record = std::make_unique<XRecord>();
        ReadCommonNonEntityData(*record);

        DwgBitReader& r = m_objReader;
        const std::uint64_t end = static_cast<std::uint32_t>(r.ReadBitLong()) + r.Position();
        while (r.Position() < end && !r.Failed())
        {
            XRecordEntry e;
            e.Code = r.ReadRawShort();
            const int code = e.Code;
            switch (GroupCodeTypeOf(code))
            {
            case GroupCodeType::String:
                e.Value = DxfValue(r.ReadTextUnicode());
                break;
            case GroupCodeType::Point3D:
                e.Value = DxfValue(r.Read3RawDouble());
                break;
            case GroupCodeType::Double:
                e.Value = DxfValue(r.ReadRawDouble());
                break;
            case GroupCodeType::Byte:
                e.Value = DxfValue(static_cast<std::int64_t>(r.ReadByte()));
                break;
            case GroupCodeType::Int16:
                e.Value = DxfValue(static_cast<std::int64_t>(r.ReadRawShort()));
                break;
            case GroupCodeType::Int32:
                e.Value = DxfValue(static_cast<std::int64_t>(r.ReadRawLong()));
                break;
            case GroupCodeType::Int64:
                e.Value = DxfValue(static_cast<std::int64_t>(r.ReadRawLongLong()));
                break;
            case GroupCodeType::Bool:
                e.Value = DxfValue(r.ReadByte() > 0);
                break;
            case GroupCodeType::Chunk:
                e.Value = DxfValue(r.ReadBytes(r.ReadByte()));
                break;
            case GroupCodeType::Handle:
                if (code == 5 || code == 105)
                {
                    // 句柄组码以十六进制字符串保存
                    const std::string hex = r.ReadTextUnicode();
                    Handle h = 0;
                    std::from_chars(hex.data(), hex.data() + hex.size(), h, 16);
                    e.Value = DxfValue(HandleValue{ h });
                }
                else
                {
                    e.Value = DxfValue(HandleValue{ r.ReadRawLongLong() });
                }
                break;
            default:
                NotifyOnce("xrecord:" + std::to_string(code), NotificationType::Warning,
                           "XRECORD 中未知的组码 " + std::to_string(code));
                r.SetPosition(end);
                continue;
            }
            record->Entries.push_back(std::move(e));
        }
        if (R2000Plus())
            record->CloningFlags = static_cast<DictionaryCloningFlags>(r.ReadBitShort());

        // 余下的句柄（拥有的对象）照常加入读取队列
        const std::uint64_t handlesEnd = m_objectInitialPos + std::uint64_t(m_objectSize) * 8 - 7;
        while (m_handleReader.PositionInBits() < handlesEnd && !m_handleReader.Failed())
            HandleRef();
        // 越界读到的失败不算对象损坏
        if (m_handleReader.Failed())
            m_handleReader = DwgBitReader();
        return record;
    }

    void Reader::ReadPlotSettings(PlotSettings& plot)
    {
        DwgBitReader& r = m_objReader;
        plot.PageName = m_s.ReadVariableText();
        plot.SystemPrinterName = m_s.ReadVariableText();
        plot.Flags = static_cast<PlotFlags>(r.ReadBitShort());
        plot.UnprintableMargin.Left = r.ReadBitDouble();
        plot.UnprintableMargin.Bottom = r.ReadBitDouble();
        plot.UnprintableMargin.Right = r.ReadBitDouble();
        plot.UnprintableMargin.Top = r.ReadBitDouble();
        plot.PaperWidth = r.ReadBitDouble();
        plot.PaperHeight = r.ReadBitDouble();
        plot.PaperSize = m_s.ReadVariableText();
        plot.PlotOriginX = r.ReadBitDouble();
        plot.PlotOriginY = r.ReadBitDouble();
        plot.PaperUnits = static_cast<PlotPaperUnits>(r.ReadBitShort());
        plot.PaperRotation = static_cast<PlotRotation>(r.ReadBitShort());
        plot.PlotType = static_cast<PlotType>(r.ReadBitShort());
        plot.WindowLowerLeftX = r.ReadBitDouble();
        plot.WindowLowerLeftY = r.ReadBitDouble();
        plot.WindowUpperLeftX = r.ReadBitDouble();
        plot.WindowUpperLeftY = r.ReadBitDouble();
        if (R13_15Only())
            plot.PlotViewName = m_s.ReadVariableText();
        plot.NumeratorScale = r.ReadBitDouble();
        plot.DenominatorScale = r.ReadBitDouble();
        plot.StyleSheet = m_s.ReadVariableText();
        plot.ScaledFit = static_cast<ScaledType>(r.ReadBitShort());
        plot.StandardScale = r.ReadBitDouble();
        const XY origin = r.Read2BitDouble();
        plot.PaperImageOriginX = origin.X;
        plot.PaperImageOriginY = origin.Y;
        if (R2004Plus())
        {
            plot.ShadePlotMode = static_cast<ShadePlotMode>(r.ReadBitShort());
            plot.ShadePlotResolutionMode = static_cast<ShadePlotResolutionMode>(r.ReadBitShort());
            plot.ShadePlotDPI = r.ReadBitShort();
            HandleRef();    // 打印视图
        }
        if (R2007Plus())
            plot.ShadePlotIDHandle = HandleRef();
    }

    std::unique_ptr<CadObject> Reader::ReadPlotSettingsObject()
    {
        auto plot = std::make_unique<PlotSettings>();
        ReadCommonNonEntityData(*plot);
        ReadPlotSettings(*plot);
        return plot;
    }

    std::unique_ptr<CadObject> Reader::ReadLayout()
    {
        auto layout = std::make_unique<Layout>();
        ReadCommonNonEntityData(*layout);
        ReadPlotSettings(*layout);

        DwgBitReader& r = m_objReader;
        layout->Name = m_s.ReadVariableText();
        layout->TabOrder = r.ReadBitLong();
        layout->LayoutFlags = static_cast<LayoutFlags>(r.ReadBitShort());
        layout->Origin = r.Read3BitDouble();
        layout->MinLimits = r.Read2RawDouble();
        layout->MaxLimits = r.Read2RawDouble();
        layout->InsertionBasePoint = r.Read3BitDouble();
        layout->XAxis = r.Read3BitDouble();
        layout->YAxis = r.Read3BitDouble();
        layout->Elevation = r.ReadBitDouble();
        layout->UcsOrthographicType = static_cast<OrthographicType>(r.ReadBitShort());
        layout->MinExtents = r.Read3BitDouble();
        layout->MaxExtents = r.Read3BitDouble();
        std::int32_t viewports = 0;
        if (R2004Plus())
            viewports = r.ReadBitLong();

        layout->AssociatedBlockHandle = HandleRef();
        layout->LastActiveViewportHandle = HandleRef();
        layout->BaseUCSHandle = HandleRef();
        layout->UCSHandle = HandleRef();
        if (R2004Plus() && r.CheckCount(viewports))
        {
            for (int i = 0; i < viewports; ++i)
                HandleRef();
        }
        return layout;
    }

    std::unique_ptr<CadObject> Reader::ReadImageDefinition()
    {
        auto def = std::make_unique<ImageDefinition>();
        ReadCommonNonEntityData(*def);
        DwgBitReader& r = m_objReader;
        def->ClassVersion = r.ReadBitLong();
        def->Size = r.Read2RawDouble();
        def->FileName = m_s.ReadVariableText();
        def->IsLoaded = r.ReadBit();
        def->Units = static_cast<ResolutionUnit>(r.ReadByte());
        def->DefaultSize = r.Read2RawDouble();
        return def;
    }

    std::unique_ptr<CadObject> Reader::ReadImageDefinitionReactor()
    {
        auto reactor = std::make_unique<ImageDefinitionReactor>();
        ReadCommonNonEntityData(*reactor);
        reactor->ClassVersion = m_objReader.ReadBitLong();
        reactor->ImageHandle = reactor->OwnerHandle;    // DXF 中的 330 就是所有者图像
        return reactor;
    }

    std::unique_ptr<CadObject> Reader::ReadRasterVariables()
    {
        auto vars = std::make_unique<RasterVariables>();
        ReadCommonNonEntityData(*vars);
        DwgBitReader& r = m_objReader;
        vars->ClassVersion = r.ReadBitLong();
        vars->IsDisplayFrameShown = r.ReadBitShort() != 0;
        vars->DisplayQuality = static_cast<ImageDisplayQuality>(r.ReadBitShort());
        vars->Units = static_cast<ImageUnits>(r.ReadBitShort());
        return vars;
    }

    std::unique_ptr<CadObject> Reader::ReadScale()
    {
        auto scale = std::make_unique<Scale>();
        ReadCommonNonEntityData(*scale);
        DwgBitReader& r = m_objReader;
        r.ReadBitShort();
        scale->Name = m_s.ReadVariableText();
        scale->PaperUnits = r.ReadBitDouble();
        scale->DrawingUnits = r.ReadBitDouble();
        scale->IsUnitScale = r.ReadBit();
        return scale;
    }

    std::unique_ptr<CadObject> Reader::ReadSortEntitiesTable()
    {
        auto table = std::make_unique<SortEntitiesTable>();
        ReadCommonNonEntityData(*table);
        table->BlockOwnerHandle = HandleRef();
        const std::int32_t count = m_objReader.ReadBitLong();
        if (!m_objReader.CheckCount(count))
            return table;
        for (int i = 0; i < count; ++i)
        {
            // 排序句柄在数据流中，实体句柄在句柄流中
            SortEntsEntry e;
            e.SortHandle = m_objReader.ReadHandle();
            e.EntityHandle = HandleRef();
            table->Entries.push_back(e);
        }
        return table;
    }

    std::unique_ptr<CadObject> Reader::ReadPlaceHolder()
    {
        auto placeHolder = std::make_unique<AcdbPlaceHolder>();
        ReadCommonNonEntityData(*placeHolder);
        return placeHolder;
    }
}
