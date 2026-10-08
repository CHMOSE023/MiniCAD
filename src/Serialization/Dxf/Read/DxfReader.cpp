#include "Dxf/Read/DxfReaderImpl.h"
#include "Database/CadFileFormat.h"
#include "Database/DxfAssign.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace MiniDWG
{
    std::unique_ptr<CadDatabase> ReadDxf(std::span<const std::uint8_t> data, const DxfReadOptions& options)
    {
        const CadFileInfo info = DetectFileFormat(data.first(std::min<std::size_t>(data.size(), 1024)));
        if (info.format != CadFileFormat::DxfAscii && info.format != CadFileFormat::DxfBinary)
        {
            if (options.Notify)
                options.Notify(NotificationType::Error, "不是 DXF 文件");
            return nullptr;
        }
        DxfRead::Reader reader(data, options);
        return reader.Run();
    }
}

namespace MiniDWG::DxfRead
{
    namespace
    {
        constexpr double kDegToRad = kPi / 180.0;

        CadObjectType ControlTypeOfTable(std::string_view name)
        {
            if (name == "BLOCK_RECORD") return CadObjectType::BLOCK_CONTROL_OBJ;
            if (name == "LAYER") return CadObjectType::LAYER_CONTROL_OBJ;
            if (name == "DIMSTYLE") return CadObjectType::DIMSTYLE_CONTROL_OBJ;
            if (name == "STYLE") return CadObjectType::STYLE_CONTROL_OBJ;
            if (name == "LTYPE") return CadObjectType::LTYPE_CONTROL_OBJ;
            if (name == "VIEW") return CadObjectType::VIEW_CONTROL_OBJ;
            if (name == "UCS") return CadObjectType::UCS_CONTROL_OBJ;
            if (name == "VPORT") return CadObjectType::VPORT_CONTROL_OBJ;
            if (name == "APPID") return CadObjectType::APPID_CONTROL_OBJ;
            return CadObjectType::INVALID;
        }

        // 表名 → 表项 DXF 名（CadTable 保存的名字必须是静态字符串）
        std::string_view StaticTableName(std::string_view name)
        {
            static constexpr std::string_view kNames[] = {
                "BLOCK_RECORD", "LAYER", "DIMSTYLE", "STYLE", "LTYPE", "VIEW", "UCS", "VPORT", "APPID",
            };
            for (std::string_view n : kNames)
            {
                if (n == name)
                    return n;
            }
            return {};
        }
    }

    void ApplyOmittedDefaults(CadObject& object)
    {
        if (auto* d = dynamic_cast<DimensionStyle*>(&object))
        {
            d->AlternateUnitDecimalPlaces = 2;              // DIMALTD
            d->AlternateUnitToleranceDecimalPlaces = 2;     // DIMALTTD
            d->DecimalPlaces = 4;                           // DIMDEC
            d->ToleranceDecimalPlaces = 4;                  // DIMTDEC
            d->DimensionLineGap = 0.09;                     // DIMGAP
            d->DimensionLineIncrement = 0.38;               // DIMDLI
            d->ExtensionLineExtension = 0.18;               // DIMEXE
            d->ExtensionLineOffset = 0.0625;                // DIMEXO
            d->TextInsideHorizontal = true;                 // DIMTIH
            d->TextOutsideHorizontal = true;                // DIMTOH
            d->TextVerticalAlignment = DimensionTextVerticalAlignment::Centered;    // DIMTAD
            d->ToleranceAlignment = ToleranceAlignment::Middle;                     // DIMTOLJ
            d->ZeroHandling = ZeroHandling::SuppressZeroFeetAndInches;              // DIMZIN
            d->ToleranceZeroHandling = ZeroHandling::SuppressZeroFeetAndInches;     // DIMTZIN
            d->DimensionFit = 3;                            // DIMFIT
        }
        else if (auto* dim = dynamic_cast<Dimension*>(&object))
        {
            dim->LineSpacingStyle = LineSpacingStyleType::AtLeast;     // 72
            dim->LineSpacingFactor = 1.0;                               // 41
        }
        else if (auto* leader = dynamic_cast<Leader*>(&object))
        {
            leader->ArrowHeadEnabled = true;                            // 71
        }
        else if (auto* tolerance = dynamic_cast<Tolerance*>(&object))
        {
            tolerance->Direction = XYZ::AxisX();                        // 11
        }
    }

