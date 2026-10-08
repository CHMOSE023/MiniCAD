// DXF 写入：准备、HEADER / CLASSES / TABLES / BLOCKS / OBJECTS 段与公共数据
#include "Dxf/Write/DxfWriterImpl.h"
#include "Database/DxfMeta.h"
#include <algorithm>
#include <array>
#include <map>

namespace MiniDWG
{
    std::vector<std::uint8_t> WriteDxf(const CadDatabase& db, const DxfWriteOptions& options)
    {
        DxfWrite::Writer writer(db, options);
        return writer.Run();
    }
}

namespace MiniDWG::DxfWrite
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

        bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix)
        {
            return s.size() >= prefix.size() && EqualsIgnoreCase(s.substr(0, prefix.size()), prefix);
        }

        // 默认写出的头变量（ACadSharp DxfWriterConfiguration.Variables）。
        // $ACADVER、$DWGCODEPAGE、$HANDSEED 总是写出
        constexpr std::string_view kDefaultHeaderVariables[] = {
            "$ANGBASE", "$ANGDIR", "$ATTMODE", "$AUNITS", "$AUPREC", "$CECOLOR", "$CELTSCALE", "$CELTYPE",
            "$CELWEIGHT", "$CLAYER", "$CMLJUST", "$CMLSCALE", "$CMLSTYLE", "$DIMSTYLE", "$TEXTSIZE",
            "$TEXTSTYLE", "$LUNITS", "$LUPREC", "$MIRRTEXT", "$EXTNAMES", "$INSBASE", "$INSUNITS",
            "$MEASUREMENT", "$LTSCALE", "$LWDISPLAY", "$PDMODE", "$PDSIZE", "$PLINEGEN", "$PSLTSCALE",
            "$SPLINESEGS", "$SURFU", "$SURFV", "$TDCREATE", "$TDUCREATE", "$TDUPDATE", "$TDUUPDATE", "$TDINDWG",
        };

        // ACadSharp 标为 Ignored 的头变量：不写
        constexpr std::string_view kIgnoredHeaderVariables[] = {
            "$DIMTFILLCLR", "$LASTSAVEDBY", "$ACADMAINTVER", "$REQUIREDVERSIONS",
        };

        template <std::size_t N>
        bool Contains(const std::string_view (&list)[N], std::string_view name)
        {
            return std::find(std::begin(list), std::end(list), name) != std::end(list);
        }

        // 按类名注册的对象（DWG 类型码 ≥ 500）需要 CLASSES 段中的定义，取值与 AutoCAD 写出的一致
        struct ClassTemplate
        {
            std::string_view DxfName;
            std::string_view CppClassName;
            std::string_view ApplicationName;
            std::int32_t     ProxyFlags;
            bool             IsAnEntity;
        };

        constexpr std::string_view kObjectDbx = "ObjectDBX Classes";
        constexpr std::string_view kIsm = "ISM";
        constexpr std::string_view kWipeoutApp =
            "WipeOut|Product Desc: Object Enabler for WipeOut entity | Company: Autodesk, Inc. | WEB Address: www.autodesk.com";
        constexpr std::string_view kDimAssocApp =
            "\"AcDbDimAssoc|Product Desc:     AcDim ARX App For Dimension|Company:          Autodesk, Inc.|WEB Address:      www.autodesk.com\"";

        constexpr ClassTemplate kClassTemplates[] = {
            { "ACDBDICTIONARYWDFLT", "AcDbDictionaryWithDefault", kObjectDbx, 0, false },
            { "ACDBPLACEHOLDER", "AcDbPlaceHolder", kObjectDbx, 0, false },
            { "DICTIONARYVAR", "AcDbDictionaryVar", kObjectDbx, 0, false },
            { "LAYOUT", "AcDbLayout", kObjectDbx, 0, false },
            { "PLOTSETTINGS", "AcDbPlotSettings", kObjectDbx, 0, false },
            { "SCALE", "AcDbScale", kObjectDbx, 1153, false },
            { "SORTENTSTABLE", "AcDbSortentsTable", kObjectDbx, 0, false },
            { "RASTERVARIABLES", "AcDbRasterVariables", kIsm, 0, false },
            { "IMAGEDEF", "AcDbRasterImageDef", kIsm, 0, false },
            { "IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", kIsm, 1, false },
            { "IMAGE", "AcDbRasterImage", kIsm, 2175, true },
            { "WIPEOUT", "AcDbWipeout", kWipeoutApp, 2175, true },
            { "ARC_DIMENSION", "AcDbArcDimension", kObjectDbx, 1025, true },
            { "DIMASSOC", "AcDbDimAssoc", kDimAssocApp, 0, false },
            { "MLEADERSTYLE", "AcDbMLeaderStyle", "ACDB_MLEADERSTYLE_CLASS", 4095, false },
            { "MULTILEADER", "AcDbMLeader", "ACDB_MLEADER_CLASS", 1025, true },
        };

        // MTEXT 等长文本按 250 字节分块（不拆开 UTF-8 多字节字符）
        constexpr std::size_t kTextChunk = 250;
    }

    Writer::Writer(const CadDatabase& db, const DxfWriteOptions& options) : m_db(db), m_options(options)
    {
    }

    void Writer::Notify(NotificationType type, std::string message)
    {
        if (m_options.Notify)
            m_options.Notify(type, message);
    }

    void Writer::NotifyOnce(const std::string& key, NotificationType type, std::string message)
    {
        if (m_notified.insert(key).second)
            Notify(type, std::move(message));
    }

    std::vector<std::uint8_t> Writer::Run()
    {
        Prepare();

        WriteHeader();
        WriteClasses();
        WriteTables();
        WriteBlocks();
        WriteEntities();
        WriteObjects();
        WriteThumbnail();

        WriteString(0, "EOF");
        return m_out->Take();
    }

    // ── 准备 ───────────────────────────────────────────────────────

    void Writer::Prepare()
    {
        // R2000 起才有布局、打印样式字典等对象，R14 及以前的 DXF 需要降级转换（尚未实现），改写为 R2000
        m_version = m_options.Version != CadVersion::Unknown ? m_options.Version : m_db.GetVersion();
        if (m_version < CadVersion::AC1015)
        {
            Notify(NotificationType::Info,
                   std::string(VersionDisplayName(m_version)) + " DXF 不支持写出，改写为 R2000（AC1015）");
            m_version = CadVersion::AC1015;
        }

        // R2007 起为 UTF-8；之前按 $DWGCODEPAGE，不支持的代码页按 ANSI_1252
        m_codePageName = m_db.Header.CodePage;
        if (m_version >= CadVersion::AC1021)
        {
            m_codePage = Codec::CodePage::Utf8;
            if (m_codePageName.empty())
                m_codePageName = "ANSI_1252";
        }
        else
        {
            m_codePage = Codec::CodePageFromName(m_codePageName);
            if (m_codePage == Codec::CodePage::Unknown || !Codec::IsSupported(m_codePage)
                || m_codePage == Codec::CodePage::Utf8)
            {
                if (!m_codePageName.empty() && !EqualsIgnoreCase(m_codePageName, "ANSI_1252"))
                    Notify(NotificationType::Warning, "不支持的代码页 " + m_codePageName + "，按 ANSI_1252 写出");
                m_codePage = Codec::CodePage::Windows1252;
                m_codePageName = "ANSI_1252";
            }
        }
        m_out = std::make_unique<DxfStreamWriter>(m_options.Binary, m_codePage);

        // 新句柄从现有最大句柄之后开始
        Handle maxHandle = 0;
        for (const auto& [handle, object] : m_db.Objects())
            maxHandle = std::max(maxHandle, handle);
        m_nextHandle = std::max({ m_db.GetHandleSeed(), static_cast<Handle>(m_db.Header.HandleSeed), maxHandle + 1 });

        if (m_db.BlockRecords() == nullptr || m_db.ModelSpace() == nullptr || m_db.PaperSpace() == nullptr
            || m_db.RootDictionary() == nullptr)
            Notify(NotificationType::Warning, "数据库缺少符号表、模型空间/图纸空间或根字典，写出的文件可能无法打开（先调用 CreateDefaults）");

        // 缺少的 BLOCK / ENDBLK
        if (const CadTable* records = m_db.BlockRecords())
        {
            for (Handle h : records->Entries)
            {
                const auto* record = m_db.FindAs<BlockRecord>(h);
                if (record == nullptr)
                    continue;
                if (m_db.FindAs<Block>(record->BlockEntityHandle) == nullptr)
                    m_blockBegins[h] = Allocate();
                if (m_db.FindAs<BlockEnd>(record->BlockEndHandle) == nullptr)
                    m_blockEnds[h] = Allocate();
            }
        }

        // 缺少的 SEQEND：POLYLINE 总要有，INSERT 有属性时才要
        for (const auto& [handle, object] : m_db.Objects())
        {
            if (const auto* pl = dynamic_cast<const Polyline*>(object.get()))
            {
                if (m_db.FindAs<Seqend>(pl->SeqendHandle) == nullptr)
                    m_seqends[handle] = Allocate();
            }
            else if (const auto* insert = dynamic_cast<const Insert*>(object.get()))
            {
                const bool hasAttributes = std::any_of(insert->Attributes.begin(), insert->Attributes.end(),
                    [&](Handle a) { return m_db.FindAs<AttributeEntity>(a) != nullptr; });
                if (hasAttributes && m_db.FindAs<Seqend>(insert->SeqendHandle) == nullptr)
                    m_seqends[handle] = Allocate();
            }
            else if (const auto* reactor = dynamic_cast<const ImageDefinitionReactor*>(object.get()))
            {
                m_imageReactors.emplace(reactor->ImageHandle, handle);
            }
        }

        // 类定义：保留已有的，补上写出的对象需要的；实例个数按写出的对象重新统计
        std::map<std::string, int> counts;
        for (const auto& [handle, object] : m_db.Objects())
            ++counts[std::string(object->GetDxfName())];
        m_classes = m_db.Classes;
        for (const ClassTemplate& t : kClassTemplates)
        {
            if (counts.count(std::string(t.DxfName)) == 0)
                continue;
            const bool known = std::any_of(m_classes.begin(), m_classes.end(),
                                           [&](const DxfClass& c) { return EqualsIgnoreCase(c.DxfName, t.DxfName); });
            if (known)
                continue;
            DxfClass c;
            c.DxfName = std::string(t.DxfName);
            c.CppClassName = std::string(t.CppClassName);
            c.ApplicationName = std::string(t.ApplicationName);
            c.ProxyFlags = t.ProxyFlags;
            c.IsAnEntity = t.IsAnEntity;
            c.ItemClassId = t.IsAnEntity ? 0x1F2 : 0x1F3;
            m_classes.push_back(std::move(c));
        }
        for (std::size_t i = 0; i < m_classes.size(); ++i)
        {
            auto it = counts.find(m_classes[i].DxfName);
            m_classes[i].InstanceCount = it != counts.end() ? it->second : 0;
            m_classes[i].ClassNumber = static_cast<std::int16_t>(500 + i);
        }
    }

    // ── 引用 ───────────────────────────────────────────────────────

    std::string Writer::NameOf(Handle h, std::string_view fallback) const
    {
        if (const auto* entry = m_db.FindAs<TableEntry>(h))
            return entry->Name;
        if (const auto* named = m_db.FindAs<NonGraphicalObject>(h))
        {
            if (!named->Name.empty())
                return named->Name;
            // 没有名称的对象（原样保留的颜色簿颜色、材质等）：取所在字典中的条目名
            if (const auto* dict = m_db.FindAs<CadDictionary>(named->OwnerHandle))
            {
                for (std::size_t i = 0; i < dict->EntryHandles.size() && i < dict->EntryNames.size(); ++i)
                {
                    if (dict->EntryHandles[i] == h)
                        return dict->EntryNames[i];
                }
            }
        }
        return std::string(fallback);
    }

    void Writer::WriteRef(int code, Handle h)
    {
        if (Exists(h))
            WriteHandle(code, h);
    }

    void Writer::WriteRefOrNull(int code, Handle h)
    {
        WriteHandle(code, Ref(h));
    }

    bool Writer::IsPaperSpaceOwner(Handle owner) const
    {
        const auto* record = m_db.FindAs<BlockRecord>(owner);
        return record != nullptr && StartsWithIgnoreCase(record->Name, "*Paper_Space");
    }

    Handle Writer::BlockBeginOf(const BlockRecord& record) const
    {
        auto it = m_blockBegins.find(record.ObjectHandle);
        return it != m_blockBegins.end() ? it->second : record.BlockEntityHandle;
    }

    Handle Writer::BlockEndOf(const BlockRecord& record) const
    {
        auto it = m_blockEnds.find(record.ObjectHandle);
        return it != m_blockEnds.end() ? it->second : record.BlockEndHandle;
    }

    Handle Writer::SeqendOf(const Entity& parent) const
    {
        auto it = m_seqends.find(parent.ObjectHandle);
        if (it != m_seqends.end())
            return it->second;
        if (const auto* pl = dynamic_cast<const Polyline*>(&parent))
            return pl->SeqendHandle;
        if (const auto* insert = dynamic_cast<const Insert*>(&parent))
            return insert->SeqendHandle;
        return kNullHandle;
    }

    // ── 公共数据 ───────────────────────────────────────────────────

    // THUMBNAILIMAGE：只有 BMP 缩略图能写进 DXF（R2013 起 DWG 中的 PNG 缩略图不写）
    void Writer::WriteThumbnail()
    {
        const CadPreview& preview = m_db.Preview;
        if (preview.Type != CadPreview::ImageType::Bmp || preview.Image.empty())
            return;
        BeginSection("THUMBNAILIMAGE");
        WriteInt(90, preview.Image.size());
        m_out->WriteBytes(310, preview.Image);     // 自动分行
        EndSection();
    }

    void Writer::BeginSection(std::string_view name)
    {
        WriteString(0, "SECTION");
        WriteString(2, name);
    }

    void Writer::EndSection()
    {
        WriteString(0, "ENDSEC");
    }

    // 句柄、反应器、扩展字典、所有者（AutoCAD 的顺序：{ACAD_REACTORS 在 {ACAD_XDICTIONARY 之前）
    void Writer::WriteCommonObjectData(const CadObject& object, Handle owner)
    {
        WriteHandle(dynamic_cast<const DimensionStyle*>(&object) != nullptr ? 105 : 5, object.ObjectHandle);

        std::vector<Handle> reactors;
        for (Handle r : object.Reactors)
        {
            if (Exists(r))
                reactors.push_back(r);
        }
        if (!reactors.empty())
        {
            WriteString(102, "{ACAD_REACTORS");
            for (Handle r : reactors)
                WriteHandle(330, r);
            WriteString(102, "}");
        }

        if (m_db.FindAs<CadDictionary>(object.XDictionaryHandle) != nullptr)
        {
            WriteString(102, "{ACAD_XDICTIONARY");
            WriteHandle(360, object.XDictionaryHandle);
            WriteString(102, "}");
            Enqueue(object.XDictionaryHandle);
        }

        WriteHandle(330, owner);
    }

    // AcDbEntity：默认值（ByLayer 颜色/线型/线宽、线型比例 1、可见）不写，与 AutoCAD 一致
    void Writer::WriteCommonEntityData(const Entity& entity, bool paperSpace)
    {
        WriteSubclass("AcDbEntity");
        if (paperSpace)
            WriteInt(67, 1);
        WriteString(8, NameOf(entity.LayerHandle, "0"));

        if (const auto* lineType = m_db.FindAs<LineType>(entity.LineTypeHandle);
            lineType != nullptr && !EqualsIgnoreCase(lineType->Name, "ByLayer"))
            WriteString(6, lineType->Name);

        if (AtLeast(CadVersion::AC1018) && Exists(entity.MaterialHandle))
            WriteHandle(347, entity.MaterialHandle);

        if (entity.Color.IsTrueColor())
        {
            WriteInt(62, entity.Color.ApproxIndex());
            if (AtLeast(CadVersion::AC1018))
                WriteInt(420, entity.Color.TrueColor());
            // 颜色簿颜色：430 为 DBCOLOR 的名称（颜色簿$颜色名）
            if (AtLeast(CadVersion::AC1018) && Exists(entity.BookColorHandle))
            {
                const std::string name = NameOf(entity.BookColorHandle);
                if (!name.empty())
                    WriteString(430, name);
            }
        }
        else if (!entity.Color.IsByLayer())
        {
            WriteInt(62, entity.Color.Index());
        }

        if (entity.LineWeight != LineWeightType::ByLayer)
            WriteInt(370, entity.LineWeight);
        WriteRealIfNot(48, entity.LineTypeScale, 1.0);
        if (entity.IsInvisible)
            WriteInt(60, 1);
        if (AtLeast(CadVersion::AC1018) && !entity.Transparency.IsByLayer())
            WriteInt(440, Transparency::ToAlphaValue(entity.Transparency));
    }

    void Writer::WriteExtendedData(const CadObject& object)
    {
        for (const ExtendedData& data : object.ExtendedDataList)
        {
            const auto* appId = m_db.FindAs<AppId>(data.AppIdHandle);
            if (appId == nullptr || appId->Name.empty())
            {
                NotifyOnce("xdata-app", NotificationType::Warning, "扩展数据的应用程序（APPID）不存在，已跳过");
                continue;
            }
            WriteString(1001, appId->Name);
            for (const ExtendedDataRecord& record : data.Records)
            {
                // 不跟在 X 之后的单独 Y/Z 分量（R12 文件中偶见）：R13 起 AutoCAD 拒绝整个对象，不写
                if (record.Code >= 1020 && record.Code <= 1033)
                {
                    NotifyOnce("xdata:lone", NotificationType::Info, "扩展数据中单独的 Y/Z 坐标分量（1020～1033）未写出");
                    continue;
                }
                // 按组码的类型写出（二进制 DXF 中值的长度由组码决定，不能按存储的类型写）
                const int code = record.Code;
                std::visit([&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, XYZ>)
                        WriteXYZ(code, v);
                    else if constexpr (std::is_same_v<T, Handle>)
                        WriteHandle(code, Ref(v));   // 与 ACadSharp 一致：找不到的对象写 0
                    else if constexpr (std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::int32_t>)
                        m_out->WriteValue(code, DxfValue(static_cast<std::int64_t>(v)));
                    else
                        m_out->WriteValue(code, DxfValue(v));
                }, record.Value);
            }
        }
    }

    // 长文本：前面的分块用 chunkCode（MTEXT 的 3），最后一块用 code
    void Writer::WriteLongText(int code, int chunkCode, std::string_view text)
    {
        while (text.size() > kTextChunk)
        {
            std::size_t n = kTextChunk;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
                --n;    // 退到 UTF-8 字符的开头
            if (n == 0)
                n = kTextChunk;
            WriteString(chunkCode, text.substr(0, n));
            text.remove_prefix(n);
        }
        WriteString(code, text);
    }

    // ── HEADER ─────────────────────────────────────────────────────

    void Writer::WriteHeader()
    {
        const CadHeader& header = m_db.Header;
        BeginSection("HEADER");

        WriteString(9, "$ACADVER");
        WriteString(1, VersionString(m_version));
        WriteString(9, "$DWGCODEPAGE");
        WriteString(3, m_codePageName);

        // 名称引用的头变量：名称不存在时写默认值，避免 AutoCAD 找不到当前图层等
        auto validName = [&](std::string_view variable, const std::string& name) -> std::string {
            if (variable == "$CLAYER")
                return m_db.FindTableEntry(m_db.Layers(), name) ? name : "0";
            if (variable == "$CELTYPE")
                return m_db.FindTableEntry(m_db.LineTypes(), name) ? name : "ByLayer";
            if (variable == "$TEXTSTYLE")
                return m_db.FindTableEntry(m_db.TextStyles(), name) ? name : "Standard";
            if (variable == "$DIMSTYLE")
                return m_db.FindTableEntry(m_db.DimensionStyles(), name) ? name : "Standard";
            if (variable == "$CMLSTYLE")
            {
                const CadDictionary* styles = m_db.FindNamedDictionary("ACAD_MLINESTYLE");
                if (styles != nullptr)
                {
                    for (const std::string& n : styles->EntryNames)
                    {
                        if (EqualsIgnoreCase(n, name))
                            return name;
                    }
                }
                return "Standard";
            }
            return name;
        };

        for (const HeaderVariableInfo& v : AllHeaderVariables())
        {
            if (v.Name == "$ACADVER" || v.Name == "$DWGCODEPAGE" || Contains(kIgnoredHeaderVariables, v.Name))
                continue;
            if (v.Name != "$HANDSEED" && !m_options.WriteAllHeaderVariables && !Contains(kDefaultHeaderVariables, v.Name))
                continue;
            if (v.Get == nullptr)
                continue;

            if (v.Name == "$HANDSEED")
            {
                WriteString(9, v.Name);
                WriteHandle(5, m_nextHandle);
                continue;
            }
            if (v.Name == "$CECOLOR")
            {
                WriteString(9, v.Name);
                WriteInt(62, header.CurrentEntityColor.ApproxIndex());
                continue;
            }

            std::vector<std::pair<int, DxfValue>> values;
            for (int k = 0; k < v.CodeCount; ++k)
            {
                DxfValue value = v.Get(header, v.Codes[k]);
                if (value.IsEmpty())
                    continue;
                if (GroupCodeTypeOf(v.Codes[k]) == GroupCodeType::Handle && !Exists(value.AsHandle()))
                    value = DxfValue(HandleValue{ kNullHandle });
                if (v.IsName)
                    value = DxfValue(validName(v.Name, value.AsString()));
                values.emplace_back(v.Codes[k], std::move(value));
            }
            if (values.empty())
                continue;
            WriteString(9, v.Name);
            for (const auto& [code, value] : values)
                m_out->WriteValue(code, value);
        }
        EndSection();
    }

    // ── CLASSES ────────────────────────────────────────────────────

    void Writer::WriteClasses()
    {
        BeginSection("CLASSES");
        for (const DxfClass& c : m_classes)
        {
            WriteString(0, "CLASS");
            WriteString(1, c.DxfName);
            WriteString(2, c.CppClassName);
            WriteString(3, c.ApplicationName);
            WriteInt(90, c.ProxyFlags);
            if (AtLeast(CadVersion::AC1018))
                WriteInt(91, c.InstanceCount);
            WriteInt(280, c.WasZombie ? 1 : 0);
            WriteInt(281, c.IsAnEntity ? 1 : 0);
        }
        EndSection();
    }

    // ── TABLES ─────────────────────────────────────────────────────

    void Writer::WriteTables()
    {
        BeginSection("TABLES");
        WriteTable(m_db.VPorts(), "VPORT", {}, true);
        WriteTable(m_db.LineTypes(), "LTYPE", {}, true);
        WriteTable(m_db.Layers(), "LAYER", {}, true);
        WriteTable(m_db.TextStyles(), "STYLE", {}, true);
        WriteTable(m_db.Views(), "VIEW", {}, true);
        WriteTable(m_db.UCSs(), "UCS", {}, true);
        WriteTable(m_db.AppIds(), "APPID", {}, true);
        WriteTable(m_db.DimensionStyles(), "DIMSTYLE", "AcDbDimStyleTable", true);
        WriteTable(m_db.BlockRecords(), "BLOCK_RECORD", {}, false);
        EndSection();
    }

    void Writer::WriteTable(const CadTable* table, std::string_view name, std::string_view subclass, bool writeFlags)
    {
        if (table == nullptr)
        {
            NotifyOnce("table:" + std::string(name), NotificationType::Warning, "数据库中没有 " + std::string(name) + " 表");
            return;
        }

        // 只写类型正确的表项（悬空句柄、类型不符的跳过）
        std::vector<const TableEntry*> entries;
        for (Handle h : table->Entries)
        {
            const auto* entry = m_db.FindAs<TableEntry>(h);
            if (entry == nullptr || entry->GetDxfName() != table->GetEntryDxfName())
            {
                NotifyOnce("entry:" + std::to_string(h), NotificationType::Warning,
                           std::string(name) + " 表中的句柄 " + DxfValue(HandleValue{ h }).AsString() + " 不是有效的表项，已跳过");
                continue;
            }
            entries.push_back(entry);
        }

        WriteString(0, "TABLE");
        WriteString(2, name);
        WriteCommonObjectData(*table, kNullHandle);
        WriteSubclass("AcDbSymbolTable");
        WriteInt(70, entries.size() > 0x7FFF ? 0 : entries.size());
        if (!subclass.empty())
            WriteSubclass(subclass);
        WriteExtendedData(*table);

        for (const TableEntry* entry : entries)
        {
            WriteString(0, entry->GetDxfName());
            WriteCommonObjectData(*entry, table->ObjectHandle);
            WriteTableEntry(*entry, writeFlags);
        }
        WriteString(0, "ENDTAB");
    }

    void Writer::WriteTableEntry(const TableEntry& entry, bool writeFlags)
    {
        WriteSubclass("AcDbSymbolTableRecord");
        WriteSubclass(entry.GetSubclassMarker());

        // 形文件样式（标志位 1）没有名字
        const auto* textStyle = dynamic_cast<const TextStyle*>(&entry);
        const bool isShapeFile = textStyle != nullptr && (static_cast<int>(textStyle->Flags) & 1) != 0;
        WriteString(2, isShapeFile ? std::string_view() : std::string_view(entry.Name));
        if (writeFlags)
            WriteInt(70, entry.Flags);

        if (const auto* record = dynamic_cast<const BlockRecord*>(&entry))
            WriteBlockRecordBody(*record);
        else if (const auto* dimStyle = dynamic_cast<const DimensionStyle*>(&entry))
            WriteDimensionStyleBody(*dimStyle);
        else if (const auto* layer = dynamic_cast<const Layer*>(&entry))
            WriteLayerBody(*layer);
        else if (const auto* lineType = dynamic_cast<const LineType*>(&entry))
            WriteLineTypeBody(*lineType);
        else if (textStyle != nullptr)
            WriteTextStyleBody(*textStyle);
        else if (const auto* ucs = dynamic_cast<const UCS*>(&entry))
            WriteUcsBody(*ucs);
        else if (const auto* view = dynamic_cast<const View*>(&entry))
            WriteViewBody(*view);
        else if (const auto* vport = dynamic_cast<const VPort*>(&entry))
            WriteVPortBody(*vport);

        WriteExtendedData(entry);
    }

    void Writer::WriteBlockRecordBody(const BlockRecord& record)
    {
        WriteRefOrNull(340, record.LayoutHandle);
        WriteInt(70, record.Units);
        WriteInt(280, record.IsExplodable ? 1 : 0);
        WriteInt(281, record.CanScale ? 1 : 0);
        if (!record.Preview.empty())
            m_out->WriteBytes(310, record.Preview);
    }

    void Writer::WriteDimensionStyleBody(const DimensionStyle& s)
    {
        if (!s.PostFix.empty())
            WriteString(3, s.PostFix);
        if (!s.AlternateDimensioningSuffix.empty())
            WriteString(4, s.AlternateDimensioningSuffix);

        WriteReal(40, s.ScaleFactor);
        WriteReal(41, s.ArrowSize);
        WriteReal(42, s.ExtensionLineOffset);
        WriteReal(43, s.DimensionLineIncrement);
        WriteReal(44, s.ExtensionLineExtension);
        WriteReal(45, s.Rounding);
        WriteReal(46, s.DimensionLineExtension);
        WriteReal(47, s.PlusTolerance);
        WriteReal(48, s.MinusTolerance);
        if (AtLeast(CadVersion::AC1021))
        {
            WriteReal(49, s.FixedExtensionLineLength);
            WriteAngle(50, s.JoggedRadiusDimensionTransverseSegmentAngle);
            if (s.TextBackgroundFillMode != DimensionTextBackgroundFillMode::NoBackground)
            {
                WriteInt(69, s.TextBackgroundFillMode);
                WriteInt(70, s.TextBackgroundColor.ApproxIndex());
            }
        }

        WriteReal(140, s.TextHeight);
        WriteReal(141, s.CenterMarkSize);
        WriteReal(142, s.TickSize);
        WriteReal(143, s.AlternateUnitScaleFactor);
        WriteReal(144, s.LinearScaleFactor);
        WriteReal(145, s.TextVerticalPosition);
        WriteReal(146, s.ToleranceScaleFactor);
        WriteReal(147, s.DimensionLineGap);
        WriteReal(148, s.AlternateUnitRounding);

        WriteInt(71, s.GenerateTolerances ? 1 : 0);
        WriteInt(72, s.LimitsGeneration ? 1 : 0);
        WriteInt(73, s.TextInsideHorizontal ? 1 : 0);
        WriteInt(74, s.TextOutsideHorizontal ? 1 : 0);
        WriteInt(75, s.SuppressFirstExtensionLine ? 1 : 0);
        WriteInt(76, s.SuppressSecondExtensionLine ? 1 : 0);
        WriteInt(77, s.TextVerticalAlignment);
        WriteInt(78, s.ZeroHandling);
        WriteInt(79, s.AngularZeroHandling);
        if (AtLeast(CadVersion::AC1021))
            WriteInt(90, s.ArcLengthSymbolPosition);

        WriteInt(170, s.AlternateUnitDimensioning ? 1 : 0);
        WriteInt(171, s.AlternateUnitDecimalPlaces);
        WriteInt(172, s.TextOutsideExtensions ? 1 : 0);
        WriteInt(173, s.SeparateArrowBlocks ? 1 : 0);
        WriteInt(174, s.TextInsideExtensions ? 1 : 0);
        WriteInt(175, s.SuppressOutsideExtensions ? 1 : 0);
        WriteInt(176, s.DimensionLineColor.ApproxIndex());
        WriteInt(177, s.ExtensionLineColor.ApproxIndex());
        WriteInt(178, s.TextColor.ApproxIndex());
        WriteInt(179, s.AngularDecimalPlaces);

        WriteInt(270, s.DimensionUnit);
        WriteInt(271, s.DecimalPlaces);
        WriteInt(272, s.ToleranceDecimalPlaces);
        WriteInt(273, s.AlternateUnitFormat);
        WriteInt(274, s.AlternateUnitToleranceDecimalPlaces);
        WriteInt(275, s.AngularUnit);
        WriteInt(276, s.FractionFormat);
        WriteInt(277, s.LinearUnitFormat);
        WriteInt(278, static_cast<unsigned char>(s.DecimalSeparator));
        WriteInt(279, s.TextMovement);
        WriteInt(280, s.TextHorizontalAlignment);
        WriteInt(281, s.SuppressFirstDimensionLine ? 1 : 0);
        WriteInt(282, s.SuppressSecondDimensionLine ? 1 : 0);
        WriteInt(283, s.ToleranceAlignment);
        WriteInt(284, s.ToleranceZeroHandling);
        WriteInt(285, s.AlternateUnitZeroHandling);
        WriteInt(286, s.AlternateUnitToleranceZeroHandling);
        WriteInt(287, s.DimensionFit);
        WriteInt(288, s.CursorUpdate ? 1 : 0);
        WriteInt(289, s.DimensionTextArrowFit);
        if (AtLeast(CadVersion::AC1021))
            WriteBool(290, s.IsExtensionLineLengthFixed);

        WriteRef(340, s.StyleHandle);
        WriteRef(341, s.LeaderArrowHandle);
        WriteRef(342, s.ArrowBlockHandle);
        WriteRef(343, s.DimArrow1Handle);
        WriteRef(344, s.DimArrow2Handle);
        if (AtLeast(CadVersion::AC1021))
        {
            WriteRef(345, s.LineTypeHandle);
            WriteRef(346, s.LineTypeExt1Handle);
            WriteRef(347, s.LineTypeExt2Handle);
        }
        WriteInt(371, s.DimensionLineWeight);
        WriteInt(372, s.ExtensionLineWeight);
    }

    void Writer::WriteLayerBody(const Layer& layer)
    {
        std::int16_t index = layer.Color.ApproxIndex();
        if (index <= 0 || index > 255)
            index = 7;  // 图层没有 ByLayer / ByBlock
        WriteInt(62, layer.IsOn ? index : -index);
        if (layer.Color.IsTrueColor() && AtLeast(CadVersion::AC1018))
            WriteInt(420, layer.Color.TrueColor());
        WriteString(6, NameOf(layer.LineTypeHandle, "Continuous"));
        // DEFPOINTS 图层总是不打印，AutoCAD 拒绝打印标志为 1 的 DEFPOINTS（R12 文件没有这个标志）
        WriteBool(290, layer.PlotFlag && !EqualsIgnoreCase(layer.Name, "Defpoints"));
        WriteInt(370, layer.LineWeight);
        WriteRefOrNull(390, layer.PlotStyleName);
        if (AtLeast(CadVersion::AC1018))
            WriteRef(347, layer.MaterialHandle);
    }

    void Writer::WriteLineTypeBody(const LineType& lineType)
    {
        double patternLength = 0.0;
        for (const LineTypeSegment& s : lineType.Segments)
            patternLength += s.Length < 0 ? -s.Length : s.Length;

        WriteString(3, lineType.Description);
        WriteInt(72, static_cast<unsigned char>(lineType.Alignment));
        WriteInt(73, lineType.Segments.size());
        WriteReal(40, patternLength);

        for (const LineTypeSegment& s : lineType.Segments)
        {
            WriteReal(49, s.Length);
            // 8 位（文字保持正向，R2013 引入）：DWG 各版本都存，AutoCAD 写 R2013 之前的 DXF 时去掉
            const int flags = static_cast<int>(s.Flags);
            WriteInt(74, m_version >= CadVersion::AC1027 ? flags : flags & ~8);
            if (s.Flags == LineTypeShapeFlags::None)
                continue;
            const bool isText = HasFlag(s.Flags, LineTypeShapeFlags::Text);
            WriteInt(75, isText ? 0 : s.ShapeNumber);
            WriteRefOrNull(340, s.StyleHandle);
            WriteReal(46, s.Scale);
            WriteAngle(50, s.Rotation);
            WriteReal(44, s.Offset.X);
            WriteReal(45, s.Offset.Y);
            if (isText)
                WriteString(9, s.Text);
        }
    }

    void Writer::WriteTextStyleBody(const TextStyle& style)
    {
        WriteReal(40, style.Height);
        WriteReal(41, style.Width);
        WriteAngle(50, style.ObliqueAngle);
        WriteInt(71, style.MirrorFlag);
        WriteReal(42, style.LastHeight);
        WriteString(3, style.Filename);
        WriteString(4, style.BigFontFilename);
    }

    void Writer::WriteUcsBody(const UCS& ucs)
    {
        WriteXYZ(10, ucs.Origin);
        WriteXYZ(11, ucs.XAxis);
        WriteXYZ(12, ucs.YAxis);
        WriteInt(79, ucs.OrthographicViewType);
        WriteReal(146, ucs.Elevation);
        WriteInt(71, ucs.OrthographicType);
    }

    void Writer::WriteViewBody(const View& view)
    {
        WriteReal(40, view.Height);
        WriteXY(10, view.Center);
        WriteReal(41, view.Width);
        WriteXYZ(11, view.Direction);
        WriteXYZ(12, view.Target);
        WriteReal(42, view.LensLength);
        WriteReal(43, view.FrontClipping);
        WriteReal(44, view.BackClipping);
        WriteAngle(50, view.Angle);
        WriteInt(71, view.ViewMode);
        WriteInt(281, view.RenderMode);
        WriteInt(72, view.IsUcsAssociated ? 1 : 0);
        if (AtLeast(CadVersion::AC1021))
            WriteInt(73, view.IsPlottable ? 1 : 0);
        WriteRef(348, view.VisualStyleHandle);
        if (view.IsUcsAssociated)
        {
            WriteXYZ(110, view.UcsOrigin);
            WriteXYZ(111, view.UcsXAxis);
            WriteXYZ(112, view.UcsYAxis);
            WriteInt(79, view.UcsOrthographicType);
            WriteReal(146, view.UcsElevation);
        }
    }

    void Writer::WriteVPortBody(const VPort& v)
    {
        WriteXY(10, v.BottomLeft);
        WriteXY(11, v.TopRight);
        WriteXY(12, v.Center);
        WriteXY(13, v.SnapBasePoint);
        WriteXY(14, v.SnapSpacing);
        WriteXY(15, v.GridSpacing);
        WriteXYZ(16, v.Direction);
        WriteXYZ(17, v.Target);
        WriteReal(40, v.ViewHeight);
        WriteReal(41, v.AspectRatio);
        WriteReal(42, v.LensLength);
        WriteReal(43, v.FrontClippingPlane);
        WriteReal(44, v.BackClippingPlane);
        WriteAngle(50, v.SnapRotation);
        WriteAngle(51, v.TwistAngle);
        WriteInt(71, v.ViewMode);
        WriteInt(72, v.CircleZoomPercent);
        WriteInt(73, 1);    // 快速缩放（已废弃，AutoCAD 总写 1）
        WriteInt(74, v.UcsIconDisplay);
        WriteInt(75, v.SnapOn ? 1 : 0);
        WriteInt(76, v.ShowGrid ? 1 : 0);
        WriteInt(77, v.IsometricSnap ? 1 : 0);
        WriteInt(78, v.SnapIsoPair);
        WriteInt(281, v.RenderMode);
        WriteInt(65, 1);    // UCSVP
        WriteXYZ(110, v.Origin);
        WriteXYZ(111, v.XAxis);
        WriteXYZ(112, v.YAxis);
        WriteInt(79, v.OrthographicType);
        WriteReal(146, v.Elevation);
        WriteRef(345, v.NamedUcsHandle);
        WriteRef(346, v.BaseUcsHandle);
        if (!AtLeast(CadVersion::AC1021))
            return;
        WriteInt(60, v.GridFlags);
        WriteInt(61, v.MinorGridLinesPerMajorGridLine);
        WriteRef(348, v.VisualStyleHandle);
        WriteInt(292, v.UseDefaultLighting ? 1 : 0);
        WriteInt(282, v.DefaultLighting);
        WriteReal(141, v.Brightness);
        WriteReal(142, v.Contrast);
        WriteInt(63, v.AmbientColor.ApproxIndex());
        if (v.AmbientColor.IsTrueColor())
            WriteInt(421, v.AmbientColor.TrueColor());
    }

    // ── BLOCKS ─────────────────────────────────────────────────────

    void Writer::WriteBlocks()
    {
        BeginSection("BLOCKS");
        if (const CadTable* records = m_db.BlockRecords())
        {
            for (Handle h : records->Entries)
            {
                const auto* record = m_db.FindAs<BlockRecord>(h);
                if (record == nullptr)
                    continue;

                const bool paper = StartsWithIgnoreCase(record->Name, "*Paper_Space");
                // *Model_Space 与 *Paper_Space 的实体写在 ENTITIES 段，其余块的实体写在块定义里
                const bool inEntitiesSection =
                    EqualsIgnoreCase(record->Name, "*Model_Space") || EqualsIgnoreCase(record->Name, "*Paper_Space");

                WriteBlock(*record);
                for (Handle e : record->Entities)
                {
                    const auto* entity = m_db.FindAs<Entity>(e);
                    if (entity == nullptr)
                        continue;
                    if (inEntitiesSection)
                        m_entitiesSection.push_back({ entity, record->ObjectHandle, paper });
                    else
                        WriteEntity(*entity, record->ObjectHandle, paper);
                }
                WriteBlockEnd(*record);
            }
        }
        EndSection();
    }

    void Writer::WriteBlock(const BlockRecord& record)
    {
        const auto* block = m_db.FindAs<Block>(record.BlockEntityHandle);
        const bool paper = StartsWithIgnoreCase(record.Name, "*Paper_Space");

        WriteString(0, "BLOCK");
        if (block != nullptr)
        {
            WriteCommonObjectData(*block, record.ObjectHandle);
            WriteCommonEntityData(*block, paper);
        }
        else
        {
            WriteHandle(5, BlockBeginOf(record));
            WriteHandle(330, record.ObjectHandle);
            WriteSubclass("AcDbEntity");
            if (paper)
                WriteInt(67, 1);
            WriteString(8, "0");
        }

        WriteSubclass("AcDbBlockBegin");
        WriteString(2, record.Name);
        WriteInt(70, block != nullptr ? block->Flags : BlockTypeFlags::None);
        WriteXYZ(10, block != nullptr ? block->BasePoint : XYZ{});
        WriteString(3, record.Name);
        WriteString(1, block != nullptr ? std::string_view(block->XRefPath) : std::string_view());
        if (block != nullptr && !block->Comments.empty())
            WriteString(4, block->Comments);

        if (block != nullptr)
            WriteExtendedData(*block);
    }

    void Writer::WriteBlockEnd(const BlockRecord& record)
    {
        const auto* end = m_db.FindAs<BlockEnd>(record.BlockEndHandle);
        const bool paper = StartsWithIgnoreCase(record.Name, "*Paper_Space");

        WriteString(0, "ENDBLK");
        if (end != nullptr)
        {
            WriteCommonObjectData(*end, record.ObjectHandle);
            WriteCommonEntityData(*end, paper);
        }
        else
        {
            WriteHandle(5, BlockEndOf(record));
            WriteHandle(330, record.ObjectHandle);
            WriteSubclass("AcDbEntity");
            if (paper)
                WriteInt(67, 1);
            WriteString(8, "0");
        }
        WriteSubclass("AcDbBlockEnd");
        if (end != nullptr)
            WriteExtendedData(*end);
    }

    // ── ENTITIES ───────────────────────────────────────────────────

    void Writer::WriteEntities()
    {
        BeginSection("ENTITIES");
        for (const SectionEntity& e : m_entitiesSection)
            WriteEntity(*e.Item, e.Owner, e.PaperSpace);
        EndSection();
    }

    // ── OBJECTS ────────────────────────────────────────────────────

    void Writer::Enqueue(Handle object)
    {
        if (object != kNullHandle && m_writtenObjects.count(object) == 0)
            m_objectQueue.push_back(object);
    }

    // 从根字典开始，依次写出字典条目、扩展字典与实体引用的对象（与 ACadSharp 相同的遍历方式）
    void Writer::WriteObjects()
    {
        BeginSection("OBJECTS");
        if (const CadDictionary* root = m_db.RootDictionary())
            m_objectQueue.push_front(root->ObjectHandle);

        for (bool added = true; added;)
        {
            while (!m_objectQueue.empty())
            {
                const Handle h = m_objectQueue.front();
                m_objectQueue.pop_front();
                if (!m_writtenObjects.insert(h).second)
                    continue;
                const CadObject* object = m_db.Find(h);
                if (object == nullptr || dynamic_cast<const Entity*>(object) != nullptr
                    || dynamic_cast<const TableEntry*>(object) != nullptr || dynamic_cast<const CadTable*>(object) != nullptr)
                    continue;
                if (!CanWriteRaw(*object))
                    continue;
                WriteObject(*object);
            }
            // 没有被引用、但所有者已写出的对象（如 ACAD_TABLE 的 TABLECONTENT）
            added = false;
            for (const auto& [handle, object] : m_db.Objects())
            {
                if (m_writtenObjects.count(handle) != 0 || dynamic_cast<const NonGraphicalObject*>(object.get()) == nullptr
                    || !Exists(handle))
                    continue;
                const CadObject* owner = m_db.Find(object->OwnerHandle);
                const bool ownerWritten = owner != nullptr
                    && (m_writtenObjects.count(object->OwnerHandle) != 0 || dynamic_cast<const Entity*>(owner) != nullptr
                        || dynamic_cast<const TableEntry*>(owner) != nullptr);
                if (!ownerWritten)
                    continue;
                Enqueue(handle);
                added = true;
            }
        }
        EndSection();

        int orphans = 0;
        for (const auto& [handle, object] : m_db.Objects())
        {
            if (dynamic_cast<const NonGraphicalObject*>(object.get()) != nullptr && m_writtenObjects.count(handle) == 0)
                ++orphans;
        }
        if (orphans > 0)
            Notify(NotificationType::Info, std::to_string(orphans) + " 个对象不在根字典之下（也没有被引用），未写出");
    }

    void Writer::WriteObject(const CadObject& object)
    {
        WriteString(0, object.GetDxfName());
        WriteCommonObjectData(object, Ref(object.OwnerHandle));

        if (const auto* unknown = dynamic_cast<const UnknownObject*>(&object))
            WriteRawGroups(unknown->Raw.Dxf);
        else if (const auto* dict = dynamic_cast<const CadDictionary*>(&object))
            WriteDictionary(*dict);
        else if (const auto* assoc = dynamic_cast<const DimensionAssociation*>(&object))
            WriteDimensionAssociation(*assoc);
        else if (const auto* var = dynamic_cast<const DictionaryVariable*>(&object))
            WriteDictionaryVariable(*var);
        else if (const auto* group = dynamic_cast<const Group*>(&object))
            WriteGroup(*group);
        else if (const auto* def = dynamic_cast<const ImageDefinition*>(&object))
            WriteImageDefinition(*def);
        else if (const auto* reactor = dynamic_cast<const ImageDefinitionReactor*>(&object))
            WriteImageDefinitionReactor(*reactor);
        else if (const auto* layout = dynamic_cast<const Layout*>(&object))
            WriteLayout(*layout);
        else if (const auto* plot = dynamic_cast<const PlotSettings*>(&object))
            WritePlotSettings(*plot);
        else if (const auto* style = dynamic_cast<const MLineStyle*>(&object))
            WriteMLineStyle(*style);
        else if (const auto* mlStyle = dynamic_cast<const MultiLeaderStyle*>(&object))
            WriteMultiLeaderStyle(*mlStyle);
        else if (const auto* vars = dynamic_cast<const RasterVariables*>(&object))
            WriteRasterVariables(*vars);
        else if (const auto* scale = dynamic_cast<const Scale*>(&object))
            WriteScale(*scale);
        else if (const auto* sortents = dynamic_cast<const SortEntitiesTable*>(&object))
            WriteSortEntitiesTable(*sortents);
        else if (const auto* record = dynamic_cast<const XRecord*>(&object))
            WriteXRecord(*record);
        else if (dynamic_cast<const AcdbPlaceHolder*>(&object) != nullptr)
        {
            // 占位对象（打印样式 Normal 等）没有数据，也不写子类标记（与 AutoCAD 一致）
        }
        else
            NotifyOnce("object:" + std::string(object.GetDxfName()), NotificationType::Warning,
                       "未实现写出的对象 " + std::string(object.GetDxfName()));

        WriteExtendedData(object);
    }

    // 组码顺序与 AutoCAD 写的一致
    void Writer::WriteDimensionAssociation(const DimensionAssociation& assoc)
    {
        WriteSubclass("AcDbDimAssoc");
        WriteRefOrNull(330, assoc.DimensionHandle);
        WriteInt(90, assoc.AssociativityFlags);
        WriteInt(70, assoc.IsTransSpace ? 1 : 0);
        WriteInt(71, assoc.RotatedDimensionType);
        for (int i = 0; i < 4; ++i)
        {
            if ((static_cast<int>(assoc.AssociativityFlags) & (1 << i)) == 0)
                continue;
            const DimensionAssociationOsnapPointRef& ref = PointRefAt(assoc, i);
            WriteString(1, "AcDbOsnapPointRef");
            WriteInt(72, ref.ObjectOsnapType);
            WriteRefOrNull(331, ref.GeometryHandle);
            WriteInt(73, ref.SubentType);
            WriteInt(91, ref.GsMarker);
            if (ref.IntersectionSubType != SubentType{} || ref.IntersectionGsMarker != 0)
            {
                WriteInt(74, ref.IntersectionSubType);
                WriteInt(92, ref.IntersectionGsMarker);
            }
            WriteReal(40, ref.GeometryParameter);
            WriteXYZ(10, ref.OsnapPoint);
            WriteInt(75, ref.HasLastPointRef ? 1 : 0);
        }
    }

    void Writer::WriteDictionary(const CadDictionary& dict)
    {
        WriteSubclass("AcDbDictionary");
        if (dict.HardOwnerFlag)
            WriteInt(280, 1);
        WriteInt(281, dict.ClonningFlags);

        // 找不到的条目（未建模的对象）不写，避免悬空引用
        const std::size_t n = std::min(dict.EntryNames.size(), dict.EntryHandles.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const Handle h = dict.EntryHandles[i];
            const CadObject* entry = m_db.Find(h);
            if (entry == nullptr || dynamic_cast<const Entity*>(entry) != nullptr || !Exists(h))
                continue;
            WriteString(3, dict.EntryNames[i]);
            WriteHandle(dict.HardOwnerFlag ? 360 : 350, h);
            Enqueue(h);
        }

        if (const auto* withDefault = dynamic_cast<const CadDictionaryWithDefault*>(&dict))
        {
            WriteSubclass("AcDbDictionaryWithDefault");
            WriteRefOrNull(340, withDefault->DefaultEntryHandle);
        }
    }

    void Writer::WriteDictionaryVariable(const DictionaryVariable& var)
    {
        WriteSubclass("DictionaryVariables");
        WriteInt(280, var.ObjectSchemaNumber);
        WriteString(1, var.Value);
    }

    void Writer::WriteGroup(const Group& group)
    {
        WriteSubclass("AcDbGroup");
        WriteString(300, group.Description);
        WriteInt(70, !group.Name.empty() && group.Name[0] == '*' ? 1 : 0);    // 匿名组
        WriteInt(71, group.Selectable ? 1 : 0);
        for (Handle h : group.Entities)
            WriteRef(340, h);
    }

    void Writer::WriteImageDefinition(const ImageDefinition& def)
    {
        WriteSubclass("AcDbRasterImageDef");
        WriteInt(90, def.ClassVersion);
        WriteString(1, def.FileName);
        WriteXY(10, def.Size);
        WriteXY(11, def.DefaultSize);
        WriteInt(280, def.IsLoaded ? 1 : 0);
        WriteInt(281, def.Units);
    }

    void Writer::WriteImageDefinitionReactor(const ImageDefinitionReactor& reactor)
    {
        WriteSubclass("AcDbRasterImageDefReactor");
        WriteInt(90, reactor.ClassVersion);
        WriteRefOrNull(330, reactor.ImageHandle);
    }

    void Writer::WritePlotSettings(const PlotSettings& plot)
    {
        WriteSubclass("AcDbPlotSettings");
        WriteString(1, plot.PageName);
        WriteString(2, plot.SystemPrinterName);
        WriteString(4, plot.PaperSize);
        WriteString(6, plot.PlotViewName);
        WriteReal(40, plot.UnprintableMargin.Left);
        WriteReal(41, plot.UnprintableMargin.Bottom);
        WriteReal(42, plot.UnprintableMargin.Right);
        WriteReal(43, plot.UnprintableMargin.Top);
        WriteReal(44, plot.PaperWidth);
        WriteReal(45, plot.PaperHeight);
        WriteReal(46, plot.PlotOriginX);
        WriteReal(47, plot.PlotOriginY);
        WriteReal(48, plot.WindowLowerLeftX);
        WriteReal(49, plot.WindowLowerLeftY);
        WriteReal(140, plot.WindowUpperLeftX);
        WriteReal(141, plot.WindowUpperLeftY);
        WriteReal(142, plot.NumeratorScale);
        WriteReal(143, plot.DenominatorScale);
        WriteInt(70, plot.Flags);
        WriteInt(72, plot.PaperUnits);
        WriteInt(73, plot.PaperRotation);
        WriteInt(74, plot.PlotType);
        WriteString(7, plot.StyleSheet);
        WriteInt(75, plot.ScaledFit);
        WriteInt(76, plot.ShadePlotMode);
        WriteInt(77, plot.ShadePlotResolutionMode);
        WriteInt(78, plot.ShadePlotDPI);
        WriteReal(147, plot.StandardScale);
        WriteReal(148, plot.PaperImageOriginX);
        WriteReal(149, plot.PaperImageOriginY);
        if (AtLeast(CadVersion::AC1018))
            WriteRef(333, plot.ShadePlotIDHandle);
    }

    void Writer::WriteLayout(const Layout& layout)
    {
        WritePlotSettings(layout);

        WriteSubclass("AcDbLayout");
        WriteString(1, layout.Name);
        WriteInt(70, layout.LayoutFlags);
        WriteInt(71, layout.TabOrder);
        WriteXY(10, layout.MinLimits);
        WriteXY(11, layout.MaxLimits);
        WriteXYZ(12, layout.InsertionBasePoint);
        WriteXYZ(14, layout.MinExtents);
        WriteXYZ(15, layout.MaxExtents);
        WriteReal(146, layout.Elevation);
        WriteXYZ(13, layout.Origin);
        WriteXYZ(16, layout.XAxis);
        WriteXYZ(17, layout.YAxis);
        WriteInt(76, layout.UcsOrthographicType);
        WriteRefOrNull(330, layout.AssociatedBlockHandle);
        WriteRef(331, layout.LastActiveViewportHandle);
        WriteRef(345, layout.UCSHandle);
        WriteRef(346, layout.BaseUCSHandle);
    }

    void Writer::WriteMLineStyle(const MLineStyle& style)
    {
        WriteSubclass("AcDbMlineStyle");
        WriteString(2, style.Name);
        WriteInt(70, style.Flags);
        WriteString(3, style.Description);
        WriteInt(62, style.FillColor.ApproxIndex());
        WriteAngle(51, style.StartAngle);
        WriteAngle(52, style.EndAngle);
        WriteInt(71, style.Elements.size());
        for (const MLineStyleElement& e : style.Elements)
        {
            WriteReal(49, e.Offset);
            WriteInt(62, e.Color.ApproxIndex());
            WriteString(6, NameOf(e.LineTypeHandle, "BYLAYER"));
        }
    }

    void Writer::WriteRasterVariables(const RasterVariables& vars)
    {
        WriteSubclass("AcDbRasterVariables");
        WriteInt(90, vars.ClassVersion);
        WriteInt(70, vars.IsDisplayFrameShown ? 1 : 0);
        WriteInt(71, vars.DisplayQuality);
        WriteInt(72, vars.Units);
    }

    void Writer::WriteScale(const Scale& scale)
    {
        WriteSubclass("AcDbScale");
        WriteInt(70, 0);
        WriteString(300, scale.Name);
        WriteReal(140, scale.PaperUnits);
        WriteReal(141, scale.DrawingUnits);
        WriteBool(290, scale.IsUnitScale);
    }

    void Writer::WriteSortEntitiesTable(const SortEntitiesTable& table)
    {
        WriteSubclass("AcDbSortentsTable");
        WriteRefOrNull(330, table.BlockOwnerHandle);
        for (const SortEntsEntry& e : table.Entries)
        {
            if (!Exists(e.EntityHandle))
                continue;
            WriteHandle(331, e.EntityHandle);
            WriteHandle(5, e.SortHandle);
        }
    }

    void Writer::WriteXRecord(const XRecord& record)
    {
        WriteSubclass("AcDbXrecord");
        WriteInt(280, record.CloningFlags);
        for (const XRecordEntry& e : record.Entries)
        {
            if (GroupCodeTypeOf(e.Code) == GroupCodeType::Handle)
            {
                WriteHandle(e.Code, Ref(e.Value.AsHandle()));
                // 350～369 是拥有关系（如 ACAD_TABLE 的 TABLECONTENT），被拥有的对象随之写出
                if (e.Code >= 350 && e.Code <= 369)
                    Enqueue(e.Value.AsHandle());
            }
            else
                m_out->WriteValue(e.Code, e.Value);
        }
    }
}
