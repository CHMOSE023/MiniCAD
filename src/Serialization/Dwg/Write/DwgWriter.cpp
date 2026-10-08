// DWG 写入：流程、准备（补句柄、要写出的对象、类定义）、CLASSES 段、句柄段与其他小段
#include "Dwg/Write/DwgWriterImpl.h"
#include "Dwg/Write/DwgCompress.h"
#include <algorithm>
#include <cmath>
#include <deque>

namespace MiniDWG
{
    std::vector<std::uint8_t> WriteDwg(const CadDatabase& db, const DwgWriteOptions& options)
    {
        DwgWrite::Writer writer(db, options);
        return writer.Run();
    }
}

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

        // 段的哨兵
        constexpr std::uint8_t kClassesStart[16] = {
            0x8D, 0xA1, 0xC4, 0xB8, 0xC4, 0xA9, 0xF8, 0xC5, 0xC0, 0xDC, 0xF4, 0x5F, 0xE7, 0xCF, 0xB6, 0x8A,
        };
        constexpr std::uint8_t kClassesEnd[16] = {
            0x72, 0x5E, 0x3B, 0x47, 0x3B, 0x56, 0x07, 0x3A, 0x3F, 0x23, 0x0B, 0xA0, 0x18, 0x30, 0x49, 0x75,
        };
        constexpr std::uint8_t kPreviewStart[16] = {
            0x1F, 0x25, 0x6D, 0x07, 0xD4, 0x36, 0x28, 0x28, 0x9D, 0x57, 0xCA, 0x3F, 0x9D, 0x44, 0x10, 0x2B,
        };
        constexpr std::uint8_t kPreviewEnd[16] = {
            0xE0, 0xDA, 0x92, 0xF8, 0x2B, 0xC9, 0xD7, 0xD7, 0x62, 0xA8, 0x35, 0xC0, 0x62, 0xBB, 0xEF, 0xD4,
        };

        void PutLe(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        // 小段中的 UTF-16 字符串：RS 字符数（含结尾 0）+ UTF-16LE + 0
        void PutUtf16(std::vector<std::uint8_t>& out, std::string_view text)
        {
            const std::vector<char16_t> units = Utf8ToUtf16(text);
            PutLe(out, units.size() + 1, 2);
            for (char16_t c : units)
                PutLe(out, c, 2);
            PutLe(out, 0, 2);
        }

        // 儒略日 → RL 日 + RL 毫秒
        void PutJulian(std::vector<std::uint8_t>& out, double julian)
        {
            double whole = std::floor(julian);
            long long ms = std::llround((julian - whole) * 86400000.0);
            if (ms >= 86400000)
            {
                whole += 1.0;
                ms -= 86400000;
            }
            PutLe(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(whole)), 4);
            PutLe(out, static_cast<std::uint32_t>(ms), 4);
        }
    }

    std::uint8_t LineWeightToIndex(LineWeightType weight)
    {
        static constexpr std::int16_t kWeights[] = {
            0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211,
        };
        switch (weight)
        {
        case LineWeightType::ByLayer: return 29;
        case LineWeightType::ByBlock: return 30;
        case LineWeightType::Default: return 31;
        default:
            break;
        }
        for (std::size_t i = 0; i < std::size(kWeights); ++i)
        {
            if (kWeights[i] == static_cast<std::int16_t>(weight))
                return static_cast<std::uint8_t>(i);
        }
        return 31;
    }

    Writer::Writer(const CadDatabase& db, const DwgWriteOptions& options) : m_db(db), m_options(options)
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

        // 对象段先写：句柄段与 R2000 的对象位置依赖它
        WriteObjects();

        std::vector<std::uint8_t> file = R2004Plus() ? AssembleAC18() : AssembleAC15();
        return file;
    }

    // ── 准备 ───────────────────────────────────────────────────────

    void Writer::Prepare()
    {
        m_version = m_options.Version != CadVersion::Unknown ? m_options.Version : m_db.GetVersion();
        if (m_version < CadVersion::AC1015)
        {
            Notify(NotificationType::Info,
                   std::string(VersionDisplayName(m_version)) + " DWG 不支持写出，改写为 R2000（AC1015）");
            m_version = CadVersion::AC1015;
        }
        else if (m_version == CadVersion::AC1021)
        {
            Notify(NotificationType::Info, "R2007（AC1021）DWG 不支持写出，改写为 R2010（AC1024）");
            m_version = CadVersion::AC1024;
        }

        // 维护版本与 AutoCAD 2018 另存为各版本时写的一致
        switch (m_version)
        {
        case CadVersion::AC1015: m_maintenanceVersion = 15; break;
        case CadVersion::AC1018: m_maintenanceVersion = 104; break;
        case CadVersion::AC1024: m_maintenanceVersion = 226; break;
        case CadVersion::AC1027: m_maintenanceVersion = 125; break;
        default: m_maintenanceVersion = 0; break;
        }

        // 代码页：R2007 起文本为 UTF-16，代码页只用于文件头；之前按 $DWGCODEPAGE，不支持的按 ANSI_1252
        m_codePage = Codec::CodePageFromName(m_db.Header.CodePage);
        if (m_codePage == Codec::CodePage::Unknown || !Codec::IsSupported(m_codePage) || m_codePage == Codec::CodePage::Utf8)
        {
            if (!m_db.Header.CodePage.empty() && !EqualsIgnoreCase(m_db.Header.CodePage, "ANSI_1252") && !R2007Plus())
                Notify(NotificationType::Warning, "不支持的代码页 " + m_db.Header.CodePage + "，按 ANSI_1252 写出");
            m_codePage = Codec::CodePage::Windows1252;
        }
        m_codePageIndex = Codec::DwgIndexFromCodePage(m_codePage);

        // 新句柄从现有最大句柄之后开始
        Handle maxHandle = 0;
        for (const auto& [handle, object] : m_db.Objects())
            maxHandle = std::max(maxHandle, handle);
        m_nextHandle = std::max({ m_db.GetHandleSeed(), static_cast<Handle>(m_db.Header.HandleSeed), maxHandle + 1 });

        // 符号表（写出顺序与 ACadSharp 一致：标注样式表在最后）
        const CadTable* tables[] = {
            m_db.BlockRecords(), m_db.Layers(), m_db.TextStyles(), m_db.LineTypes(), m_db.Views(),
            m_db.UCSs(), m_db.VPorts(), m_db.AppIds(), m_db.DimensionStyles(),
        };
        for (const CadTable* table : tables)
        {
            if (table == nullptr)
                continue;
            m_tables.push_back(table);
            m_written.insert(table->ObjectHandle);
            for (Handle h : table->Entries)
            {
                const auto* entry = m_db.FindAs<TableEntry>(h);
                if (entry != nullptr && entry->GetDxfName() == table->GetEntryDxfName())
                    m_written.insert(h);
            }
        }
        if (m_tables.size() != std::size(tables) || m_db.ModelSpace() == nullptr || m_db.PaperSpace() == nullptr
            || m_db.RootDictionary() == nullptr)
            Notify(NotificationType::Warning, "数据库缺少符号表、模型空间/图纸空间或根字典，写出的文件可能无法打开（先调用 CreateDefaults）");

        if (R2004Pre())
        {
            m_vEntityControl = Allocate();
            m_written.insert(m_vEntityControl);
        }

        // 图像 → 图像定义反应器（读取时没有记录反应器句柄的情况）
        for (const auto& [handle, object] : m_db.Objects())
        {
            if (const auto* reactor = dynamic_cast<const ImageDefinitionReactor*>(object.get()))
                m_imageReactors.emplace(reactor->ImageHandle, handle);
        }

        // 块：缺少的 BLOCK / ENDBLK 补上，再规划块中的实体
        if (const CadTable* records = m_db.BlockRecords())
        {
            const Handle layer0 = TableEntryByName(m_db.Layers(), "0");
            const Handle byLayer = TableEntryByName(m_db.LineTypes(), "ByLayer");
            for (Handle h : records->Entries)
            {
                const auto* record = m_db.FindAs<BlockRecord>(h);
                if (record == nullptr || !Written(h))
                    continue;
                if (m_db.FindAs<Block>(record->BlockEntityHandle) == nullptr)
                {
                    auto block = std::make_unique<Block>();
                    block->ObjectHandle = Allocate();
                    block->OwnerHandle = h;
                    block->Name = record->Name;
                    block->LayerHandle = layer0;
                    block->LineTypeHandle = byLayer;
                    m_blockBegins[h] = block->ObjectHandle;
                    m_synthetic[block->ObjectHandle] = std::move(block);
                }
                if (m_db.FindAs<BlockEnd>(record->BlockEndHandle) == nullptr)
                {
                    auto end = std::make_unique<BlockEnd>();
                    end->ObjectHandle = Allocate();
                    end->OwnerHandle = h;
                    end->LayerHandle = layer0;
                    end->LineTypeHandle = byLayer;
                    m_blockEnds[h] = end->ObjectHandle;
                    m_synthetic[end->ObjectHandle] = std::move(end);
                }
                m_written.insert(BlockBeginOf(*record));
                m_written.insert(BlockEndOf(*record));
                PlanEntities(*record);
            }
        }

        // R2000：当前布局中的视口各有一个视口实体头表项
        if (R2004Pre() && m_db.PaperSpace() != nullptr)
        {
            for (Handle e : m_blockEntities[m_db.PaperSpace()->ObjectHandle])
            {
                if (m_db.FindAs<Viewport>(e) == nullptr)
                    continue;
                const Handle vx = Allocate();
                m_written.insert(vx);
                m_vxEntries.emplace_back(vx, e);
                m_viewportVx[e] = vx;
            }
        }

        PlanObjects();

        // 数据存储段（三维实体等的 ACIS 数据）原样写回同一版本
        for (const RawSection& raw : m_db.RawSections)
        {
            if (raw.Name == "AcDb:AcDsPrototype_1b" && raw.Version == m_version && !raw.Data.empty())
                m_writeDataStore = true;
        }

        // 类定义：保留已有的，补上写出的对象需要的；类号按顺序从 500 重排，实例个数按写出的对象重新统计
        std::map<std::string, int> counts;
        for (Handle h : m_written)
        {
            if (const CadObject* object = Find(h))
                ++counts[ToUpper(object->GetDxfName())];
        }
        m_classes = m_db.Classes;
        for (const ClassTemplate& t : kClassTemplates)
        {
            const bool needed = counts.count(std::string(t.DxfName)) != 0;
            const bool known = std::any_of(m_classes.begin(), m_classes.end(),
                                           [&](const DxfClass& c) { return EqualsIgnoreCase(c.DxfName, t.DxfName); });
            if (!needed || known)
                continue;
            DxfClass c;
            c.DxfName = std::string(t.DxfName);
            c.CppClassName = std::string(t.CppClassName);
            c.ApplicationName = std::string(t.ApplicationName);
            c.ProxyFlags = t.ProxyFlags;
            c.IsAnEntity = t.IsAnEntity;
            c.ItemClassId = t.IsAnEntity ? 0x1F2 : 0x1F3;
            c.DwgVersion = 0;
            c.MaintenanceVersion = 0;
            m_classes.push_back(std::move(c));
        }
        for (std::size_t i = 0; i < m_classes.size(); ++i)
        {
            DxfClass& c = m_classes[i];
            auto it = counts.find(ToUpper(c.DxfName));
            c.InstanceCount = it != counts.end() ? it->second : 0;
            c.ClassNumber = static_cast<std::int16_t>(500 + i);
            c.ItemClassId = c.IsAnEntity ? 0x1F2 : 0x1F3;
            m_classNumbers.emplace(ToUpper(c.DxfName), c.ClassNumber);
        }
    }

    bool Writer::CanWriteRaw(const CadObject& object)
    {
        const RawObjectData* raw = RawDataOf(object);
        if (raw == nullptr || CanWriteRawDwg(*raw, m_version))
            return true;
        NotifyOnce("raw:" + raw->DxfName, NotificationType::Info,
                   "未建模的 " + raw->DxfName + " 只能原样写回同一版本的 DWG，未写出");
        return false;
    }

    bool Writer::IsEntitySupported(const Entity& entity)
    {
        if (const auto* table = dynamic_cast<const TableEntity*>(&entity);
            table != nullptr && !CanWriteRawDwg(table->Raw, m_version))
        {
            if (m_db.FindAs<BlockRecord>(table->BlockHandle) == nullptr)
                return false;
            NotifyOnce("table-insert", NotificationType::Info,
                       "表格（ACAD_TABLE）只能原样写回同一版本的 DWG，写为引用其匿名块的块参照");
            m_tableInserts[entity.ObjectHandle] = MakeTableInsert(*table);
            return true;
        }
        if (!CanWriteRaw(entity))
            return false;
        if (dynamic_cast<const Shape*>(&entity) != nullptr)
        {
            // 形号没有建模（与 DXF 写出一致）
            NotifyOnce("entity:SHAPE", NotificationType::Info, "SHAPE 实体未写出");
            return false;
        }
        if (const auto* insert = dynamic_cast<const Insert*>(&entity);
            insert != nullptr && m_db.FindAs<BlockRecord>(insert->BlockHandle) == nullptr)
        {
            NotifyOnce("insert:" + std::to_string(entity.ObjectHandle), NotificationType::Warning,
                       "块参照 " + DxfValue(HandleValue{ entity.ObjectHandle }).AsString() + " 引用的块不存在，未写出");
            return false;
        }
        return true;
    }

    void Writer::PlanEntities(const BlockRecord& record)
    {
        std::vector<Handle>& list = m_blockEntities[record.ObjectHandle];
        for (Handle e : record.Entities)
        {
            const auto* entity = m_db.FindAs<Entity>(e);
            // BLOCK / ENDBLK / SEQEND / VERTEX / ATTRIB 由块记录或父实体写出，不能直接属于块
            if (entity == nullptr || Written(e) || dynamic_cast<const Block*>(entity) || dynamic_cast<const BlockEnd*>(entity)
                || dynamic_cast<const Seqend*>(entity) || dynamic_cast<const Vertex*>(entity)
                || dynamic_cast<const AttributeEntity*>(entity))
                continue;
            if (!IsEntitySupported(*entity))
                continue;
            m_written.insert(e);
            list.push_back(e);
            PlanEntity(e);
        }
    }

    // 子实体：多段线的顶点、块参照的属性，及其 SEQEND
    void Writer::PlanEntity(Handle h)
    {
        const CadObject* object = Find(h);
        std::vector<Handle> children;
        Handle seqend = kNullHandle;
        bool needSeqend = false;
        if (const auto* pl = dynamic_cast<const Polyline*>(object))
        {
            // 顶点类型必须与多段线一致；多面网格的面在顶点之后
            auto accept = [&](const CadObject* vertex) {
                if (dynamic_cast<const PolyfaceMesh*>(pl) != nullptr)
                    return dynamic_cast<const VertexFaceMesh*>(vertex) != nullptr;
                if (dynamic_cast<const PolygonMesh*>(pl) != nullptr)
                    return dynamic_cast<const PolygonMeshVertex*>(vertex) != nullptr;
                if (dynamic_cast<const Polyline3D*>(pl) != nullptr)
                    return dynamic_cast<const Vertex3D*>(vertex) != nullptr;
                return dynamic_cast<const Vertex2D*>(vertex) != nullptr;
            };
            for (Handle v : pl->Vertices)
            {
                if (accept(m_db.Find(v)) && !Written(v))
                    children.push_back(v);
            }
            if (const auto* mesh = dynamic_cast<const PolyfaceMesh*>(pl))
            {
                for (Handle f : mesh->Faces)
                {
                    if (m_db.FindAs<VertexFaceRecord>(f) != nullptr && !Written(f))
                        children.push_back(f);
                }
            }
            seqend = pl->SeqendHandle;
            needSeqend = true;
        }
        else if (const auto* insert = dynamic_cast<const Insert*>(object))
        {
            for (Handle a : insert->Attributes)
            {
                if (m_db.FindAs<AttributeEntity>(a) != nullptr && !Written(a))
                    children.push_back(a);
            }
            seqend = insert->SeqendHandle;
            needSeqend = !children.empty();
            m_inserts[insert->BlockHandle].push_back(h);
        }
        else
        {
            return;
        }

        for (Handle c : children)
            m_written.insert(c);
        m_children[h] = children;
        if (!needSeqend)
            return;
        if (m_db.FindAs<Seqend>(seqend) == nullptr || Written(seqend))
        {
            const auto* parent = static_cast<const Entity*>(object);
            auto s = std::make_unique<Seqend>();
            s->ObjectHandle = Allocate();
            s->OwnerHandle = h;
            s->LayerHandle = parent->LayerHandle;
            s->LineTypeHandle = TableEntryByName(m_db.LineTypes(), "ByLayer");
            m_seqends[h] = s->ObjectHandle;
            m_synthetic[s->ObjectHandle] = std::move(s);
        }
        m_written.insert(SeqendOf(*static_cast<const Entity*>(object)));
    }

    // 非图形对象：从根字典开始遍历字典条目，加上所有写出对象的扩展字典与图像定义反应器
    void Writer::PlanObjects()
    {
        std::deque<Handle> queue;
        if (const CadDictionary* root = m_db.RootDictionary())
            queue.push_back(root->ObjectHandle);
        std::vector<Handle> already(m_written.begin(), m_written.end());
        std::sort(already.begin(), already.end());
        for (Handle h : already)
        {
            const CadObject* object = Find(h);
            if (object == nullptr)
                continue;
            if (object->XDictionaryHandle != kNullHandle)
                queue.push_back(object->XDictionaryHandle);
            // 未建模实体引用的对象（数据原样写出，引用必须存在）
            if (const RawObjectData* raw = RawDataOf(*object))
                queue.insert(queue.end(), raw->Dwg.References.begin(), raw->Dwg.References.end());
            if (const auto* image = dynamic_cast<const CadWipeoutBase*>(object))
            {
                Handle reactor = image->DefinitionReactorHandle;
                if (m_db.FindAs<ImageDefinitionReactor>(reactor) == nullptr)
                {
                    auto it = m_imageReactors.find(image->ObjectHandle);
                    reactor = it != m_imageReactors.end() ? it->second : kNullHandle;
                }
                if (reactor != kNullHandle)
                    queue.push_back(reactor);
            }
        }

        while (!queue.empty())
        {
            const Handle h = queue.front();
            queue.pop_front();
            if (Written(h))
                continue;
            const CadObject* object = m_db.Find(h);
            if (object == nullptr || dynamic_cast<const Entity*>(object) != nullptr
                || dynamic_cast<const TableEntry*>(object) != nullptr || dynamic_cast<const CadTable*>(object) != nullptr)
                continue;
            if (!CanWriteRaw(*object))
                continue;
            m_written.insert(h);
            m_objectOrder.push_back(h);
            if (object->XDictionaryHandle != kNullHandle)
                queue.push_back(object->XDictionaryHandle);
            if (const RawObjectData* raw = RawDataOf(*object))
                queue.insert(queue.end(), raw->Dwg.References.begin(), raw->Dwg.References.end());
            if (const auto* dict = dynamic_cast<const CadDictionary*>(object))
            {
                for (Handle e : dict->EntryHandles)
                    queue.push_back(e);
            }
            // XRECORD 中 350～369 是拥有关系（如 ACAD_TABLE 的 TABLECONTENT），被拥有的对象随之写出
            if (const auto* record = dynamic_cast<const XRecord*>(object))
            {
                for (const XRecordEntry& e : record->Entries)
                {
                    if (e.Code >= 350 && e.Code <= 369)
                        queue.push_back(e.Value.AsHandle());
                }
            }
        }

        // 没有被引用、但所有者会写出的对象（如 ACAD_TABLE 的 TABLECONTENT）
        for (bool added = true; added;)
        {
            added = false;
            for (const auto& [handle, object] : m_db.Objects())
            {
                if (Written(handle) || dynamic_cast<const NonGraphicalObject*>(object.get()) == nullptr
                    || !Written(object->OwnerHandle) || !CanWriteRaw(*object))
                    continue;
                queue.push_back(handle);
                added = true;
            }
            while (!queue.empty())
            {
                const Handle h = queue.front();
                queue.pop_front();
                const CadObject* object = m_db.Find(h);
                if (Written(h) || object == nullptr || dynamic_cast<const NonGraphicalObject*>(object) == nullptr
                    || !CanWriteRaw(*object))
                    continue;
                m_written.insert(h);
                m_objectOrder.push_back(h);
                if (object->XDictionaryHandle != kNullHandle)
                    queue.push_back(object->XDictionaryHandle);
                if (const RawObjectData* raw = RawDataOf(*object))
                    queue.insert(queue.end(), raw->Dwg.References.begin(), raw->Dwg.References.end());
                if (const auto* dict = dynamic_cast<const CadDictionary*>(object))
                    queue.insert(queue.end(), dict->EntryHandles.begin(), dict->EntryHandles.end());
            }
        }

        int orphans = 0;
        for (const auto& [handle, object] : m_db.Objects())
        {
            if (dynamic_cast<const NonGraphicalObject*>(object.get()) != nullptr && !Written(handle))
                ++orphans;
        }
        if (orphans > 0)
            Notify(NotificationType::Info, std::to_string(orphans) + " 个对象不在根字典之下（也没有被引用），未写出");
    }

    // ── 引用 ───────────────────────────────────────────────────────

    const CadObject* Writer::Find(Handle h) const
    {
        if (auto it = m_tableInserts.find(h); it != m_tableInserts.end())
            return it->second.get();
        if (const CadObject* object = m_db.Find(h))
            return object;
        auto it = m_synthetic.find(h);
        return it != m_synthetic.end() ? it->second.get() : nullptr;
    }

    Handle Writer::TableEntryByName(const CadTable* table, std::string_view name) const
    {
        if (table == nullptr || name.empty())
            return kNullHandle;
        const TableEntry* entry = m_db.FindTableEntry(table, name);
        return entry != nullptr ? entry->ObjectHandle : kNullHandle;
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

    const std::vector<Handle>& Writer::ChildrenOf(Handle parent) const
    {
        static const std::vector<Handle> kEmpty;
        auto it = m_children.find(parent);
        return it != m_children.end() ? it->second : kEmpty;
    }

    // ── 段的公共部分 ───────────────────────────────────────────────

    std::vector<std::uint8_t> Writer::MergeSectionStreams(DwgBitWriter& main, DwgBitWriter& text, DwgBitWriter& refs,
                                                          std::uint64_t sizePos)
    {
        if (!R2007Plus())
        {
            // 句柄与字符串都在主流中
            main.AlignToByte();
            return main.Take();
        }
        // R2007 起：数据、字符串流、字符串流大小与标志位、句柄流
        const std::uint64_t textBits = text.PositionInBits();
        DwgBitWriter all = main;
        if (textBits > 0)
        {
            all.AppendBits(text, textBits);
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
        // RL：从 RL 本身到标志位（含）的位数
        all.PatchRawLong(sizePos, static_cast<std::uint32_t>(all.PositionInBits() - sizePos));
        all.AppendBits(refs, refs.PositionInBits());
        all.AlignToByte();
        return all.Take();
    }

    std::vector<std::uint8_t> Writer::WrapSection(const std::uint8_t (&start)[16], const std::uint8_t (&end)[16],
                                                  const std::vector<std::uint8_t>& body, bool extraSize)
    {
        std::vector<std::uint8_t> out(start, start + 16);
        const std::size_t crcStart = out.size();
        PutLe(out, body.size(), 4);
        if (extraSize)
            PutLe(out, 0, 4);
        out.insert(out.end(), body.begin(), body.end());
        const std::uint16_t crc = DwgCodec::Crc16(0xC0C1, std::span<const std::uint8_t>(out).subspan(crcStart));
        PutLe(out, crc, 2);
        out.insert(out.end(), end, end + 16);
        return out;
    }

    // ── CLASSES ────────────────────────────────────────────────────

    std::vector<std::uint8_t> Writer::WriteClassesSection()
    {
        DwgBitWriter main(m_version, m_codePage);
        DwgBitWriter text(m_version, m_codePage);
        DwgBitWriter refs(m_version, m_codePage);
        DwgBitWriter& s = R2007Plus() ? text : main;

        std::uint64_t sizePos = 0;
        if (R2007Plus())
        {
            sizePos = main.PositionInBits();
            main.WriteRawLong(0);
        }
        if (R2004Plus())
        {
            std::int16_t maxClass = 0;
            for (const DxfClass& c : m_classes)
                maxClass = std::max(maxClass, c.ClassNumber);
            main.WriteBitShort(maxClass);
            main.WriteByte(0);
            main.WriteByte(0);
            main.WriteBit(true);
        }
        for (const DxfClass& c : m_classes)
        {
            main.WriteBitShort(c.ClassNumber);
            main.WriteBitShort(static_cast<std::int16_t>(c.ProxyFlags));
            s.WriteVariableText(c.ApplicationName);
            s.WriteVariableText(c.CppClassName);
            s.WriteVariableText(c.DxfName);
            main.WriteBit(c.WasZombie);
            main.WriteBitShort(c.ItemClassId);
            if (R2004Plus())
            {
                main.WriteBitLong(c.InstanceCount);
                main.WriteBitLong(c.DwgVersion);
                main.WriteBitLong(c.MaintenanceVersion);
                main.WriteBitLong(0);
                main.WriteBitLong(0);
            }
        }
        const std::vector<std::uint8_t> body = MergeSectionStreams(main, text, refs, sizePos);
        const bool extraSize = (m_version >= CadVersion::AC1024 && m_maintenanceVersion > 3) || m_version > CadVersion::AC1027;
        std::vector<std::uint8_t> out = WrapSection(kClassesStart, kClassesEnd, body, extraSize);
        if (R2004Plus())
            PutLe(out, 0, 8);   // R2004 起末尾 8 个未知字节，AutoCAD 写 0
        return out;
    }

    // ── 句柄段：句柄 → 对象位置 ────────────────────────────────────

    // 每节：RS（大端）节大小（含这两字节，不含 CRC），节内为句柄增量（MC）与位置增量（有符号 MC），
    // 最后是 RS（大端）CRC；每节不超过 2032 字节，最后是只有大小（2）的空节
    std::vector<std::uint8_t> Writer::WriteHandlesSection(std::int64_t baseOffset)
    {
        std::vector<std::uint8_t> out;
        std::size_t chunkStart = 0;
        auto beginChunk = [&]() {
            chunkStart = out.size();
            out.push_back(0);
            out.push_back(0);
        };
        auto endChunk = [&]() {
            const std::size_t size = out.size() - chunkStart;
            out[chunkStart] = static_cast<std::uint8_t>(size >> 8);
            out[chunkStart + 1] = static_cast<std::uint8_t>(size);
            const std::uint16_t crc =
                DwgCodec::Crc16(0xC0C1, std::span<const std::uint8_t>(out).subspan(chunkStart, size));
            out.push_back(static_cast<std::uint8_t>(crc >> 8));
            out.push_back(static_cast<std::uint8_t>(crc));
        };

        beginChunk();
        Handle lastHandle = 0;
        std::int64_t lastLocation = 0;
        for (const auto& [handle, location] : m_map)
        {
            const std::int64_t loc = location + baseOffset;
            DwgBitWriter entry;
            entry.WriteModularChar(handle - lastHandle);
            entry.WriteSignedModularChar(loc - lastLocation);
            if (out.size() - chunkStart + entry.Data().size() > 2032)
            {
                endChunk();
                beginChunk();
                lastHandle = 0;
                lastLocation = 0;
                entry.Clear();
                entry.WriteModularChar(handle);
                entry.WriteSignedModularChar(loc);
            }
            out.insert(out.end(), entry.Data().begin(), entry.Data().end());
            lastHandle = handle;
            lastLocation = loc;
        }
        endChunk();
        beginChunk();
        endChunk();
        return out;
    }

    // ── 其他小段 ───────────────────────────────────────────────────

    // AcDb:AuxHeader（字段含义见 ODA 规范，取值与 ACadSharp 一致）
    std::vector<std::uint8_t> Writer::WriteAuxHeader()
    {
        static constexpr int kVersionCodes[] = { 0, 0, 0, 21, 23, 25, 27, 29, 31, 33 };   // 按 CadVersion 顺序
        const int code = kVersionCodes[static_cast<int>(m_version)];
        const bool longMaintenance = m_version > CadVersion::AC1027;
        std::vector<std::uint8_t> out = { 0xFF, 0x77, 0x01 };
        auto maintenance = [&]() { PutLe(out, static_cast<std::uint32_t>(m_maintenanceVersion), longMaintenance ? 4 : 2); };
        PutLe(out, code, 2);
        maintenance();
        PutLe(out, 1, 4);                       // 保存次数
        PutLe(out, 0xFFFFFFFFu, 4);
        PutLe(out, 1, 2);
        PutLe(out, 0, 2);
        PutLe(out, 0, 4);
        PutLe(out, code, 2);
        maintenance();
        PutLe(out, code, 2);
        maintenance();
        PutLe(out, 0x0005, 2);
        PutLe(out, 0x0893, 2);
        PutLe(out, 0x0005, 2);
        PutLe(out, 0x0893, 2);
        PutLe(out, 0x0000, 2);
        PutLe(out, 0x0001, 2);
        for (int i = 0; i < 5; ++i)
            PutLe(out, 0, 4);
        PutJulian(out, m_db.Header.CreateDateTime.Value);
        PutJulian(out, m_db.Header.UpdateDateTime.Value);
        const Handle seed = m_nextHandle;
        PutLe(out, seed <= 0x7FFFFFFF ? static_cast<std::uint32_t>(seed) : 0xFFFFFFFFu, 4);
        PutLe(out, 0, 4);                       // 教育版打印戳记
        PutLe(out, 0, 2);
        PutLe(out, 1, 2);
        for (int i = 0; i < 3; ++i)
            PutLe(out, 0, 4);
        PutLe(out, 1, 4);
        for (int i = 0; i < 4; ++i)
            PutLe(out, 0, 4);
        if (R2018Plus())
        {
            for (int i = 0; i < 3; ++i)
                PutLe(out, 0, 2);
        }
        return out;
    }

    // AcDb:Preview：开始哨兵、RL 总大小、RC 项数，每项 RC 代码 + RL 位置 + RL 大小，之后是头数据与图像。
    // base 为段开头的位置：R2000 是文件中的绝对位置，R2004 起 AutoCAD 写段内位置加 0x1C0
    std::vector<std::uint8_t> Writer::WritePreview(std::int64_t base)
    {
        const CadPreview& preview = m_db.Preview;
        std::vector<std::uint8_t> out(kPreviewStart, kPreviewStart + 16);
        if (preview.Type == CadPreview::ImageType::None || preview.Image.empty())
        {
            PutLe(out, 1, 4);       // 图像区总大小
            out.push_back(0);       // 图像个数
        }
        else
        {
            // 头数据的内容未知，没有时与 AutoCAD 一样写 80 字节 0
            const std::vector<std::uint8_t> header = preview.Header.empty() ? std::vector<std::uint8_t>(80, 0) : preview.Header;
            PutLe(out, header.size() + preview.Image.size() + 19, 4);
            out.push_back(2);
            const std::int64_t headerPos = base + 16 + 4 + 1 + 9 + 9;
            out.push_back(1);
            PutLe(out, static_cast<std::uint32_t>(headerPos), 4);
            PutLe(out, header.size(), 4);
            out.push_back(static_cast<std::uint8_t>(preview.Type));
            PutLe(out, static_cast<std::uint32_t>(headerPos + static_cast<std::int64_t>(header.size())), 4);
            PutLe(out, preview.Image.size(), 4);
            out.insert(out.end(), header.begin(), header.end());
            out.insert(out.end(), preview.Image.begin(), preview.Image.end());
        }
        out.insert(out.end(), kPreviewEnd, kPreviewEnd + 16);
        return out;
    }

    // AcDb:SummaryInfo：标题、主题、作者 …… 都为空，日期取 TDCREATE、TDUPDATE
    std::vector<std::uint8_t> Writer::WriteSummaryInfo()
    {
        std::vector<std::uint8_t> out;
        auto text = [&](std::string_view s) {
            if (R2007Plus())
            {
                PutUtf16(out, s);
                return;
            }
            const std::string bytes = Codec::FromUtf8(s, m_codePage);
            PutLe(out, bytes.size() + 1, 2);
            out.insert(out.end(), bytes.begin(), bytes.end());
            out.push_back(0);
        };
        for (int i = 0; i < 5; ++i)     // 标题、主题、作者、关键字、注释
            text("");
        text(m_db.Header.LastSavedBy);
        text("");                       // 修订号
        text(m_db.Header.HyperLinkBase);
        PutLe(out, 0, 4);               // 总编辑时间
        PutLe(out, 0, 4);
        PutJulian(out, m_db.Header.CreateDateTime.Value);
        PutJulian(out, m_db.Header.UpdateDateTime.Value);
        PutLe(out, 0, 2);               // 自定义属性个数
        PutLe(out, 0, 4);
        PutLe(out, 0, 4);
        return out;
    }

    // AcDb:AppInfo（R2004 起；格式与 AutoCAD 2018 写的一致，内容为 MiniDWG）
    std::vector<std::uint8_t> Writer::WriteAppInfo()
    {
        std::vector<std::uint8_t> out;
        PutLe(out, 3, 4);
        PutUtf16(out, "AppInfoDataList");
        PutLe(out, 3, 4);
        out.insert(out.end(), 16, 0);
        PutUtf16(out, "0.1.0");
        out.insert(out.end(), 16, 0);
        PutUtf16(out, "MiniDWG");
        out.insert(out.end(), 16, 0);
        PutUtf16(out, "<ProductInformation name =\"MiniDWG\" build_version=\"0.1.0\" registry_version=\"0.1.0\" "
                      "install_id_string=\"MiniDWG\" registry_localeID=\"2052\"/>");
        return out;
    }

    std::vector<std::uint8_t> Writer::WriteRevHistory()
    {
        std::vector<std::uint8_t> out;
        PutLe(out, 0, 4);
        PutLe(out, 0, 4);
        PutLe(out, 1, 4);
        PutLe(out, 0, 4);
        return out;
    }

    // AcDb:ObjFreeSpace：R2010 起各字段为 64 位，没有对象段偏移
    std::vector<std::uint8_t> Writer::WriteObjFreeSpace()
    {
        std::vector<std::uint8_t> out;
        const bool wide = R2010Plus();
        PutLe(out, 0, wide ? 8 : 4);
        PutLe(out, m_map.size(), wide ? 8 : 4);
        PutJulian(out, R2000Plus() ? m_db.Header.UpdateDateTime.Value : m_db.Header.UpdateDateTime.Value);
        if (!wide)
            PutLe(out, 0, 4);
        out.push_back(4);
        for (std::uint64_t v : { 0x32ull, 0x64ull, 0x200ull, 0xFFFFFFFFull })
        {
            PutLe(out, v, wide ? 8 : 4);
            PutLe(out, 0, wide ? 8 : 4);
        }
        return out;
    }

    // AcDb:Template：RS 说明长度（AutoCAD 写空说明）、RS MEASUREMENT
    std::vector<std::uint8_t> Writer::WriteTemplate()
    {
        std::vector<std::uint8_t> out;
        if (R2007Plus())
            PutUtf16(out, "");
        else
            PutLe(out, 0, 2);
        PutLe(out, static_cast<std::uint16_t>(m_db.Header.MeasurementUnits), 2);
        return out;
    }
}