    // ── Record / RecordStream ───────────────────────────────────────

    std::string Record::Find(int code) const
    {
        for (const DxfGroup& g : Groups)
        {
            if (g.Code == code)
                return g.Value.AsString();
        }
        return {};
    }

    bool RecordStream::Fill()
    {
        if (m_ended)
            return false;

        DxfGroup g;
        if (!m_hasPendingStart)
        {
            // 找到第一个组码 0
            do
            {
                if (!m_reader.ReadNext(g))
                {
                    m_ended = true;
                    return false;
                }
            } while (g.Code != 0);
            m_pendingStart = std::move(g);
            m_hasPendingStart = true;
        }

        m_next.Type = m_pendingStart.Value.AsString();
        m_next.Groups.clear();
        m_next.Position = m_reader.Position();
        m_hasPendingStart = false;

        while (m_reader.ReadNext(g))
        {
            if (g.Code == 0)
            {
                m_pendingStart = std::move(g);
                m_hasPendingStart = true;
                break;
            }
            m_next.Groups.push_back(std::move(g));
        }
        if (!m_hasPendingStart && m_next.Type != "EOF")
            m_ended = true;     // 文件在记录中途结束：这一条仍然返回
        m_hasNext = true;
        return true;
    }

    const Record* RecordStream::Peek()
    {
        if (!m_hasNext && !Fill())
            return nullptr;
        return &m_next;
    }

    bool RecordStream::Next(Record& out)
    {
        if (!m_hasNext && !Fill())
            return false;
        out = std::move(m_next);
        m_hasNext = false;
        if (out.Type == "EOF")
            m_ended = true;
        return true;
    }

    // ── Reader ─────────────────────────────────────────────────────

    Reader::Reader(std::span<const std::uint8_t> data, const DxfReadOptions& options)
        : m_options(options), m_stream(data), m_records(m_stream), m_db(std::make_unique<CadDatabase>())
    {
        for (const DxfClassInfo* info : AllDxfClasses())
        {
            auto [it, inserted] = m_classByDxfName.try_emplace(info->DxfName, info);
            if (!inserted)
                it->second = nullptr;   // 同名多个类（DIMENSION、POLYLINE、VERTEX）：需要按内容判断
        }
    }

    void Reader::Notify(NotificationType type, std::string message)
    {
        if (m_options.Notify)
            m_options.Notify(type, message);
    }

    void Reader::NotifyOnce(const std::string& key, NotificationType type, std::string message)
    {
        if (m_notified.insert(key).second)
            Notify(type, std::move(message));
    }

    // 先扫一遍头段取版本与代码页，再从头读取（与 ACadSharp DxfReader.getReader 相同）
    void Reader::Prescan()
    {
        DxfGroup g;
        std::string variable;
        bool inHeader = false;
        while (m_stream.ReadNext(g))
        {
            if (g.Code == 0)
            {
                const std::string v = g.Value.AsString();
                if (inHeader && v == "ENDSEC")
                    break;
                if (v == "EOF")
                    break;
                continue;
            }
            if (g.Code == 2 && g.Value.AsString() == "HEADER")
                inHeader = true;
            if (!inHeader)
                continue;
            if (g.Code == 9)
            {
                variable = g.Value.AsString();
                continue;
            }
            if (variable == "$ACADVER")
                m_version = ParseVersionString(g.Value.AsString());
            else if (variable == "$DWGCODEPAGE")
            {
                const Codec::CodePage cp = Codec::CodePageFromName(g.Value.AsString());
                if (cp != Codec::CodePage::Unknown && Codec::IsSupported(cp))
                    m_stream.SetCodePage(cp);
                else
                    Notify(NotificationType::Warning, "不支持的代码页 " + g.Value.AsString() + "，按 ANSI_1252 读取");
            }
            variable.clear();
        }
        if (m_version >= CadVersion::AC1021)
            m_stream.SetCodePage(Codec::CodePage::Utf8);
        m_stream.Rewind();
    }

    std::unique_ptr<CadDatabase> Reader::Run()
    {
        Prescan();
        m_db->SetVersion(m_version == CadVersion::Unknown ? CadVersion::AC1009 : m_version);

        Record record;
        while (m_records.Next(record))
        {
            if (record.Type == "EOF")
                break;
            if (record.Type != "SECTION")
                continue;

            const std::string name = record.Find(2);
            if (name == "HEADER")
                ReadHeader(record);
            else if (name == "CLASSES")
                ReadClasses();
            else if (name == "TABLES")
                ReadTables();
            else if (name == "BLOCKS")
                ReadBlocks();
            else if (name == "ENTITIES")
                ReadEntitiesSection();
            else if (name == "OBJECTS")
                ReadObjects();
            else if (name == "THUMBNAILIMAGE")
            {
                // 90 字节数，310 图像数据（不含文件头的 BMP）分多行
                std::vector<std::uint8_t> image;
                for (const DxfGroup& g : record.Groups)
                {
                    if (g.Code != 310)
                        continue;
                    if (const auto* bytes = g.Value.AsBytes())
                        image.insert(image.end(), bytes->begin(), bytes->end());
                }
                if (!image.empty())
                {
                    m_db->Preview.Type = CadPreview::ImageType::Bmp;
                    m_db->Preview.Image = std::move(image);
                }
                SkipSection();
            }
            else
            {
                NotifyOnce("section:" + name, NotificationType::Info, "跳过未支持的段 " + name);
                SkipSection();
            }
        }

        Build();
        return std::move(m_db);
    }

    void Reader::SkipSection()
    {
        Record r;
        while (m_records.Next(r))
        {
            if (r.Type == "ENDSEC" || r.Type == "EOF")
                break;
        }
    }

    // ── HEADER ─────────────────────────────────────────────────────

    void Reader::ReadHeader(const Record& section)
    {
        std::unordered_map<std::string_view, const HeaderVariableInfo*> vars;
        for (const HeaderVariableInfo& v : AllHeaderVariables())
            vars.emplace(v.Name, &v);

        const HeaderVariableInfo* current = nullptr;
        for (const DxfGroup& g : section.Groups)
        {
            if (g.Code == 2 && current == nullptr)
                continue;   // 段名 HEADER
            if (g.Code == 9)
            {
                auto it = vars.find(g.Value.AsString());
                current = it != vars.end() ? it->second : nullptr;
                continue;
            }
            if (current != nullptr && current->Set != nullptr)
                current->Set(m_db->Header, g.Code, g.Value);
        }
        // 读完后 HEADER 段的记录就结束了（下一条是 ENDSEC）
        Record end;
        if (m_records.Peek() != nullptr && m_records.Peek()->Type == "ENDSEC")
            m_records.Next(end);
    }

    // ── CLASSES ────────────────────────────────────────────────────

    void Reader::ReadClasses()
    {
        Record r;
        while (m_records.Next(r))
        {
            if (r.Type == "ENDSEC" || r.Type == "EOF")
                break;
            if (r.Type != "CLASS")
                continue;
            DxfClass c;
            for (const DxfGroup& g : r.Groups)
            {
                switch (g.Code)
                {
                case 1: c.DxfName = g.Value.AsString(); break;
                case 2: c.CppClassName = g.Value.AsString(); break;
                case 3: c.ApplicationName = g.Value.AsString(); break;
                case 90: c.ProxyFlags = static_cast<std::int32_t>(g.Value.AsInt()); break;
                case 91: c.InstanceCount = static_cast<std::int32_t>(g.Value.AsInt()); break;
                case 280: c.WasZombie = g.Value.AsBool(); break;
                case 281: c.IsAnEntity = g.Value.AsBool(); break;
                default: break;
                }
            }
            c.ItemClassId = c.IsAnEntity ? 0x1F2 : 0x1F3;
            c.ClassNumber = static_cast<std::int16_t>(500 + m_db->Classes.size());
            m_db->Classes.push_back(std::move(c));
        }
    }

    // ── 通用组码 ───────────────────────────────────────────────────

    std::size_t Reader::AddItem(std::unique_ptr<CadObject> object)
    {
        ReadItem item;
        item.Object = std::move(object);
        m_items.push_back(std::move(item));
        return m_items.size() - 1;
    }

    bool Reader::HandleCommon(ReadItem& item, const Record& record, std::size_t& i, ParseState& state)
    {
        const DxfGroup& g = record.Groups[i];
        switch (g.Code)
        {
        case 5:
            // 子类标记之后的 5 不是对象句柄（如 SORTENTSTABLE 的排序句柄、DIMSTYLE 的 DIMBLK 名称）
            if (state.SeenSubclass)
                return false;
            item.Object->ObjectHandle = g.Value.AsHandle();
            return true;
        case 105:
            item.Object->ObjectHandle = g.Value.AsHandle();     // DIMSTYLE 的句柄
            return true;
        case 330:
            if (state.SeenSubclass)
                return false;
            item.Object->OwnerHandle = g.Value.AsHandle();
            return true;
        case 102:
        {
            const std::string tag = g.Value.AsString();
            if (tag == "{ACAD_XDICTIONARY")
            {
                for (++i; i < record.Groups.size() && record.Groups[i].Code != 102; ++i)
                {
                    if (record.Groups[i].Code == 360)
                        item.Object->XDictionaryHandle = record.Groups[i].Value.AsHandle();
                }
            }
            else if (tag == "{ACAD_REACTORS")
            {
                for (++i; i < record.Groups.size() && record.Groups[i].Code != 102; ++i)
                    item.Object->Reactors.push_back(record.Groups[i].Value.AsHandle());
            }
            else if (!tag.empty() && tag[0] == '{')
            {
                // 其他应用定义组：跳到结束标记
                for (++i; i < record.Groups.size() && record.Groups[i].Code != 102; ++i)
                {
                }
            }
            return true;
        }
        case 67:
            item.PaperSpace = g.Value.AsInt() != 0;
            return true;
        default:
            return false;
        }
    }

    void Reader::ReadUnknownGroups(ReadItem& item, const Record& record, RawObjectData& raw, bool entity)
    {
        raw.Dxf.Version = m_version;
        bool keep = false;
        Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
            if (!keep && g.Code == 100 && (!entity || g.Value.AsString() != "AcDbEntity"))
                keep = true;
            if (!keep)
                return false;
            raw.Dxf.Groups.push_back(RawDxfGroup{ static_cast<std::int16_t>(g.Code), g.Value });
            return true;
        });
    }

    // 按元数据赋值（对应 ACadSharp tryAssignCurrentValue）：先找当前子类，再从最派生的子类往上找
    bool Reader::TryAssign(ReadItem& item, const DxfGroup& group, const ParseState& state)
    {
        const DxfClassInfo& info = item.Object->GetClassInfo();

        auto findIn = [&](const DxfSubclassInfo& sub) -> const DxfPropertyInfo* {
            for (const DxfPropertyInfo& p : sub.Properties)
            {
                for (int k = 0; k < p.CodeCount; ++k)
                {
                    if (p.Codes[k] == group.Code)
                        return &p;
                }
            }
            return nullptr;
        };

        const DxfPropertyInfo* prop = nullptr;
        if (!state.Subclass.empty())
        {
            for (const DxfSubclassInfo& sub : info.Subclasses)
            {
                if (sub.Marker == state.Subclass)
                {
                    prop = findIn(sub);
                    break;
                }
            }
        }
        for (auto it = info.Subclasses.rbegin(); prop == nullptr && it != info.Subclasses.rend(); ++it)
            prop = findIn(*it);
        if (prop == nullptr)
            return false;

        if (prop->HasFlag(DxfReferenceType::Count) || prop->HasFlag(DxfReferenceType::Ignored) || prop->Computed)
            return true;
        // 引用既可能是句柄也可能是名称（MLINE 的样式标注为 Handle | Name）：按组码的值类型区分
        if (prop->HasFlag(DxfReferenceType::Name) && GroupCodeTypeOf(group.Code) == GroupCodeType::String)
        {
            item.NameRefs.push_back({ prop, group.Value.AsString() });
            return true;
        }
        if (prop->Set == nullptr)
            return false;
        if (prop->HasFlag(DxfReferenceType::IsAngle))
        {
            prop->Set(*item.Object, group.Code, DxfValue(group.Value.AsDouble() * kDegToRad));
            return true;
        }
        prop->Set(*item.Object, group.Code, group.Value);
        return true;
    }

    void Reader::ProcessGeneric(ReadItem& item, const Record& record)
    {
        Process(item, record, [](const DxfGroup&, std::size_t&, const ParseState&) { return false; });
    }

    // 扩展数据：1001 应用名，之后的 1000～1071 属于该应用，直到下一个 1001
    void Reader::ReadXData(ReadItem& item, const Record& record, std::size_t& i)
    {
        ExtendedData* current = nullptr;
        for (; i < record.Groups.size(); ++i)
        {
            const DxfGroup& g = record.Groups[i];
            if (g.Code == 1001)
            {
                item.Object->ExtendedDataList.emplace_back();
                item.XDataAppNames.push_back(g.Value.AsString());
                current = &item.Object->ExtendedDataList.back();
                continue;
            }
            if (current == nullptr || g.Code < 1000)
                continue;

            ExtendedDataRecord rec;
            rec.Code = static_cast<std::int16_t>(g.Code);
            if (g.Code >= 1010 && g.Code <= 1013)
            {
                // 三维点：X、Y、Z 三个组码合为一条
                XYZ p{ g.Value.AsDouble(), 0, 0 };
                if (i + 1 < record.Groups.size() && record.Groups[i + 1].Code == g.Code + 10)
                    p.Y = record.Groups[++i].Value.AsDouble();
                if (i + 1 < record.Groups.size() && record.Groups[i + 1].Code == g.Code + 20)
                    p.Z = record.Groups[++i].Value.AsDouble();
                rec.Value = p;
            }
            else if (g.Code == 1004)
            {
                const auto* bytes = g.Value.AsBytes();
                rec.Value = bytes != nullptr ? *bytes : std::vector<std::uint8_t>{};
            }
            else if (g.Code == 1005)
                rec.Value = g.Value.AsHandle();
            else if (g.Code >= 1020 && g.Code <= 1059)
                rec.Value = g.Value.AsDouble();     // 实数，及不跟在 X 之后的单独 Y/Z 分量（AutoCAD 偶尔这样写）
            else if (g.Code == 1070)
                rec.Value = static_cast<std::int16_t>(g.Value.AsInt());
            else if (g.Code == 1071)
                rec.Value = static_cast<std::int32_t>(g.Value.AsInt());
            else
                rec.Value = g.Value.AsString();     // 1000 字符串、1002 控制符、1003 图层名
            current->Records.push_back(std::move(rec));
        }
    }

    // ── TABLES ─────────────────────────────────────────────────────

    void Reader::ReadTables()
    {
        Record r;
        TableDef* table = nullptr;
        while (m_records.Next(r))
        {
            if (r.Type == "ENDSEC" || r.Type == "EOF")
                break;
            if (r.Type == "TABLE")
            {
                const std::string name = r.Find(2);
                const CadObjectType controlType = ControlTypeOfTable(name);
                if (controlType == CadObjectType::INVALID)
                {
                    NotifyOnce("table:" + name, NotificationType::Warning, "跳过未知的表 " + name);
                    table = nullptr;
                    continue;
                }
                const std::size_t index = AddItem(std::make_unique<CadTable>(controlType, StaticTableName(name)));
                ReadItem& item = m_items[index];
                ParseState state;
                for (std::size_t i = 0; i < r.Groups.size(); ++i)
                {
                    const int code = r.Groups[i].Code;
                    if (code == 100)
                        state.SeenSubclass = true;
                    else if (code == 1001)
                    {
                        ReadXData(item, r, i);
                        break;
                    }
                    else if (code == 5 || code == 105 || code == 330 || code == 102)
                        HandleCommon(item, r, i, state);
                    // 70 表项个数、DIMSTYLE 表的 71/340：由实际表项决定
                }
                m_tables.push_back({ name, index, {} });
                table = &m_tables.back();
                continue;
            }
            if (r.Type == "ENDTAB")
            {
                table = nullptr;
                continue;
            }
            if (table == nullptr)
                continue;
            if (auto entry = ReadTableEntry(table->Name, r))
                table->Entries.push_back(*entry);
        }
    }

    std::optional<std::size_t> Reader::ReadTableEntry(const std::string& tableName, const Record& record)
    {
        std::unique_ptr<CadObject> object;
        if (tableName == "LAYER") object = std::make_unique<Layer>();
        else if (tableName == "LTYPE") object = std::make_unique<LineType>();
        else if (tableName == "STYLE") object = std::make_unique<TextStyle>();
        else if (tableName == "DIMSTYLE") object = std::make_unique<DimensionStyle>();
        else if (tableName == "BLOCK_RECORD") object = std::make_unique<BlockRecord>();
        else if (tableName == "APPID") object = std::make_unique<AppId>();
        else if (tableName == "UCS") object = std::make_unique<UCS>();
        else if (tableName == "VIEW") object = std::make_unique<View>();
        else if (tableName == "VPORT") object = std::make_unique<VPort>();
        else
            return std::nullopt;

        ApplyOmittedDefaults(*object);
        const std::size_t index = AddItem(std::move(object));
        ReadItem& item = m_items[index];
        CadObject* obj = item.Object.get();
        bool dimStyleFlagsRead = false;

        Process(item, record, [&](const DxfGroup& g, std::size_t& i, const ParseState& state) -> bool {
            if (auto* layer = dynamic_cast<Layer*>(obj))
            {
                // 颜色为负表示图层关闭
                if (g.Code == 62)
                {
                    std::int64_t index = g.Value.AsInt();
                    if (index < 0)
                    {
                        layer->IsOn = false;
                        index = -index;
                    }
                    if (index > 0 && index < 256)
                        layer->Color = Color(static_cast<std::int16_t>(index));
                    return true;
                }
                return false;
            }
            if (auto* lineType = dynamic_cast<LineType*>(obj))
            {
                // 49 开始一个分段，其后的 74/75/340/46/50/44/45/9 属于该分段
                if (g.Code == 49)
                {
                    LineTypeSegment segment;
                    segment.Length = g.Value.AsDouble();
                    while (i + 1 < record.Groups.size())
                    {
                        const DxfGroup& s = record.Groups[i + 1];
                        if (s.Code == 49 || s.Code == 100 || s.Code == 1001 || s.Code == 0)
                            break;
                        switch (s.Code)
                        {
                        case 74: segment.Flags = static_cast<LineTypeShapeFlags>(s.Value.AsInt()); break;
                        case 75: segment.ShapeNumber = static_cast<std::int16_t>(s.Value.AsInt()); break;
                        case 340: segment.StyleHandle = s.Value.AsHandle(); break;
                        case 46: segment.Scale = s.Value.AsDouble(); break;
                        case 50: segment.Rotation = s.Value.AsDouble() * kDegToRad; break;
                        case 44: segment.Offset.X = s.Value.AsDouble(); break;
                        case 45: segment.Offset.Y = s.Value.AsDouble(); break;
                        case 9: segment.Text = s.Value.AsString(); break;
                        default: break;
                        }
                        ++i;
                    }
                    lineType->Segments.push_back(std::move(segment));
                    return true;
                }
                return g.Code == 73 || g.Code == 40;     // 段数、总长：由分段决定
            }
            if (auto* record_ = dynamic_cast<BlockRecord*>(obj))
            {
                if (g.Code == 70 && state.Subclass == "AcDbBlockTableRecord")
                {
                    record_->Units = static_cast<UnitsType>(g.Value.AsInt());
                    return true;
                }
                return false;
            }
            if (auto* dimStyle = dynamic_cast<DimensionStyle*>(obj))
            {
                switch (g.Code)
                {
                case 70:
                    // 第一个 70 是表项标志，之后的 70 是文字背景色（ACadSharp 同样处理）
                    if (!dimStyleFlagsRead)
                    {
                        dimStyleFlagsRead = true;
                        dimStyle->Flags = static_cast<StandardFlags>(g.Value.AsInt());
                    }
                    else if (g.Value.AsInt() >= 0)
                        dimStyle->TextBackgroundColor = Color(static_cast<std::int16_t>(g.Value.AsInt()));
                    return true;
                case 5:
                case 6:
                case 7:
                    // R12 的 DIMBLK / DIMBLK1 / DIMBLK2 块名：R13 起改用 342～344 句柄
                    return state.SeenSubclass || m_version <= CadVersion::AC1009;
                default:
                    return false;
                }
            }
            return false;
        });
        return index;
    }

    // ── OBJECTS ────────────────────────────────────────────────────

    void Reader::ReadObjects()
    {
        Record r;
        while (m_records.Next(r))
        {
            if (r.Type == "ENDSEC" || r.Type == "EOF")
                break;
            if (auto index = ReadObject(r))
                m_objects.push_back(*index);
        }
    }

    std::optional<std::size_t> Reader::ReadObject(const Record& record)
    {
        auto it = m_classByDxfName.find(record.Type);
        // 多重引线的注释比例上下文数据只建模了嵌在 MULTILEADER 中的部分，单独的对象原样保留
        if (it == m_classByDxfName.end() || it->second == nullptr || it->second->Create == nullptr
            || record.Type == "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS")
        {
            // 未建模的对象原样保留
            auto unknown = std::make_unique<UnknownObject>();
            UnknownObject* object = unknown.get();
            const std::size_t index = AddItem(std::move(unknown));
            object->Raw.DxfName = record.Type;
            ReadUnknownGroups(m_items[index], record, object->Raw, false);
            ++m_preserved[record.Type];
            return index;
        }

        const std::size_t index = AddItem(it->second->Create());
        ReadItem& item = m_items[index];
        CadObject* obj = item.Object.get();

        if (auto* dict = dynamic_cast<CadDictionary*>(obj))
        {
            Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState& state) -> bool {
                if (!state.SeenSubclass)
                    return false;
                switch (g.Code)
                {
                case 3:
                    dict->EntryNames.push_back(g.Value.AsString());
                    dict->EntryHandles.push_back(kNullHandle);
                    return true;
                case 350:
                case 360:
                    if (!dict->EntryHandles.empty())
                        dict->EntryHandles.back() = g.Value.AsHandle();
                    return true;
                default:
                    return false;
                }
            });
            return index;
        }

        // 标注关联：每个点引用以 1 AcDbOsnapPointRef 开头，按 90 中置位的顺序（1、2、4、8）对应第一～第四个
        if (auto* assoc = dynamic_cast<DimensionAssociation*>(obj))
        {
            DimensionAssociationOsnapPointRef* ref = nullptr;
            int bit = -1;
            Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState& state) -> bool {
                if (!state.SeenSubclass)
                    return false;
                if (g.Code == 1)
                {
                    const int flags = static_cast<int>(assoc->AssociativityFlags);
                    for (++bit; bit < 4 && (flags & (1 << bit)) == 0; ++bit)
                    {
                    }
                    ref = bit < 4 ? &PointRefAt(*assoc, bit) : nullptr;
                    return true;
                }
                if (ref == nullptr)
                    return false;
                switch (g.Code)
                {
                case 72: ref->ObjectOsnapType = static_cast<ObjectOsnapType>(g.Value.AsInt()); return true;
                case 331: ref->GeometryHandle = g.Value.AsHandle(); return true;
                case 73: ref->SubentType = static_cast<SubentType>(g.Value.AsInt()); return true;
                case 91: ref->GsMarker = static_cast<std::int32_t>(g.Value.AsInt()); return true;
                case 74: ref->IntersectionSubType = static_cast<SubentType>(g.Value.AsInt()); return true;
                case 92: ref->IntersectionGsMarker = static_cast<std::int32_t>(g.Value.AsInt()); return true;
                case 40: ref->GeometryParameter = g.Value.AsDouble(); return true;
                case 10: ref->OsnapPoint.X = g.Value.AsDouble(); return true;
                case 20: ref->OsnapPoint.Y = g.Value.AsDouble(); return true;
                case 30: ref->OsnapPoint.Z = g.Value.AsDouble(); return true;
                case 75: ref->HasLastPointRef = g.Value.AsInt() != 0; return true;
                default: return true;   // 交点对象（332、302）等：未建模
                }
            });
            return index;
        }

        if (dynamic_cast<MultiLeaderStyle*>(obj) != nullptr)
        {
            ReadMultiLeaderStyle(item, record);
            return index;
        }

        if (auto* xrecord = dynamic_cast<XRecord*>(obj))
        {
            bool inData = false;
            Process(item, record, [&](const DxfGroup& g, std::size_t& i, const ParseState& state) -> bool {
                if (g.Code == 100)
                {
                    inData = state.Subclass == "AcDbXrecord";
                    return true;
                }
                if (!inData)
                    return false;
                if (g.Code == 280 && xrecord->Entries.empty())
                {
                    xrecord->CloningFlags = static_cast<DictionaryCloningFlags>(g.Value.AsInt());
                    return true;
                }
                XRecordEntry entry;
                entry.Code = static_cast<std::int16_t>(g.Code);
                if (GroupCodeTypeOf(g.Code) == GroupCodeType::Point3D && g.Code < 20)
                {
                    XYZ p{ g.Value.AsDouble(), 0, 0 };
                    if (i + 1 < record.Groups.size() && record.Groups[i + 1].Code == g.Code + 10)
                        p.Y = record.Groups[++i].Value.AsDouble();
                    if (i + 1 < record.Groups.size() && record.Groups[i + 1].Code == g.Code + 20)
                        p.Z = record.Groups[++i].Value.AsDouble();
                    entry.Value = DxfValue(p);
                }
                else
                {
                    entry.Value = g.Value;
                }
                xrecord->Entries.push_back(std::move(entry));
                return true;
            });
            return index;
        }

        if (auto* group = dynamic_cast<Group*>(obj))
        {
            // 340：组中的实体（元数据中没有，单独处理）
            Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState& state) -> bool {
                if (!state.SeenSubclass || g.Code != 340)
                    return false;
                group->Entities.push_back(g.Value.AsHandle());
                return true;
            });
            return index;
        }

        if (auto* sortents = dynamic_cast<SortEntitiesTable*>(obj))
        {
            Handle entity = kNullHandle;
            Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState& state) -> bool {
                if (!state.SeenSubclass)
                    return false;
                if (g.Code == 331)
                {
                    entity = g.Value.AsHandle();
                    return true;
                }
                if (g.Code == 5)
                {
                    sortents->Entries.push_back({ entity, g.Value.AsHandle() });
                    return true;
                }
                return false;
            });
            return index;
        }

        if (auto* style = dynamic_cast<MLineStyle*>(obj))
        {
            // 49 开始一个元素，其后的 62 颜色、6 线型名属于该元素
            Process(item, record, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 49:
                    style->Elements.emplace_back();
                    style->Elements.back().Offset = g.Value.AsDouble();
                    return true;
                case 62:
                case 420:
                    if (style->Elements.empty())
                        return false;
                    Assign(style->Elements.back().Color, g.Code, g.Value);
                    return true;
                case 6:
                    if (style->Elements.empty())
                        return false;
                    item.ElementLineTypes.emplace_back(style->Elements.size() - 1, g.Value.AsString());
                    return true;
                case 71:
                    return true;    // 元素个数
                default:
                    return false;
                }
            });
            return index;
        }

        if (dynamic_cast<Scale*>(obj) != nullptr)
        {
            Process(item, record, [](const DxfGroup& g, std::size_t&, const ParseState&) { return g.Code == 70; });
            return index;
        }

        ProcessGeneric(item, record);
        return index;
    }
}
